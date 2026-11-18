/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2012, 2013 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick 2.0;
import QtQuick.Window 2.0;
import QtQml.Models 2.14;
import org.kde.plasma.core 2.0 as PlasmaCore;
import org.kde.plasma.extras 2.0 as PlasmaExtras
import org.kde.kquickcontrolsaddons 2.0 as KQuickControlsAddons;
import org.kde.kwin 2.0;

Item {
    id: root

    // Shared state for all screen dialogs
    property int currentDesktop: 0
    property int previousDesktop: 0
    property int animationDuration: 1000
    property bool showGrid: true
    property var spatialCells: []
    property string activityName: ""
    property string lastActivityId: ""
    property bool dialogsVisible: false
    property int nameRevision: 0
    property int gridColumns: 1
    property int gridRows: 1
    property int gridModel: 0
    // Sticky: shown by the Alt+Space shortcut, stays until toggled off or
    // until a desktop/activity change shows the normal timed OSD instead.
    property bool sticky: false
    // Bumped on monitor hotplug/resize. clientArea() is a plain function call,
    // so bindings using it never re-evaluate on their own: after the external
    // monitor was unplugged (2026-11-06) the dialogs kept a 0x0 screen area and
    // every OSD showed as an invisible 1x1 window ("trying to show an empty dialog").
    property int screenRevision: 0

    // Area of a screen, falling back to screen 0 for an index that no longer exists.
    function screenArea(screenIndex) {
        var revision = root.screenRevision; // binding dependency
        var desktop = workspace.currentDesktop;
        var index = (screenIndex >= 0 && screenIndex < workspace.numScreens) ? screenIndex : 0;
        var area = workspace.clientArea(KWin.FullScreenArea, index, desktop);
        if (area.width <= 0 || area.height <= 0) {
            area = workspace.clientArea(KWin.FullScreenArea, 0, desktop);
        }
        return area;
    }

    function loadConfig() {
        root.animationDuration = KWin.readConfig("PopupHideDelay", 1000);
        if (KWin.readConfig("TextOnly", "false") == "true") {
            root.showGrid = false;
        } else {
            root.showGrid = true;
        }
    }

    // Build spatial grid via BFS from spatial neighbor API.
    // Returns array of desktop numbers (1-based, 0=empty) in row-major order.
    function buildSpatialGrid() {
        var count = workspace.desktops;
        var cols = workspace.desktopGridWidth;
        var rows = workspace.desktopGridHeight;

        var positions = {};
        var visited = {};
        var queue = [1];
        positions[1] = {x: 0, y: 0};
        visited[1] = true;

        while (queue.length > 0) {
            var cur = queue.shift();
            var pos = positions[cur];

            var neighbors = [
                {d: workspace.spatialNeighborRight(cur), dx: 1, dy: 0},
                {d: workspace.spatialNeighborDown(cur),  dx: 0, dy: 1},
                {d: workspace.spatialNeighborLeft(cur),  dx: -1, dy: 0},
                {d: workspace.spatialNeighborUp(cur),    dx: 0, dy: -1}
            ];

            for (var i = 0; i < neighbors.length; i++) {
                var n = neighbors[i];
                if (n.d > 0 && !visited[n.d]) {
                    visited[n.d] = true;
                    positions[n.d] = {x: pos.x + n.dx, y: pos.y + n.dy};
                    queue.push(n.d);
                }
            }
        }

        var minX = 0, minY = 0;
        for (var d in positions) {
            if (positions[d].x < minX) minX = positions[d].x;
            if (positions[d].y < minY) minY = positions[d].y;
        }

        var grid = [];
        for (var r = 0; r < rows; r++) {
            for (var c = 0; c < cols; c++) {
                grid.push(0);
            }
        }

        for (var d in positions) {
            var gx = positions[d].x - minX;
            var gy = positions[d].y - minY;
            if (gy >= 0 && gy < rows && gx >= 0 && gx < cols) {
                grid[gy * cols + gx] = parseInt(d);
            }
        }

        return grid;
    }

    function desktopForCell(cellIndex) {
        if (spatialCells.length > 0) {
            var num = spatialCells[cellIndex];
            return (num > 0) ? num - 1 : -1;
        }
        return cellIndex;
    }

    function fetchActivityName() {
        activityNameCall.arguments = [workspace.currentActivity];
        activityNameCall.call();
    }

    function hide() {
        root.sticky = false;
        timer.stop();
        root.dialogsVisible = false;
    }

    // force: show even when neither desktop nor activity changed
    // keepOpen: stay visible until hide() instead of auto-hiding
    function show(force, keepOpen) {
        var desktopChanged = (root.currentDesktop != workspace.currentDesktop - 1);
        var activityChanged = (root.lastActivityId != workspace.currentActivity);

        if (!force && !desktopChanged && !activityChanged) return;

        // Suppress OSD when desktop switch was initiated by an effect
        // (e.g. desktop grid zoom-out view)
        if (!force && desktopChanged && workspace.effectDesktopSwitch) {
            root.previousDesktop = root.currentDesktop;
            root.currentDesktop = workspace.currentDesktop - 1;
            if (root.sticky) {
                root.hide();
            }
            return;
        }

        root.previousDesktop = root.currentDesktop;
        timer.stop();
        root.currentDesktop = workspace.currentDesktop - 1;

        if (activityChanged) {
            root.lastActivityId = workspace.currentActivity;
            fetchActivityName();
        }

        if (root.showGrid) {
            root.gridColumns = workspace.desktopGridWidth;
            root.gridRows = workspace.desktopGridHeight;
            // Force Repeater to destroy and recreate all items so
            // workspace.desktopName() calls always return current names.
            // Without this, stale bindings persist because desktopName()
            // is an imperative function call with no QML change notification.
            root.gridModel = 0;
            if (workspace.spatialMode) {
                var grid = root.buildSpatialGrid();
                root.spatialCells = grid;
                root.gridModel = grid.length;
            } else {
                root.spatialCells = [];
                root.gridModel = workspace.desktops;
            }
        }

        root.nameRevision++;
        root.sticky = (keepOpen === true);
        root.dialogsVisible = true;
        sizeCheck.restart();
        if (!root.sticky) {
            timer.start();
        }
    }

    DBusCall {
        id: activityNameCall
        service: "org.kde.ActivityManager"
        path: "/ActivityManager/Activities"
        dbusInterface: "org.kde.ActivityManager.Activities"
        method: "ActivityName"
        onFinished: root.activityName = returnValue[0]
    }

    // Self-heal (2026-11-10): the dialogs sometimes stay 0x0 for hours (PlasmaQuick
    // then logs "trying to show an empty dialog" and shows nothing), while a freshly
    // created OSD always works. After each show, check the sizes; if any dialog is
    // empty, log what it was built from and rebuild all dialogs from scratch.
    property int healCount: 0

    function emptyDialogs() {
        var bad = [];
        for (var i = 0; i < screenInstantiator.count; i++) {
            var dlg = screenInstantiator.objectAt(i);
            var mi = dlg ? dlg.mainItem : null;
            if (!mi || mi.width <= 0 || mi.height <= 0) {
                bad.push("dialog " + i + ": screenIndex=" + (dlg ? dlg.screenIndex : "-")
                    + " mainItem=" + (mi ? mi.width + "x" + mi.height : "null")
                    + " screenGeom=" + (mi ? JSON.stringify(mi.screenGeom) : "-")
                    + " sw=" + (mi ? mi.screenWidth : "-") + " sh=" + (mi ? mi.screenHeight : "-"));
            }
        }
        return bad;
    }

    Timer {
        id: sizeCheck
        interval: 50
        repeat: false
        onTriggered: {
            if (!root.dialogsVisible) {
                return;
            }
            var bad = root.emptyDialogs();
            if (bad.length === 0) {
                return;
            }
            root.healCount++;
            print("desktopchangeosd: empty OSD dialog, rebuilding (heal #" + root.healCount + ")"
                + " showGrid=" + root.showGrid + " cols=" + root.gridColumns + " rows=" + root.gridRows
                + " gridModel=" + root.gridModel + " numScreens=" + workspace.numScreens
                + " instCount=" + screenInstantiator.count + " screenRevision=" + root.screenRevision
                + " desktop=" + workspace.currentDesktop + " | " + bad.join(" | "));
            var keepSticky = root.sticky;
            root.dialogsVisible = false;
            root.screenRevision++;
            screenInstantiator.active = false;
            screenInstantiator.active = true;
            root.dialogsVisible = true;
            if (!keepSticky) {
                timer.restart();
            }
            recheck.restart();
        }
    }

    Timer {
        id: recheck
        interval: 100
        repeat: false
        onTriggered: {
            var bad = root.emptyDialogs();
            if (root.dialogsVisible && bad.length > 0) {
                print("desktopchangeosd: OSD still empty after rebuild | " + bad.join(" | "));
            }
        }
    }

    Timer {
        id: timer
        repeat: false
        interval: root.animationDuration
        onTriggered: root.dialogsVisible = false
    }

    Connections {
        target: workspace
        function onCurrentDesktopChanged() { root.show() }
        function onCurrentActivityChanged() { root.show() }
        function onNumberScreensChanged() { root.screenRevision++ }
        function onScreenResized() { root.screenRevision++ }
        function onNumberDesktopsChanged() {
            if (!workspace.spatialMode) {
                root.gridModel = workspace.desktops;
            }
        }
    }

    Connections {
        target: options
        function onConfigChanged() { root.loadConfig() }
    }

    // One dialog per screen
    Instantiator {
        id: screenInstantiator
        model: workspace.numScreens
        delegate: PlasmaCore.Dialog {
            id: screenDialog
            property int screenIndex: index

            location: PlasmaCore.Types.Floating
            visible: root.dialogsVisible
            flags: Qt.X11BypassWindowManagerHint | Qt.FramelessWindowHint
            outputOnly: true

            x: {
                var screen = root.screenArea(screenIndex);
                return screen.x + screen.width/2 - mainItem.width/2;
            }
            y: {
                var screen = root.screenArea(screenIndex);
                return screen.y + screen.height/2 - mainItem.height/2;
            }

            mainItem: Item {
                id: dialogContent

                property var screenGeom: root.screenArea(screenDialog.screenIndex)
                property int screenWidth: screenGeom.width
                property int screenHeight: screenGeom.height

                width: root.showGrid
                    ? gridView.itemWidth * root.gridColumns
                    : Math.ceil(Math.max(activityText.implicitWidth, desktopText.implicitWidth))
                height: root.showGrid
                    ? gridView.itemHeight * root.gridRows
                    : activityText.height + desktopText.height

                // Activity name - shown above grid in text-only mode only
                PlasmaExtras.Heading {
                    id: activityText
                    level: 1
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                    text: root.activityName
                    visible: !root.showGrid && root.activityName.length > 0
                    height: visible ? implicitHeight : 0
                }

                // Desktop name - shown above grid in text-only mode only
                PlasmaExtras.Heading {
                    id: desktopText
                    level: 2
                    anchors.top: activityText.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                    text: workspace.desktopName(workspace.currentDesktop)
                    visible: !root.showGrid
                    height: visible ? implicitHeight : 0
                }

                Grid {
                    id: gridView
                    columns: root.gridColumns
                    rows: root.gridRows
                    property int itemWidth: dialogContent.screenWidth * Math.min(0.8/columns, 0.1)
                    property int itemHeight: Math.min(itemWidth * (dialogContent.screenHeight / dialogContent.screenWidth), dialogContent.screenHeight * Math.min(0.8/rows, 0.1))
                    anchors {
                        top: desktopText.bottom
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                    }
                    visible: root.showGrid
                    Repeater {
                        model: root.gridModel
                        Item {
                            width: gridView.itemWidth
                            height: gridView.itemHeight
                            property int desktopIndex: root.desktopForCell(index)
                            opacity: desktopIndex >= 0 ? 1.0 : 0.0
                            PlasmaCore.FrameSvgItem {
                                anchors.fill: parent
                                imagePath: "widgets/pager"
                                prefix: "normal"
                                visible: parent.desktopIndex >= 0
                            }
                            PlasmaCore.FrameSvgItem {
                                id: activeElement
                                anchors.fill: parent
                                imagePath: "widgets/pager"
                                prefix: "active"
                                opacity: 0.0
                                Behavior on opacity {
                                    NumberAnimation { duration: root.animationDuration/2 }
                                }
                            }
                            Item {
                                anchors.fill: parent
                                visible: parent.desktopIndex >= 0
                                KQuickControlsAddons.QIconItem {
                                    anchors.fill: parent
                                    icon: "go-up"
                                    visible: false
                                }
                                KQuickControlsAddons.QIconItem {
                                    anchors.fill: parent
                                    icon: "go-down"
                                    visible: {
                                        if (desktopIndex < 0) return false;
                                        if (root.currentDesktop <= desktopIndex) return false;
                                        if (desktopIndex < root.previousDesktop) return false;
                                        if (root.currentDesktop < root.previousDesktop) return false;
                                        if (Math.floor(root.currentDesktop/root.gridColumns) == Math.floor(index/root.gridColumns)) return false;
                                        if (root.previousDesktop % root.gridColumns == index % root.gridColumns) return true;
                                        return false;
                                    }
                                }
                                KQuickControlsAddons.QIconItem {
                                    anchors.fill: parent
                                    icon: "go-up"
                                    visible: {
                                        if (desktopIndex < 0) return false;
                                        if (root.currentDesktop >= desktopIndex) return false;
                                        if (desktopIndex > root.previousDesktop) return false;
                                        if (root.currentDesktop > root.previousDesktop) return false;
                                        if (Math.floor(root.currentDesktop/root.gridColumns) == Math.floor(index/root.gridColumns)) return false;
                                        if (root.previousDesktop % root.gridColumns == index % root.gridColumns) return true;
                                        return false;
                                    }
                                }
                                KQuickControlsAddons.QIconItem {
                                    anchors.fill: parent
                                    icon: "go-next"
                                    visible: {
                                        if (desktopIndex < 0) return false;
                                        if (root.currentDesktop <= desktopIndex) return false;
                                        if (desktopIndex < root.previousDesktop) {
                                            if (Math.floor(root.currentDesktop/root.gridColumns) == Math.floor(index/root.gridColumns)) {
                                                if (index % root.gridColumns >= root.previousDesktop % root.gridColumns) return true;
                                            }
                                            return false;
                                        }
                                        if (root.currentDesktop < root.previousDesktop) return false;
                                        if (Math.floor(root.currentDesktop/root.gridColumns) == Math.floor(index/root.gridColumns)) {
                                            if (index % root.gridColumns < root.previousDesktop % root.gridColumns) return false;
                                            return true;
                                        }
                                        return false;
                                    }
                                }
                                KQuickControlsAddons.QIconItem {
                                    anchors.fill: parent
                                    icon: "go-previous"
                                    visible: {
                                        if (desktopIndex < 0) return false;
                                        if (root.currentDesktop >= desktopIndex) return false;
                                        if (desktopIndex > root.previousDesktop) {
                                            if (Math.floor(root.currentDesktop/root.gridColumns) == Math.floor(index/root.gridColumns)) {
                                                if (index % root.gridColumns <= root.previousDesktop % root.gridColumns) return true;
                                            }
                                            return false;
                                        }
                                        if (root.currentDesktop > root.previousDesktop) return false;
                                        if (Math.floor(root.currentDesktop/root.gridColumns) == Math.floor(index/root.gridColumns)) {
                                            if (index % root.gridColumns > root.previousDesktop % root.gridColumns) return false;
                                            return true;
                                        }
                                        return false;
                                    }
                                }
                            }
                            // Activity name on highlighted tile only; desktop name on all tiles
                            Column {
                                anchors.centerIn: parent
                                width: parent.width - 4
                                opacity: desktopIndex == root.currentDesktop ? 1.0 : 0.7
                                visible: desktopIndex >= 0

                                Behavior on opacity {
                                    NumberAnimation { duration: root.animationDuration/2 }
                                }

                                Text {
                                    width: parent.width
                                    text: root.activityName
                                    visible: desktopIndex == root.currentDesktop && root.activityName.length > 0
                                    height: visible ? implicitHeight : 0
                                    color: "white"
                                    font.bold: true
                                    style: Text.Outline
                                    styleColor: "black"
                                    font.pixelSize: Math.max(8, gridView.itemHeight * 0.18)
                                    wrapMode: Text.NoWrap
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignHCenter
                                }

                                Text {
                                    id: tileNameText
                                    property int nameChangeCounter: 0
                                    property string tileDesktopName: {
                                        void(nameChangeCounter)
                                        void(root.nameRevision)
                                        return desktopIndex >= 0 ? workspace.desktopName(desktopIndex + 1) : ""
                                    }
                                    property bool isDefaultName: /^Desktop \d+$/.test(tileDesktopName) || tileDesktopName === ""
                                    width: parent.width
                                    text: tileDesktopName
                                    visible: !isDefaultName
                                    height: visible ? implicitHeight : 0
                                    color: "white"
                                    style: Text.Outline
                                    styleColor: "black"
                                    font.pixelSize: Math.max(7, gridView.itemHeight * 0.14)
                                    wrapMode: Text.NoWrap
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignHCenter
                                    Connections {
                                        target: workspace
                                        function onDesktopNameChanged(desktopNum, name) {
                                            if (desktopNum === desktopIndex + 1)
                                                tileNameText.nameChangeCounter++
                                        }
                                    }
                                }
                            }

                            states: [
                                State {
                                    name: "NORMAL"
                                    when: desktopIndex != root.currentDesktop
                                    PropertyChanges {
                                        target: activeElement
                                        opacity: 0.0
                                    }
                                },
                                State {
                                    name: "SELECTED"
                                    when: desktopIndex == root.currentDesktop
                                    PropertyChanges {
                                        target: activeElement
                                        opacity: 1.0
                                    }
                                }
                            ]
                        }
                    }
                }
            }

            Component.onCompleted: {
                KWin.registerWindow(screenDialog);
                console.log("OSD dialog created: screen=" + screenIndex
                    + " x=" + x + " y=" + y
                    + " geom=" + JSON.stringify(workspace.clientArea(KWin.FullScreenArea, screenIndex, workspace.currentDesktop)));
            }
        }
    }

    Component.onCompleted: {
        console.log("OSD: numScreens=" + workspace.numScreens);
        root.gridColumns = workspace.desktopGridWidth;
        root.gridRows = workspace.desktopGridHeight;
        root.lastActivityId = workspace.currentActivity;
        root.loadConfig();
        root.fetchActivityName();
        root.show();
    }
}
