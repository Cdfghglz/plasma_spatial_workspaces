/**
 * Spatial Desktop Navigation - Core Logic
 *
 * Manages a spatial neighbor map for virtual desktops and provides
 * directional navigation, dynamic desktop creation, and position
 * computation for the schematic overview.
 */

// The spatial map: desktopName -> { up, down, left, right } (values are desktop names or null)
var spatialMap = {};

// Computed grid positions for overview: desktopName -> { x, y }
var gridPositions = {};

// Per-activity name overlay: { activityId: { desktopNum: name } }
// The active activity's names are always reflected in the global KDE desktop names.
// On activity switch we save the outgoing names and restore the incoming ones, so
// all KWin components (OSD, tile overlay, pager, etc.) automatically show the
// correct per-activity name without needing changes.
var activityDesktopNames = {};

// Global desktop names captured at first init, used as the baseline for activities
// that have never had a per-activity rename.
var originalDesktopNames = {};

// Activity ID that was active on the last init/switch, used by onActivityChanged().
var previousActivityId = "";

// Guard so captureOriginalNames() and previousActivityId init run only once.
var _firstInit = true;

/**
 * Initialize the spatial map from existing desktops.
 * In spatial mode, reads neighbor relationships from KWin's VirtualDesktopManager
 * via the scripting API. Falls back to dense grid layout when not in spatial mode.
 */
function init() {
    spatialMap = {};
    var count = workspace.desktops;

    if (_firstInit) {
        captureOriginalNames();
        previousActivityId = workspace.currentActivity;
        _firstInit = false;
    }

    if (workspace.spatialMode) {
        // Read neighbor relationships directly from KWin's spatial neighbor data.
        // This respects the actual spatial layout rather than assuming dense packing.
        for (var i = 1; i <= count; i++) {
            var name = workspace.desktopName(i);
            var up = workspace.spatialNeighborUp(i);
            var down = workspace.spatialNeighborDown(i);
            var left = workspace.spatialNeighborLeft(i);
            var right = workspace.spatialNeighborRight(i);

            spatialMap[name] = {
                up:    (up > 0)    ? workspace.desktopName(up)    : null,
                down:  (down > 0)  ? workspace.desktopName(down)  : null,
                left:  (left > 0)  ? workspace.desktopName(left)  : null,
                right: (right > 0) ? workspace.desktopName(right) : null
            };
        }
    } else {
        // Non-spatial mode: dense row-major grid layout
        var cols = workspace.desktopGridWidth;
        var rows = workspace.desktopGridHeight;

        var grid = [];
        for (var r = 0; r < rows; r++) {
            grid[r] = [];
            for (var c = 0; c < cols; c++) {
                var desktopNum = r * cols + c + 1;
                if (desktopNum <= count) {
                    grid[r][c] = workspace.desktopName(desktopNum);
                } else {
                    grid[r][c] = null;
                }
            }
        }

        for (var r = 0; r < rows; r++) {
            for (var c = 0; c < cols; c++) {
                var name = grid[r][c];
                if (name === null) continue;

                spatialMap[name] = {
                    up:    (r > 0 && grid[r-1][c]) ? grid[r-1][c] : null,
                    down:  (r < rows-1 && grid[r+1][c]) ? grid[r+1][c] : null,
                    left:  (c > 0 && grid[r][c-1]) ? grid[r][c-1] : null,
                    right: (c < cols-1 && grid[r][c+1]) ? grid[r][c+1] : null
                };
            }
        }
    }

    computePositions();
}

/**
 * Find desktop number by name.
 * Returns 0 if not found.
 */
function desktopNumberByName(name) {
    var count = workspace.desktops;
    for (var i = 1; i <= count; i++) {
        if (workspace.desktopName(i) === name) {
            return i;
        }
    }
    return 0;
}

/**
 * Get the name of the current desktop.
 */
function currentDesktopName() {
    return workspace.desktopName(workspace.currentDesktop);
}

/**
 * Capture the current global desktop names as the baseline for all activities.
 * Called once on first init, before any per-activity renaming has occurred.
 * Also eagerly seeds activityDesktopNames for all known activities so that
 * applyNamesForActivity() has a correct baseline even for activities that are
 * never explicitly visited (avoiding the || originalDesktopNames fallback being
 * needed for activities that may have been visited in a prior session).
 */
function captureOriginalNames() {
    var count = workspace.desktops;
    originalDesktopNames = {};
    for (var i = 1; i <= count; i++) {
        originalDesktopNames[i] = workspace.desktopName(i);
    }

    // Eagerly seed all known activities with the original names as their baseline.
    // This ensures activities never explicitly visited have consistent starting names
    // regardless of signal ordering or startup races.
    var activities = workspace.activities;
    for (var a = 0; a < activities.length; a++) {
        var actId = activities[a];
        if (!activityDesktopNames[actId]) {
            var seed = {};
            for (var i = 1; i <= count; i++) {
                seed[i] = originalDesktopNames[i];
            }
            activityDesktopNames[actId] = seed;
        }
    }
}

/**
 * Save the current global desktop names as the stored names for actId.
 * Called just before leaving an activity so any renames (including those done
 * via the tile overlay / C++ path) are captured.
 */
function saveNamesForActivity(actId) {
    var count = workspace.desktops;
    var saved = {};
    for (var i = 1; i <= count; i++) {
        saved[i] = workspace.desktopName(i);
    }
    activityDesktopNames[actId] = saved;
}

/**
 * Apply the stored per-activity names for actId by setting the global KDE names.
 * For desktops without a stored per-activity name, falls back to originalDesktopNames.
 */
function applyNamesForActivity(actId) {
    var count = workspace.desktops;
    var stored = activityDesktopNames[actId] || {};
    for (var i = 1; i <= count; i++) {
        var name = stored[i] || originalDesktopNames[i];
        if (name && name !== workspace.desktopName(i)) {
            workspace.setDesktopName(i, name);
        }
    }
}

/**
 * Handle an activity switch: save the outgoing activity's names, apply the
 * incoming activity's names, then rebuild the spatial map with the new global names.
 * Connected to workspace.currentActivityChanged in main.qml.
 */
function onActivityChanged() {
    var newActId = workspace.currentActivity;
    if (newActId === previousActivityId) return;

    // Save current global names for the activity we are leaving.
    if (previousActivityId) {
        saveNamesForActivity(previousActivityId);
    }

    // Apply the incoming activity's names (or fall back to originals if first visit).
    applyNamesForActivity(newActId);

    // Rebuild the spatial map so its keys reflect the newly-applied global names.
    init();

    previousActivityId = newActId;
}

/**
 * Navigate in a direction. Returns true if navigation happened.
 */
function navigate(direction) {
    var current = currentDesktopName();
    var mapping = spatialMap[current];
    if (!mapping) return false;

    var targetName = mapping[direction];
    if (!targetName) return false;

    var targetNum = desktopNumberByName(targetName);
    if (targetNum === 0) return false;

    workspace.currentDesktop = targetNum;
    return true;
}

/**
 * Create a new desktop in the given direction from the current desktop.
 * Returns the name of the new desktop, or null if a neighbor already exists.
 */
function createInDirection(direction) {
    var current = currentDesktopName();
    var mapping = spatialMap[current];

    // If there's already a neighbor, just navigate to it
    if (mapping && mapping[direction]) {
        navigate(direction);
        return null;
    }

    // Generate name for new desktop
    var suffix = "";
    if (direction === "up") suffix = ".north";
    else if (direction === "down") suffix = ".south";
    else if (direction === "left") suffix = ".west";
    else if (direction === "right") suffix = ".east";

    var newName = current + suffix;

    // Create the desktop (appends to end of list)
    var newCount = workspace.desktops + 1;
    workspace.desktops = newCount;

    // Rename the new desktop
    workspace.setDesktopName(newCount, newName);

    // Track in per-activity maps so the name is preserved across activity switches.
    originalDesktopNames[newCount] = newName;
    var actId = workspace.currentActivity;
    if (!activityDesktopNames[actId]) activityDesktopNames[actId] = {};
    activityDesktopNames[actId][newCount] = newName;

    // Initialize spatial map entry for new desktop
    spatialMap[newName] = { up: null, down: null, left: null, right: null };

    // Wire bidirectional links
    var opposite = { up: "down", down: "up", left: "right", right: "left" };
    if (!spatialMap[current]) {
        spatialMap[current] = { up: null, down: null, left: null, right: null };
    }
    spatialMap[current][direction] = newName;
    spatialMap[newName][opposite[direction]] = current;

    // Recompute positions for overview
    computePositions();

    // Switch to the new desktop
    workspace.currentDesktop = newCount;

    return newName;
}

/**
 * Compute grid positions for all desktops via BFS from a starting desktop.
 * Result stored in gridPositions: { name: { x, y } }
 */
function computePositions() {
    gridPositions = {};

    var names = Object.keys(spatialMap);
    if (names.length === 0) return;

    // Start BFS from the first desktop
    var startName = names[0];
    gridPositions[startName] = { x: 0, y: 0 };
    var queue = [startName];
    var visited = {};
    visited[startName] = true;

    var dx = { left: -1, right: 1, up: 0, down: 0 };
    var dy = { left: 0, right: 0, up: -1, down: 1 };

    while (queue.length > 0) {
        var current = queue.shift();
        var pos = gridPositions[current];
        var mapping = spatialMap[current];
        if (!mapping) continue;

        var directions = ["up", "down", "left", "right"];
        for (var d = 0; d < directions.length; d++) {
            var dir = directions[d];
            var neighbor = mapping[dir];
            if (neighbor && !visited[neighbor]) {
                visited[neighbor] = true;
                gridPositions[neighbor] = {
                    x: pos.x + dx[dir],
                    y: pos.y + dy[dir]
                };
                queue.push(neighbor);
            }
        }
    }

    // Normalize positions so minimum is (0, 0)
    var minX = 0, minY = 0;
    var posNames = Object.keys(gridPositions);
    for (var i = 0; i < posNames.length; i++) {
        var p = gridPositions[posNames[i]];
        if (p.x < minX) minX = p.x;
        if (p.y < minY) minY = p.y;
    }
    for (var i = 0; i < posNames.length; i++) {
        gridPositions[posNames[i]].x -= minX;
        gridPositions[posNames[i]].y -= minY;
    }
}

/**
 * Get the spatial map as an array of objects for QML consumption.
 * Each entry: { name, x, y, isActive, hasUp, hasDown, hasLeft, hasRight }
 */
function getOverviewModel() {
    computePositions();
    var result = [];
    var currentName = currentDesktopName();
    var names = Object.keys(gridPositions);

    for (var i = 0; i < names.length; i++) {
        var name = names[i];
        var pos = gridPositions[name];
        var mapping = spatialMap[name] || {};

        result.push({
            name: name,
            x: pos.x,
            y: pos.y,
            isActive: (name === currentName),
            hasUp: !mapping.up,
            hasDown: !mapping.down,
            hasLeft: !mapping.left,
            hasRight: !mapping.right
        });
    }
    return result;
}

/**
 * Get the ghost positions (empty slots adjacent to existing desktops)
 * for showing creation points in the overview.
 */
function getGhostPositions() {
    computePositions();
    var ghosts = [];
    var occupied = {};

    // Mark all occupied positions
    var names = Object.keys(gridPositions);
    for (var i = 0; i < names.length; i++) {
        var p = gridPositions[names[i]];
        occupied[p.x + "," + p.y] = true;
    }

    var dx = { left: -1, right: 1, up: 0, down: 0 };
    var dy = { left: 0, right: 0, up: -1, down: 1 };

    // For each desktop with an empty neighbor slot, add a ghost
    for (var i = 0; i < names.length; i++) {
        var name = names[i];
        var pos = gridPositions[name];
        var mapping = spatialMap[name] || {};
        var directions = ["up", "down", "left", "right"];

        for (var d = 0; d < directions.length; d++) {
            var dir = directions[d];
            if (!mapping[dir]) {
                var gx = pos.x + dx[dir];
                var gy = pos.y + dy[dir];
                var key = gx + "," + gy;
                if (!occupied[key]) {
                    ghosts.push({
                        x: gx,
                        y: gy,
                        parentName: name,
                        direction: dir
                    });
                    occupied[key] = true; // prevent duplicate ghosts
                }
            }
        }
    }
    return ghosts;
}

/**
 * Rename a desktop in the current activity.
 * Sets the global KDE name (so all components display it immediately) and records
 * the name in activityDesktopNames so it is restored when returning to this activity
 * after visiting another one.
 * Returns true on success.
 */
function renameDesktop(oldName, newName) {
    if (!newName || newName === oldName) return false;
    if (spatialMap[newName]) return false; // name collision

    var num = desktopNumberByName(oldName);
    if (num === 0) return false;

    // Rename in KDE globally (onActivityChanged will restore per-activity names on switch).
    workspace.setDesktopName(num, newName);

    // Record so we can restore this name when returning to this activity.
    var actId = workspace.currentActivity;
    if (!activityDesktopNames[actId]) {
        activityDesktopNames[actId] = {};
    }
    activityDesktopNames[actId][num] = newName;

    // Update spatial map: copy entry under new key
    spatialMap[newName] = spatialMap[oldName];
    delete spatialMap[oldName];

    // Update all neighbors that pointed to oldName
    var names = Object.keys(spatialMap);
    for (var i = 0; i < names.length; i++) {
        var m = spatialMap[names[i]];
        if (m.up === oldName) m.up = newName;
        if (m.down === oldName) m.down = newName;
        if (m.left === oldName) m.left = newName;
        if (m.right === oldName) m.right = newName;
    }

    // Update grid positions
    if (gridPositions[oldName]) {
        gridPositions[newName] = gridPositions[oldName];
        delete gridPositions[oldName];
    }

    return true;
}

/**
 * Move the active window to the spatial neighbor in the given direction.
 * Optionally follow the window.
 */
function moveWindowToDirection(direction, follow) {
    var current = currentDesktopName();
    var mapping = spatialMap[current];
    if (!mapping) return false;

    var targetName = mapping[direction];
    if (!targetName) return false;

    var targetNum = desktopNumberByName(targetName);
    if (targetNum === 0) return false;

    var client = workspace.activeClient;
    if (!client) return false;

    client.desktop = targetNum;

    if (follow) {
        workspace.currentDesktop = targetNum;
    }

    return true;
}
