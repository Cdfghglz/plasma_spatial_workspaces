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

/**
 * Initialize the spatial map from existing desktops.
 * Arranges them according to KDE's current grid layout.
 */
function init() {
    spatialMap = {};
    var count = workspace.desktops;
    var cols = workspace.desktopGridWidth;
    var rows = workspace.desktopGridHeight;

    // Build a 2D array of desktop names based on current grid
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

    // Build spatial map from grid
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
 * Rename a desktop. Updates KDE and the spatial map.
 * Returns true on success.
 */
function renameDesktop(oldName, newName) {
    if (!newName || newName === oldName) return false;
    if (spatialMap[newName]) return false; // name collision

    var num = desktopNumberByName(oldName);
    if (num === 0) return false;

    // Rename in KDE
    workspace.setDesktopName(num, newName);

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
