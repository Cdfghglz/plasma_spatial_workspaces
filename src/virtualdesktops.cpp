/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "virtualdesktops.h"
#include "activities.h"
#include "input.h"
// KDE
#include <KConfigGroup>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <NETWM>

#include <KWaylandServer/plasmavirtualdesktop_interface.h>
// Qt
#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <QDebug>
namespace KWin {

static bool s_loadingDesktopSettings = false;

static QString generateDesktopId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

VirtualDesktop::VirtualDesktop(QObject *parent)
    : QObject(parent)
{
}

VirtualDesktop::~VirtualDesktop()
{
    Q_EMIT aboutToBeDestroyed();
}

void VirtualDesktopManager::setVirtualDesktopManagement(KWaylandServer::PlasmaVirtualDesktopManagementInterface *management)
{
    using namespace KWaylandServer;
    Q_ASSERT(!m_virtualDesktopManagement);
    m_virtualDesktopManagement = management;

    auto createPlasmaVirtualDesktop = [this](VirtualDesktop *desktop) {
        PlasmaVirtualDesktopInterface *pvd = m_virtualDesktopManagement->createDesktop(desktop->id(), desktop->x11DesktopNumber() - 1);
        pvd->setName(desktop->name());
        pvd->sendDone();

        connect(desktop, &VirtualDesktop::nameChanged, pvd,
            [this, desktop, pvd] {
                pvd->setName(desktop->name());
                pvd->sendDone();
                save();
            }
        );
        connect(pvd, &PlasmaVirtualDesktopInterface::activateRequested, this,
            [this, desktop] {
                setCurrent(desktop);
            }
        );
    };

    connect(this, &VirtualDesktopManager::desktopCreated, m_virtualDesktopManagement, createPlasmaVirtualDesktop);

    connect(this, &VirtualDesktopManager::rowsChanged, m_virtualDesktopManagement,
        [this](uint rows) {
            m_virtualDesktopManagement->setRows(rows);
            m_virtualDesktopManagement->sendDone();
        }
    );

    //handle removed: from VirtualDesktopManager to the wayland interface
    connect(this, &VirtualDesktopManager::desktopRemoved, m_virtualDesktopManagement,
        [this](VirtualDesktop *desktop) {
            m_virtualDesktopManagement->removeDesktop(desktop->id());
        }
    );

    //create a new desktop when the client asks to
    connect (m_virtualDesktopManagement, &PlasmaVirtualDesktopManagementInterface::desktopCreateRequested, this,
        [this](const QString &name, quint32 position) {
            createVirtualDesktop(position, name);
        }
    );

    //remove when the client asks to
    connect (m_virtualDesktopManagement, &PlasmaVirtualDesktopManagementInterface::desktopRemoveRequested, this,
        [this](const QString &id) {
            //here there can be some nice kauthorized check?
            //remove only from VirtualDesktopManager, the other connections will remove it from m_virtualDesktopManagement as well
            removeVirtualDesktop(id);
        }
    );

    std::for_each(m_desktops.constBegin(), m_desktops.constEnd(), createPlasmaVirtualDesktop);

    //Now we are sure all ids are there
    save();

    connect(this, &VirtualDesktopManager::currentChanged, m_virtualDesktopManagement,
        [this]() {
            const QList <PlasmaVirtualDesktopInterface *> deskIfaces = m_virtualDesktopManagement->desktops();
            for (auto *deskInt : deskIfaces) {
                if (deskInt->id() == currentDesktop()->id()) {
                    deskInt->setActive(true);
                } else {
                    deskInt->setActive(false);
                }
            }
        }
    );
}

void VirtualDesktop::setId(const QString &id)
{
    Q_ASSERT(m_id.isEmpty());
    m_id = id;
}

void VirtualDesktop::setX11DesktopNumber(uint number)
{
    //x11DesktopNumber can be changed now
    if (static_cast<uint>(m_x11DesktopNumber) == number) {
        return;
    }

    m_x11DesktopNumber = number;

    if (m_x11DesktopNumber != 0) {
        Q_EMIT x11DesktopNumberChanged();
    }
}

void VirtualDesktop::setName(const QString &name)
{
    if (m_name == name) {
        return;
    }
    m_name = name;
    Q_EMIT nameChanged();
}

// ---- VirtualDesktopSpatialMap ----

QString VirtualDesktopSpatialMap::directionSuffix(Direction direction)
{
    switch (direction) {
    case Direction::Above: return QStringLiteral("_Above");
    case Direction::Below: return QStringLiteral("_Below");
    case Direction::Left:  return QStringLiteral("_Left");
    case Direction::Right: return QStringLiteral("_Right");
    }
    Q_UNREACHABLE();
}

void VirtualDesktopSpatialMap::setNeighbor(const QString &desktopId, Direction direction, const QString &neighborId)
{
    auto &entry = m_neighbors[desktopId];
    switch (direction) {
    case Direction::Above: entry.above = neighborId; break;
    case Direction::Below: entry.below = neighborId; break;
    case Direction::Left:  entry.left  = neighborId; break;
    case Direction::Right: entry.right = neighborId; break;
    }
}

QString VirtualDesktopSpatialMap::neighbor(const QString &desktopId, Direction direction) const
{
    auto it = m_neighbors.constFind(desktopId);
    if (it == m_neighbors.constEnd()) {
        return QString();
    }
    switch (direction) {
    case Direction::Above: return it->above;
    case Direction::Below: return it->below;
    case Direction::Left:  return it->left;
    case Direction::Right: return it->right;
    }
    Q_UNREACHABLE();
}

void VirtualDesktopSpatialMap::removeDesktop(const QString &desktopId)
{
    m_neighbors.remove(desktopId);
    // Also clear any references to this desktop as a neighbor
    for (auto &entry : m_neighbors) {
        if (entry.above == desktopId) entry.above.clear();
        if (entry.below == desktopId) entry.below.clear();
        if (entry.left  == desktopId) entry.left.clear();
        if (entry.right == desktopId) entry.right.clear();
    }
}

bool VirtualDesktopSpatialMap::isEmpty() const
{
    return m_neighbors.isEmpty();
}

bool VirtualDesktopSpatialMap::containsDesktop(const QString &desktopId) const
{
    return m_neighbors.contains(desktopId);
}

void VirtualDesktopSpatialMap::mergeFrom(const VirtualDesktopSpatialMap &other)
{
    // Merge entries from @p other into this map.
    // - Missing desktops are added wholesale.
    // - For existing desktops, empty neighbor slots are filled from @p other
    //   (non-empty slots are NOT overwritten).
    for (auto it = other.m_neighbors.constBegin(); it != other.m_neighbors.constEnd(); ++it) {
        if (!m_neighbors.contains(it.key())) {
            m_neighbors[it.key()] = it.value();
        } else {
            auto &local = m_neighbors[it.key()];
            const auto &remote = it.value();
            if (local.above.isEmpty() && !remote.above.isEmpty()) local.above = remote.above;
            if (local.below.isEmpty() && !remote.below.isEmpty()) local.below = remote.below;
            if (local.left.isEmpty()  && !remote.left.isEmpty())  local.left  = remote.left;
            if (local.right.isEmpty() && !remote.right.isEmpty()) local.right = remote.right;
        }
    }
}

void VirtualDesktopSpatialMap::load(const KConfigGroup &group)
{
    m_neighbors.clear();

    // Scan for all Spatial_<uuid>_<Direction> keys in the group
    const QMap<QString, QString> entries = group.entryMap();
    const QString prefix = QStringLiteral("Spatial_");
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        const QString &key = it.key();
        if (!key.startsWith(prefix)) {
            continue;
        }
        // Key format: Spatial_<uuid>_<Direction>
        // Find last underscore to split direction suffix
        const int lastUnderscore = key.lastIndexOf(QLatin1Char('_'));
        if (lastUnderscore <= prefix.length()) {
            continue; // malformed
        }
        const QString desktopId = key.mid(prefix.length(), lastUnderscore - prefix.length());
        const QString dirStr = key.mid(lastUnderscore + 1);
        const QString neighborId = it.value();

        if (neighborId.isEmpty()) {
            continue;
        }

        Direction dir;
        if (dirStr == QStringLiteral("Above")) {
            dir = Direction::Above;
        } else if (dirStr == QStringLiteral("Below")) {
            dir = Direction::Below;
        } else if (dirStr == QStringLiteral("Left")) {
            dir = Direction::Left;
        } else if (dirStr == QStringLiteral("Right")) {
            dir = Direction::Right;
        } else {
            continue; // unknown suffix
        }

        setNeighbor(desktopId, dir, neighborId);
    }
}

void VirtualDesktopSpatialMap::save(KConfigGroup &group, const QStringList &knownIds) const
{
    const QString prefix = QStringLiteral("Spatial_");

    // Remove stale entries (desktops that no longer exist)
    const QMap<QString, QString> existing = group.entryMap();
    for (auto it = existing.constBegin(); it != existing.constEnd(); ++it) {
        const QString &key = it.key();
        if (!key.startsWith(prefix)) {
            continue;
        }
        const int lastUnderscore = key.lastIndexOf(QLatin1Char('_'));
        if (lastUnderscore <= prefix.length()) {
            group.deleteEntry(key);
            continue;
        }
        const QString desktopId = key.mid(prefix.length(), lastUnderscore - prefix.length());
        if (!knownIds.contains(desktopId)) {
            group.deleteEntry(key);
        }
    }

    // Write current entries
    for (auto it = m_neighbors.constBegin(); it != m_neighbors.constEnd(); ++it) {
        const QString &desktopId = it.key();
        const DesktopNeighbors &neighbors = it.value();
        const QString keyBase = prefix + desktopId;

        auto writeOrDelete = [&](const QString &dirSuffix, const QString &neighborId) {
            const QString key = keyBase + dirSuffix;
            if (neighborId.isEmpty()) {
                group.deleteEntry(key);
            } else {
                group.writeEntry(key, neighborId);
            }
        };

        writeOrDelete(directionSuffix(Direction::Above), neighbors.above);
        writeOrDelete(directionSuffix(Direction::Below), neighbors.below);
        writeOrDelete(directionSuffix(Direction::Left),  neighbors.left);
        writeOrDelete(directionSuffix(Direction::Right), neighbors.right);
    }
}

void VirtualDesktopSpatialMap::loadJson(const QString &filePath)
{
    QFile file(filePath);
    if (!file.exists()) {
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "VirtualDesktopSpatialMap: cannot open" << filePath << "for reading";
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "VirtualDesktopSpatialMap: failed to parse" << filePath
                   << "-" << parseError.errorString();
        return;
    }

    m_neighbors.clear();
    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QString desktopId = it.key();
        if (!it.value().isObject()) {
            continue;
        }
        const QJsonObject nbr = it.value().toObject();
        auto apply = [&](const QString &key, Direction dir) {
            const QString neighborId = nbr.value(key).toString();
            if (!neighborId.isEmpty()) {
                setNeighbor(desktopId, dir, neighborId);
            }
        };
        apply(QStringLiteral("above"), Direction::Above);
        apply(QStringLiteral("below"), Direction::Below);
        apply(QStringLiteral("left"),  Direction::Left);
        apply(QStringLiteral("right"), Direction::Right);
    }
}

void VirtualDesktopSpatialMap::saveJson(const QString &filePath, const QStringList &knownIds) const
{
    QJsonObject root;
    for (auto it = m_neighbors.constBegin(); it != m_neighbors.constEnd(); ++it) {
        const QString &desktopId = it.key();
        if (!knownIds.contains(desktopId)) {
            continue; // omit stale entries for removed desktops
        }
        const DesktopNeighbors &n = it.value();
        QJsonObject nbr;
        nbr[QStringLiteral("above")] = n.above;
        nbr[QStringLiteral("below")] = n.below;
        nbr[QStringLiteral("left")]  = n.left;
        nbr[QStringLiteral("right")] = n.right;
        root[desktopId] = nbr;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "VirtualDesktopSpatialMap: cannot open" << filePath << "for writing";
        return;
    }
    file.write(QJsonDocument(root).toJson());
    if (!file.commit()) {
        qWarning() << "VirtualDesktopSpatialMap: failed to commit" << filePath;
    }
}

// ---- VirtualDesktopGrid ----

VirtualDesktopGrid::VirtualDesktopGrid()
    : m_size(1, 2) // Default to tow rows
    , m_grid(QVector<QVector<VirtualDesktop*>>{QVector<VirtualDesktop*>{}, QVector<VirtualDesktop*>{}})
{
}

VirtualDesktopGrid::~VirtualDesktopGrid() = default;

void VirtualDesktopGrid::update(const QSize &size, Qt::Orientation orientation, const QVector<VirtualDesktop*> &desktops)
{
    // Set private variables
    m_size = size;
    const uint width = size.width();
    const uint height = size.height();

    m_grid.clear();
    auto it = desktops.begin();
    auto end = desktops.end();
    if (orientation == Qt::Horizontal) {
        for (uint y = 0; y < height; ++y) {
            QVector<VirtualDesktop*> row;
            for (uint x = 0; x < width && it != end; ++x) {
                row << *it;
                it++;
            }
            m_grid << row;
        }
    } else {
        for (uint y = 0; y < height; ++y) {
            m_grid << QVector<VirtualDesktop*>();
        }
        for (uint x = 0; x < width; ++x) {
            for (uint y = 0; y < height && it != end; ++y) {
                auto &row = m_grid[y];
                row << *it;
                it++;
            }
        }
    }
}

void VirtualDesktopGrid::updateFromSpatialMap(const VirtualDesktopSpatialMap &spatialMap, const QVector<VirtualDesktop*> &desktops)
{
    // BFS from each desktop, assigning relative (col, row) coordinates
    // based on neighbor links.  Unvisited desktops get appended at the end.
    using Dir = VirtualDesktopSpatialMap::Direction;

    // Build a desktop-id-to-pointer lookup
    QHash<QString, VirtualDesktop*> byId;
    for (auto *vd : desktops) {
        byId[vd->id()] = vd;
    }

    // Assign grid coordinates via BFS
    QHash<QString, QPoint> coords;
    QSet<QString> visited;
    QQueue<QString> queue;

    // Start from the first desktop
    if (desktops.isEmpty()) {
        m_grid.clear();
        m_size = QSize(0, 0);
        return;
    }

    // Find the top-left desktop: one with no above and no left neighbor
    QString startId;
    for (auto *vd : desktops) {
        const QString &id = vd->id();
        if (spatialMap.neighbor(id, Dir::Above).isEmpty() &&
            spatialMap.neighbor(id, Dir::Left).isEmpty() &&
            spatialMap.containsDesktop(id)) {
            startId = id;
            break;
        }
    }
    if (startId.isEmpty()) {
        startId = desktops.first()->id();
    }

    coords[startId] = QPoint(0, 0);
    visited.insert(startId);
    queue.enqueue(startId);

    while (!queue.isEmpty()) {
        const QString current = queue.dequeue();
        const QPoint pos = coords[current];

        auto tryNeighbor = [&](Dir dir, QPoint offset) {
            const QString nbId = spatialMap.neighbor(current, dir);
            if (!nbId.isEmpty() && !visited.contains(nbId) && byId.contains(nbId)) {
                coords[nbId] = pos + offset;
                visited.insert(nbId);
                queue.enqueue(nbId);
            }
        };

        tryNeighbor(Dir::Right, QPoint(1, 0));
        tryNeighbor(Dir::Below, QPoint(0, 1));
        tryNeighbor(Dir::Left, QPoint(-1, 0));
        tryNeighbor(Dir::Above, QPoint(0, -1));
    }

    // Normalize coordinates so minimum is (0, 0)
    int minX = 0, minY = 0;
    for (auto it = coords.constBegin(); it != coords.constEnd(); ++it) {
        minX = qMin(minX, it.value().x());
        minY = qMin(minY, it.value().y());
    }
    int maxX = 0, maxY = 0;
    for (auto it = coords.begin(); it != coords.end(); ++it) {
        it.value() -= QPoint(minX, minY);
        maxX = qMax(maxX, it.value().x());
        maxY = qMax(maxY, it.value().y());
    }

    // Desktops not in the spatial map for this activity are simply omitted
    // from the grid — they belong to other activities and should not appear.

    // Recompute maxX/maxY after adding stragglers
    maxX = 0;
    maxY = 0;
    for (auto it = coords.constBegin(); it != coords.constEnd(); ++it) {
        maxX = qMax(maxX, it.value().x());
        maxY = qMax(maxY, it.value().y());
    }

    const int width = maxX + 1;
    const int height = maxY + 1;

    // Build the grid
    m_grid.clear();
    m_grid.resize(height);
    for (int y = 0; y < height; ++y) {
        m_grid[y].fill(nullptr, width);
    }
    for (auto it = coords.constBegin(); it != coords.constEnd(); ++it) {
        VirtualDesktop *vd = byId.value(it.key());
        if (vd) {
            const QPoint &p = it.value();
            m_grid[p.y()][p.x()] = vd;
        }
    }

    m_size = QSize(width, height);
}

QPoint VirtualDesktopGrid::gridCoords(uint id) const
{
    return gridCoords(VirtualDesktopManager::self()->desktopForX11Id(id));
}

QPoint VirtualDesktopGrid::gridCoords(VirtualDesktop *vd) const
{
    for (int y = 0; y < m_grid.count(); ++y) {
        const auto &row = m_grid.at(y);
        for (int x = 0; x < row.count(); ++x) {
            if (row.at(x) == vd) {
                return QPoint(x, y);
            }
        }
    }
    return QPoint(-1, -1);
}

VirtualDesktop *VirtualDesktopGrid::at(const QPoint &coords) const
{
    if (coords.y() >= m_grid.count()) {
        return nullptr;
    }
    const auto &row = m_grid.at(coords.y());
    if (coords.x() >= row.count()) {
        return nullptr;
    }
    return row.at(coords.x());
}

KWIN_SINGLETON_FACTORY_VARIABLE(VirtualDesktopManager, s_manager)

VirtualDesktopManager::VirtualDesktopManager(QObject *parent)
    : QObject(parent)
    , m_navigationWrapsAround(false)
    , m_rootInfo(nullptr)
{
}

VirtualDesktopManager::~VirtualDesktopManager()
{
    s_manager = nullptr;
}

static const QString &defaultActivityKey()
{
    static const QString key = QStringLiteral("__default__");
    return key;
}

VirtualDesktopSpatialMap &VirtualDesktopManager::activeSpatialMap()
{
    if (Activities *activities = Activities::self()) {
        const QString activityId = activities->current();
        if (!activityId.isEmpty()) {
            auto it = m_spatialMaps.find(activityId);
            if (it != m_spatialMaps.end() && !it.value().isEmpty()) {
                return it.value();
            }
            // Per-activity map missing or empty — seed from __default__ so that
            // the first write to a new activity inherits the existing layout.
            auto def = m_spatialMaps.constFind(defaultActivityKey());
            if (def != m_spatialMaps.constEnd() && !def.value().isEmpty()) {
                m_spatialMaps[activityId] = def.value();
            }
            return m_spatialMaps[activityId];
        }
        // Activities service present but current activity not yet set (startup race).
        // Return a throwaway slot so transient writes don't pollute __default__.
        return m_pendingSpatialMap;
    }
    return m_spatialMaps[defaultActivityKey()];
}

const VirtualDesktopSpatialMap &VirtualDesktopManager::activeSpatialMap() const
{
    static const VirtualDesktopSpatialMap empty;
    if (Activities *activities = Activities::self()) {
        const QString activityId = activities->current();
        if (activityId.isEmpty()) {
            // Activities present but not yet initialized — no map to read.
            return empty;
        }
        auto it = m_spatialMaps.constFind(activityId);
        if (it != m_spatialMaps.constEnd() && !it.value().isEmpty()) {
            return it.value();
        }
        // Per-activity map missing or empty — fall back to __default__.
        // This handles the common case where per-activity JSON files haven't
        // been populated yet (e.g. first boot with Activities enabled).
        auto def = m_spatialMaps.constFind(defaultActivityKey());
        return (def != m_spatialMaps.constEnd()) ? def.value() : empty;
    }
    auto it = m_spatialMaps.constFind(defaultActivityKey());
    return (it != m_spatialMaps.constEnd()) ? it.value() : empty;
}

void VirtualDesktopManager::initActivities()
{
    if (Activities *activities = Activities::self()) {
        connect(activities, &Activities::removed, this, &VirtualDesktopManager::slotActivityRemoved,
                Qt::UniqueConnection);
        // When the current activity changes, the active spatial map changes too.
        // Re-compute the grid layout and notify callers (e.g. KWin scripts via D-Bus).
        connect(activities, &Activities::currentChanged, this,
                [this, firstTime = true](const QString &) mutable {
                    if (m_spatialMode) {
                        if (firstTime) {
                            // First currentChanged after startup: the activity
                            // was unknown during load() so updateLayout() could
                            // not push spatial dimensions to _NET_DESKTOP_LAYOUT.
                            // Do the full updateSpatialLayout() now that we know
                            // which activity map to use.
                            firstTime = false;
                            m_grid.updateFromSpatialMap(activeSpatialMap(), m_desktops);
                            m_rows = qMax(1, m_grid.height());
                            updateSpatialLayout();
                            QTimer::singleShot(0, this, [this]() {
                                Q_EMIT layoutChanged(m_grid.width(), m_rows);
                                Q_EMIT rowsChanged(m_rows);
                                Q_EMIT spatialMapChanged();
                            });
                            return;
                        }
                        // Subsequent activity switches: rebuild the internal
                        // grid but do NOT call updateSpatialLayout() —
                        // _NET_DESKTOP_LAYOUT uses max dimensions across all
                        // activities and must not change on activity switch
                        // (causes plasmashell containment churn and memory leak).
                        m_grid.updateFromSpatialMap(activeSpatialMap(), m_desktops);
                        m_rows = qMax(1, m_grid.height());
                        // Coalesce signals into a single event-loop tick so
                        // plasmashell processes one structural change, not three.
                        QTimer::singleShot(0, this, [this]() {
                            Q_EMIT layoutChanged(m_grid.width(), m_rows);
                            Q_EMIT rowsChanged(m_rows);
                            Q_EMIT spatialMapChanged();
                        });
                    }
                },
                Qt::UniqueConnection);
    }
}

void VirtualDesktopManager::slotActivityRemoved(const QString &activityId)
{
    m_spatialMaps.remove(activityId);
    const QString path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/spatial-desktop-nav-") + activityId + QStringLiteral(".json");
    QFile::remove(path);
}

bool VirtualDesktopManager::isActivityAwareSpatialMode() const
{
    return m_spatialMode && (Activities::self() != nullptr);
}

bool VirtualDesktopManager::isDesktopInAnyActivityMap(const QString &desktopId) const
{
    for (const auto &smap : m_spatialMaps) {
        if (smap.containsDesktop(desktopId)) {
            return true;
        }
    }
    return false;
}

void VirtualDesktopManager::removeDesktopFromCurrentActivityMap(const QString &desktopId)
{
    activeSpatialMap().removeDesktop(desktopId);
    save();
    updateSpatialLayout();
    Q_EMIT spatialMapChanged();
}

void VirtualDesktopManager::setRootInfo(NETRootInfo *info)
{
    m_rootInfo = info;

    // Nothing will be connected to rootInfo
    if (m_rootInfo) {
        if (!m_spatialMode) {
            // Only write the simple rows-based grid when NOT in spatial mode.
            // In spatial mode updateLayout() (called by updateRootInfo below)
            // derives the grid from the neighbor graph; writing a naive grid
            // here would briefly flash wrong dimensions.
            int columns = count() / m_rows;
            if (count() % m_rows > 0) {
                columns++;
            }
            m_rootInfo->setDesktopLayout(NET::OrientationHorizontal, columns, m_rows, NET::DesktopLayoutCornerTopLeft);
        }
        updateRootInfo();
        m_rootInfo->setCurrentDesktop(currentDesktop()->x11DesktopNumber());
        for (auto *vd : qAsConst(m_desktops)) {
            m_rootInfo->setDesktopName(vd->x11DesktopNumber(), vd->name().toUtf8().data());
        }
    }
}

uint VirtualDesktopManager::above(uint id, bool wrap) const
{
    auto vd = above(desktopForX11Id(id), wrap);
    return vd ? vd->x11DesktopNumber() : 0;
}

VirtualDesktop *VirtualDesktopManager::above(VirtualDesktop *desktop, bool wrap) const
{
    Q_ASSERT(m_current);
    if (!desktop) {
        desktop = m_current;
    }

    if (m_spatialMode) {
        const QString neighborId = activeSpatialMap().neighbor(desktop->id(), VirtualDesktopSpatialMap::Direction::Above);
        if (!neighborId.isEmpty()) {
            VirtualDesktop *neighbor = desktopForId(neighborId);
            if (neighbor) {
                return neighbor;
            }
        }
        // No spatial neighbor set: stay on current desktop (no wrap in spatial mode)
        return desktop;
    }

    QPoint coords = m_grid.gridCoords(desktop);
    Q_ASSERT(coords.x() >= 0);
    while (true) {
        coords.ry()--;
        if (coords.y() < 0) {
            if (wrap) {
                coords.setY(m_grid.height() - 1);
            } else {
                return desktop; // Already at the top-most desktop
            }
        }
        if (VirtualDesktop *vd = m_grid.at(coords)) {
            return vd;
        }
    }
    return nullptr;
}

uint VirtualDesktopManager::toRight(uint id, bool wrap) const
{
    auto vd = toRight(desktopForX11Id(id), wrap);
    return vd ? vd->x11DesktopNumber() : 0;
}

VirtualDesktop *VirtualDesktopManager::toRight(VirtualDesktop *desktop, bool wrap) const
{
    Q_ASSERT(m_current);
    if (!desktop) {
        desktop = m_current;
    }

    if (m_spatialMode) {
        const QString neighborId = activeSpatialMap().neighbor(desktop->id(), VirtualDesktopSpatialMap::Direction::Right);
        if (!neighborId.isEmpty()) {
            VirtualDesktop *neighbor = desktopForId(neighborId);
            if (neighbor) {
                return neighbor;
            }
        }
        return desktop;
    }

    QPoint coords = m_grid.gridCoords(desktop);
    Q_ASSERT(coords.x() >= 0);
    while (true) {
        coords.rx()++;
        if (coords.x() >= m_grid.width()) {
            if (wrap) {
                coords.setX(0);
            } else {
                return desktop; // Already at the right-most desktop
            }
        }
        if (VirtualDesktop *vd = m_grid.at(coords)) {
            return vd;
        }
    }
    return nullptr;
}

uint VirtualDesktopManager::below(uint id, bool wrap) const
{
    auto vd = below(desktopForX11Id(id), wrap);
    return vd ? vd->x11DesktopNumber() : 0;
}

VirtualDesktop *VirtualDesktopManager::below(VirtualDesktop *desktop, bool wrap) const
{
    Q_ASSERT(m_current);
    if (!desktop) {
        desktop = m_current;
    }

    if (m_spatialMode) {
        const QString neighborId = activeSpatialMap().neighbor(desktop->id(), VirtualDesktopSpatialMap::Direction::Below);
        if (!neighborId.isEmpty()) {
            VirtualDesktop *neighbor = desktopForId(neighborId);
            if (neighbor) {
                return neighbor;
            }
        }
        return desktop;
    }

    QPoint coords = m_grid.gridCoords(desktop);
    Q_ASSERT(coords.x() >= 0);
    while (true) {
        coords.ry()++;
        if (coords.y() >= m_grid.height()) {
            if (wrap) {
                coords.setY(0);
            } else {
                // Already at the bottom-most desktop
                return desktop;
            }
        }
        if (VirtualDesktop *vd = m_grid.at(coords)) {
            return vd;
        }
    }
    return nullptr;
}

uint VirtualDesktopManager::toLeft(uint id, bool wrap) const
{
    auto vd = toLeft(desktopForX11Id(id), wrap);
    return vd ? vd->x11DesktopNumber() : 0;
}

VirtualDesktop *VirtualDesktopManager::toLeft(VirtualDesktop *desktop, bool wrap) const
{
    Q_ASSERT(m_current);
    if (!desktop) {
        desktop = m_current;
    }

    if (m_spatialMode) {
        const QString neighborId = activeSpatialMap().neighbor(desktop->id(), VirtualDesktopSpatialMap::Direction::Left);
        if (!neighborId.isEmpty()) {
            VirtualDesktop *neighbor = desktopForId(neighborId);
            if (neighbor) {
                return neighbor;
            }
        }
        return desktop;
    }

    QPoint coords = m_grid.gridCoords(desktop);
    Q_ASSERT(coords.x() >= 0);
    while (true) {
        coords.rx()--;
        if (coords.x() < 0) {
            if (wrap) {
                coords.setX(m_grid.width() - 1);
            } else {
                return desktop; // Already at the left-most desktop
            }
        }
        if (VirtualDesktop *vd = m_grid.at(coords)) {
            return vd;
        }
    }
    return nullptr;
}

VirtualDesktop *VirtualDesktopManager::next(VirtualDesktop *desktop, bool wrap) const
{
    Q_ASSERT(m_current);
    if (!desktop) {
        desktop = m_current;
    }
    auto it = std::find(m_desktops.begin(), m_desktops.end(), desktop);
    Q_ASSERT(it != m_desktops.end());
    it++;
    if (it == m_desktops.end()) {
        if (wrap) {
            return m_desktops.first();
        } else {
            return desktop;
        }
    }
    return *it;
}

VirtualDesktop *VirtualDesktopManager::previous(VirtualDesktop *desktop, bool wrap) const
{
    Q_ASSERT(m_current);
    if (!desktop) {
        desktop = m_current;
    }
    auto it = std::find(m_desktops.begin(), m_desktops.end(), desktop);
    Q_ASSERT(it != m_desktops.end());
    if (it == m_desktops.begin()) {
        if (wrap) {
            return m_desktops.last();
        } else {
            return desktop;
        }
    }
    it--;
    return *it;
}

VirtualDesktop *VirtualDesktopManager::desktopForX11Id(uint id) const
{
    if (id == 0 || id > count()) {
        return nullptr;
    }
    return m_desktops.at(id - 1);
}

VirtualDesktop *VirtualDesktopManager::desktopForId(const QString &id) const
{
    auto desk = std::find_if(
        m_desktops.constBegin(),
        m_desktops.constEnd(),
        [id] (const VirtualDesktop *desk ) {
            return desk->id() == id;
        }
    );

    if (desk != m_desktops.constEnd()) {
        return *desk;
    }

    return nullptr;
}

VirtualDesktop *VirtualDesktopManager::createVirtualDesktop(uint position, const QString &name)
{
    //too many, can't insert new ones
    if ((uint)m_desktops.count() == VirtualDesktopManager::maximum()) {
        return nullptr;
    }

    position = qBound(0u, position, static_cast<uint>(m_desktops.count()));

    QString desktopName = name;
    if (desktopName.isEmpty()) {
        desktopName = defaultName(position + 1);
    }

    auto *vd = new VirtualDesktop(this);
    vd->setX11DesktopNumber(position + 1);
    vd->setId(generateDesktopId());
    vd->setName(desktopName);

    connect(vd, &VirtualDesktop::nameChanged, this,
        [this, vd]() {
            if (m_rootInfo) {
                m_rootInfo->setDesktopName(vd->x11DesktopNumber(), vd->name().toUtf8().data());
            }
        }
    );

    if (m_rootInfo) {
        m_rootInfo->setDesktopName(vd->x11DesktopNumber(), vd->name().toUtf8().data());
    }

    m_desktops.insert(position, vd);

    //update the id of displaced desktops
    for (uint i = position + 1; i < (uint)m_desktops.count(); ++i) {
        m_desktops[i]->setX11DesktopNumber(i + 1);
        if (m_rootInfo) {
            m_rootInfo->setDesktopName(i + 1, m_desktops[i]->name().toUtf8().data());
        }
    }

    save();

    updateRootInfo();
    Q_EMIT desktopCreated(vd);
    Q_EMIT countChanged(m_desktops.count()-1, m_desktops.count());
    return vd;
}

void VirtualDesktopManager::removeVirtualDesktop(const QString &id)
{
    auto desktop = desktopForId(id);
    if (desktop) {
        removeVirtualDesktop(desktop);
    }
}

void VirtualDesktopManager::removeVirtualDesktop(VirtualDesktop *desktop)
{
    //don't end up without any desktop
    if (m_desktops.count() == 1) {
        return;
    }

    if (isActivityAwareSpatialMode()) {
        // Only remove the desktop from the current activity's spatial map.
        // If other activities still reference this desktop, preserve it
        // globally — do not truly delete it.
        activeSpatialMap().removeDesktop(desktop->id());
        const bool inAnyMap = isDesktopInAnyActivityMap(desktop->id());
        if (inAnyMap) {
            save();
            updateSpatialLayout();
            Q_EMIT spatialMapChanged();
            return;
        }
        // No activity references this desktop any more; fall through to
        // global deletion.
    } else {
        // Remove from all per-activity spatial maps before deleting.
        for (auto &smap : m_spatialMaps) {
            smap.removeDesktop(desktop->id());
        }
    }

    // Notify effects *before* renumbering and grid rebuild so they can snapshot
    // the desktop's current grid position for removal animations.
    Q_EMIT desktopAboutToBeRemoved(desktop);

    const uint oldCurrent = m_current->x11DesktopNumber();
    const uint i = desktop->x11DesktopNumber() - 1;
    m_desktops.remove(i);

    for (uint j = i; j < (uint)m_desktops.count(); ++j) {
        m_desktops[j]->setX11DesktopNumber(j + 1);
        if (m_rootInfo) {
            m_rootInfo->setDesktopName(j + 1, m_desktops[j]->name().toUtf8().data());
        }
    }
    // Clear the stale name at the old last position so _NET_DESKTOP_NAMES
    // doesn't retain a phantom entry (off-by-one that confuses plasmashell).
    if (m_rootInfo) {
        m_rootInfo->setDesktopName(m_desktops.count() + 1, "");
    }

    const uint newCurrent = qMin(oldCurrent, (uint)m_desktops.count());
    m_current = m_desktops.at(newCurrent - 1);
    if (oldCurrent != newCurrent) {
        Q_EMIT currentChanged(oldCurrent, newCurrent);
    }

    save();

    updateRootInfo();
    Q_EMIT desktopRemoved(desktop);
    Q_EMIT countChanged(m_desktops.count()+1, m_desktops.count());

    desktop->deleteLater();
}

uint VirtualDesktopManager::current() const
{
    return m_current ? m_current->x11DesktopNumber() : 0;
}

VirtualDesktop *VirtualDesktopManager::currentDesktop() const
{
    return m_current;
}

bool VirtualDesktopManager::setCurrent(uint newDesktop)
{
    if (newDesktop < 1 || newDesktop > count() || newDesktop == current()) {
        return false;
    }
    auto d = desktopForX11Id(newDesktop);
    Q_ASSERT(d);
    return setCurrent(d);
}

bool VirtualDesktopManager::setCurrent(VirtualDesktop *newDesktop)
{
    Q_ASSERT(newDesktop);
    if (m_current == newDesktop) {
        return false;
    }
    const uint oldDesktop = current();
    m_current = newDesktop;
    Q_EMIT currentChanged(oldDesktop, newDesktop->x11DesktopNumber());
    return true;
}

void VirtualDesktopManager::setCount(uint count)
{
    count = qBound<uint>(1, count, VirtualDesktopManager::maximum());
    if (count == uint(m_desktops.count())) {
        // nothing to change
        return;
    }
    QList<VirtualDesktop *> newDesktops;
    const uint oldCount = m_desktops.count();
    //this explicit check makes it more readable
    if ((uint)m_desktops.count() > count) {
        const auto desktopsToRemove = m_desktops.mid(count);
        m_desktops.resize(count);
        if (m_current) {
            uint oldCurrent = current();
            uint newCurrent = qMin(oldCurrent, count);
            m_current = m_desktops.at(newCurrent - 1);
            if (oldCurrent != newCurrent) {
                Q_EMIT currentChanged(oldCurrent, newCurrent);
            }
        }
        for (auto desktop : desktopsToRemove) {
            for (auto &smap : m_spatialMaps) {
                smap.removeDesktop(desktop->id());
            }
            Q_EMIT desktopRemoved(desktop);
            desktop->deleteLater();
        }
    } else {
        while (uint(m_desktops.count()) < count) {
            auto vd = new VirtualDesktop(this);
            const int x11Number = m_desktops.count() + 1;
            vd->setX11DesktopNumber(x11Number);
            vd->setName(defaultName(x11Number));
            if (!s_loadingDesktopSettings) {
                vd->setId(generateDesktopId());
            }
            m_desktops << vd;
            newDesktops << vd;
            connect(vd, &VirtualDesktop::nameChanged, this,
                [this, vd] {
                    if (m_rootInfo) {
                        m_rootInfo->setDesktopName(vd->x11DesktopNumber(), vd->name().toUtf8().data());
                    }
                }
            );
            if (m_rootInfo) {
                m_rootInfo->setDesktopName(vd->x11DesktopNumber(), vd->name().toUtf8().data());
            }
        }
    }

    updateRootInfo();

    if (!s_loadingDesktopSettings) {
        save();
    }
    for (auto vd : qAsConst(newDesktops)) {
        Q_EMIT desktopCreated(vd);
    }
    Q_EMIT countChanged(oldCount, m_desktops.count());
}


uint VirtualDesktopManager::rows() const
{
    return m_rows;
}

int VirtualDesktopManager::spatialGridRows() const
{
    return m_grid.height();
}

int VirtualDesktopManager::spatialGridColumns() const
{
    return m_grid.width();
}

QString VirtualDesktopManager::spatialGridLayout() const
{
    // Return a flat comma-separated list of x11 desktop numbers in row-major
    // order.  Empty cells are represented as 0.  This format is consumed by
    // the spatial pager QML widget which splits on ',' and parseInt()s each
    // element.
    QStringList cells;
    cells.reserve(m_grid.width() * m_grid.height());
    for (int r = 0; r < m_grid.height(); ++r) {
        for (int c = 0; c < m_grid.width(); ++c) {
            VirtualDesktop *vd = m_grid.at({c, r});
            cells << QString::number(vd ? vd->x11DesktopNumber() : 0);
        }
    }
    return cells.join(QLatin1Char(','));
}

void VirtualDesktopManager::setRows(uint rows)
{
    if (rows == 0 || rows > count() || rows == m_rows) {
        return;
    }

    m_rows = rows;

    int columns = count() / m_rows;
    if (count() % m_rows > 0) {
        columns++;
    }
    if (m_rootInfo) {
        m_rootInfo->setDesktopLayout(NET::OrientationHorizontal, columns, m_rows, NET::DesktopLayoutCornerTopLeft);
        m_rootInfo->activate();
    }

    updateLayout();

    //rowsChanged will be emitted by setNETDesktopLayout called by updateLayout
}

void VirtualDesktopManager::updateRootInfo()
{
    if (!m_rootInfo) {
        // Make sure the layout is still valid
        updateLayout();
        return;
    }
    const int n = count();
    m_rootInfo->setNumberOfDesktops(n);
    NETPoint *viewports = new NETPoint[n];
    m_rootInfo->setDesktopViewport(n, *viewports);
    delete[] viewports;
    // Make sure the layout is still valid
    updateLayout();
}

void VirtualDesktopManager::updateLayout()
{
    if (m_spatialMode && !spatialMap().isEmpty()) {
        // In spatial mode, derive the grid from the neighbor graph
        // so that desktops appear at their spatial positions.
        m_grid.updateFromSpatialMap(spatialMap(), m_desktops);
        m_rows = qMax(1, m_grid.height());
        const int columns = qMax(1, m_grid.width());
        if (m_rootInfo) {
            m_rootInfo->setDesktopLayout(NET::OrientationHorizontal, columns, m_rows, NET::DesktopLayoutCornerTopLeft);
        }
        Q_EMIT layoutChanged(columns, m_rows);
        Q_EMIT rowsChanged(m_rows);
        return;
    }

    m_rows = qMin(m_rows, count());
    int columns = count() / m_rows;
    Qt::Orientation orientation = Qt::Horizontal;
    if (m_rootInfo) {
        // TODO: Is there a sane way to avoid overriding the existing grid?
        columns = m_rootInfo->desktopLayoutColumnsRows().width();
        m_rows = qMax(1, m_rootInfo->desktopLayoutColumnsRows().height());
        orientation = m_rootInfo->desktopLayoutOrientation() == NET::OrientationHorizontal ? Qt::Horizontal : Qt::Vertical;
    }

    if (columns == 0) {
        // Not given, set default layout
        m_rows = count() == 1u ? 1 : 2;
        columns = count() / m_rows;
    }
    setNETDesktopLayout(orientation,
        columns, m_rows, 0 //rootInfo->desktopLayoutCorner() // Not really worth implementing right now.
    );
}

void VirtualDesktopManager::load()
{
    s_loadingDesktopSettings = true;
    if (!m_config) {
        return;
    }

    KConfigGroup group(m_config, QStringLiteral("Desktops"));
    const int n = group.readEntry("Number", 1);
    setCount(n);

    for (int i = 1; i <= n; i++) {
        QString s = group.readEntry(QStringLiteral("Name_%1").arg(i), i18n("Desktop %1", i));
        if (m_rootInfo) {
            m_rootInfo->setDesktopName(i, s.toUtf8().data());
        }
        m_desktops[i-1]->setName(s);

        const QString sId = group.readEntry(QStringLiteral("Id_%1").arg(i), QString());

        if (m_desktops[i-1]->id().isEmpty()) {
            m_desktops[i-1]->setId(sId.isEmpty() ? generateDesktopId() : sId);
        } else {
            Q_ASSERT(sId.isEmpty() || m_desktops[i-1]->id() == sId);
        }

        // TODO: update desktop focus chain, why?
//         m_desktopFocusChain.value()[i-1] = i;
    }

    int rows = group.readEntry<int>("Rows", 2);
    m_rows = qBound(1, rows, n);

    m_spatialMode = group.readEntry("SpatialMode", false);
    m_spatialMaps[defaultActivityKey()].load(group);

    // JSON file takes precedence over kwinrc entries when present.
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    m_spatialMaps[defaultActivityKey()].loadJson(configDir + QStringLiteral("/spatial-desktop-nav.json"));

    // Load per-activity spatial maps from individual JSON files.
    const QDir dir(configDir);
    const QString activityFilePrefix = QStringLiteral("spatial-desktop-nav-");
    const QStringList activityFiles = dir.entryList(
        QStringList() << activityFilePrefix + QStringLiteral("*.json"), QDir::Files);
    for (const QString &fileName : activityFiles) {
        const QString activityId = fileName.mid(
            activityFilePrefix.length(),
            fileName.length() - activityFilePrefix.length() - 5); // strip prefix and ".json"
        if (!activityId.isEmpty()) {
            m_spatialMaps[activityId].loadJson(configDir + QLatin1Char('/') + fileName);
        }
    }

    // Merge missing desktop entries from __default__ into per-activity maps.
    // This handles two cases:
    // (1) Per-activity file is completely empty → gets all entries from default
    // (2) Per-activity file is stale (e.g. a new desktop was added to default
    //     but the per-activity file was never updated) → gets the new entries
    const auto &defaultMap = m_spatialMaps[defaultActivityKey()];
    if (!defaultMap.isEmpty()) {
        for (auto it = m_spatialMaps.begin(); it != m_spatialMaps.end(); ++it) {
            if (it.key() != defaultActivityKey()) {
                if (it.value().isEmpty()) {
                    it.value() = defaultMap;
                } else {
                    it.value().mergeFrom(defaultMap);
                }
            }
        }
    }

    s_loadingDesktopSettings = false;

    initActivities();
}

void VirtualDesktopManager::save()
{
    if (s_loadingDesktopSettings) {
        return;
    }
    if (!m_config) {
        return;
    }
    KConfigGroup group(m_config, QStringLiteral("Desktops"));

    for (int i = count() + 1;  group.hasKey(QStringLiteral("Id_%1").arg(i)); i++) {
        group.deleteEntry(QStringLiteral("Id_%1").arg(i));
        group.deleteEntry(QStringLiteral("Name_%1").arg(i));
    }

    group.writeEntry("Number", count());
    QStringList knownIds;
    for (VirtualDesktop *desktop : qAsConst(m_desktops)) {
        const uint position = desktop->x11DesktopNumber();
        knownIds << desktop->id();

        QString s = desktop->name();
        const QString defaultvalue = defaultName(position);
        if (s.isEmpty()) {
            s = defaultvalue;
            if (m_rootInfo) {
                m_rootInfo->setDesktopName(position, s.toUtf8().data());
            }
        }

        if (s != defaultvalue) {
            group.writeEntry(QStringLiteral("Name_%1").arg(position), s);
        } else {
            QString currentvalue = group.readEntry(QStringLiteral("Name_%1").arg(position), QString());
            if (currentvalue != defaultvalue) {
                group.deleteEntry(QStringLiteral("Name_%1").arg(position));
            }
        }
        group.writeEntry(QStringLiteral("Id_%1").arg(position), desktop->id());
    }

    group.writeEntry("Rows", m_rows);
    group.writeEntry("SpatialMode", m_spatialMode);

    // Only write the __default__ kwinrc entries when Activities are disabled.
    // When Activities are enabled every map lives in its own JSON file; writing
    // __default__ into kwinrc would cause stale data to be re-read on the next
    // cold boot before the per-activity JSON files are loaded.
    const bool activitiesEnabled = (Activities::self() != nullptr);
    if (!activitiesEnabled) {
        m_spatialMaps[defaultActivityKey()].save(group, knownIds);
    }

    // Save to disk
    group.sync();

    // Persist spatial neighbor maps to JSON files.
    // When Activities are disabled the single map goes to the legacy file.
    // When Activities are enabled each activity map gets its own file;
    // the legacy default file is intentionally left alone so that older
    // KWin versions that don't know about activities can still read something.
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (!activitiesEnabled) {
        m_spatialMaps[defaultActivityKey()].saveJson(
            configDir + QStringLiteral("/spatial-desktop-nav.json"), knownIds);
    }

    for (auto it = m_spatialMaps.constBegin(); it != m_spatialMaps.constEnd(); ++it) {
        if (it.key() == defaultActivityKey()) {
            continue;
        }
        const QString path = configDir + QStringLiteral("/spatial-desktop-nav-")
            + it.key() + QStringLiteral(".json");
        it.value().saveJson(path, knownIds);
    }
}

void VirtualDesktopManager::setSpatialMode(bool enabled)
{
    if (enabled == m_spatialMode) {
        return;
    }
    m_spatialMode = enabled;
    save();
    if (m_spatialMode) {
        updateSpatialLayout();
    } else {
        // Revert to standard grid-based layout
        updateLayout();
    }
    Q_EMIT spatialModeChanged();
}

QVariantMap VirtualDesktopManager::spatialNeighbors(const QString &desktopId) const
{
    QVariantMap result;
    result[QStringLiteral("above")] = activeSpatialMap().neighbor(desktopId, VirtualDesktopSpatialMap::Direction::Above);
    result[QStringLiteral("below")] = activeSpatialMap().neighbor(desktopId, VirtualDesktopSpatialMap::Direction::Below);
    result[QStringLiteral("left")]  = activeSpatialMap().neighbor(desktopId, VirtualDesktopSpatialMap::Direction::Left);
    result[QStringLiteral("right")] = activeSpatialMap().neighbor(desktopId, VirtualDesktopSpatialMap::Direction::Right);
    return result;
}

void VirtualDesktopManager::setSpatialNeighbor(const QString &desktopId, const QString &direction, const QString &neighborId)
{
    VirtualDesktopSpatialMap::Direction dir;
    const QString dirLower = direction.toLower();
    if (dirLower == QStringLiteral("above")) {
        dir = VirtualDesktopSpatialMap::Direction::Above;
    } else if (dirLower == QStringLiteral("below")) {
        dir = VirtualDesktopSpatialMap::Direction::Below;
    } else if (dirLower == QStringLiteral("left")) {
        dir = VirtualDesktopSpatialMap::Direction::Left;
    } else if (dirLower == QStringLiteral("right")) {
        dir = VirtualDesktopSpatialMap::Direction::Right;
    } else {
        qWarning() << "VirtualDesktopManager::setSpatialNeighbor: unknown direction" << direction;
        return;
    }

    activeSpatialMap().setNeighbor(desktopId, dir, neighborId);

    if (m_batchSpatialDepth > 0) {
        // Defer save/rebuild/signals until endBatchSpatialUpdate().
        return;
    }

    save();

    // Rebuild m_grid from the updated spatial map so that
    // desktopGridCoords() returns correct positions for the slide effect.
    m_grid.updateFromSpatialMap(activeSpatialMap(), m_desktops);
    m_rows = qMax(1, m_grid.height());

    // Update _NET_DESKTOP_LAYOUT (max dimensions across all activity maps).
    updateSpatialLayout();
    Q_EMIT layoutChanged(m_grid.width(), m_rows);
    Q_EMIT rowsChanged(m_rows);
    Q_EMIT spatialMapChanged();
}

void VirtualDesktopManager::beginBatchSpatialUpdate()
{
    ++m_batchSpatialDepth;
}

void VirtualDesktopManager::endBatchSpatialUpdate()
{
    if (m_batchSpatialDepth <= 0) {
        return;
    }
    if (--m_batchSpatialDepth > 0) {
        return;
    }
    // Batch complete — do the deferred work once.
    save();
    m_grid.updateFromSpatialMap(activeSpatialMap(), m_desktops);
    m_rows = qMax(1, m_grid.height());
    updateSpatialLayout();
    Q_EMIT layoutChanged(m_grid.width(), m_rows);
    Q_EMIT rowsChanged(m_rows);
    Q_EMIT spatialMapChanged();
}

void VirtualDesktopManager::updateSpatialLayout()
{
    if (!m_rootInfo || m_desktops.isEmpty()) {
        return;
    }

    // Compute the maximum grid dimensions across ALL activity spatial maps
    // so that _NET_DESKTOP_LAYOUT stays constant during activity switches.
    // This prevents plasmashell from churning containments on every switch.
    uint maxColumns = 1, maxRows = 1;

    for (auto it = m_spatialMaps.constBegin(); it != m_spatialMaps.constEnd(); ++it) {
        const auto &smap = it.value();
        if (smap.isEmpty()) {
            continue;
        }

        // BFS to compute this activity's bounding box
        QHash<QString, QPoint> positions;
        QQueue<VirtualDesktop *> queue;

        VirtualDesktop *start = nullptr;
        for (auto *vd : qAsConst(m_desktops)) {
            if (smap.containsDesktop(vd->id())) {
                if (smap.neighbor(vd->id(), VirtualDesktopSpatialMap::Direction::Above).isEmpty() &&
                    smap.neighbor(vd->id(), VirtualDesktopSpatialMap::Direction::Left).isEmpty()) {
                    start = vd;
                    break;
                }
                if (!start) {
                    start = vd;
                }
            }
        }
        if (!start) {
            continue;
        }

        positions[start->id()] = QPoint(0, 0);
        queue.enqueue(start);

        auto tryVisit = [&](const QString &fromId, const QString &neighborId, int dc, int dr) {
            if (neighborId.isEmpty() || positions.contains(neighborId)) {
                return;
            }
            VirtualDesktop *neighbor = desktopForId(neighborId);
            if (!neighbor) {
                return;
            }
            const QPoint fromPos = positions.value(fromId);
            positions[neighborId] = QPoint(fromPos.x() + dc, fromPos.y() + dr);
            queue.enqueue(neighbor);
        };

        while (!queue.isEmpty()) {
            VirtualDesktop *vd = queue.dequeue();
            const QString &id = vd->id();
            tryVisit(id, smap.neighbor(id, VirtualDesktopSpatialMap::Direction::Right), 1,  0);
            tryVisit(id, smap.neighbor(id, VirtualDesktopSpatialMap::Direction::Left),  -1, 0);
            tryVisit(id, smap.neighbor(id, VirtualDesktopSpatialMap::Direction::Below),  0,  1);
            tryVisit(id, smap.neighbor(id, VirtualDesktopSpatialMap::Direction::Above),  0, -1);
        }

        int minCol = 0, maxCol = 0, minRow = 0, maxRow = 0;
        bool first = true;
        for (const QPoint &pos : qAsConst(positions)) {
            if (first) {
                minCol = maxCol = pos.x();
                minRow = maxRow = pos.y();
                first = false;
            } else {
                minCol = qMin(minCol, pos.x());
                maxCol = qMax(maxCol, pos.x());
                minRow = qMin(minRow, pos.y());
                maxRow = qMax(maxRow, pos.y());
            }
        }

        const uint cols = static_cast<uint>(maxCol - minCol + 1);
        const uint rows = static_cast<uint>(maxRow - minRow + 1);
        maxColumns = qMax(maxColumns, cols);
        maxRows = qMax(maxRows, rows);
    }

    m_rootInfo->setDesktopLayout(NET::OrientationHorizontal, maxColumns, maxRows, NET::DesktopLayoutCornerTopLeft);
    m_rootInfo->activate();
}

QString VirtualDesktopManager::defaultName(int desktop) const
{
    return i18n("Desktop %1", desktop);
}

void VirtualDesktopManager::setNETDesktopLayout(Qt::Orientation orientation, uint width, uint height, int startingCorner)
{
    Q_UNUSED(startingCorner);   // Not really worth implementing right now.
    const uint count = m_desktops.count();

    // Calculate valid grid size
    Q_ASSERT(width > 0 || height > 0);
    if ((width <= 0) && (height > 0)) {
        width = (count + height - 1) / height;
    } else if ((height <= 0) && (width > 0)) {
        height = (count + width - 1) / width;
    }
    while (width * height < count) {
        if (orientation == Qt::Horizontal) {
            ++width;
        } else {
            ++height;
        }
    }

    m_rows = qMax(1u, height);

    m_grid.update(QSize(width, height), orientation, m_desktops);
    // TODO: why is there no call to m_rootInfo->setDesktopLayout?
    Q_EMIT layoutChanged(width, height);
    Q_EMIT rowsChanged(height);
}

void VirtualDesktopManager::initShortcuts()
{
    initSwitchToShortcuts();

    QAction *nextAction = addAction(QStringLiteral("Switch to Next Desktop"), i18n("Switch to Next Desktop"), &VirtualDesktopManager::slotNext);
    input()->registerTouchpadSwipeShortcut(SwipeDirection::Right, nextAction);
    QAction *previousAction = addAction(QStringLiteral("Switch to Previous Desktop"), i18n("Switch to Previous Desktop"), &VirtualDesktopManager::slotPrevious);
    input()->registerTouchpadSwipeShortcut(SwipeDirection::Left, previousAction);
    QAction *slotRightAction = addAction(QStringLiteral("Switch One Desktop to the Right"), i18n("Switch One Desktop to the Right"), &VirtualDesktopManager::slotRight);
    KGlobalAccel::setGlobalShortcut(slotRightAction, QKeySequence(Qt::CTRL + Qt::META + Qt::Key_Right));
    QAction *slotLeftAction = addAction(QStringLiteral("Switch One Desktop to the Left"), i18n("Switch One Desktop to the Left"), &VirtualDesktopManager::slotLeft);
    KGlobalAccel::setGlobalShortcut(slotLeftAction, QKeySequence(Qt::CTRL + Qt::META + Qt::Key_Left));
    QAction *slotUpAction = addAction(QStringLiteral("Switch One Desktop Up"), i18n("Switch One Desktop Up"), &VirtualDesktopManager::slotUp);
    KGlobalAccel::setGlobalShortcut(slotUpAction, QKeySequence(Qt::CTRL + Qt::META + Qt::Key_Up));
    QAction *slotDownAction = addAction(QStringLiteral("Switch One Desktop Down"), i18n("Switch One Desktop Down"), &VirtualDesktopManager::slotDown);
    KGlobalAccel::setGlobalShortcut(slotDownAction, QKeySequence(Qt::CTRL + Qt::META + Qt::Key_Down));

    // axis events
    input()->registerAxisShortcut(Qt::ControlModifier | Qt::AltModifier, PointerAxisDown,
                                  findChild<QAction*>(QStringLiteral("Switch to Next Desktop")));
    input()->registerAxisShortcut(Qt::ControlModifier | Qt::AltModifier, PointerAxisUp,
                                  findChild<QAction*>(QStringLiteral("Switch to Previous Desktop")));
}

void VirtualDesktopManager::initSwitchToShortcuts()
{
    const QString toDesktop = QStringLiteral("Switch to Desktop %1");
    const KLocalizedString toDesktopLabel = ki18n("Switch to Desktop %1");
    addAction(toDesktop, toDesktopLabel, 1, QKeySequence(Qt::CTRL + Qt::Key_F1), &VirtualDesktopManager::slotSwitchTo);
    addAction(toDesktop, toDesktopLabel, 2, QKeySequence(Qt::CTRL + Qt::Key_F2), &VirtualDesktopManager::slotSwitchTo);
    addAction(toDesktop, toDesktopLabel, 3, QKeySequence(Qt::CTRL + Qt::Key_F3), &VirtualDesktopManager::slotSwitchTo);
    addAction(toDesktop, toDesktopLabel, 4, QKeySequence(Qt::CTRL + Qt::Key_F4), &VirtualDesktopManager::slotSwitchTo);

    for (uint i = 5; i <= maximum(); ++i) {
        addAction(toDesktop, toDesktopLabel, i, QKeySequence(), &VirtualDesktopManager::slotSwitchTo);
    }
}

QAction *VirtualDesktopManager::addAction(const QString &name, const KLocalizedString &label, uint value, const QKeySequence &key, void (VirtualDesktopManager::*slot)())
{
    QAction *a = new QAction(this);
    a->setProperty("componentName", QStringLiteral(KWIN_NAME));
    a->setObjectName(name.arg(value));
    a->setText(label.subs(value).toString());
    a->setData(value);
    KGlobalAccel::setGlobalShortcut(a, key);
    input()->registerShortcut(key, a, this, slot);
    return a;
}

QAction *VirtualDesktopManager::addAction(const QString &name, const QString &label, void (VirtualDesktopManager::*slot)())
{
    QAction *a = new QAction(this);
    a->setProperty("componentName", QStringLiteral(KWIN_NAME));
    a->setObjectName(name);
    a->setText(label);
    KGlobalAccel::setGlobalShortcut(a, QKeySequence());
    input()->registerShortcut(QKeySequence(), a, this, slot);
    return a;
}

void VirtualDesktopManager::slotSwitchTo()
{
    QAction *act = qobject_cast<QAction*>(sender());
    if (!act) {
        return;
    }
    bool ok = false;
    const uint i = act->data().toUInt(&ok);
    if (!ok) {
        return;
    }
    setCurrent(i);
}

void VirtualDesktopManager::setNavigationWrappingAround(bool enabled)
{
    if (enabled == m_navigationWrapsAround) {
        return;
    }
    m_navigationWrapsAround = enabled;
    Q_EMIT navigationWrappingAroundChanged();
}

void VirtualDesktopManager::slotDown()
{
    moveTo<DesktopBelow>(isNavigationWrappingAround());
}

void VirtualDesktopManager::slotLeft()
{
    moveTo<DesktopLeft>(isNavigationWrappingAround());
}

void VirtualDesktopManager::slotPrevious()
{
    moveTo<DesktopPrevious>(isNavigationWrappingAround());
}

void VirtualDesktopManager::slotNext()
{
    moveTo<DesktopNext>(isNavigationWrappingAround());
}

void VirtualDesktopManager::slotRight()
{
    moveTo<DesktopRight>(isNavigationWrappingAround());
}

void VirtualDesktopManager::slotUp()
{
    moveTo<DesktopAbove>(isNavigationWrappingAround());
}

} // KWin
