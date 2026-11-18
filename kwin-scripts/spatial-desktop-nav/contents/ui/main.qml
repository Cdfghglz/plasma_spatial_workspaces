import QtQuick 2.0
import org.kde.kwin 2.0
import "../code/spatial.js" as Spatial

Item {
    id: root

    property bool overviewVisible: false

    Component.onCompleted: {
        Spatial.init();
        console.log("Spatial Desktop Nav: initialized with " + workspace.desktops + " desktops");

        // Directional navigation shortcuts
        KWin.registerShortcut("SpatialNavUp",
            "Spatial Nav: Go Up", "Meta+Alt+Up",
            function() { Spatial.navigate("up"); });

        KWin.registerShortcut("SpatialNavDown",
            "Spatial Nav: Go Down", "Meta+Alt+Down",
            function() { Spatial.navigate("down"); });

        KWin.registerShortcut("SpatialNavLeft",
            "Spatial Nav: Go Left", "Meta+Alt+Left",
            function() { Spatial.navigate("left"); });

        KWin.registerShortcut("SpatialNavRight",
            "Spatial Nav: Go Right", "Meta+Alt+Right",
            function() { Spatial.navigate("right"); });

        // Dynamic desktop creation shortcuts
        KWin.registerShortcut("SpatialCreateUp",
            "Spatial Nav: Create Desktop Up", "Meta+Alt+Shift+Up",
            function() { Spatial.createInDirection("up"); });

        KWin.registerShortcut("SpatialCreateDown",
            "Spatial Nav: Create Desktop Down", "Meta+Alt+Shift+Down",
            function() { Spatial.createInDirection("down"); });

        KWin.registerShortcut("SpatialCreateLeft",
            "Spatial Nav: Create Desktop Left", "Meta+Alt+Shift+Left",
            function() { Spatial.createInDirection("left"); });

        KWin.registerShortcut("SpatialCreateRight",
            "Spatial Nav: Create Desktop Right", "Meta+Alt+Shift+Right",
            function() { Spatial.createInDirection("right"); });

        // Move window shortcuts
        KWin.registerShortcut("SpatialMoveWindowUp",
            "Spatial Nav: Move Window Up", "Meta+Ctrl+Alt+Up",
            function() { Spatial.moveWindowToDirection("up", true); });

        KWin.registerShortcut("SpatialMoveWindowDown",
            "Spatial Nav: Move Window Down", "Meta+Ctrl+Alt+Down",
            function() { Spatial.moveWindowToDirection("down", true); });

        KWin.registerShortcut("SpatialMoveWindowLeft",
            "Spatial Nav: Move Window Left", "Meta+Ctrl+Alt+Left",
            function() { Spatial.moveWindowToDirection("left", true); });

        KWin.registerShortcut("SpatialMoveWindowRight",
            "Spatial Nav: Move Window Right", "Meta+Ctrl+Alt+Right",
            function() { Spatial.moveWindowToDirection("right", true); });

        // Overview toggle
        KWin.registerShortcut("SpatialOverview",
            "Spatial Nav: Toggle Overview", "Meta+Alt+G",
            function() { toggleOverview(); });
    }

    function toggleOverview() {
        if (overviewLoader.item && overviewLoader.item.visible) {
            overviewLoader.item.visible = false;
        } else {
            var model = Spatial.getOverviewModel();
            var ghosts = Spatial.getGhostPositions();
            if (!overviewLoader.item) {
                overviewLoader.setSource("overview.qml", {
                    "desktopModel": model,
                    "ghostModel": ghosts,
                    "spatialRef": Spatial
                });
            } else {
                overviewLoader.item.desktopModel = model;
                overviewLoader.item.ghostModel = ghosts;
                overviewLoader.item.visible = true;
            }
        }
    }

    Loader {
        id: overviewLoader
    }

    // Re-initialize spatial map when desktops change externally
    Connections {
        target: workspace
        function onNumberDesktopsChanged() {
            // Only re-init if a desktop was removed externally
            // (our createInDirection already updates the map)
            var count = workspace.desktops;
            var mapCount = Object.keys(Spatial.spatialMap).length;
            if (count < mapCount) {
                Spatial.init();
            }
        }
    }

    // Swap per-activity desktop names when the user switches activities.
    // This makes the global KDE names reflect the active activity's names, so all
    // KWin components (OSD, desktop grid tile overlay, pager, etc.) automatically
    // show the correct per-activity name without needing individual changes.
    Connections {
        target: workspace
        function onCurrentActivityChanged() {
            Spatial.onActivityChanged();
        }
    }
}
