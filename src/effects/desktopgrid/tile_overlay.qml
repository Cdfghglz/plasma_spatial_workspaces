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
        onAccepted: {
            if (bridge) bridge.desktopName = text
            focus = false
        }
        Keys.onEscapePressed: {
            if (bridge) text = bridge.desktopName
            focus = false
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onDoubleClicked: {
            nameInput.text = bridge ? bridge.desktopName : ""
            nameInput.forceActiveFocus()
            nameInput.selectAll()
        }
    }

    Rectangle {
        anchors { top: parent.top; right: parent.right; margins: 4 }
        width: 20; height: 20; radius: 10
        color: closeHover.containsMouse ? "#cc0000" : "#990000"
        visible: hoverArea.containsMouse && bridge && bridge.totalDesktops > 1
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

    // Spatial edge '+' buttons — visible in spatial mode when that edge has no neighbor

    // Above
    Rectangle {
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter }
        width: 24; height: 24; radius: 12
        color: "#80000000"
        visible: hoverArea.containsMouse && bridge && bridge.spatialMode && !bridge.hasAbove
        Text { text: "+"; anchors.centerIn: parent; color: "white"; font.pixelSize: 16; font.bold: true }
        MouseArea {
            anchors.fill: parent
            onClicked: if (bridge) bridge.addDesktopInDirection("above")
        }
    }

    // Below
    Rectangle {
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter }
        width: 24; height: 24; radius: 12
        color: "#80000000"
        visible: hoverArea.containsMouse && bridge && bridge.spatialMode && !bridge.hasBelow
        Text { text: "+"; anchors.centerIn: parent; color: "white"; font.pixelSize: 16; font.bold: true }
        MouseArea {
            anchors.fill: parent
            onClicked: if (bridge) bridge.addDesktopInDirection("below")
        }
    }

    // Left
    Rectangle {
        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
        width: 24; height: 24; radius: 12
        color: "#80000000"
        visible: hoverArea.containsMouse && bridge && bridge.spatialMode && !bridge.hasLeft
        Text { text: "+"; anchors.centerIn: parent; color: "white"; font.pixelSize: 16; font.bold: true }
        MouseArea {
            anchors.fill: parent
            onClicked: if (bridge) bridge.addDesktopInDirection("left")
        }
    }

    // Right
    Rectangle {
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        width: 24; height: 24; radius: 12
        color: "#80000000"
        visible: hoverArea.containsMouse && bridge && bridge.spatialMode && !bridge.hasRight
        Text { text: "+"; anchors.centerIn: parent; color: "white"; font.pixelSize: 16; font.bold: true }
        MouseArea {
            anchors.fill: parent
            onClicked: if (bridge) bridge.addDesktopInDirection("right")
        }
    }
}
