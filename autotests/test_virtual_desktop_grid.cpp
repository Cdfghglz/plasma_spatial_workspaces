/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 plasma-spatial-workspaces contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

/**
 * Standalone unit tests for VirtualDesktopGrid::updateFromSpatialMap() and
 * VirtualDesktopGrid::inDirection() — the query-time adjacency logic that
 * makes a newly-created desktop reachable from every grid-adjacent desktop,
 * not just the one it was created from (psw-sne).
 *
 * VirtualDesktopGrid depends only on VirtualDesktopSpatialMap and
 * VirtualDesktop::id(), neither of which need Activities/Workspace. This
 * file embeds a minimal VirtualDesktop stand-in and copies of both classes
 * so it compiles without linking the full KWin library (same approach as
 * test_spatial_map.cpp).
 *
 * Keep in sync with:
 *   src/virtualdesktops.h  (VirtualDesktopGrid, VirtualDesktopSpatialMap sections)
 *   src/virtualdesktops.cpp (same)
 */

// Qt core
#include <QHash>
#include <QPoint>
#include <QQueue>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVector>

// Qt Test
#include <QtTest>

#ifndef KWIN_EXPORT
#define KWIN_EXPORT
#endif

namespace KWin {

// ---------------------------------------------------------------------------
// Minimal VirtualDesktop stand-in (only the id() accessor VirtualDesktopGrid
// actually touches; the real class is a QObject with name/x11 number too)
// ---------------------------------------------------------------------------
class VirtualDesktop
{
public:
    explicit VirtualDesktop(QString id) : m_id(std::move(id)) {}
    QString id() const { return m_id; }
private:
    QString m_id;
};

// ---------------------------------------------------------------------------
// Minimal VirtualDesktopSpatialMap
// (extracted from src/virtualdesktops.h/.cpp, stripped of persistence)
// ---------------------------------------------------------------------------
class KWIN_EXPORT VirtualDesktopSpatialMap
{
public:
    enum class Direction {
        Above,
        Below,
        Left,
        Right
    };

    void setNeighbor(const QString &desktopId, Direction direction, const QString &neighborId)
    {
        auto &entry = m_neighbors[desktopId];
        switch (direction) {
        case Direction::Above: entry.above = neighborId; break;
        case Direction::Below: entry.below = neighborId; break;
        case Direction::Left:  entry.left  = neighborId; break;
        case Direction::Right: entry.right = neighborId; break;
        }
    }

    QString neighbor(const QString &desktopId, Direction direction) const
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

    bool containsDesktop(const QString &desktopId) const
    {
        return m_neighbors.contains(desktopId);
    }

private:
    struct DesktopNeighbors {
        QString above;
        QString below;
        QString left;
        QString right;
    };
    QHash<QString, DesktopNeighbors> m_neighbors;
};

// ---------------------------------------------------------------------------
// VirtualDesktopGrid
// (copied from src/virtualdesktops.h/.cpp — updateFromSpatialMap(),
// gridCoords(), at(), inDirection() sections)
// ---------------------------------------------------------------------------
class VirtualDesktopGrid
{
public:
    VirtualDesktopGrid() = default;
    ~VirtualDesktopGrid() = default;

    void updateFromSpatialMap(const VirtualDesktopSpatialMap &spatialMap, const QVector<VirtualDesktop*> &desktops);
    QPoint gridCoords(VirtualDesktop *vd) const;
    VirtualDesktop *at(const QPoint &coords) const;
    VirtualDesktop *inDirection(VirtualDesktop *vd, VirtualDesktopSpatialMap::Direction dir) const;
    int width() const { return m_size.width(); }
    int height() const { return m_size.height(); }

private:
    QSize m_size;
    QVector<QVector<VirtualDesktop*>> m_grid;
};

void VirtualDesktopGrid::updateFromSpatialMap(const VirtualDesktopSpatialMap &spatialMap, const QVector<VirtualDesktop*> &desktops)
{
    using Dir = VirtualDesktopSpatialMap::Direction;

    QHash<QString, VirtualDesktop*> byId;
    for (auto *vd : desktops) {
        byId[vd->id()] = vd;
    }

    QHash<QString, QPoint> coords;
    QSet<QString> visited;
    QQueue<QString> queue;

    if (desktops.isEmpty()) {
        m_grid.clear();
        m_size = QSize(0, 0);
        return;
    }

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

    maxX = 0;
    maxY = 0;
    for (auto it = coords.constBegin(); it != coords.constEnd(); ++it) {
        maxX = qMax(maxX, it.value().x());
        maxY = qMax(maxY, it.value().y());
    }

    const int width = maxX + 1;
    const int height = maxY + 1;

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
    if (coords.y() < 0 || coords.y() >= m_grid.count()) {
        return nullptr;
    }
    const auto &row = m_grid.at(coords.y());
    if (coords.x() < 0 || coords.x() >= row.count()) {
        return nullptr;
    }
    return row.at(coords.x());
}

VirtualDesktop *VirtualDesktopGrid::inDirection(VirtualDesktop *vd, VirtualDesktopSpatialMap::Direction dir) const
{
    const QPoint coords = gridCoords(vd);
    if (coords.x() < 0 || coords.y() < 0) {
        return nullptr;
    }

    QPoint offset;
    switch (dir) {
    case VirtualDesktopSpatialMap::Direction::Above: offset = QPoint(0, -1); break;
    case VirtualDesktopSpatialMap::Direction::Below: offset = QPoint(0, 1);  break;
    case VirtualDesktopSpatialMap::Direction::Left:  offset = QPoint(-1, 0); break;
    case VirtualDesktopSpatialMap::Direction::Right: offset = QPoint(1, 0);  break;
    }
    return at(coords + offset);
}

} // namespace KWin

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

using Dir  = KWin::VirtualDesktopSpatialMap::Direction;
using SMap = KWin::VirtualDesktopSpatialMap;
using KWin::VirtualDesktop;
using KWin::VirtualDesktopGrid;

class TestVirtualDesktopGrid : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    // Core scenario from psw-sne: a filled 2x2 square placed via only 3
    // creation-style links (a spanning tree, not a full mesh) must be
    // navigable from every adjacent side, including sides with no explicit
    // link to the desktop being queried from.
    void testFilledSquareReachableFromNonCreatorSide();

    // Sparse layouts must not fabricate adjacency across a gap.
    void testSparseGapYieldsNullptr();

    // inDirection() must not wrap and must not fall off the grid edges.
    void testNoWrapAtBoundary();

    // A desktop absent from the desktops vector passed to
    // updateFromSpatialMap() is not placed in the grid at all.
    void testUnplacedDesktopYieldsNullptr();

    // inDirection() is a pure query — it must never mutate the spatial map
    // (no fabricated links get persisted just by navigating).
    void testInDirectionDoesNotMutateSpatialMap();

    void testSingleDesktopGrid();
};

void TestVirtualDesktopGrid::testFilledSquareReachableFromNonCreatorSide()
{
    // Simulates: user creates b to the right of a, c below a, then d to the
    // right of c (as if navigating to c first). Only 3 links are ever set —
    // b and d are never explicitly linked to each other — yet they end up
    // grid-adjacent:
    //   a(0,0) b(1,0)
    //   c(0,1) d(1,1)
    VirtualDesktop a(QStringLiteral("a"));
    VirtualDesktop b(QStringLiteral("b"));
    VirtualDesktop c(QStringLiteral("c"));
    VirtualDesktop d(QStringLiteral("d"));
    QVector<VirtualDesktop*> desktops{&a, &b, &c, &d};

    SMap map;
    map.setNeighbor(a.id(), Dir::Right, b.id());
    map.setNeighbor(b.id(), Dir::Left,  a.id());
    map.setNeighbor(a.id(), Dir::Below, c.id());
    map.setNeighbor(c.id(), Dir::Above, a.id());
    map.setNeighbor(c.id(), Dir::Right, d.id());
    map.setNeighbor(d.id(), Dir::Left,  c.id());

    VirtualDesktopGrid grid;
    grid.updateFromSpatialMap(map, desktops);

    QCOMPARE(grid.width(), 2);
    QCOMPARE(grid.height(), 2);

    // The two links every creator wired
    QCOMPARE(grid.inDirection(&a, Dir::Right), &b);
    QCOMPARE(grid.inDirection(&a, Dir::Below), &c);
    QCOMPARE(grid.inDirection(&c, Dir::Right), &d);

    // The reachability this bug is about: b and d have NO explicit link
    // to each other in the spatial map, yet they are grid-adjacent.
    QVERIFY(map.neighbor(b.id(), Dir::Below).isEmpty());
    QVERIFY(map.neighbor(d.id(), Dir::Above).isEmpty());
    QCOMPARE(grid.inDirection(&b, Dir::Below), &d);
    QCOMPARE(grid.inDirection(&d, Dir::Above), &b);

    // And the reverse directions work too
    QCOMPARE(grid.inDirection(&b, Dir::Left),  &a);
    QCOMPARE(grid.inDirection(&c, Dir::Above), &a);
    QCOMPARE(grid.inDirection(&d, Dir::Left),  &c);
}

void TestVirtualDesktopGrid::testSparseGapYieldsNullptr()
{
    // L-shape with a gap at (1,1):
    //   a(0,0) b(1,0)
    //   c(0,1)  .
    VirtualDesktop a(QStringLiteral("a"));
    VirtualDesktop b(QStringLiteral("b"));
    VirtualDesktop c(QStringLiteral("c"));
    QVector<VirtualDesktop*> desktops{&a, &b, &c};

    SMap map;
    map.setNeighbor(a.id(), Dir::Right, b.id());
    map.setNeighbor(b.id(), Dir::Left,  a.id());
    map.setNeighbor(a.id(), Dir::Below, c.id());
    map.setNeighbor(c.id(), Dir::Above, a.id());

    VirtualDesktopGrid grid;
    grid.updateFromSpatialMap(map, desktops);

    QCOMPARE(grid.width(), 2);
    QCOMPARE(grid.height(), 2);

    // The gap cell (1,1) has no occupant
    QVERIFY(grid.at(QPoint(1, 1)) == nullptr);

    // No fabricated adjacency across the gap
    QVERIFY(grid.inDirection(&b, Dir::Below) == nullptr);
    QVERIFY(grid.inDirection(&c, Dir::Right) == nullptr);
}

void TestVirtualDesktopGrid::testNoWrapAtBoundary()
{
    VirtualDesktop a(QStringLiteral("a"));
    VirtualDesktop b(QStringLiteral("b"));
    QVector<VirtualDesktop*> desktops{&a, &b};

    SMap map;
    map.setNeighbor(a.id(), Dir::Right, b.id());
    map.setNeighbor(b.id(), Dir::Left,  a.id());

    VirtualDesktopGrid grid;
    grid.updateFromSpatialMap(map, desktops);

    // Off the right edge from b, off the left edge from a: no wrap
    QVERIFY(grid.inDirection(&b, Dir::Right) == nullptr);
    QVERIFY(grid.inDirection(&a, Dir::Left) == nullptr);
    QVERIFY(grid.inDirection(&a, Dir::Above) == nullptr);
    QVERIFY(grid.inDirection(&b, Dir::Below) == nullptr);
}

void TestVirtualDesktopGrid::testUnplacedDesktopYieldsNullptr()
{
    VirtualDesktop a(QStringLiteral("a"));
    VirtualDesktop b(QStringLiteral("b"));
    VirtualDesktop notPlaced(QStringLiteral("ghost"));
    // "ghost" is intentionally omitted from the desktops vector, as if it
    // belongs to a different activity's map.
    QVector<VirtualDesktop*> desktops{&a, &b};

    SMap map;
    map.setNeighbor(a.id(), Dir::Right, b.id());
    map.setNeighbor(b.id(), Dir::Left,  a.id());

    VirtualDesktopGrid grid;
    grid.updateFromSpatialMap(map, desktops);

    QCOMPARE(grid.gridCoords(&notPlaced), QPoint(-1, -1));
    QVERIFY(grid.inDirection(&notPlaced, Dir::Right) == nullptr);
}

void TestVirtualDesktopGrid::testInDirectionDoesNotMutateSpatialMap()
{
    VirtualDesktop a(QStringLiteral("a"));
    VirtualDesktop b(QStringLiteral("b"));
    VirtualDesktop c(QStringLiteral("c"));
    QVector<VirtualDesktop*> desktops{&a, &b, &c};

    SMap map;
    map.setNeighbor(a.id(), Dir::Right, b.id());
    map.setNeighbor(b.id(), Dir::Left,  a.id());
    map.setNeighbor(a.id(), Dir::Below, c.id());
    map.setNeighbor(c.id(), Dir::Above, a.id());

    VirtualDesktopGrid grid;
    grid.updateFromSpatialMap(map, desktops);

    // Query every direction from every desktop repeatedly.
    for (int i = 0; i < 3; ++i) {
        grid.inDirection(&a, Dir::Right);
        grid.inDirection(&a, Dir::Below);
        grid.inDirection(&b, Dir::Below); // grid-adjacent, no explicit link
        grid.inDirection(&c, Dir::Right); // grid-adjacent, no explicit link
    }

    // The spatial map still contains only the 3 originally-set links —
    // navigation never persisted a fabricated mesh.
    QVERIFY(map.neighbor(b.id(), Dir::Below).isEmpty());
    QVERIFY(map.neighbor(c.id(), Dir::Right).isEmpty());
    QCOMPARE(map.neighbor(a.id(), Dir::Right), b.id());
    QCOMPARE(map.neighbor(a.id(), Dir::Below), c.id());
}

void TestVirtualDesktopGrid::testSingleDesktopGrid()
{
    VirtualDesktop only(QStringLiteral("only"));
    QVector<VirtualDesktop*> desktops{&only};

    SMap map; // no links at all

    VirtualDesktopGrid grid;
    grid.updateFromSpatialMap(map, desktops);

    QCOMPARE(grid.width(), 1);
    QCOMPARE(grid.height(), 1);
    QCOMPARE(grid.at(QPoint(0, 0)), &only);

    QVERIFY(grid.inDirection(&only, Dir::Above) == nullptr);
    QVERIFY(grid.inDirection(&only, Dir::Below) == nullptr);
    QVERIFY(grid.inDirection(&only, Dir::Left) == nullptr);
    QVERIFY(grid.inDirection(&only, Dir::Right) == nullptr);
}

QTEST_MAIN(TestVirtualDesktopGrid)
#include "test_virtual_desktop_grid.moc"
