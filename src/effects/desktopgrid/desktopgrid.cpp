/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2007 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2008 Lucas Murray <lmurray@undefinedfire.com>
    SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Carson Black <uhhadd@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "desktopgrid.h"
// KConfigSkeleton
#include "desktopgridconfig.h"

// For spatial mode access
#include "effects.h"
#include "virtualdesktops.h"

#include <functional>

#include "../presentwindows/presentwindows_proxy.h"

#include <QAction>
#include <QApplication>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <netwm_def.h>
#include <QEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QVector2D>
#include <QMatrix4x4>

#include <QQuickItem>
#include <QQmlContext>
#include <KWaylandServer/surface_interface.h>

#include <KActivities/Consumer>
#include <KActivities/Info>

#include <cmath>

namespace KWin
{

// WARNING, TODO: This effect relies on the desktop layout being EWMH-compliant.

// ---- TileOverlayBridge ----

class TileOverlayBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int desktop READ desktop CONSTANT)
    Q_PROPERTY(QString desktopName READ desktopName WRITE setDesktopName NOTIFY desktopNameChanged)
    Q_PROPERTY(int totalDesktops READ totalDesktops NOTIFY totalDesktopsChanged)
    Q_PROPERTY(bool spatialMode READ spatialMode CONSTANT)
    Q_PROPERTY(bool hasAbove READ hasAbove NOTIFY neighborsChanged)
    Q_PROPERTY(bool hasBelow READ hasBelow NOTIFY neighborsChanged)
    Q_PROPERTY(bool hasLeft  READ hasLeft  NOTIFY neighborsChanged)
    Q_PROPERTY(bool hasRight READ hasRight NOTIFY neighborsChanged)
    Q_PROPERTY(QString activityName READ activityName NOTIFY activityNameChanged)
public:
    explicit TileOverlayBridge(int desktop, QObject *parent = nullptr)
        : QObject(parent), m_desktop(desktop)
    {
        connect(&m_activityConsumer, &KActivities::Consumer::currentActivityChanged,
                this, [this](const QString &) { Q_EMIT activityNameChanged(); });
    }

    int desktop() const { return m_desktop; }

    QString desktopName() const {
        return effects->desktopName(m_desktop);
    }

    void setDesktopName(const QString &name) {
        QString safeName = name.trimmed().isEmpty()
            ? QStringLiteral("Desktop %1").arg(m_desktop)
            : name;
        if (safeName == effects->desktopName(m_desktop))
            return;
        VirtualDesktop *vd = VirtualDesktopManager::self()->desktopForX11Id(m_desktop);
        if (vd)
            vd->setName(safeName);
        Q_EMIT desktopNameChanged();
    }

    int totalDesktops() const { return effects->numberOfDesktops(); }

    void setRemoveCallback(std::function<void()> cb) { m_removeCallback = std::move(cb); }

    Q_INVOKABLE void removeDesktop() {
        if (m_removeCallback)
            m_removeCallback();
    }

    bool spatialMode() const {
        return static_cast<EffectsHandlerImpl*>(effects)->isSpatialMode();
    }

    bool hasAbove() const { return hasSpatialNeighbor(VirtualDesktopSpatialMap::Direction::Above); }
    bool hasBelow() const { return hasSpatialNeighbor(VirtualDesktopSpatialMap::Direction::Below); }
    bool hasLeft()  const { return hasSpatialNeighbor(VirtualDesktopSpatialMap::Direction::Left);  }
    bool hasRight() const { return hasSpatialNeighbor(VirtualDesktopSpatialMap::Direction::Right); }

    QString activityName() const {
        const QString id = m_activityConsumer.currentActivity();
        if (id.isEmpty())
            return QString();
        return KActivities::Info(id).name();
    }

    Q_INVOKABLE void addDesktopInDirection(const QString &direction) {
        Q_EMIT addDesktopRequested(m_desktop, direction);
    }

    Q_INVOKABLE void setEditing(bool editing) { Q_EMIT editingChanged(editing); }
    void startEditing() { Q_EMIT editingStartRequested(); }

    void notifyDesktopNameChanged() { Q_EMIT desktopNameChanged(); }
    void notifyTotalDesktopsChanged() { Q_EMIT totalDesktopsChanged(); }
    void notifyNeighborsChanged() { Q_EMIT neighborsChanged(); }

Q_SIGNALS:
    void desktopNameChanged();
    void totalDesktopsChanged();
    void neighborsChanged();
    void activityNameChanged();
    void addDesktopRequested(int desktop, const QString &direction);
    void editingChanged(bool editing);
    void editingStartRequested();

private:
    bool hasSpatialNeighbor(VirtualDesktopSpatialMap::Direction dir) const {
        VirtualDesktopManager *vds = VirtualDesktopManager::self();
        const VirtualDesktop *vd = vds->desktopForX11Id(m_desktop);
        if (!vd) return false;
        return !vds->spatialMap().neighbor(vd->id(), dir).isEmpty();
    }

    int m_desktop;
    std::function<void()> m_removeCallback;
    KActivities::Consumer m_activityConsumer;
};

// ---- DesktopGridEffect ----


DesktopGridEffect::DesktopGridEffect()
    : activated(false)
    , timeline()
    , keyboardGrab(false)
    , wasWindowMove(false)
    , wasWindowCopy(false)
    , wasDesktopMove(false)
    , isValidMove(false)
    , windowMove(nullptr)
    , windowMoveDiff()
    , windowMoveElevateTimer(new QTimer(this))
    , lastPresentTime(std::chrono::milliseconds::zero())
    , gridSize()
    , orientation(Qt::Horizontal)
    , activeCell(1, 1)
    , scale()
    , unscaledBorder()
    , scaledSize()
    , scaledOffset()
    , m_proxy(nullptr)
    , m_gestureAction(new QAction(this))
    , m_shortcutAction(new QAction(this))
{
    // Debounce timer for spatialMapChanged — coalesces rapid signals
    // (e.g. from desktop removal triggering both map change + layout update)
    // into a single overlay rebuild on the next event loop tick.
    // Single-click deferred activation: waits for double-click interval before
    // switching desktop, so a double-click can intercept and start rename instead.
    m_singleClickTimer = new QTimer(this);
    m_singleClickTimer->setSingleShot(true);
    connect(m_singleClickTimer, &QTimer::timeout, this, [this]() {
        if (m_pendingClickDesktop > 0 && activated) {
            setCurrentDesktop(m_pendingClickDesktop);
            deactivate();
        }
        m_pendingClickDesktop = 0;
    });

    m_spatialRebuildTimer = new QTimer(this);
    m_spatialRebuildTimer->setSingleShot(true);
    m_spatialRebuildTimer->setInterval(0);
    connect(m_spatialRebuildTimer, &QTimer::timeout, this, [this]() {
        if (!activated)
            return;
        if (!static_cast<EffectsHandlerImpl*>(effects)->isSpatialMode())
            return;
        setupGrid();
        destroyTileOverlays();
        createTileOverlays();
        effects->addRepaintFull();
    });

    initConfig<DesktopGridConfig>();

    // First we set up the gestures...
    QAction* a = m_gestureAction;

    connect(a, &QAction::triggered, this, [this]() {
        if ((qreal(timeline.currentTime()) / qreal(timeline.duration())) > 0.5) {
            if (effects->isScreenLocked()) {
                return;
            }
            activated = true;
            timeline.setDirection(QTimeLine::Forward);
            timelineRunning = true;
        } else {
            activated = false;
            timeline.setDirection(QTimeLine::Backward);
            timelineRunning = true;
        }
    });
    effects->registerRealtimeTouchpadSwipeShortcut(SwipeDirection::Up, a, [this](qreal cb) {
        if (activated) return;

        if (timeline.currentValue() == 0) {
            activated = true;
            setup();
            activated = false;
        }

        timeline.setDirection(QTimeLine::Forward);
        timeline.setCurrentTime(timeline.duration() * cb);
        effects->addRepaintFull();
    });
    effects->registerRealtimeTouchpadSwipeShortcut(SwipeDirection::Down, a, [this](qreal cb) {
        if (!activated) return;

        timeline.setDirection(QTimeLine::Backward);
        timeline.setCurrentTime(timeline.duration() - (timeline.duration() * cb));
        effects->addRepaintFull();
    });
    connect(&timeline, &QTimeLine::frameChanged, this, []() {
        effects->addRepaintFull();
    });
    connect(&timeline, &QTimeLine::finished, this, [this]() {
        timelineRunning = false;
        if (timeline.currentTime() == 0) {
            finish();
        }
    });

    // Now we set up the shortcut
    QAction* s = m_shortcutAction;
    s->setObjectName(QStringLiteral("ShowDesktopGrid"));
    s->setText(i18n("Show Desktop Grid"));

    KGlobalAccel::self()->setDefaultShortcut(s, QList<QKeySequence>() << Qt::CTRL + Qt::Key_F8);
    KGlobalAccel::self()->setShortcut(s, QList<QKeySequence>() << Qt::CTRL + Qt::Key_F8);
    shortcut = KGlobalAccel::self()->shortcut(s);
    effects->registerGlobalShortcut(Qt::CTRL + Qt::Key_F8, s);

    connect(s, &QAction::triggered, this, &DesktopGridEffect::toggle);

    connect(KGlobalAccel::self(), &KGlobalAccel::globalShortcutChanged, this, &DesktopGridEffect::globalShortcutChanged);
    connect(effects, &EffectsHandler::windowAdded, this, &DesktopGridEffect::slotWindowAdded);
    connect(effects, &EffectsHandler::windowClosed, this, &DesktopGridEffect::slotWindowClosed);
    connect(effects, &EffectsHandler::windowDeleted, this, &DesktopGridEffect::slotWindowDeleted);
    connect(effects, &EffectsHandler::numberDesktopsChanged, this, &DesktopGridEffect::slotNumberDesktopsChanged);
    connect(effects, &EffectsHandler::windowFrameGeometryChanged, this, &DesktopGridEffect::slotWindowFrameGeometryChanged);
    connect(VirtualDesktopManager::self(), &VirtualDesktopManager::spatialMapChanged,
            this, &DesktopGridEffect::slotSpatialMapChanged);
    connect(effects, &EffectsHandler::screenAdded, this, &DesktopGridEffect::setup);
    connect(effects, &EffectsHandler::screenRemoved, this, &DesktopGridEffect::setup);

    connect(effects, &EffectsHandler::screenAboutToLock, this, [this]() {
        deactivate();
        windowMoveElevateTimer->stop();
        if (keyboardGrab) {
            effects->ungrabKeyboard();
            keyboardGrab = false;
        }
    });

    windowMoveElevateTimer->setInterval(QApplication::startDragTime());
    windowMoveElevateTimer->setSingleShot(true);
    connect(windowMoveElevateTimer, &QTimer::timeout, this, [this]() {
        effects->setElevatedWindow(windowMove, true);
        wasWindowMove = true;
    });

    // Load all other configuration details
    reconfigure(ReconfigureAll);
}

DesktopGridEffect::~DesktopGridEffect()
{
}

void DesktopGridEffect::reconfigure(ReconfigureFlags)
{
    DesktopGridConfig::self()->read();

    for (ElectricBorder border : qAsConst(borderActivate)) {
        effects->unreserveElectricBorder(border, this);
    }
    borderActivate.clear();
    const auto desktopGridConfigActivate = DesktopGridConfig::borderActivate();
    for (int i : desktopGridConfigActivate) {
        borderActivate.append(ElectricBorder(i));
        effects->reserveElectricBorder(ElectricBorder(i), this);
    }

    // TODO: rename zoomDuration to duration
    zoomDuration = animationTime(DesktopGridConfig::zoomDuration() != 0 ? DesktopGridConfig::zoomDuration() : 300);
    timeline.setEasingCurve(QEasingCurve::InOutSine);
    timeline.setDuration(zoomDuration);

    border = DesktopGridConfig::borderWidth();
    desktopNameAlignment = Qt::Alignment(DesktopGridConfig::desktopNameAlignment());
    layoutMode = DesktopGridConfig::layoutMode();
    customLayoutRows = DesktopGridConfig::customLayoutRows();
    clickBehavior = DesktopGridConfig::clickBehavior();

    // deactivate and activate all touch border
    const QVector<ElectricBorder> relevantBorders{ElectricLeft, ElectricTop, ElectricRight, ElectricBottom};
    for (auto e : relevantBorders) {
        effects->unregisterTouchBorder(e, m_shortcutAction);
    }
    const auto touchBorders = DesktopGridConfig::touchBorderActivate();
    for (int i : touchBorders) {
        if (!relevantBorders.contains(ElectricBorder(i))) {
            continue;
        }
        effects->registerTouchBorder(ElectricBorder(i), m_shortcutAction);
    }
}

//-----------------------------------------------------------------------------
// Screen painting

void DesktopGridEffect::prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime)
{
    // The animation code assumes that the time diff cannot be 0, let's work around it.
    int time;
    if (lastPresentTime.count()) {
        time = std::max(1, int((presentTime - lastPresentTime).count()));
    } else {
        time = 1;
    }
    lastPresentTime = presentTime;
    if (timelineRunning) {
        timeline.setCurrentTime(timeline.currentTime() + (timeline.direction() == QTimeLine::Forward ? time : -time));

        if ((timeline.currentTime() <= 0 && timeline.direction() == QTimeLine::Backward)) {
            timelineRunning = false;
            // defer until the event loop to finish
            QTimer::singleShot(0, [this]() {
                finish();
            });
        }
    }
    for (int i = 0; i < effects->numberOfDesktops(); i++) {
        auto item = hoverTimeline[i];

        if (i == highlightedDesktop-1) { // if this is the highlighted desktop, we want to progress the animation from "not highlighted" to "highlight"
            item->setCurrentTime(item->currentTime() + time);
        } else { // otherwise we progress from "highlighted" to "not highlighted"
            item->setCurrentTime(item->currentTime() - time);
        }
    }

    if (timeline.currentValue() != 0 || activated || (isUsingPresentWindows() && isMotionManagerMovingWindows())) {
        if (isUsingPresentWindows()) {
            for (auto i = m_managers.begin(); i != m_managers.end(); ++i) {
                for (WindowMotionManager &manager : *i) {
                    manager.calculate(time);
                }
            }
        }
        // PAINT_SCREEN_BACKGROUND_FIRST is needed because screen will be actually painted more than once,
        // so with normal screen painting second screen paint would erase parts of the first paint
        if (timeline.currentValue() != 0 || (isUsingPresentWindows() && isMotionManagerMovingWindows()))
            data.mask |= PAINT_SCREEN_TRANSFORMED | PAINT_SCREEN_BACKGROUND_FIRST;
    }

    const EffectWindowList windows = effects->stackingOrder();
    for (auto *w : windows) {
        w->setData(WindowForceBlurRole, QVariant(true));
    }

    effects->prePaintScreen(data, presentTime);
}

void DesktopGridEffect::paintScreen(int mask, const QRegion &region, ScreenPaintData& data)
{
    if (timeline.currentValue() == 0 && !isUsingPresentWindows()) {
        effects->paintScreen(mask, region, data);
        return;
    }
    VirtualDesktopManager *vdm = VirtualDesktopManager::self();
    const bool activityAwareSpatial = vdm->isActivityAwareSpatialMode();
    const VirtualDesktopSpatialMap &paintSmap = vdm->spatialMap();
    const bool spatialMapNonEmpty = activityAwareSpatial && !paintSmap.isEmpty();

    for (int desktop = 1; desktop <= effects->numberOfDesktops(); desktop++) {
        // In activity-aware spatial mode, skip desktops that are not part of the
        // current activity's spatial map (they are hidden from this activity).
        // When no spatial links are set yet (empty map) all desktops are shown.
        if (spatialMapNonEmpty) {
            VirtualDesktop *vd = vdm->desktopForX11Id(desktop);
            if (vd && !paintSmap.containsDesktop(vd->id()))
                continue;
        }
        ScreenPaintData d = data;
        paintingDesktop = desktop;
        effects->paintScreen(mask, region, d);
    }

    // Render per-tile overlays (desktop names + inline rename)
    if (!m_tileOverlays.isEmpty()) {
        // Geometry is stable once the grid is laid out — only update when
        // the dirty flag is set (e.g. after setupGrid / createTileOverlays).
        if (m_tileOverlayGeometryDirty) {
            updateTileOverlayGeometry();
            m_tileOverlayGeometryDirty = false;
        }
        const qreal opacity = timeline.currentValue();
        for (OffscreenQuickScene *view : qAsConst(m_tileOverlays)) {
            if (view->rootItem()->opacity() != opacity)
                view->rootItem()->setOpacity(opacity);
            effects->renderOffscreenQuickView(view);
        }
    }

    if (isUsingPresentWindows() && windowMove && wasWindowMove) {
        // the moving window has to be painted on top of all desktops
        QPoint diff = cursorPos() - m_windowMoveStartPoint;
        QRect geo = m_windowMoveGeometry.translated(diff);
        WindowPaintData d(windowMove, data.projectionMatrix());
        d *= QVector2D((qreal)geo.width() / (qreal)windowMove->width(), (qreal)geo.height() / (qreal)windowMove->height());
        d += QPoint(geo.left() - windowMove->x(), geo.top() - windowMove->y());
        effects->drawWindow(windowMove, PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_LANCZOS, infiniteRegion(), d);
    }

}

void DesktopGridEffect::postPaintScreen()
{
    bool resetLastPresentTime = true;

    if (timelineRunning || activated ? timeline.currentValue() != 1 : timeline.currentValue() != 0) {
        effects->addRepaintFull(); // Repaint during zoom
        resetLastPresentTime = false;
    }
    if (isUsingPresentWindows() && isMotionManagerMovingWindows()) {
        effects->addRepaintFull();
        resetLastPresentTime = false;
    }
    if (activated) {
        for (int i = 0; i < effects->numberOfDesktops(); i++) {
            if (hoverTimeline[i]->currentValue() != 0.0 && hoverTimeline[i]->currentValue() != 1.0) {
                // Repaint during soft highlighting
                effects->addRepaintFull();
                resetLastPresentTime = false;
                break;
            }
        }
    }

    if (resetLastPresentTime) {
        lastPresentTime = std::chrono::milliseconds::zero();
    }

    for (auto &w : effects->stackingOrder()) {
        w->setData(WindowForceBlurRole, QVariant());
    }

    effects->postPaintScreen();
}

//-----------------------------------------------------------------------------
// Window painting

void DesktopGridEffect::prePaintWindow(EffectWindow* w, WindowPrePaintData& data, std::chrono::milliseconds presentTime)
{
    if (timeline.currentValue() != 0 || (isUsingPresentWindows() && isMotionManagerMovingWindows())) {
        if (w->isOnDesktop(paintingDesktop)) {
            w->enablePainting(EffectWindow::PAINT_DISABLED_BY_DESKTOP);
            if (w->isMinimized() && isUsingPresentWindows())
                w->enablePainting(EffectWindow::PAINT_DISABLED_BY_MINIMIZE);
            data.mask |= PAINT_WINDOW_TRANSFORMED;

            if (windowMove && wasWindowMove && windowMove->findModal() == w)
                w->disablePainting(EffectWindow::PAINT_DISABLED_BY_DESKTOP);
        } else
            w->disablePainting(EffectWindow::PAINT_DISABLED_BY_DESKTOP);
    }
    effects->prePaintWindow(w, data, presentTime);
}

void DesktopGridEffect::paintWindow(EffectWindow* w, int mask, QRegion region, WindowPaintData& data)
{
    if (timeline.currentValue() != 0 || (isUsingPresentWindows() && isMotionManagerMovingWindows())) {
        if (isUsingPresentWindows() && w == windowMove && wasWindowMove &&
            ((!wasWindowCopy && sourceDesktop == paintingDesktop) ||
             (sourceDesktop != highlightedDesktop && highlightedDesktop == paintingDesktop))) {
            return; // will be painted on top of all other windows
        }

        qreal xScale = data.xScale();
        qreal yScale = data.yScale();

        data.multiplyBrightness(1.0 - (0.3 * (1.0 - hoverTimeline[paintingDesktop - 1]->currentValue())));

        const QList<EffectScreen *> screens = effects->screens();
        for (EffectScreen *screen : screens) {
            QRect screenGeom = effects->clientArea(ScreenArea, screen, effects->currentDesktop());

            QRectF transformedGeo = w->frameGeometry();
            if (isUsingPresentWindows()) {
                WindowMotionManager& manager = m_managers[screen][paintingDesktop - 1];
                if (manager.isManaging(w)) {
                    transformedGeo = manager.transformedGeometry(w);
                    if (!manager.areWindowsMoving() && timeline.currentValue() == 1.0)
                        mask |= PAINT_WINDOW_LANCZOS;
                } else if (w->screen() != screen) {
                    continue; // we don't want parts of overlapping windows on the other screen
                }
                if (w->isDesktop() && !transformedGeo.intersects(screenGeom)) {
                    continue;
                }
            } else if (!transformedGeo.intersects(screenGeom)) {
                continue; // Nothing is being displayed, don't bother
            }
            WindowPaintData d = data;

            QPointF newPos = scalePos(transformedGeo.topLeft().toPoint(), paintingDesktop, screen);
            double progress = timeline.currentValue();
            d.setXScale(interpolate(1, xScale * scale[screen] * (float)transformedGeo.width() / (float)w->frameGeometry().width(), progress));
            d.setYScale(interpolate(1, yScale * scale[screen] * (float)transformedGeo.height() / (float)w->frameGeometry().height(), progress));
            d += QPoint(qRound(newPos.x() - w->x()), qRound(newPos.y() - w->y()));

            if (isUsingPresentWindows() && (w->isDock() || w->isSkipSwitcher())) {
                // fade out panels if present windows is used
                d.multiplyOpacity((1.0 - timeline.currentValue()));
            }
            if (isUsingPresentWindows() && w->isMinimized()) {
                d.multiplyOpacity(timeline.currentValue());
            }
            if (w->isDesktop() && timeline.currentValue() == 1.0) {
                // desktop windows are not in a motion manager and can always be rendered with
                // lanczos sampling except for animations
                mask |= PAINT_WINDOW_LANCZOS;
            }
            effects->paintWindow(w, mask, effects->clientArea(ScreenArea, screen, 0), d);
        }
    } else
        effects->paintWindow(w, mask, region, data);
}

//-----------------------------------------------------------------------------
// User interaction

void DesktopGridEffect::slotWindowAdded(EffectWindow* w)
{
    if (!activated)
        return;
    if (isUsingPresentWindows()) {
        if (!isRelevantWithPresentWindows(w)) {
            return; // don't add
        }
        const auto desktops = desktopList(w);
        for (const int i : desktops) {
            WindowMotionManager& manager = m_managers[w->screen()][i];
            manager.manage(w);
            m_proxy->calculateWindowTransformations(manager.managedWindows(), w->screen(), manager);
        }
    }
    effects->addRepaintFull();
}

void DesktopGridEffect::slotWindowClosed(EffectWindow* w)
{
    if (!activated && timeline.currentValue() == 0)
        return;
    if (w == windowMove) {
        effects->setElevatedWindow(windowMove, false);
        windowMove = nullptr;
    }
    if (isUsingPresentWindows()) {
        const auto desktops = desktopList(w);
        for (const int i : desktops) {
            WindowMotionManager& manager = m_managers[w->screen()][i];
            manager.unmanage(w);
            m_proxy->calculateWindowTransformations(manager.managedWindows(), w->screen(), manager);
        }
    }
    effects->addRepaintFull();
}

void DesktopGridEffect::slotWindowDeleted(EffectWindow* w)
{
    if (w == windowMove)
        windowMove = nullptr;
    if (isUsingPresentWindows()) {
        for (auto it = m_managers.begin(); it != m_managers.end(); ++it) {
            for (WindowMotionManager &manager : *it) {
                manager.unmanage(w);
            }
        }
    }
}

void DesktopGridEffect::slotWindowFrameGeometryChanged(EffectWindow* w, const QRect& old)
{
    Q_UNUSED(old)
    if (!activated)
        return;
    if (w == windowMove && wasWindowMove)
        return;
    if (isUsingPresentWindows()) {
        const auto desktops = desktopList(w);
        for (const int i : desktops) {
            WindowMotionManager& manager = m_managers[w->screen()][i];
            m_proxy->calculateWindowTransformations(manager.managedWindows(), w->screen(), manager);
        }
    }
}

void DesktopGridEffect::windowInputMouseEvent(QEvent* e)
{
    if ((e->type() != QEvent::MouseMove
            && e->type() != QEvent::MouseButtonPress
            && e->type() != QEvent::MouseButtonRelease
            && e->type() != QEvent::MouseButtonDblClick)
            || timeline.currentValue() != 1)  // Block user input during animations
        return;

    // Double-click: find the tile bridge and start rename editing.
    // Stop the pending single-click deactivation so the grid stays open.
    if (e->type() == QEvent::MouseButtonDblClick) {
        m_singleClickTimer->stop();
        m_pendingClickDesktop = 0;
        QMouseEvent* me = static_cast<QMouseEvent*>(e);
        const int desk = posToDesktop(me->pos());
        for (TileOverlayBridge *bridge : qAsConst(m_tileBridges)) {
            if (bridge->desktop() == desk) {
                bridge->startEditing();
                e->setAccepted(true);
                return;
            }
        }
        return;
    }
    QMouseEvent* me = static_cast< QMouseEvent* >(e);
    if (!(wasWindowMove || wasDesktopMove)) {
        for (OffscreenQuickScene *view : qAsConst(m_tileOverlays)) {
            view->forwardMouseEvent(me);
            if (e->isAccepted()) {
                return;
            }
        }
    }

    if (e->type() == QEvent::MouseMove) {
        int d = posToDesktop(me->pos());
        if (windowMove != nullptr &&
                (me->pos() - dragStartPos).manhattanLength() > QApplication::startDragDistance()) {
            // Handle window moving
            if (windowMoveElevateTimer->isActive()) { // Window started moving, but is not elevated yet!
                windowMoveElevateTimer->stop();
                effects->setElevatedWindow(windowMove, true);
            }
            if (!wasWindowMove) { // Activate on move
                if (isUsingPresentWindows()) {
                    const auto desktops = desktopList(windowMove);
                    for (const int i : desktops) {
                        WindowMotionManager& manager = m_managers[windowMove->screen()][i];
                        if ((i + 1) == sourceDesktop) {
                            const QRectF transformedGeo = manager.transformedGeometry(windowMove);
                            const QPointF pos = scalePos(transformedGeo.topLeft().toPoint(), sourceDesktop, windowMove->screen());
                            const QSize size(scale[windowMove->screen()] *(float)transformedGeo.width(),
                                             scale[windowMove->screen()] *(float)transformedGeo.height());
                            m_windowMoveGeometry = QRect(pos.toPoint(), size);
                            m_windowMoveStartPoint = me->pos();
                        }
                        manager.unmanage(windowMove);
                        if (EffectWindow* modal = windowMove->findModal()) {
                            if (manager.isManaging(modal))
                                manager.unmanage(modal);
                        }
                        m_proxy->calculateWindowTransformations(manager.managedWindows(), windowMove->screen(), manager);
                    }
                    wasWindowMove = true;
                }
            }
            if (windowMove->isMovable() && !isUsingPresentWindows()) {
                wasWindowMove = true;
                EffectScreen *screen = effects->screenAt(me->pos());
                effects->moveWindow(windowMove, unscalePos(me->pos(), nullptr) + windowMoveDiff, true, 1.0 / scale[screen]);
            }
            if (wasWindowMove) {
                if (effects->waylandDisplay() && (me->modifiers() & Qt::ControlModifier)) {
                    wasWindowCopy = true;
                    effects->defineCursor(Qt::DragCopyCursor);
                } else {
                    wasWindowCopy = false;
                    effects->defineCursor(Qt::ClosedHandCursor);
                }
                if (d != highlightedDesktop) {
                    auto desktops = windowMove->desktops();
                    if (!desktops.contains(d)) {
                        desktops.append(d);
                    }
                    if (highlightedDesktop != sourceDesktop || !wasWindowCopy) {
                        desktops.removeOne(highlightedDesktop);
                    }
                    effects->windowToDesktops(windowMove, desktops);
                    EffectScreen *screen = effects->screenAt(me->pos());
                    if (screen != windowMove->screen())
                        effects->windowToScreen(windowMove, screen);
                }
                effects->addRepaintFull();
            }
        } else if ((me->buttons() & Qt::LeftButton) && !wasDesktopMove &&
                  (me->pos() - dragStartPos).manhattanLength() > QApplication::startDragDistance()) {
            wasDesktopMove = true;
            effects->defineCursor(Qt::ClosedHandCursor);
        }
        if (d != highlightedDesktop) { // Highlight desktop
            if ((me->buttons() & Qt::LeftButton) && isValidMove && !wasWindowMove && d <= effects->numberOfDesktops()) {
                EffectWindowList windows = effects->stackingOrder();
                EffectWindowList stack[3];
                for (EffectWindowList::const_iterator it = windows.constBegin(),
                                                      end = windows.constEnd(); it != end; ++it) {
                    EffectWindow *w = const_cast<EffectWindow*>(*it); // we're not really touching it here but below
                    if (w->isOnAllDesktops())
                        continue;
                    if (w->isOnDesktop(highlightedDesktop))
                        stack[0] << w;
                    if (w->isOnDesktop(d))
                        stack[1] << w;
                    if (w->isOnDesktop(m_originalMovingDesktop))
                        stack[2] << w;
                }
                const int desks[4] = {highlightedDesktop, d, m_originalMovingDesktop, highlightedDesktop};
                for (int i = 0; i < 3; ++i ) {
                    if (desks[i] == desks[i+1])
                        continue;
                    for (EffectWindow *w : qAsConst(stack[i])) {
                        auto desktops = w->desktops();
                        desktops.removeOne(desks[i]);
                        desktops.append(desks[i+1]);
                        effects->windowToDesktops(w, desktops);

                        if (isUsingPresentWindows()) {
                            m_managers[w->screen()][desks[i] - 1].unmanage(w);
                            m_managers[w->screen()][desks[i + 1] - 1].manage(w);
                        }
                    }
                }
                if (isUsingPresentWindows()) {
                    const QList<EffectScreen *> screens = effects->screens();
                    for (EffectScreen *screen : screens) {
                        for (int j = 0; j < 3; ++j) {
                            WindowMotionManager& manager = m_managers[screen][desks[j] - 1];
                            m_proxy->calculateWindowTransformations(manager.managedWindows(), screen, manager);
                        }
                    }
                    effects->addRepaintFull();
                }
            }
            setHighlightedDesktop(d);
        }
    }
    if (e->type() == QEvent::MouseButtonPress) {
        if (me->buttons() == Qt::LeftButton) {
            isValidMove = true;
            dragStartPos = me->pos();
            sourceDesktop = posToDesktop(me->pos());
            bool isDesktop = (me->modifiers() & Qt::ShiftModifier);
            EffectWindow* w = isDesktop ? nullptr : windowAt(me->pos());
            if (w != nullptr)
                isDesktop = w->isDesktop();
            if (isDesktop)
                m_originalMovingDesktop = posToDesktop(me->pos());
            else
                m_originalMovingDesktop = 0;
            if (w != nullptr && !w->isDesktop() && (w->isMovable() || w->isMovableAcrossScreens() || isUsingPresentWindows())) {
                // Prepare it for moving
                windowMoveDiff = w->pos() - unscalePos(me->pos(), nullptr);
                windowMove = w;
                windowMoveElevateTimer->start();
            }
        } else if ((me->buttons() == Qt::MiddleButton || me->buttons() == Qt::RightButton) && windowMove == nullptr) {
            EffectWindow* w = windowAt(me->pos());
            if (w && w->isDesktop()) {
                w = nullptr;
            }
            if (w != nullptr) {
                const int desktop = posToDesktop(me->pos());
                if (w->isOnAllDesktops()) {
                    effects->windowToDesktop(w, desktop);
                } else {
                    effects->windowToDesktop(w, NET::OnAllDesktops);
                }
                const bool isOnAllDesktops = w->isOnAllDesktops();
                if (isUsingPresentWindows()) {
                    for (int i = 0; i < effects->numberOfDesktops(); i++) {
                        if (i != desktop - 1) {
                            WindowMotionManager& manager = m_managers[w->screen()][i];
                            if (isOnAllDesktops)
                                manager.manage(w);
                            else
                                manager.unmanage(w);
                            m_proxy->calculateWindowTransformations(manager.managedWindows(), w->screen(), manager);
                        }
                    }
                }
                effects->addRepaintFull();
            }
        }
    }
    if (e->type() == QEvent::MouseButtonRelease && me->button() == Qt::LeftButton) {
        isValidMove = false;
        if (windowMove) {
            if (windowMoveElevateTimer->isActive()) {
                // no need to elevate window, it was just a click
                windowMoveElevateTimer->stop();
            }
            if (clickBehavior == SwitchDesktopAndActivateWindow || wasWindowMove) {
                // activate window if relevant config is set or window was moved
                effects->activateWindow(windowMove);
            }
        }
        if (wasWindowMove || wasDesktopMove) { // reset pointer
            effects->defineCursor(Qt::ArrowCursor);
        } else { // click -> defer exit to allow double-click to cancel
            const int desk = posToDesktop(me->pos());
            if (desk > effects->numberOfDesktops())
                return; // don't quit when missing desktop
            m_pendingClickDesktop = desk;
            m_singleClickTimer->start(QApplication::doubleClickInterval());
        }
        if (windowMove) {
            if (wasWindowMove && isUsingPresentWindows()) {
                const int targetDesktop = posToDesktop(cursorPos());
                const auto desktops = desktopList(windowMove);
                for (const int i : desktops) {
                    WindowMotionManager& manager = m_managers[windowMove->screen()][i];
                    manager.manage(windowMove);
                    if (EffectWindow* modal = windowMove->findModal())
                        manager.manage(modal);
                    if (i + 1 == targetDesktop) {
                        // for the desktop the window is dropped on, we use the current geometry
                        manager.setTransformedGeometry(windowMove, moveGeometryToDesktop(targetDesktop));
                    }
                    m_proxy->calculateWindowTransformations(manager.managedWindows(), windowMove->screen(), manager);
                }
                effects->addRepaintFull();
            }
            effects->setElevatedWindow(windowMove, false);
            windowMove = nullptr;
        }
        wasWindowMove = false;
        wasWindowCopy = false;
        wasDesktopMove = false;
    }
}

void DesktopGridEffect::activate()
{
    activated = true;
    setup();
    timeline.setDirection(QTimeLine::Forward);
    timelineRunning = true;
    // timeline.resume();
    effects->addRepaintFull();
}

void DesktopGridEffect::deactivate()
{
    m_singleClickTimer->stop();
    m_pendingClickDesktop = 0;
    activated = false;
    timeline.setDirection(QTimeLine::Backward);
    timelineRunning = true;
    // timeline.resume();
    effects->addRepaintFull();
}

void DesktopGridEffect::toggle()
{
    if (activated) deactivate(); else activate();
}

void DesktopGridEffect::grabbedKeyboardEvent(QKeyEvent* e)
{
    if (timeline.currentValue() != 1)   // Block user input during animations
        return;
    if (windowMove != nullptr)
        return;
    // Forward keyboard events to tile overlay when editing desktop name
    if (m_editingTileOverlay) {
        m_editingTileOverlay->forwardKeyEvent(e);
        return;
    }
    if (e->type() == QEvent::KeyPress) {
        // check for global shortcuts
        // HACK: keyboard grab disables the global shortcuts so we have to check for global shortcut (bug 156155)
        if (shortcut.contains(e->key() + e->modifiers())) {
            deactivate();
            return;
        }

        int desktop = -1;
        // switch by F<number> or just <number>
        if (e->key() >= Qt::Key_F1 && e->key() <= Qt::Key_F35)
            desktop = e->key() - Qt::Key_F1 + 1;
        else if (e->key() >= Qt::Key_0 && e->key() <= Qt::Key_9)
            desktop = e->key() == Qt::Key_0 ? 10 : e->key() - Qt::Key_0;
        if (desktop != -1) {
            if (desktop <= effects->numberOfDesktops()) {
                setHighlightedDesktop(desktop);
                setCurrentDesktop(desktop);
                deactivate();
            }
            return;
        }
        switch(e->key()) {
            // Wrap only on autorepeat
        case Qt::Key_Left:
        case Qt::Key_H:    // vim: left
            setHighlightedDesktop(desktopToLeft(highlightedDesktop, !e->isAutoRepeat()));
            break;
        case Qt::Key_Right:
        case Qt::Key_L:    // vim: right
            setHighlightedDesktop(desktopToRight(highlightedDesktop, !e->isAutoRepeat()));
            break;
        case Qt::Key_Up:
        case Qt::Key_K:    // vim: up
            setHighlightedDesktop(desktopUp(highlightedDesktop, !e->isAutoRepeat()));
            break;
        case Qt::Key_Down:
        case Qt::Key_J:    // vim: down
            setHighlightedDesktop(desktopDown(highlightedDesktop, !e->isAutoRepeat()));
            break;
        case Qt::Key_Escape:
            deactivate();
            return;
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Space:
            setCurrentDesktop(highlightedDesktop);
            deactivate();
            return;
        case Qt::Key_Plus:
            slotAddDesktop();
            break;
        case Qt::Key_Minus:
            slotRemoveDesktop();
            break;
        default:
            break;
        }
    }
}

bool DesktopGridEffect::borderActivated(ElectricBorder border)
{
    if (!borderActivate.contains(border))
        return false;
    if (effects->activeFullScreenEffect() && effects->activeFullScreenEffect() != this)
        return true;
    toggle();
    return true;
}

//-----------------------------------------------------------------------------
// Helper functions

// Transform a point to its position on the scaled grid
QPointF DesktopGridEffect::scalePos(const QPoint& pos, int desktop, EffectScreen *screen) const
{
    QRect screenGeom = effects->clientArea(ScreenArea, screen, 0);
    QPoint desktopCell;
    if (static_cast<EffectsHandlerImpl*>(effects)->isSpatialMode()) {
        // In spatial mode, use actual grid coordinates so sparse layouts
        // position each desktop at its assigned cell, leaving empty cells vacant.
        QPoint coords = effects->desktopGridCoords(desktop); // 0-based (col, row)
        desktopCell = QPoint(coords.x() + 1, coords.y() + 1); // convert to 1-based
    } else if (orientation == Qt::Horizontal) {
        desktopCell.setX((desktop - 1) % gridSize.width() + 1);
        desktopCell.setY((desktop - 1) / gridSize.width() + 1);
    } else {
        desktopCell.setX((desktop - 1) / gridSize.height() + 1);
        desktopCell.setY((desktop - 1) % gridSize.height() + 1);
    }

    double progress = timeline.currentValue();
    QPointF point(
        interpolate(
            (
                (screenGeom.width() + unscaledBorder[screen]) *(desktopCell.x() - 1)
                - (screenGeom.width() + unscaledBorder[screen]) *(activeCell.x() - 1)
            ) + pos.x(),
            (
                (scaledSize[screen].width() + m_effectiveBorder) *(desktopCell.x() - 1)
                + scaledOffset[screen].x()
                + (pos.x() - screenGeom.x()) * scale[screen]
            ),
            progress),
        interpolate(
            (
                (screenGeom.height() + unscaledBorder[screen]) *(desktopCell.y() - 1)
                - (screenGeom.height() + unscaledBorder[screen]) *(activeCell.y() - 1)
            ) + pos.y(),
            (
                (scaledSize[screen].height() + m_effectiveBorder) *(desktopCell.y() - 1)
                + scaledOffset[screen].y()
                + (pos.y() - screenGeom.y()) * scale[screen]
            ),
            progress)
    );

    return point;
}

// Detransform a point to its position on the full grid
// TODO: Doesn't correctly interpolate (Final position is correct though), don't forget to copy to posToDesktop()
QPoint DesktopGridEffect::unscalePos(const QPoint& pos, int* desktop) const
{
    EffectScreen *screen = effects->screenAt(pos);
    QRect screenGeom = effects->clientArea(ScreenArea, screen, effects->currentDesktop());

    //double progress = timeline.currentValue();
    double scaledX = /*interpolate(
        ( pos.x() - screenGeom.x() + unscaledBorder[screen] / 2.0 ) / ( screenGeom.width() + unscaledBorder[screen] ) + activeCell.x() - 1,*/
        (pos.x() - scaledOffset[screen].x() + double(m_effectiveBorder) / 2.0) / (scaledSize[screen].width() + m_effectiveBorder)/*,
        progress )*/;
    double scaledY = /*interpolate(
        ( pos.y() - screenGeom.y() + unscaledBorder[screen] / 2.0 ) / ( screenGeom.height() + unscaledBorder[screen] ) + activeCell.y() - 1,*/
        (pos.y() - scaledOffset[screen].y() + double(m_effectiveBorder) / 2.0) / (scaledSize[screen].height() + m_effectiveBorder)/*,
        progress )*/;
    int gx = qBound(0, int(scaledX), gridSize.width() - 1);     // Zero-based
    int gy = qBound(0, int(scaledY), gridSize.height() - 1);
    scaledX -= gx;
    scaledY -= gy;
    if (desktop != nullptr) {
        if (static_cast<EffectsHandlerImpl*>(effects)->isSpatialMode())
            *desktop = effects->desktopAtCoords(QPoint(gx, gy)); // 0 if cell vacant
        else if (orientation == Qt::Horizontal)
            *desktop = gy * gridSize.width() + gx + 1;
        else
            *desktop = gx * gridSize.height() + gy + 1;
    }

    return QPoint(
               qBound(
                   screenGeom.x(),
                   qRound(
                       scaledX * (screenGeom.width() + unscaledBorder[screen])
                       - unscaledBorder[screen] / 2.0
                       + screenGeom.x()
                   ),
                   screenGeom.right()
               ),
               qBound(
                   screenGeom.y(),
                   qRound(
                       scaledY * (screenGeom.height() + unscaledBorder[screen])
                       - unscaledBorder[screen] / 2.0
                       + screenGeom.y()
                   ),
                   screenGeom.bottom()
               )
           );
}

int DesktopGridEffect::posToDesktop(const QPoint& pos) const
{
    // Copied from unscalePos()
    EffectScreen *screen = effects->screenAt(pos);

    double scaledX = (pos.x() - scaledOffset[screen].x() + double(m_effectiveBorder) / 2.0) / (scaledSize[screen].width() + m_effectiveBorder);
    double scaledY = (pos.y() - scaledOffset[screen].y() + double(m_effectiveBorder) / 2.0) / (scaledSize[screen].height() + m_effectiveBorder);
    int gx = qBound(0, int(scaledX), gridSize.width() - 1);     // Zero-based
    int gy = qBound(0, int(scaledY), gridSize.height() - 1);
    if (static_cast<EffectsHandlerImpl*>(effects)->isSpatialMode())
        return effects->desktopAtCoords(QPoint(gx, gy)); // 0 if cell vacant
    if (orientation == Qt::Horizontal)
        return gy * gridSize.width() + gx + 1;
    return gx * gridSize.height() + gy + 1;
}

EffectWindow* DesktopGridEffect::windowAt(QPoint pos) const
{
    // Get stacking order top first
    EffectWindowList windows = effects->stackingOrder();
    EffectWindowList::Iterator begin = windows.begin();
    EffectWindowList::Iterator end = windows.end();
    --end;
    while (begin < end)
        qSwap(*begin++, *end--);

    int desktop;
    pos = unscalePos(pos, &desktop);
    if (desktop <= 0 || desktop > effects->numberOfDesktops())
        return nullptr;
    if (isUsingPresentWindows()) {
        EffectScreen *screen = effects->screenAt(pos);
        EffectWindow *w = m_managers[screen][desktop - 1].windowAtPoint(pos, false);
        if (w)
            return w;
        for (EffectWindow * w : qAsConst(windows)) {
            if (w->isOnDesktop(desktop) && w->isDesktop() && w->frameGeometry().contains(pos)) {
                return w;
            }
        }
    } else {
        for (EffectWindow * w : qAsConst(windows)) {
            if (w->isOnDesktop(desktop) && w->isOnCurrentActivity() && !w->isMinimized() && w->frameGeometry().contains(pos)) {
                return w;
            }
        }
    }
    return nullptr;
}

void DesktopGridEffect::setCurrentDesktop(int desktop)
{
    if (static_cast<EffectsHandlerImpl*>(effects)->isSpatialMode()) {
        QPoint coords = effects->desktopGridCoords(desktop); // 0-based (col, row)
        activeCell = QPoint(coords.x() + 1, coords.y() + 1); // convert to 1-based
    } else if (orientation == Qt::Horizontal) {
        activeCell.setX((desktop - 1) % gridSize.width() + 1);
        activeCell.setY((desktop - 1) / gridSize.width() + 1);
    } else {
        activeCell.setX((desktop - 1) / gridSize.height() + 1);
        activeCell.setY((desktop - 1) % gridSize.height() + 1);
    }
    if (effects->currentDesktop() != desktop)
        effects->setCurrentDesktop(desktop);
}

void DesktopGridEffect::setHighlightedDesktop(int d)
{
    if (d == highlightedDesktop || d <= 0 || d > effects->numberOfDesktops())
        return;
    if (highlightedDesktop > 0 && highlightedDesktop <= hoverTimeline.count())
        hoverTimeline[highlightedDesktop-1]->setCurrentTime(qMin(hoverTimeline[highlightedDesktop-1]->currentTime(),
                                                                 hoverTimeline[highlightedDesktop-1]->duration()));
    highlightedDesktop = d;
    if (highlightedDesktop <= hoverTimeline.count())
        hoverTimeline[highlightedDesktop-1]->setCurrentTime(qMax(hoverTimeline[highlightedDesktop-1]->currentTime(), 0));
    effects->addRepaintFull();
}

int DesktopGridEffect::desktopToRight(int desktop, bool wrap) const
{
    if (static_cast<EffectsHandlerImpl*>(effects)->isSpatialMode())
        return effects->desktopToRight(desktop, wrap);
    // Copied from Workspace::desktopToRight()
    int dt = desktop - 1;
    if (orientation == Qt::Vertical) {
        dt += gridSize.height();
        if (dt >= effects->numberOfDesktops()) {
            if (wrap)
                dt -= effects->numberOfDesktops();
            else
                return desktop;
        }
    } else {
        int d = (dt % gridSize.width()) + 1;
        if (d >= gridSize.width()) {
            if (wrap)
                d -= gridSize.width();
            else
                return desktop;
        }
        dt = dt - (dt % gridSize.width()) + d;
    }
    return dt + 1;
}

int DesktopGridEffect::desktopToLeft(int desktop, bool wrap) const
{
    if (static_cast<EffectsHandlerImpl*>(effects)->isSpatialMode())
        return effects->desktopToLeft(desktop, wrap);
    // Copied from Workspace::desktopToLeft()
    int dt = desktop - 1;
    if (orientation == Qt::Vertical) {
        dt -= gridSize.height();
        if (dt < 0) {
            if (wrap)
                dt += effects->numberOfDesktops();
            else
                return desktop;
        }
    } else {
        int d = (dt % gridSize.width()) - 1;
        if (d < 0) {
            if (wrap)
                d += gridSize.width();
            else
                return desktop;
        }
        dt = dt - (dt % gridSize.width()) + d;
    }
    return dt + 1;
}

int DesktopGridEffect::desktopUp(int desktop, bool wrap) const
{
    if (static_cast<EffectsHandlerImpl*>(effects)->isSpatialMode())
        return effects->desktopAbove(desktop, wrap);
    // Copied from Workspace::desktopUp()
    int dt = desktop - 1;
    if (orientation == Qt::Horizontal) {
        dt -= gridSize.width();
        if (dt < 0) {
            if (wrap)
                dt += effects->numberOfDesktops();
            else
                return desktop;
        }
    } else {
        int d = (dt % gridSize.height()) - 1;
        if (d < 0) {
            if (wrap)
                d += gridSize.height();
            else
                return desktop;
        }
        dt = dt - (dt % gridSize.height()) + d;
    }
    return dt + 1;
}

int DesktopGridEffect::desktopDown(int desktop, bool wrap) const
{
    if (static_cast<EffectsHandlerImpl*>(effects)->isSpatialMode())
        return effects->desktopBelow(desktop, wrap);
    // Copied from Workspace::desktopDown()
    int dt = desktop - 1;
    if (orientation == Qt::Horizontal) {
        dt += gridSize.width();
        if (dt >= effects->numberOfDesktops()) {
            if (wrap)
                dt -= effects->numberOfDesktops();
            else
                return desktop;
        }
    } else {
        int d = (dt % gridSize.height()) + 1;
        if (d >= gridSize.height()) {
            if (wrap)
                d -= gridSize.height();
            else
                return desktop;
        }
        dt = dt - (dt % gridSize.height()) + d;
    }
    return dt + 1;
}

//-----------------------------------------------------------------------------
// Activation

void DesktopGridEffect::setup()
{
    if (!isActive())
        return;
    if (!keyboardGrab) {
        keyboardGrab = effects->grabKeyboard(this);
        effects->startMouseInterception(this, Qt::ArrowCursor);
        effects->setActiveFullScreenEffect(this);
    }
    setHighlightedDesktop(effects->currentDesktop());

    // Soft highlighting
    qDeleteAll(hoverTimeline);
    hoverTimeline.clear();
    for (int i = 0; i < effects->numberOfDesktops(); i++) {
        QTimeLine *newTimeline = new QTimeLine(zoomDuration, this);
        newTimeline->setEasingCurve(QEasingCurve::InOutSine);
        hoverTimeline.append(newTimeline);
    }
    hoverTimeline[effects->currentDesktop() - 1]->setCurrentTime(hoverTimeline[effects->currentDesktop() - 1]->duration());

    setupGrid();
    setCurrentDesktop(effects->currentDesktop());

    // setup the motion managers
    if (clickBehavior == SwitchDesktopAndActivateWindow)
        m_proxy = static_cast<PresentWindowsEffectProxy*>(effects->getProxy(QStringLiteral("presentwindows")));
    if (isUsingPresentWindows()) {
        m_proxy->reCreateGrids(); // revalidation on multiscreen, bug #351724
        const QList<EffectScreen *> screens = effects->screens();
        for (EffectScreen *screen : screens) {
            QList<WindowMotionManager> managers;
            for (int i = 1; i <= effects->numberOfDesktops(); i++) {
                    WindowMotionManager manager;
                    const auto stackingOrder = effects->stackingOrder();
                    for (EffectWindow * w : stackingOrder) {
                        if (w->isOnDesktop(i) && w->screen() == screen &&isRelevantWithPresentWindows(w)) {
                            manager.manage(w);
                        }
                    }
                    m_proxy->calculateWindowTransformations(manager.managedWindows(), screen, manager);
                    managers.append(manager);
            }
            m_managers[screen] = managers;
        }
    }

    createTileOverlays();
}

void DesktopGridEffect::setupGrid()
{
    // We need these variables for every paint so lets cache them
    int x, y;
    int numDesktops = effects->numberOfDesktops();
    // In spatial mode, always use the pager layout so that the grid
    // reflects the actual spatial neighbor topology from _NET_DESKTOP_LAYOUT,
    // regardless of the user's layoutMode config setting.
    int effectiveLayoutMode = (effects->isSpatialMode()) ? LayoutPager : layoutMode;
    switch(effectiveLayoutMode) {
    default:
    case LayoutPager:
        orientation = Qt::Horizontal;
        gridSize = effects->desktopGridSize();
        // sanity check: pager may report incorrect size in case of one desktop
        if (numDesktops == 1) {
            gridSize = QSize(1, 1);
        }
        break;
    case LayoutAutomatic:
        y = sqrt(float(numDesktops)) + 0.5;
        x = float(numDesktops) / float(y) + 0.5;
        if (x * y < numDesktops)
            x++;
        orientation = Qt::Horizontal;
        gridSize.setWidth(x);
        gridSize.setHeight(y);
        break;
    case LayoutCustom:
        orientation = Qt::Horizontal;
        gridSize.setWidth(ceil(effects->numberOfDesktops() / double(customLayoutRows)));
        gridSize.setHeight(customLayoutRows);
        break;
    }
    // Dynamically reduce border for grids larger than 4 in any dimension so that
    // thumbnails remain usable without consuming negative space.
    {
        int maxDim = qMax(gridSize.width(), gridSize.height());
        m_effectiveBorder = (maxDim > 4) ? qMax(2, int(border * 4.0 / maxDim)) : border;
    }
    scale.clear();
    unscaledBorder.clear();
    scaledSize.clear();
    scaledOffset.clear();

    const QList<EffectScreen *> screens = effects->screens();
    for (EffectScreen *screen : screens) {
        QRect geom = effects->clientArea(ScreenArea, screen, effects->currentDesktop());
        double sScaleX = (geom.width() - m_effectiveBorder * (gridSize.width() + 1)) / double(geom.width() * gridSize.width());
        double sScaleY = (geom.height() - m_effectiveBorder * (gridSize.height() + 1)) / double(geom.height() * gridSize.height());
        double sScale = qMin(sScaleX, sScaleY);
        if (sScale <= 0.0) sScale = 0.01; // guard against degenerate grids
        double sBorder = m_effectiveBorder / sScale;
        QSizeF size(
            double(geom.width()) * sScale,
            double(geom.height()) * sScale
        );
        QPointF offset(
            geom.x() + (geom.width() - size.width() * gridSize.width() - m_effectiveBorder *(gridSize.width() - 1)) / 2.0,
            geom.y() + (geom.height() - size.height() * gridSize.height() - m_effectiveBorder *(gridSize.height() - 1)) / 2.0
        );
        scale[screen] = sScale;
        unscaledBorder[screen] = sBorder;
        scaledSize[screen] = size;
        scaledOffset[screen] = offset;
    }
    m_tileOverlayGeometryDirty = true;
}

void DesktopGridEffect::finish()
{
    destroyTileOverlays();

    if (isUsingPresentWindows()) {
        for (auto it = m_managers.begin(); it != m_managers.end(); ++it) {
            for (WindowMotionManager &manager : *it) {
                const auto windows = manager.managedWindows();
                for (EffectWindow * w : windows) {
                    manager.moveWindow(w, w->frameGeometry());
                }
            }
        }
    }
    setHighlightedDesktop(effects->currentDesktop());   // Ensure selected desktop is highlighted

    windowMoveElevateTimer->stop();

    if (keyboardGrab)
        effects->ungrabKeyboard();
    keyboardGrab = false;
    lastPresentTime = std::chrono::milliseconds::zero();
    effects->stopMouseInterception(this);
    effects->setActiveFullScreenEffect(nullptr);
    if (isUsingPresentWindows()) {
        for (auto it = m_managers.begin(); it != m_managers.end(); ++it) {
            for (WindowMotionManager &manager : *it) {
                manager.unmanageAll();
            }
        }
        m_managers.clear();
        m_proxy = nullptr;
    }

    effects->addRepaintFull();
}

void DesktopGridEffect::globalShortcutChanged(QAction *action, const QKeySequence& seq)
{
    if (action->objectName() != QStringLiteral("ShowDesktopGrid")) {
        return;
    }
    shortcut.clear();
    shortcut.append(seq);
}

bool DesktopGridEffect::isMotionManagerMovingWindows() const
{
    if (isUsingPresentWindows()) {
        for (auto it = m_managers.constBegin(); it != m_managers.constEnd(); ++it) {
            for (const WindowMotionManager &manager : *it) {
                if (manager.areWindowsMoving())
                    return true;
            }
        }
    }
    return false;
}

bool DesktopGridEffect::isUsingPresentWindows() const
{
    return (m_proxy != nullptr);
}

// transforms the geometry of the moved window to a geometry on the desktop
// internal method only used when a window is dropped onto a desktop
QRectF DesktopGridEffect::moveGeometryToDesktop(int desktop) const
{
    QPointF point = unscalePos(m_windowMoveGeometry.topLeft() + cursorPos() - m_windowMoveStartPoint);
    const double scaleFactor = scale[ windowMove->screen()];
    if (posToDesktop(m_windowMoveGeometry.topLeft() + cursorPos() - m_windowMoveStartPoint) != desktop) {
        // topLeft is not on the desktop - check other corners
        // if all corners are not on the desktop the window is bigger than the desktop - no matter what it will look strange
        if (posToDesktop(m_windowMoveGeometry.topRight() + cursorPos() - m_windowMoveStartPoint) == desktop) {
            point = unscalePos(m_windowMoveGeometry.topRight() + cursorPos() - m_windowMoveStartPoint) -
                    QPointF(m_windowMoveGeometry.width(), 0) / scaleFactor;
        } else if (posToDesktop(m_windowMoveGeometry.bottomLeft() + cursorPos() - m_windowMoveStartPoint) == desktop) {
            point = unscalePos(m_windowMoveGeometry.bottomLeft() + cursorPos() - m_windowMoveStartPoint) -
                    QPointF(0, m_windowMoveGeometry.height()) / scaleFactor;
        } else if (posToDesktop(m_windowMoveGeometry.bottomRight() + cursorPos() - m_windowMoveStartPoint) == desktop) {
            point = unscalePos(m_windowMoveGeometry.bottomRight() + cursorPos() - m_windowMoveStartPoint) -
                    QPointF(m_windowMoveGeometry.width(), m_windowMoveGeometry.height()) / scaleFactor;
        }
    }
    return QRectF(point, m_windowMoveGeometry.size() / scaleFactor);
}

void DesktopGridEffect::slotAddDesktop()
{
    effects->setNumberOfDesktops(effects->numberOfDesktops() + 1);
}

void DesktopGridEffect::slotRemoveDesktop()
{
    effects->setNumberOfDesktops(effects->numberOfDesktops() - 1);
}

void DesktopGridEffect::slotRemoveSpecificDesktop(int desktop)
{
    if (effects->numberOfDesktops() <= 1)
        return;

    VirtualDesktopManager *vdm = VirtualDesktopManager::self();
    VirtualDesktop *vd = vdm->desktopForX11Id(desktop);
    if (!vd)
        return;

    const QString id = vd->id();

    // 1. Gather spatial neighbors (empty string = no neighbor).
    VirtualDesktopSpatialMap &smap = vdm->spatialMap();
    using Dir = VirtualDesktopSpatialMap::Direction;
    const QString leftId  = smap.neighbor(id, Dir::Left);
    const QString rightId = smap.neighbor(id, Dir::Right);
    const QString aboveId = smap.neighbor(id, Dir::Above);
    const QString belowId = smap.neighbor(id, Dir::Below);

    // 2. Stitch horizontal neighbors together.
    if (!leftId.isEmpty() && !rightId.isEmpty()) {
        smap.setNeighbor(leftId,  Dir::Right, rightId);
        smap.setNeighbor(rightId, Dir::Left,  leftId);
    } else if (!leftId.isEmpty()) {
        smap.setNeighbor(leftId, Dir::Right, QString());
    } else if (!rightId.isEmpty()) {
        smap.setNeighbor(rightId, Dir::Left, QString());
    }

    // 3. Stitch vertical neighbors together.
    if (!aboveId.isEmpty() && !belowId.isEmpty()) {
        smap.setNeighbor(aboveId, Dir::Below, belowId);
        smap.setNeighbor(belowId, Dir::Above, aboveId);
    } else if (!aboveId.isEmpty()) {
        smap.setNeighbor(aboveId, Dir::Below, QString());
    } else if (!belowId.isEmpty()) {
        smap.setNeighbor(belowId, Dir::Above, QString());
    }

    // 4. In activity-aware spatial mode: remove from the current activity's map only.
    //    After removing, check whether any other activity still references this desktop.
    //    If yes: hide it from this activity but preserve it globally.
    //    If no: fall through to the global-delete path below.
    //
    //    Both paths are deferred to the next event loop iteration to avoid
    //    use-after-free: removeDesktopFromCurrentActivityMap emits spatialMapChanged
    //    synchronously (→ slotSpatialMapChanged → destroyTileOverlays), and
    //    removeVirtualDesktop fires slotNumberDesktopsChanged (→ desktopsRemoved →
    //    destroyTileOverlays). Either would destroy the bridge whose callback is
    //    currently executing.
    if (vdm->isActivityAwareSpatialMode()) {
        QMetaObject::invokeMethod(this, [vdm, id]() {
            vdm->removeDesktopFromCurrentActivityMap(id);
            if (!vdm->isDesktopInAnyActivityMap(id)) {
                vdm->removeVirtualDesktop(id);
            }
        }, Qt::QueuedConnection);
        return;
    }

    // 5. Non-activity-aware path: choose a preferred neighbor to receive orphaned windows
    //    (right > below > left > above > desktop 1).
    auto findNeighborDesktop = [&]() -> int {
        for (const QString &nid : {rightId, belowId, leftId, aboveId}) {
            if (!nid.isEmpty()) {
                VirtualDesktop *nd = vdm->desktopForId(nid);
                if (nd)
                    return nd->x11DesktopNumber();
            }
        }
        return 1;
    };
    const int targetDesktop = findNeighborDesktop();

    // 6. Move all windows from the deleted desktop to the target.
    const auto windows = effects->stackingOrder();
    for (EffectWindow *w : windows) {
        if (!w->isOnAllDesktops() && w->isOnDesktop(desktop))
            effects->windowToDesktop(w, targetDesktop);
    }

    // 7. Defer the actual removal to the next event loop iteration.
    //    removeVirtualDesktop() fires slotNumberDesktopsChanged synchronously,
    //    which destroys the tile overlays — including the bridge whose callback
    //    we're currently executing. Deferring avoids use-after-free.
    QMetaObject::invokeMethod(this, [vdm, id]() {
        vdm->removeVirtualDesktop(id);
    }, Qt::QueuedConnection);
}

void DesktopGridEffect::slotNumberDesktopsChanged(uint old)
{
    if (!activated)
        return;
    const uint desktop = effects->numberOfDesktops();
    if (old < desktop)
        desktopsAdded(old);
    else
        desktopsRemoved(old);
}

void DesktopGridEffect::slotSpatialMapChanged()
{
    // Debounce: multiple spatialMapChanged signals may fire in quick succession
    // during a single desktop removal (map change + layout update + save).
    // Defer the expensive overlay rebuild to the next event loop tick so that
    // all signals coalesce into one rebuild.
    m_spatialRebuildTimer->start();
}

void DesktopGridEffect::desktopsAdded(int old)
{
    const int desktop = effects->numberOfDesktops();
    for (int i = old; i <= effects->numberOfDesktops(); i++) {
        // add a timeline for the new desktop
        QTimeLine *newTimeline = new QTimeLine(zoomDuration, this);
        newTimeline->setEasingCurve(QEasingCurve::InOutSine);
        hoverTimeline.append(newTimeline);
    }

    if (isUsingPresentWindows()) {
        const QList<EffectScreen *> screens = effects->screens();
        for (EffectScreen *screen : screens) {
            for (int i = old+1; i <= effects->numberOfDesktops(); ++i) {
                WindowMotionManager manager;
                const auto stackingOrder = effects->stackingOrder();
                for (EffectWindow * w : stackingOrder) {
                    if (w->isOnDesktop(i) && w->screen() == screen &&isRelevantWithPresentWindows(w)) {
                        manager.manage(w);
                    }
                }
                m_proxy->calculateWindowTransformations(manager.managedWindows(), screen, manager);
                m_managers[screen].append(manager);
            }
        }
    }

    setupGrid();
    destroyTileOverlays();
    createTileOverlays();

    // and repaint
    effects->addRepaintFull();
}

void DesktopGridEffect::desktopsRemoved(int old)
{
    const int desktop = effects->numberOfDesktops();
    for (int i = desktop; i < old; i++) {
        delete hoverTimeline.takeLast();
        if (isUsingPresentWindows()) {
            const QList<EffectScreen *> screens = effects->screens();
            for (EffectScreen *screen : screens) {
                WindowMotionManager& manager = m_managers[screen].last();
                manager.unmanageAll();
                m_managers[screen].removeLast();
            }
        }
    }
    // add removed windows to the last desktop
    if (isUsingPresentWindows()) {
        const QList<EffectScreen *> screens = effects->screens();
        for (EffectScreen *screen : screens) {
            WindowMotionManager& manager = m_managers[screen][desktop - 1];
            const auto stackingOrder = effects->stackingOrder();
            for (EffectWindow * w : stackingOrder) {
                if (manager.isManaging(w))
                    continue;
                if (w->isOnDesktop(desktop) && w->screen() == screen && isRelevantWithPresentWindows(w)) {
                    manager.manage(w);
                }
            }
            m_proxy->calculateWindowTransformations(manager.managedWindows(), screen, manager);
        }
    }

    // Clamp desktop indices that may now be out of bounds.
    if (highlightedDesktop > desktop)
        highlightedDesktop = desktop;
    if (sourceDesktop > desktop)
        sourceDesktop = desktop;

    setupGrid();
    destroyTileOverlays();
    createTileOverlays();

    // and repaint
    effects->addRepaintFull();
}
//TODO: kill this function? or at least keep a consistent numeration with desktops starting from 1
QVector<int> DesktopGridEffect::desktopList(const EffectWindow *w) const
{
    if (w->isOnAllDesktops()) {
        static QVector<int> allDesktops;
        if (allDesktops.count() != effects->numberOfDesktops()) {
            allDesktops.resize(effects->numberOfDesktops());
            for (int i = 0; i < effects->numberOfDesktops(); ++i)
                allDesktops[i] = i;
        }
        return allDesktops;
    }

    QVector<int> desks;
    desks.resize(w->desktops().count());
    int i = 0;

    const QVector<uint> allDesks = w->desktops();
    for (const int desk : allDesks) {
        desks[i++] = desk-1;
    }
    return desks;
}

bool DesktopGridEffect::isActive() const
{
    return (timeline.currentValue() != 0 || activated || (isUsingPresentWindows() && isMotionManagerMovingWindows())) && !effects->isScreenLocked();
}

bool DesktopGridEffect::isRelevantWithPresentWindows(EffectWindow *w) const
{
    if (w->isSpecialWindow() || w->isUtility()) {
        return false;
    }

    if (w->isSkipSwitcher()) {
        return false;
    }

    if (w->isDeleted()) {
        return false;
    }

    if (!w->acceptsFocus()) {
        return false;
    }

    if (!w->isOnCurrentActivity()) {
        return false;
    }

    return true;
}

// ---- Per-tile overlay management ----

void DesktopGridEffect::createTileOverlays()
{
    destroyTileOverlays();

    const int n = effects->numberOfDesktops();
    if (n == 0)
        return;

    const QString qmlPath = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("kwin/effects/desktopgrid/tile_overlay.qml"));
    if (qmlPath.isEmpty()) {
        qWarning() << "DesktopGridEffect: tile_overlay.qml not found";
        return;
    }

    m_tileOverlays.reserve(n);
    m_tileBridges.reserve(n);

    VirtualDesktopManager *vdm = VirtualDesktopManager::self();
    const bool activityAwareSpatial = vdm->isActivityAwareSpatialMode();
    const VirtualDesktopSpatialMap &smap = vdm->spatialMap();

    for (int i = 0; i < n; ++i) {
        const int desktop = i + 1;
        // In activity-aware spatial mode, don't create tiles for desktops hidden
        // from the current activity (i.e. not in this activity's spatial map).
        if (activityAwareSpatial && !smap.isEmpty()) {
            VirtualDesktop *vd = vdm->desktopForX11Id(desktop);
            if (vd && !smap.containsDesktop(vd->id()))
                continue;
        }
        auto *bridge = new TileOverlayBridge(desktop, this);
        bridge->setRemoveCallback([this, desktop]() { slotRemoveSpecificDesktop(desktop); });
        connect(bridge, &TileOverlayBridge::addDesktopRequested,
                this, &DesktopGridEffect::slotAddDesktopInDirection);
        m_tileBridges.append(bridge);

        auto *view = new OffscreenQuickScene(this);
        connect(view, &OffscreenQuickView::repaintNeeded, this, []() {
            effects->addRepaintFull();
        });
        connect(bridge, &TileOverlayBridge::editingChanged, this, [this, view](bool editing) {
            m_editingTileOverlay = editing ? view : nullptr;
        });
        view->rootContext()->setContextProperty(QStringLiteral("bridge"), bridge);
        view->setSource(QUrl::fromLocalFile(qmlPath));

        QQuickItem *rootItem = view->rootItem();
        if (!rootItem) {
            qWarning() << "DesktopGridEffect: failed to load tile_overlay.qml for desktop" << desktop;
            delete view;
            delete m_tileBridges.takeLast();
            continue;
        }

        // Don't show() yet — defer until updateTileOverlayGeometry() confirms
        // non-zero dimensions. Calling show() before geometry is set can
        // trigger a CreatePixmap BadValue X11 error on zero-size surfaces.
        m_tileOverlays.append(view);
    }

    m_tileOverlayGeometryDirty = true;
    updateTileOverlayGeometry();
}

void DesktopGridEffect::destroyTileOverlays()
{
    m_editingTileOverlay = nullptr;
    qDeleteAll(m_tileOverlays);
    m_tileOverlays.clear();
    qDeleteAll(m_tileBridges);
    m_tileBridges.clear();
}

void DesktopGridEffect::updateTileOverlayGeometry()
{
    if (m_tileOverlays.isEmpty())
        return;

    const QList<EffectScreen *> screens = effects->screens();
    if (screens.isEmpty())
        return;

    // Position overlays on the primary (first) screen
    EffectScreen *screen = screens.first();

    if (!scaledOffset.contains(screen) || !scaledSize.contains(screen))
        return;

    const QPointF offset   = scaledOffset[screen];
    const QSizeF  tileSize = scaledSize[screen];

    for (int i = 0; i < m_tileOverlays.count(); ++i) {
        // Each overlay's bridge stores the actual desktop number (1-based),
        // which may differ from i+1 when desktops are skipped for activity
        // filtering in createTileOverlays().
        const int desktop = (i < m_tileBridges.count())
            ? m_tileBridges[i]->desktop()
            : i + 1;

        // Compute 1-based grid cell for this desktop
        QPoint cell;
        if (static_cast<EffectsHandlerImpl*>(effects)->isSpatialMode()) {
            const QPoint coords = effects->desktopGridCoords(desktop); // 0-based (col, row)
            cell = QPoint(coords.x() + 1, coords.y() + 1);
        } else if (orientation == Qt::Horizontal) {
            cell = QPoint((desktop - 1) % gridSize.width() + 1,
                          (desktop - 1) / gridSize.width() + 1);
        } else {
            cell = QPoint((desktop - 1) / gridSize.height() + 1,
                          (desktop - 1) % gridSize.height() + 1);
        }

        const QPointF tl(
            offset.x() + (cell.x() - 1) * (tileSize.width()  + m_effectiveBorder),
            offset.y() + (cell.y() - 1) * (tileSize.height() + m_effectiveBorder));
        const QRect tileRect(tl.toPoint(),
                             QSize(qRound(tileSize.width()), qRound(tileSize.height())));

        // Guard against zero-dimension rects: showing a surface with zero width
        // or height triggers a CreatePixmap BadValue X11 error.  This can happen
        // transiently when geometry hasn't been computed yet (e.g. scaledSize is
        // still zero during the first paint after createTileOverlays()).
        if (tileRect.width() < 1 || tileRect.height() < 1)
            continue;
        // Only call setGeometry when the rect actually changed to avoid
        // triggering repaintNeeded → addRepaintFull() on every paint frame.
        if (m_tileOverlays[i]->geometry() != tileRect) {
            m_tileOverlays[i]->setGeometry(tileRect);
        }

        // Deferred show(): now that we have confirmed non-zero geometry, it is
        // safe to make the view visible.  show() is idempotent — calling it on
        // an already-visible view is a no-op.
        m_tileOverlays[i]->show();
    }
}

void DesktopGridEffect::slotAddDesktopInDirection(int desktop, const QString &direction)
{
    VirtualDesktopManager *vds = VirtualDesktopManager::self();
    const VirtualDesktop *fromVd = vds->desktopForX11Id(desktop);
    if (!fromVd)
        return;

    QString oppDirection;
    if (direction == QStringLiteral("above")) {
        oppDirection = QStringLiteral("below");
    } else if (direction == QStringLiteral("below")) {
        oppDirection = QStringLiteral("above");
    } else if (direction == QStringLiteral("left")) {
        oppDirection = QStringLiteral("right");
    } else if (direction == QStringLiteral("right")) {
        oppDirection = QStringLiteral("left");
    } else {
        return;
    }

    // Capture the source desktop ID before deferring — the bridge that called us
    // will be destroyed when createVirtualDesktop fires slotNumberDesktopsChanged.
    const QString fromId = fromVd->id();

    // Defer to next event loop iteration to avoid use-after-free:
    // createVirtualDesktop() synchronously fires slotNumberDesktopsChanged →
    // desktopsAdded → destroyTileOverlays, which deletes the bridge whose
    // signal we're currently handling.
    QMetaObject::invokeMethod(this, [this, vds, fromId, direction, oppDirection]() {
        const VirtualDesktop *newVd = vds->createVirtualDesktop(vds->count());
        if (!newVd)
            return;

        // Link the two desktops as spatial neighbors in both directions.
        // Batch the two calls so save/rebuild/signals happen only once.
        vds->beginBatchSpatialUpdate();
        vds->setSpatialNeighbor(fromId,      direction,    newVd->id());
        vds->setSpatialNeighbor(newVd->id(), oppDirection, fromId);
        vds->endBatchSpatialUpdate();

        // desktopsAdded() already rebuilt grid+overlays, but neighbor links
        // weren't set yet at that point. Rebuild once more with correct state.
        setupGrid();
        destroyTileOverlays();
        createTileOverlays();
        effects->addRepaintFull();
    }, Qt::QueuedConnection);
}

} // namespace

#include "desktopgrid.moc"

