/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2012, 2013 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick 2.0;
import QtQuick.Window 2.0;
import org.kde.plasma.core 2.0 as PlasmaCore;
import org.kde.plasma.extras 2.0 as PlasmaExtras
import org.kde.kquickcontrolsaddons 2.0 as KQuickControlsAddons;
import org.kde.kwin 2.0;

PlasmaCore.Dialog {
    id: dialog
    location: PlasmaCore.Types.Floating
    visible: false
    flags: Qt.X11BypassWindowManagerHint | Qt.FramelessWindowHint
    outputOnly: true

    mainItem: Item {
        function loadConfig() {
            dialogItem.animationDuration = KWin.readConfig("PopupHideDelay", 1000);
            if (KWin.readConfig("TextOnly", "false") == "true") {
                dialogItem.showGrid = false;
            } else {
                dialogItem.showGrid = true;
            }
        }

        // Parse the CSV spatial grid layout into an array of desktop numbers.
        // Each element is the x11 desktop number at that grid cell (0 = empty).
        function parseSpatialGrid() {
            var layout = workspace.spatialGridLayout;
            if (!layout || layout.length === 0) {
                return [];
            }
            var parts = layout.split(",");
            var result = [];
            for (var i = 0; i < parts.length; i++) {
                result.push(parseInt(parts[i], 10));
            }
            return result;
        }

        function show() {
            if (dialogItem.currentDesktop == workspace.currentDesktop - 1) {
                return;
            }
            dialogItem.previousDesktop = dialogItem.currentDesktop;
            timer.stop();
            dialogItem.currentDesktop = workspace.currentDesktop - 1;
            textElement.text = workspace.desktopName(workspace.currentDesktop);
            // screen geometry might have changed
            var screen = workspace.clientArea(KWin.FullScreenArea, workspace.activeScreen, workspace.currentDesktop);
            dialogItem.screenWidth = screen.width;
            dialogItem.screenHeight = screen.height;
            if (dialogItem.showGrid) {
                // non dependable properties might have changed
                view.columns = workspace.desktopGridWidth;
                view.rows = workspace.desktopGridHeight;
                // Update spatial grid model
                if (workspace.spatialMode) {
                    var grid = dialogItem.parseSpatialGrid();
                    dialogItem.spatialCells = grid;
                    repeater.model = grid.length;
                } else {
                    dialogItem.spatialCells = [];
                    repeater.model = workspace.desktops;
                }
            }
            dialog.visible = true;
            // position might have changed
            dialog.x = screen.x + screen.width/2 - dialogItem.width/2;
            dialog.y = screen.y + screen.height/2 - dialogItem.height/2;
            // start the hide timer
            timer.start();
        }

        id: dialogItem
        property int screenWidth: 0
        property int screenHeight: 0
        // we count desktops starting from 0 to have it better match the layout in the Grid
        property int currentDesktop: 0
        property int previousDesktop: 0
        property int animationDuration: 1000
        property bool showGrid: true
        // Spatial grid: array of x11 desktop numbers per cell (0 = empty)
        property var spatialCells: []

        // Map a cell index to a 0-based desktop index.
        // In spatial mode: read from spatialCells array.
        // In non-spatial mode: cell index IS the desktop index.
        function desktopForCell(cellIndex) {
            if (spatialCells.length > 0) {
                var num = spatialCells[cellIndex];
                return (num > 0) ? num - 1 : -1;  // -1 means empty cell
            }
            return cellIndex;
        }

        width: dialogItem.showGrid ? view.itemWidth * view.columns : Math.ceil(textElement.implicitWidth)
        height: dialogItem.showGrid ? view.itemHeight * view.rows + textElement.height : textElement.height

        PlasmaExtras.Heading {
            id: textElement
            anchors.top: dialogItem.showGrid ? parent.top : undefined
            anchors.left: parent.left
            anchors.right: parent.right
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            text: workspace.desktopName(workspace.currentDesktop)
        }

        Grid {
            id: view
            columns: 1
            rows: 1
            property int itemWidth: dialogItem.screenWidth * Math.min(0.8/columns, 0.1)
            property int itemHeight: Math.min(itemWidth * (dialogItem.screenHeight / dialogItem.screenWidth), dialogItem.screenHeight * Math.min(0.8/rows, 0.1))
            anchors {
                top: textElement.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            visible: dialogItem.showGrid
            Repeater {
                id: repeater
                model: workspace.desktops
                Item {
                    width: view.itemWidth
                    height: view.itemHeight
                    // In spatial mode, some cells may be empty
                    property int desktopIndex: dialogItem.desktopForCell(index)
                    visible: desktopIndex >= 0
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
                            NumberAnimation { duration: dialogItem.animationDuration/2 }
                        }
                    }
                    Item {
                        id: arrowsContainer
                        anchors.fill: parent
                        visible: parent.desktopIndex >= 0
                        // Arrows use desktopIndex for position logic
                        KQuickControlsAddons.QIconItem {
                            anchors.fill: parent
                            icon: "go-up"
                            visible: false
                        }
                        KQuickControlsAddons.QIconItem {
                            anchors.fill: parent
                            icon: "go-down"
                            visible: {
                                var di = desktopIndex;
                                if (di < 0) return false;
                                if (dialogItem.currentDesktop <= di) return false;
                                if (di < dialogItem.previousDesktop) return false;
                                if (dialogItem.currentDesktop < dialogItem.previousDesktop) return false;
                                if (Math.floor(dialogItem.currentDesktop/view.columns) == Math.floor(index/view.columns)) return false;
                                if (dialogItem.previousDesktop % view.columns == index % view.columns) return true;
                                return false;
                            }
                        }
                        KQuickControlsAddons.QIconItem {
                            anchors.fill: parent
                            icon: "go-up"
                            visible: {
                                var di = desktopIndex;
                                if (di < 0) return false;
                                if (dialogItem.currentDesktop >= di) return false;
                                if (di > dialogItem.previousDesktop) return false;
                                if (dialogItem.currentDesktop > dialogItem.previousDesktop) return false;
                                if (Math.floor(dialogItem.currentDesktop/view.columns) == Math.floor(index/view.columns)) return false;
                                if (dialogItem.previousDesktop % view.columns == index % view.columns) return true;
                                return false;
                            }
                        }
                        KQuickControlsAddons.QIconItem {
                            anchors.fill: parent
                            icon: "go-next"
                            visible: {
                                var di = desktopIndex;
                                if (di < 0) return false;
                                if (dialogItem.currentDesktop <= di) return false;
                                if (di < dialogItem.previousDesktop) {
                                    if (Math.floor(dialogItem.currentDesktop/view.columns) == Math.floor(index/view.columns)) {
                                        if (index % view.columns >= dialogItem.previousDesktop % view.columns) return true;
                                    }
                                    return false;
                                }
                                if (dialogItem.currentDesktop < dialogItem.previousDesktop) return false;
                                if (Math.floor(dialogItem.currentDesktop/view.columns) == Math.floor(index/view.columns)) {
                                    if (index % view.columns < dialogItem.previousDesktop % view.columns) return false;
                                    return true;
                                }
                                return false;
                            }
                        }
                        KQuickControlsAddons.QIconItem {
                            anchors.fill: parent
                            icon: "go-previous"
                            visible: {
                                var di = desktopIndex;
                                if (di < 0) return false;
                                if (dialogItem.currentDesktop >= di) return false;
                                if (di > dialogItem.previousDesktop) {
                                    if (Math.floor(dialogItem.currentDesktop/view.columns) == Math.floor(index/view.columns)) {
                                        if (index % view.columns <= dialogItem.previousDesktop % view.columns) return true;
                                    }
                                    return false;
                                }
                                if (dialogItem.currentDesktop > dialogItem.previousDesktop) return false;
                                if (Math.floor(dialogItem.currentDesktop/view.columns) == Math.floor(index/view.columns)) {
                                    if (index % view.columns > dialogItem.previousDesktop % view.columns) return false;
                                    return true;
                                }
                                return false;
                            }
                        }
                    }
                    states: [
                        State {
                            name: "NORMAL"
                            when: desktopIndex != dialogItem.currentDesktop
                            PropertyChanges {
                                target: activeElement
                                opacity: 0.0
                            }
                        },
                        State {
                            name: "SELECTED"
                            when: desktopIndex == dialogItem.currentDesktop
                            PropertyChanges {
                                target: activeElement
                                opacity: 1.0
                            }
                        }
                    ]
                    Component.onCompleted: {
                        view.state = (desktopIndex == dialogItem.currentDesktop) ? "SELECTED" : "NORMAL"
                    }
                }
            }
        }

        Timer {
            id: timer
            repeat: false
            interval: dialogItem.animationDuration
            onTriggered: dialog.visible = false
        }

        Connections {
            target: workspace
            function onCurrentDesktopChanged() {
                dialogItem.show()
            }
            function onNumberDesktopsChanged() {
                if (!workspace.spatialMode) {
                    repeater.model = workspace.desktops;
                }
                // In spatial mode, show() will update the model from spatialGridLayout
            }
        }
        Connections {
            target: options
            function onConfigChanged() {
                dialogItem.loadConfig()
            }
        }
        Component.onCompleted: {
            view.columns = workspace.desktopGridWidth;
            view.rows = workspace.desktopGridHeight;
            dialogItem.loadConfig();
            dialogItem.show();
        }
    }

    Component.onCompleted: {
        KWin.registerWindow(dialog);
    }
}
