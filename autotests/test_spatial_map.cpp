/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 plasma-spatial-workspaces contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

/**
 * Standalone unit tests for VirtualDesktopSpatialMap.
 *
 * VirtualDesktopSpatialMap is a pure data structure (neighbor map + KConfig/JSON
 * persistence) with no dependency on KWin internals (Activities, Workspace, etc.).
 * This file embeds a copy of the class definition and implementation so it can be
 * compiled without linking the full KWin library.
 *
 * Keep the definition and implementation here in sync with:
 *   src/virtualdesktops.h  (class definition, VirtualDesktopSpatialMap section)
 *   src/virtualdesktops.cpp (implementation, VirtualDesktopSpatialMap section)
 */

// Qt core
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QQueue>
#include <QSaveFile>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>

// KDE config
#include <KConfig>
#include <KConfigGroup>

// Qt Test
#include <QtTest>

// ---------------------------------------------------------------------------
// Minimal VirtualDesktopSpatialMap class definition
// (extracted from src/virtualdesktops.h, stripped of KWin infrastructure)
// ---------------------------------------------------------------------------

#ifndef KWIN_EXPORT
#define KWIN_EXPORT
#endif

namespace KWin {

/**
 * Stores explicit spatial neighbor relationships between virtual desktops.
 *
 * When spatialMode is enabled in VirtualDesktopManager, navigation methods
 * (above/below/toLeft/toRight) delegate to this map instead of computing
 * neighbors from the grid layout.
 */
class KWIN_EXPORT VirtualDesktopSpatialMap
{
public:
    enum class Direction {
        Above,
        Below,
        Left,
        Right
    };

    VirtualDesktopSpatialMap() = default;
    ~VirtualDesktopSpatialMap() = default;

    void setNeighbor(const QString &desktopId, Direction direction, const QString &neighborId);
    QString neighbor(const QString &desktopId, Direction direction) const;
    void removeDesktop(const QString &desktopId);

    void load(const KConfigGroup &group);
    void save(KConfigGroup &group, const QStringList &knownIds) const;

    void loadJson(const QString &filePath);
    void saveJson(const QString &filePath, const QStringList &knownIds) const;

    bool isEmpty() const;
    bool containsDesktop(const QString &desktopId) const;

private:
    static QString directionSuffix(Direction direction);

    struct DesktopNeighbors {
        QString above;
        QString below;
        QString left;
        QString right;
    };

    QHash<QString, DesktopNeighbors> m_neighbors;
};

// ---------------------------------------------------------------------------
// VirtualDesktopSpatialMap implementation
// (extracted from src/virtualdesktops.cpp, VirtualDesktopSpatialMap section)
// ---------------------------------------------------------------------------

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

void VirtualDesktopSpatialMap::load(const KConfigGroup &group)
{
    m_neighbors.clear();

    const QMap<QString, QString> entries = group.entryMap();
    const QString prefix = QStringLiteral("Spatial_");
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        const QString &key = it.key();
        if (!key.startsWith(prefix)) {
            continue;
        }
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

} // namespace KWin

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

using Dir  = KWin::VirtualDesktopSpatialMap::Direction;
using SMap = KWin::VirtualDesktopSpatialMap;

class TestVirtualDesktopSpatialMap : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    // Basic add / remove
    void testIsEmpty();
    void testSetAndGetNeighbor();
    void testAllDirections();
    void testClearNeighborByEmptyString();
    void testContainsDesktop();

    // Removal semantics
    void testRemoveDesktopClearsEntries();
    void testRemoveDesktopClearsDanglingRefs();

    // Bidirectionality is NOT auto-enforced
    void testBidirectionalNotAutoEnforced();

    // Topology tests (BFS-traversable structures)
    void testGridTopology();     // 2x2 grid
    void testLinearChain();      // A -- B -- C -- D
    void testSingleDesktop();
    void testDisconnectedGraph();
    void testBfsTraversal();     // BFS from root visits all connected desktops

    // Persistence: KConfig
    void testLoadSaveConfig();
    void testSaveConfigOmitsStaleEntries();

    // Persistence: JSON
    void testLoadSaveJson();
    void testSaveJsonOmitsStaleEntries();
    void testLoadNonExistentJson();
};

// --- Basic add / remove ---

void TestVirtualDesktopSpatialMap::testIsEmpty()
{
    SMap map;
    QVERIFY(map.isEmpty());

    map.setNeighbor(QStringLiteral("a"), Dir::Right, QStringLiteral("b"));
    QVERIFY(!map.isEmpty());

    map.removeDesktop(QStringLiteral("a"));
    QVERIFY(map.isEmpty());
}

void TestVirtualDesktopSpatialMap::testSetAndGetNeighbor()
{
    SMap map;
    const QString a = QStringLiteral("desktop-a");
    const QString b = QStringLiteral("desktop-b");

    // Initially no neighbor
    QCOMPARE(map.neighbor(a, Dir::Right), QString());

    // Set and query
    map.setNeighbor(a, Dir::Right, b);
    QCOMPARE(map.neighbor(a, Dir::Right), b);

    // Other directions unaffected
    QCOMPARE(map.neighbor(a, Dir::Left),  QString());
    QCOMPARE(map.neighbor(a, Dir::Above), QString());
    QCOMPARE(map.neighbor(a, Dir::Below), QString());
}

void TestVirtualDesktopSpatialMap::testAllDirections()
{
    SMap map;
    const QString id = QStringLiteral("center");

    map.setNeighbor(id, Dir::Above, QStringLiteral("north"));
    map.setNeighbor(id, Dir::Below, QStringLiteral("south"));
    map.setNeighbor(id, Dir::Left,  QStringLiteral("west"));
    map.setNeighbor(id, Dir::Right, QStringLiteral("east"));

    QCOMPARE(map.neighbor(id, Dir::Above), QStringLiteral("north"));
    QCOMPARE(map.neighbor(id, Dir::Below), QStringLiteral("south"));
    QCOMPARE(map.neighbor(id, Dir::Left),  QStringLiteral("west"));
    QCOMPARE(map.neighbor(id, Dir::Right), QStringLiteral("east"));
}

void TestVirtualDesktopSpatialMap::testClearNeighborByEmptyString()
{
    SMap map;
    const QString a = QStringLiteral("a");
    const QString b = QStringLiteral("b");

    map.setNeighbor(a, Dir::Right, b);
    QCOMPARE(map.neighbor(a, Dir::Right), b);

    // Clear by setting empty string
    map.setNeighbor(a, Dir::Right, QString());
    QCOMPARE(map.neighbor(a, Dir::Right), QString());

    // The desktop entry still exists in the map (just with empty neighbor)
    QVERIFY(map.containsDesktop(a));
}

void TestVirtualDesktopSpatialMap::testContainsDesktop()
{
    SMap map;
    const QString a = QStringLiteral("a");

    QVERIFY(!map.containsDesktop(a));

    map.setNeighbor(a, Dir::Right, QStringLiteral("b"));
    QVERIFY(map.containsDesktop(a));

    // "b" was only used as a neighbor value (target), not as a source
    QVERIFY(!map.containsDesktop(QStringLiteral("b")));
}

// --- Removal semantics ---

void TestVirtualDesktopSpatialMap::testRemoveDesktopClearsEntries()
{
    SMap map;
    const QString a = QStringLiteral("a");

    map.setNeighbor(a, Dir::Right, QStringLiteral("b"));
    map.setNeighbor(a, Dir::Above, QStringLiteral("top"));
    QVERIFY(map.containsDesktop(a));

    map.removeDesktop(a);

    QVERIFY(!map.containsDesktop(a));
    QCOMPARE(map.neighbor(a, Dir::Right), QString());
    QCOMPARE(map.neighbor(a, Dir::Above), QString());
}

void TestVirtualDesktopSpatialMap::testRemoveDesktopClearsDanglingRefs()
{
    SMap map;
    const QString a = QStringLiteral("a");
    const QString b = QStringLiteral("b");
    const QString c = QStringLiteral("c");

    // a.right = b, b.right = c, b.above = a
    map.setNeighbor(a, Dir::Right, b);
    map.setNeighbor(b, Dir::Right, c);
    map.setNeighbor(b, Dir::Above, a);

    // Remove 'b'
    map.removeDesktop(b);

    // a's reference to b must be cleared
    QCOMPARE(map.neighbor(a, Dir::Right), QString());

    // b must be gone
    QVERIFY(!map.containsDesktop(b));

    // a is still in the map
    QVERIFY(map.containsDesktop(a));
}

// --- Bidirectionality ---

void TestVirtualDesktopSpatialMap::testBidirectionalNotAutoEnforced()
{
    SMap map;
    const QString a = QStringLiteral("a");
    const QString b = QStringLiteral("b");

    // Set only one direction
    map.setNeighbor(a, Dir::Right, b);

    QCOMPARE(map.neighbor(a, Dir::Right), b);
    // The class does NOT automatically set the reverse link
    QCOMPARE(map.neighbor(b, Dir::Left), QString());
}

// --- Topology tests ---

void TestVirtualDesktopSpatialMap::testGridTopology()
{
    // 2x2 grid:  [TL] [TR]
    //            [BL] [BR]
    SMap map;
    const QString tl = QStringLiteral("tl");
    const QString tr = QStringLiteral("tr");
    const QString bl = QStringLiteral("bl");
    const QString br = QStringLiteral("br");

    // Horizontal links
    map.setNeighbor(tl, Dir::Right, tr);  map.setNeighbor(tr, Dir::Left,  tl);
    map.setNeighbor(bl, Dir::Right, br);  map.setNeighbor(br, Dir::Left,  bl);
    // Vertical links
    map.setNeighbor(tl, Dir::Below, bl);  map.setNeighbor(bl, Dir::Above, tl);
    map.setNeighbor(tr, Dir::Below, br);  map.setNeighbor(br, Dir::Above, tr);

    // Verify all interior links
    QCOMPARE(map.neighbor(tl, Dir::Right), tr);
    QCOMPARE(map.neighbor(tl, Dir::Below), bl);
    QCOMPARE(map.neighbor(tr, Dir::Left),  tl);
    QCOMPARE(map.neighbor(tr, Dir::Below), br);
    QCOMPARE(map.neighbor(bl, Dir::Right), br);
    QCOMPARE(map.neighbor(bl, Dir::Above), tl);
    QCOMPARE(map.neighbor(br, Dir::Left),  bl);
    QCOMPARE(map.neighbor(br, Dir::Above), tr);

    // Verify boundary: no neighbor at edges
    QCOMPARE(map.neighbor(tl, Dir::Left),  QString());
    QCOMPARE(map.neighbor(tl, Dir::Above), QString());
    QCOMPARE(map.neighbor(tr, Dir::Right), QString());
    QCOMPARE(map.neighbor(tr, Dir::Above), QString());
    QCOMPARE(map.neighbor(bl, Dir::Left),  QString());
    QCOMPARE(map.neighbor(bl, Dir::Below), QString());
    QCOMPARE(map.neighbor(br, Dir::Right), QString());
    QCOMPARE(map.neighbor(br, Dir::Below), QString());
}

void TestVirtualDesktopSpatialMap::testLinearChain()
{
    // Horizontal chain: A <-> B <-> C <-> D
    SMap map;
    const QString a = QStringLiteral("a");
    const QString b = QStringLiteral("b");
    const QString c = QStringLiteral("c");
    const QString d = QStringLiteral("d");

    map.setNeighbor(a, Dir::Right, b);  map.setNeighbor(b, Dir::Left,  a);
    map.setNeighbor(b, Dir::Right, c);  map.setNeighbor(c, Dir::Left,  b);
    map.setNeighbor(c, Dir::Right, d);  map.setNeighbor(d, Dir::Left,  c);

    // Traverse left to right
    QStringList visited;
    QString cur = a;
    while (!cur.isEmpty()) {
        visited << cur;
        cur = map.neighbor(cur, Dir::Right);
    }
    QCOMPARE(visited, QStringList({a, b, c, d}));

    // Traverse right to left
    visited.clear();
    cur = d;
    while (!cur.isEmpty()) {
        visited << cur;
        cur = map.neighbor(cur, Dir::Left);
    }
    QCOMPARE(visited, QStringList({d, c, b, a}));
}

void TestVirtualDesktopSpatialMap::testSingleDesktop()
{
    SMap map;
    const QString id = QStringLiteral("only");

    // A desktop with no neighbors explicitly set
    map.setNeighbor(id, Dir::Right, QString());

    QVERIFY(map.containsDesktop(id));
    QCOMPARE(map.neighbor(id, Dir::Right), QString());
    QCOMPARE(map.neighbor(id, Dir::Left),  QString());
    QCOMPARE(map.neighbor(id, Dir::Above), QString());
    QCOMPARE(map.neighbor(id, Dir::Below), QString());

    map.removeDesktop(id);
    QVERIFY(map.isEmpty());
}

void TestVirtualDesktopSpatialMap::testDisconnectedGraph()
{
    // Two disjoint pairs: {a, b} and {c, d}
    SMap map;
    const QString a = QStringLiteral("a");
    const QString b = QStringLiteral("b");
    const QString c = QStringLiteral("c");
    const QString d = QStringLiteral("d");

    map.setNeighbor(a, Dir::Right, b);  map.setNeighbor(b, Dir::Left, a);
    map.setNeighbor(c, Dir::Right, d);  map.setNeighbor(d, Dir::Left, c);

    // No connection between groups
    QCOMPARE(map.neighbor(b, Dir::Right), QString());
    QCOMPARE(map.neighbor(a, Dir::Left),  QString());
    QCOMPARE(map.neighbor(c, Dir::Left),  QString());

    // Each group navigates correctly within itself
    QCOMPARE(map.neighbor(a, Dir::Right), b);
    QCOMPARE(map.neighbor(c, Dir::Right), d);
}

void TestVirtualDesktopSpatialMap::testBfsTraversal()
{
    // 3-wide, 2-tall grid: verify BFS from top-left visits all 6 desktops
    // [d00] [d01] [d02]
    // [d10] [d11] [d12]
    SMap map;
    const QString d00 = QStringLiteral("d00");
    const QString d01 = QStringLiteral("d01");
    const QString d02 = QStringLiteral("d02");
    const QString d10 = QStringLiteral("d10");
    const QString d11 = QStringLiteral("d11");
    const QString d12 = QStringLiteral("d12");

    // Row 0
    map.setNeighbor(d00, Dir::Right, d01);  map.setNeighbor(d01, Dir::Left, d00);
    map.setNeighbor(d01, Dir::Right, d02);  map.setNeighbor(d02, Dir::Left, d01);
    // Row 1
    map.setNeighbor(d10, Dir::Right, d11);  map.setNeighbor(d11, Dir::Left, d10);
    map.setNeighbor(d11, Dir::Right, d12);  map.setNeighbor(d12, Dir::Left, d11);
    // Columns
    map.setNeighbor(d00, Dir::Below, d10);  map.setNeighbor(d10, Dir::Above, d00);
    map.setNeighbor(d01, Dir::Below, d11);  map.setNeighbor(d11, Dir::Above, d01);
    map.setNeighbor(d02, Dir::Below, d12);  map.setNeighbor(d12, Dir::Above, d02);

    // BFS from top-left
    const QVector<Dir> allDirs = {Dir::Above, Dir::Below, Dir::Left, Dir::Right};
    QSet<QString> seen;
    QQueue<QString> queue;
    seen.insert(d00);
    queue.enqueue(d00);

    while (!queue.isEmpty()) {
        const QString cur = queue.dequeue();
        for (Dir dir : allDirs) {
            const QString next = map.neighbor(cur, dir);
            if (!next.isEmpty() && !seen.contains(next)) {
                seen.insert(next);
                queue.enqueue(next);
            }
        }
    }

    // All six desktops reachable
    QCOMPARE(seen.size(), 6);
    QVERIFY(seen.contains(d00));
    QVERIFY(seen.contains(d01));
    QVERIFY(seen.contains(d02));
    QVERIFY(seen.contains(d10));
    QVERIFY(seen.contains(d11));
    QVERIFY(seen.contains(d12));
}

// --- Persistence: KConfig ---

void TestVirtualDesktopSpatialMap::testLoadSaveConfig()
{
    KConfig config(QString(), KConfig::SimpleConfig);
    KConfigGroup group = config.group(QStringLiteral("Desktops"));

    const QString a = QStringLiteral("aaaa-uuid");
    const QString b = QStringLiteral("bbbb-uuid");

    // Build a map and save it
    SMap map1;
    map1.setNeighbor(a, Dir::Right, b);
    map1.setNeighbor(b, Dir::Left,  a);
    map1.save(group, {a, b});

    // Load into a fresh map
    SMap map2;
    map2.load(group);

    QCOMPARE(map2.neighbor(a, Dir::Right), b);
    QCOMPARE(map2.neighbor(b, Dir::Left),  a);

    // Directions not explicitly set should be empty
    QCOMPARE(map2.neighbor(a, Dir::Left),  QString());
    QCOMPARE(map2.neighbor(b, Dir::Right), QString());
}

void TestVirtualDesktopSpatialMap::testSaveConfigOmitsStaleEntries()
{
    // save(group, knownIds) removes config entries whose desktop IDs are absent
    // from knownIds.  It does NOT filter the write pass — it only cleans up
    // entries that were previously written but belong to now-removed desktops.
    KConfig config(QString(), KConfig::SimpleConfig);
    KConfigGroup group = config.group(QStringLiteral("Desktops"));

    const QString a = QStringLiteral("aaaa-uuid");
    const QString b = QStringLiteral("bbbb-uuid");

    // First save: both desktops present → both written
    SMap map1;
    map1.setNeighbor(a, Dir::Right, b);
    map1.setNeighbor(b, Dir::Left,  a);
    map1.save(group, {a, b});
    QVERIFY(!group.entryMap().isEmpty());

    // Second save: desktop 'b' has been removed from the manager.
    // map2 only has 'a' as a source; knownIds = {a} → b's config entries purged.
    SMap map2;
    map2.setNeighbor(a, Dir::Right, b); // 'a' still references 'b' as its neighbor value
    // 'b' is NOT a key in map2's m_neighbors, so its entries won't be re-written
    map2.save(group, {a});

    SMap loaded;
    loaded.load(group);

    QCOMPARE(loaded.neighbor(a, Dir::Right), b);
    // b's config entries were purged (b not in knownIds, b not in map2's m_neighbors)
    QVERIFY(!loaded.containsDesktop(b));
}

// --- Persistence: JSON ---

void TestVirtualDesktopSpatialMap::testLoadSaveJson()
{
    QTemporaryFile tmpFile;
    QVERIFY(tmpFile.open());
    const QString path = tmpFile.fileName();
    tmpFile.close(); // QSaveFile needs to create it

    const QString a = QStringLiteral("aaaa-uuid");
    const QString b = QStringLiteral("bbbb-uuid");

    SMap map1;
    map1.setNeighbor(a, Dir::Right, b);
    map1.setNeighbor(b, Dir::Left,  a);
    map1.setNeighbor(a, Dir::Below, b);
    map1.saveJson(path, {a, b});

    SMap map2;
    map2.loadJson(path);

    QCOMPARE(map2.neighbor(a, Dir::Right), b);
    QCOMPARE(map2.neighbor(b, Dir::Left),  a);
    QCOMPARE(map2.neighbor(a, Dir::Below), b);
    QCOMPARE(map2.neighbor(a, Dir::Above), QString()); // not set
}

void TestVirtualDesktopSpatialMap::testSaveJsonOmitsStaleEntries()
{
    QTemporaryFile tmpFile;
    QVERIFY(tmpFile.open());
    const QString path = tmpFile.fileName();
    tmpFile.close();

    const QString a = QStringLiteral("aaaa-uuid");
    const QString b = QStringLiteral("bbbb-uuid");

    SMap map;
    map.setNeighbor(a, Dir::Right, b);
    map.setNeighbor(b, Dir::Left,  a);

    // Save with only 'a' as a known desktop — 'b' should be omitted
    map.saveJson(path, {a});

    SMap loaded;
    loaded.loadJson(path);

    QCOMPARE(loaded.neighbor(a, Dir::Right), b);
    // 'b' was not in knownIds so its entry was not written
    QVERIFY(!loaded.containsDesktop(b));
}

void TestVirtualDesktopSpatialMap::testLoadNonExistentJson()
{
    SMap map;
    map.setNeighbor(QStringLiteral("a"), Dir::Right, QStringLiteral("b"));

    // Loading a nonexistent file is a no-op; existing state is preserved
    map.loadJson(QStringLiteral("/tmp/nonexistent_psw_spatial_map_test_xyz.json"));

    QCOMPARE(map.neighbor(QStringLiteral("a"), Dir::Right), QStringLiteral("b"));
}

QTEST_MAIN(TestVirtualDesktopSpatialMap)
#include "test_spatial_map.moc"
