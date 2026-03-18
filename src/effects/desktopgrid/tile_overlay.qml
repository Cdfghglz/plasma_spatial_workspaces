/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KDE Contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.0

Item {
    id: root

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }

    // Hover state for the whole tile.  Only the full-surface hoverArea drives
    // this — child button MouseAreas sit *inside* hoverArea so they cannot
    // produce containsMouse=true without hoverArea also being true.  Referring
    // to the child hover states here caused a binding loop (tileHovered →
    // button visible → MouseArea geometry → containsMouse → tileHovered).
    property bool tileHovered: hoverArea.containsMouse

    Text {
        id: nameLabel
        anchors.centerIn: parent
        text: bridge ? bridge.desktopName : ""
        color: "white"
        font.pixelSize: 14
        font.bold: true
        visible: !nameInput.activeFocus
        style: Text.Outline
        styleColor: "black"
    }

    TextInput {
        id: nameInput
        anchors.centerIn: parent
        width: parent.width - 16
        text: bridge ? bridge.desktopName : ""
        color: "white"
        font.pixelSize: 14
        font.bold: true
        horizontalAlignment: TextInput.AlignHCenter
        visible: activeFocus
        onActiveFocusChanged: {
            if (bridge) bridge.setEditing(activeFocus)
        }
        onAccepted: {
            if (bridge) bridge.desktopName = text
            focus = false
        }
        Keys.onEscapePressed: {
            if (bridge) text = bridge.desktopName
            focus = false
        }
    }

    // Double-click to rename: single clicks pass through to C++ for desktop selection.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: mouse.accepted = false
        onReleased: mouse.accepted = false
        onDoubleClicked: {
            nameInput.text = bridge ? bridge.desktopName : ""
            nameInput.forceActiveFocus()
            nameInput.selectAll()
            mouse.accepted = true
        }
    }

    // Close button
    Rectangle {
        anchors { top: parent.top; right: parent.right; margins: 4 }
        width: 20; height: 20; radius: 10
        color: closeHover.containsMouse ? "#cc0000" : "#990000"
        visible: root.tileHovered && bridge && bridge.totalDesktops > 1
        Text {
            text: "×"
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 12
            font.bold: true
        }
        MouseArea {
            id: closeHover
            anchors.fill: parent
            hoverEnabled: true
            onClicked: {
                if (bridge) bridge.removeDesktop()
            }
        }
    }

    // Spatial edge '+' buttons — visible when that edge has no neighbor

    // Above
    Rectangle {
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 8 }
        width: 24; height: 24; radius: 12
        color: aboveHover.containsMouse ? "#cc000000" : "#80000000"
        visible: root.tileHovered && bridge && !bridge.hasAbove
        Text { text: "+"; anchors.centerIn: parent; color: "white"; font.pixelSize: 16; font.bold: true }
        MouseArea {
            id: aboveHover
            anchors.fill: parent
            hoverEnabled: true
            onClicked: if (bridge) bridge.addDesktopInDirection("above")
        }
    }

    // Below
    Rectangle {
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 8 }
        width: 24; height: 24; radius: 12
        color: belowHover.containsMouse ? "#cc000000" : "#80000000"
        visible: root.tileHovered && bridge && !bridge.hasBelow
        Text { text: "+"; anchors.centerIn: parent; color: "white"; font.pixelSize: 16; font.bold: true }
        MouseArea {
            id: belowHover
            anchors.fill: parent
            hoverEnabled: true
            onClicked: if (bridge) bridge.addDesktopInDirection("below")
        }
    }

    // Left
    Rectangle {
        anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 8 }
        width: 24; height: 24; radius: 12
        color: leftHover.containsMouse ? "#cc000000" : "#80000000"
        visible: root.tileHovered && bridge && !bridge.hasLeft
        Text { text: "+"; anchors.centerIn: parent; color: "white"; font.pixelSize: 16; font.bold: true }
        MouseArea {
            id: leftHover
            anchors.fill: parent
            hoverEnabled: true
            onClicked: if (bridge) bridge.addDesktopInDirection("left")
        }
    }

    // Right
    Rectangle {
        anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 8 }
        width: 24; height: 24; radius: 12
        color: rightHover.containsMouse ? "#cc000000" : "#80000000"
        visible: root.tileHovered && bridge && !bridge.hasRight
        Text { text: "+"; anchors.centerIn: parent; color: "white"; font.pixelSize: 16; font.bold: true }
        MouseArea {
            id: rightHover
            anchors.fill: parent
            hoverEnabled: true
            onClicked: if (bridge) bridge.addDesktopInDirection("right")
        }
    }
}
