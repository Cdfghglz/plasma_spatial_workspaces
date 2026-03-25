/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KDE Contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.0

Item {
    id: root

    Text {
        id: activityLabel
        anchors.centerIn: parent
        text: bridge ? bridge.activityName : ""
        color: "white"
        font.pixelSize: 18
        font.bold: true
        style: Text.Outline
        styleColor: "black"
        visible: text.length > 0
    }
}
