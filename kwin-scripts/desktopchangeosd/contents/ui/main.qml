/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick 2.0;
import org.kde.kwin 2.0;

Item {
    id: root

    Loader {
        id: mainItemLoader
    }

    // Toggle the OSD for the current desktop/activity without a switch.
    // It stays up until the next press, or until a desktop/activity change
    // replaces it with the normal timed OSD.
    function toggleCurrent() {
        if (!mainItemLoader.item) {
            mainItemLoader.source = "osd.qml";
        }
        if (!mainItemLoader.item) {
            return;
        }
        if (mainItemLoader.item.dialogsVisible && mainItemLoader.item.sticky) {
            mainItemLoader.item.hide();
            root.setLauncherOpen(false);
        } else {
            mainItemLoader.item.show(true, true);
            root.setLauncherOpen(true);
        }
    }

    // The launcher D-Bus call only toggles, and the menu also closes on its
    // own (app launched, click elsewhere) while the OSD stays pinned. So look
    // at what is on screen instead of counting toggles: an open panel popup is
    // a managed, transient plasmashell window (hidden ones are unmapped).
    property bool wantLauncherOpen: false

    function launcherOpen() {
        var clients = workspace.clientList();
        for (var i = 0; i < clients.length; i++) {
            var c = clients[i];
            if (c.transient && !c.dock && c.resourceClass.toString() === "plasmashell") {
                return true;
            }
        }
        return false;
    }

    function setLauncherOpen(open) {
        root.wantLauncherOpen = open;
        if (root.launcherOpen() !== open) {
            launcherMenuCall.call();
            launcherCheck.restart();
        }
    }

    Timer {
        id: launcherCheck
        interval: 400
        repeat: false
        // Kickoff's own expanded flag can drift from what is on screen;
        // one more toggle brings it back in line.
        onTriggered: {
            if (root.launcherOpen() !== root.wantLauncherOpen) {
                launcherMenuCall.call();
            }
        }
    }

    DBusCall {
        id: launcherMenuCall
        service: "org.kde.plasmashell"
        path: "/PlasmaShell"
        dbusInterface: "org.kde.PlasmaShell"
        method: "activateLauncherMenu"
    }

    Connections {
        target: workspace
        function onCurrentDesktopChanged() {
            if (!mainItemLoader.item) {
                mainItemLoader.source = "osd.qml";
            }
        }
    }

    Component.onCompleted: {
        // Owns the launcher key so the OSD can be shown alongside the
        // panel and application menu.
        KWin.registerShortcut("DesktopChangeOsdShowWithLauncher",
            "Show Desktop OSD and Application Launcher",
            "Alt+Space",
            function() {
                root.toggleCurrent();
            });
    }
}
