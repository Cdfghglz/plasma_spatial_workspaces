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
}
