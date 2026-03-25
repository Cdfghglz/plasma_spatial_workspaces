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
    property int gridColumns: 1
    property int gridRows: 1
    property int gridModel: 0

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

    function show() {
        var desktopChanged = (root.currentDesktop != workspace.currentDesktop - 1);
        var activityChanged = (root.lastActivityId != workspace.currentActivity);

        if (!desktopChanged && !activityChanged) return;

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
            if (workspace.spatialMode) {
                var grid = root.buildSpatialGrid();
                root.spatialCells = grid;
                root.gridModel = grid.length;
            } else {
                root.spatialCells = [];
                root.gridModel = workspace.desktops;
            }
        }

        root.dialogsVisible = true;
        timer.start();
    }

    DBusCall {
        id: activityNameCall
        service: "org.kde.ActivityManager"
        path: "/ActivityManager/Activities"
        dbusInterface: "org.kde.ActivityManager.Activities"
        method: "ActivityName"
        onFinished: root.activityName = returnValue[0]
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
                var screen = workspace.clientArea(KWin.FullScreenArea, screenIndex, workspace.currentDesktop);
                return screen.x + screen.width/2 - mainItem.width/2;
            }
            y: {
                var screen = workspace.clientArea(KWin.FullScreenArea, screenIndex, workspace.currentDesktop);
                return screen.y + screen.height/2 - mainItem.height/2;
            }

            mainItem: Item {
                id: dialogContent

                property var screenGeom: workspace.clientArea(KWin.FullScreenArea, screenDialog.screenIndex, workspace.currentDesktop)
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
                                    property int nameChangeCounter: 0
                                    property string tileDesktopName: {
                                        void(nameChangeCounter)
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
                                                nameChangeCounter++
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
            }
        }
    }

    Component.onCompleted: {
        root.gridColumns = workspace.desktopGridWidth;
        root.gridRows = workspace.desktopGridHeight;
        root.lastActivityId = workspace.currentActivity;
        root.loadConfig();
        root.fetchActivityName();
        root.show();
    }
}
