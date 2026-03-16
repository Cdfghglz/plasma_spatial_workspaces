import QtQuick 2.12
import QtQuick.Layouts 1.12
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 2.0 as Plasma
import org.kde.plasma.extras 2.0 as PlasmaExtras
import org.kde.kwin 2.0
import "../code/spatial.js" as Spatial

PlasmaCore.Dialog {
    id: dialog
    location: PlasmaCore.Types.Floating
    visible: true
    flags: Qt.X11BypassWindowManagerHint | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    outputOnly: false

    property var desktopModel: []
    property var ghostModel: []
    property var spatialRef: null

    // Cell sizing
    property int cellWidth: 160
    property int cellHeight: 100
    property int cellSpacing: 12

    // Search filter (built manually from key events)
    property string filterText: ""

    // Index of the desktop currently being renamed (-1 = none)
    property int editingIndex: -1
    // Buffer for rename text (also built from key events)
    property string renameText: ""

    Component.onCompleted: {
        KWin.registerWindow(dialog);
        positionDialog();
        grabFocusTimer.start();
    }

    onVisibleChanged: {
        if (visible) {
            filterText = "";
            editingIndex = -1;
            renameText = "";
            positionDialog();
            grabFocusTimer.start();
        }
    }

    function positionDialog() {
        var screen = workspace.clientArea(KWin.FullScreenArea, workspace.activeScreen, workspace.currentDesktop);
        dialog.x = screen.x + screen.width / 2 - mainItem.width / 2;
        dialog.y = screen.y + screen.height / 2 - mainItem.height / 2;
    }

    function switchToDesktop(name) {
        var num = Spatial.desktopNumberByName(name);
        if (num > 0) {
            workspace.currentDesktop = num;
        }
        dialog.visible = false;
    }

    function createGhostDesktop(parentName, direction) {
        var parentNum = Spatial.desktopNumberByName(parentName);
        if (parentNum > 0) {
            workspace.currentDesktop = parentNum;
        }
        Spatial.createInDirection(direction);
        dialog.visible = false;
    }

    function matchesFilter(name) {
        if (filterText.length === 0) return true;
        return name.toLowerCase().indexOf(filterText.toLowerCase()) >= 0;
    }

    function getFilteredDesktops() {
        var result = [];
        for (var i = 0; i < desktopModel.length; i++) {
            if (matchesFilter(desktopModel[i].name)) {
                result.push(desktopModel[i]);
            }
        }
        return result;
    }

    function startRename(index) {
        editingIndex = index;
        renameText = desktopModel[index].name;
    }

    function commitRename() {
        if (editingIndex < 0) return;
        var entry = desktopModel[editingIndex];
        var newName = renameText.trim();
        if (newName.length > 0 && newName !== entry.name) {
            Spatial.renameDesktop(entry.name, newName);
            entry.name = newName;
            // Force model update
            var m = desktopModel;
            desktopModel = [];
            desktopModel = m;
        }
        editingIndex = -1;
        renameText = "";
    }

    function cancelRename() {
        editingIndex = -1;
        renameText = "";
    }

    mainItem: FocusScope {
        id: mainContent
        focus: true

        Timer {
            id: grabFocusTimer
            interval: 50
            onTriggered: {
                dialog.flags = Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint;
                dialog.requestActivate();
                mainContent.forceActiveFocus();
                restoreTimer.start();
            }
        }

        Timer {
            id: restoreTimer
            interval: 100
            onTriggered: {
                dialog.flags = Qt.X11BypassWindowManagerHint | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint;
            }
        }

        property int maxCol: {
            var m = 0;
            for (var i = 0; i < desktopModel.length; i++)
                if (desktopModel[i].x > m) m = desktopModel[i].x;
            for (var i = 0; i < ghostModel.length; i++)
                if (ghostModel[i].x > m) m = ghostModel[i].x;
            return m;
        }
        property int maxRow: {
            var m = 0;
            for (var i = 0; i < desktopModel.length; i++)
                if (desktopModel[i].y > m) m = desktopModel[i].y;
            for (var i = 0; i < ghostModel.length; i++)
                if (ghostModel[i].y > m) m = ghostModel[i].y;
            return m;
        }

        width: (maxCol + 1) * (cellWidth + cellSpacing) + cellSpacing + 40
        height: (maxRow + 1) * (cellHeight + cellSpacing) + cellSpacing + searchRow.height + 60

        // Capture all key events manually
        Keys.onPressed: {
            if (event.key === Qt.Key_Escape) {
                if (dialog.editingIndex >= 0) {
                    dialog.cancelRename();
                } else if (dialog.filterText.length > 0) {
                    dialog.filterText = "";
                } else {
                    dialog.visible = false;
                }
                event.accepted = true;
                return;
            }

            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (dialog.editingIndex >= 0) {
                    dialog.commitRename();
                } else {
                    var matches = dialog.getFilteredDesktops();
                    if (matches.length === 1) {
                        dialog.switchToDesktop(matches[0].name);
                    }
                }
                event.accepted = true;
                return;
            }

            if (event.key === Qt.Key_Backspace) {
                if (dialog.editingIndex >= 0) {
                    if (dialog.renameText.length > 0)
                        dialog.renameText = dialog.renameText.substring(0, dialog.renameText.length - 1);
                } else {
                    if (dialog.filterText.length > 0)
                        dialog.filterText = dialog.filterText.substring(0, dialog.filterText.length - 1);
                }
                event.accepted = true;
                return;
            }

            // Printable characters
            if (event.text && event.text.length > 0) {
                var ch = event.text;
                // Filter out control characters
                if (ch.charCodeAt(0) >= 32) {
                    if (dialog.editingIndex >= 0) {
                        dialog.renameText += ch;
                    } else {
                        dialog.filterText += ch;
                    }
                    event.accepted = true;
                }
            }
        }

        // Search bar row
        Row {
            id: searchRow
            anchors.top: parent.top
            anchors.topMargin: 10
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8

            PlasmaExtras.Heading {
                id: titleText
                text: "Spatial Desktop Overview"
                level: 3
                anchors.verticalCenter: parent.verticalCenter
            }

            Rectangle {
                width: 200
                height: 28
                radius: 4
                color: PlasmaCore.Theme.backgroundColor
                border.color: dialog.editingIndex < 0 ? PlasmaCore.Theme.highlightColor : PlasmaCore.Theme.textColor
                border.width: dialog.editingIndex < 0 ? 2 : 1
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    anchors.fill: parent
                    anchors.margins: 4
                    text: dialog.filterText.length > 0 ? dialog.filterText : (dialog.editingIndex < 0 ? "Type to filter..." : "")
                    color: PlasmaCore.Theme.textColor
                    opacity: dialog.filterText.length > 0 ? 1.0 : 0.4
                    font.pixelSize: 13
                    verticalAlignment: Text.AlignVCenter
                }

                // Blinking cursor
                Rectangle {
                    visible: dialog.editingIndex < 0
                    x: filterCursorMetrics.width + 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 1
                    height: 16
                    color: PlasmaCore.Theme.textColor
                    opacity: cursorBlink.running ? (cursorBlink.visible ? 1 : 0) : 0

                    Timer {
                        id: cursorBlink
                        interval: 530
                        repeat: true
                        running: dialog.editingIndex < 0 && dialog.visible
                        property bool visible: true
                        onTriggered: visible = !visible
                    }

                    TextMetrics {
                        id: filterCursorMetrics
                        font.pixelSize: 13
                        text: dialog.filterText
                    }
                }
            }
        }

        // Container for desktop cells
        Item {
            id: gridContainer
            anchors.top: searchRow.bottom
            anchors.topMargin: 10
            anchors.horizontalCenter: parent.horizontalCenter
            width: (mainContent.maxCol + 1) * (cellWidth + cellSpacing) + cellSpacing
            height: (mainContent.maxRow + 1) * (cellHeight + cellSpacing) + cellSpacing

            // Real desktops
            Repeater {
                model: desktopModel.length

                Rectangle {
                    id: desktopCell
                    property var entry: desktopModel[index]
                    property bool isEditing: dialog.editingIndex === index
                    property bool isFiltered: !dialog.matchesFilter(entry.name)

                    x: entry.x * (cellWidth + cellSpacing) + cellSpacing
                    y: entry.y * (cellHeight + cellSpacing) + cellSpacing
                    width: cellWidth
                    height: cellHeight
                    radius: 6
                    color: entry.isActive ? PlasmaCore.Theme.highlightColor : PlasmaCore.Theme.backgroundColor
                    border.color: isEditing ? PlasmaCore.Theme.highlightColor :
                                  (entry.isActive ? PlasmaCore.Theme.highlightColor : PlasmaCore.Theme.textColor)
                    border.width: isEditing ? 3 : (entry.isActive ? 3 : 1)
                    opacity: isFiltered ? 0.15 : 1.0

                    // Display label (hidden when editing)
                    Text {
                        id: labelText
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -10
                        text: entry.name
                        color: entry.isActive ? PlasmaCore.Theme.highlightedTextColor : PlasmaCore.Theme.textColor
                        font.bold: entry.isActive
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        width: parent.width - 16
                        visible: !desktopCell.isEditing
                    }

                    // Rename editor display (shows renameText when editing)
                    Rectangle {
                        id: editBg
                        anchors.centerIn: parent
                        width: parent.width - 12
                        height: 24
                        radius: 3
                        color: PlasmaCore.Theme.backgroundColor
                        border.color: PlasmaCore.Theme.highlightColor
                        border.width: 2
                        visible: desktopCell.isEditing

                        Text {
                            anchors.fill: parent
                            anchors.margins: 3
                            text: dialog.renameText
                            color: PlasmaCore.Theme.textColor
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            clip: true
                        }
                    }

                    // Rename button
                    Rectangle {
                        id: renameBtn
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 6
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: renameLabel.width + 12
                        height: 18
                        radius: 3
                        color: renameBtnArea.containsMouse ? PlasmaCore.Theme.highlightColor : "transparent"
                        border.color: entry.isActive ? PlasmaCore.Theme.highlightedTextColor : PlasmaCore.Theme.textColor
                        border.width: 1
                        opacity: renameBtnArea.containsMouse ? 1.0 : 0.5
                        visible: !desktopCell.isEditing && !desktopCell.isFiltered

                        Text {
                            id: renameLabel
                            anchors.centerIn: parent
                            text: "rename"
                            color: renameBtnArea.containsMouse ? PlasmaCore.Theme.highlightedTextColor :
                                   (entry.isActive ? PlasmaCore.Theme.highlightedTextColor : PlasmaCore.Theme.textColor)
                            font.pixelSize: 10
                        }

                        MouseArea {
                            id: renameBtnArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                dialog.startRename(index);
                            }
                        }
                    }

                    MouseArea {
                        id: hoverArea
                        anchors.fill: parent
                        // Don't steal clicks from rename button
                        z: -1
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (!desktopCell.isFiltered && !desktopCell.isEditing) {
                                dialog.switchToDesktop(entry.name);
                            }
                        }
                    }

                    states: State {
                        name: "hovered"
                        when: hoverArea.containsMouse && !entry.isActive && !desktopCell.isFiltered && !desktopCell.isEditing
                        PropertyChanges {
                            target: desktopCell
                            color: Qt.lighter(PlasmaCore.Theme.backgroundColor, 1.2)
                            border.width: 2
                        }
                    }

                    Behavior on opacity {
                        NumberAnimation { duration: 150 }
                    }
                }
            }

            // Ghost desktops - hidden when filter active
            Repeater {
                model: ghostModel.length

                Rectangle {
                    property var ghost: ghostModel[index]

                    x: ghost.x * (cellWidth + cellSpacing) + cellSpacing
                    y: ghost.y * (cellHeight + cellSpacing) + cellSpacing
                    width: cellWidth
                    height: cellHeight
                    radius: 6
                    color: "transparent"
                    border.color: PlasmaCore.Theme.textColor
                    border.width: 1
                    opacity: dialog.filterText.length > 0 ? 0 : (ghostHover.containsMouse ? 0.6 : 0.2)
                    visible: dialog.filterText.length === 0

                    Text {
                        anchors.centerIn: parent
                        text: "+"
                        color: PlasmaCore.Theme.textColor
                        font.pixelSize: 28
                        opacity: parent.opacity
                    }

                    MouseArea {
                        id: ghostHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            dialog.createGhostDesktop(ghost.parentName, ghost.direction);
                        }
                    }

                    Behavior on opacity {
                        NumberAnimation { duration: 150 }
                    }
                }
            }
        }

        // Close hint
        Text {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Click to switch  |  Type to filter  |  Enter on match  |  Esc to close"
            color: PlasmaCore.Theme.textColor
            opacity: 0.5
            font.pixelSize: 11
        }
    }
}
