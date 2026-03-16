# Research: Custom Spatial Virtual Desktop Layout on KDE Plasma 5.24 (Kubuntu 22.04)

## Executive Summary

**No existing tool provides exactly what you want out of the box** -- a free-form, non-rectangular 2D spatial desktop topology with sub-desktop hierarchy on KDE Plasma. However, a custom KWin script is a highly viable path to implement this. The KWin scripting API provides all the necessary primitives: `registerShortcut()` for custom keybindings, `workspace.currentDesktop` (read/write) for switching desktops, and the full list of `VirtualDesktop` objects. The missing piece is the spatial mapping logic, which must be authored as a custom lookup table in a KWin script.

Below is a comprehensive breakdown of everything found.

---

## 1. KWin Scripts for Desktop Navigation

### 1.1 Swap/Add/Remove - Virtual Desktop Shortcuts
- **URL**: https://store.kde.org/p/1333377 (Plasma 5 legacy) / https://store.kde.org/p/2164947 (Plasma 6 v2)
- **GitHub**: https://github.com/clementballot38/virtual_desktop_tools (fork/continuation)
- **What it does**: Adds shortcuts to swap windows between adjacent desktops in 4 directions (up/down/left/right), add/remove desktops dynamically.
- **Relevance**: **HIGH** -- demonstrates that KWin scripts can intercept directional navigation and remap it. The 4-directional swap (Meta+Shift+Alt+Arrow) proves directional desktop navigation is scriptable.
- **Maintained**: Legacy version last updated Dec 2019; v2 available for Plasma 6. The virtual_desktop_tools fork is active.
- **Plasma compatibility**: Plasma 5 (legacy) and Plasma 6 (v2).

### 1.2 Dynamic Workspaces
- **URL**: https://github.com/maurges/dynamic_workspaces
- **KDE Store**: https://store.kde.org/p/1312691
- **What it does**: Auto-creates/removes desktops (GNOME-style). Always keeps one empty desktop at the end.
- **Relevance**: MEDIUM -- demonstrates desktop creation/deletion via KWin scripting API.
- **Maintained**: Active, tested on Plasma 5.27 through 6.2.5.
- **Limitation**: Horizontal left-to-right only.

### 1.3 KWin Cycle Non-Empty Desktops
- **URL**: https://github.com/shaansubbaiah/kwin-cycle-non-empty-desktops
- **KDE Store**: https://store.kde.org/p/1700772
- **What it does**: Skips empty desktops when cycling next/previous.
- **Relevance**: MEDIUM -- good reference for `registerShortcut` + `workspace.currentDesktop` patterns. Clean, simple codebase to study.
- **Maintained**: 800+ downloads; works with modern KWin scripting API.

### 1.4 Parachute (ARCHIVED)
- **URL**: https://github.com/tcorreabr/Parachute
- **What it does**: Bird's-eye overview of all desktops and windows (like GNOME Overview). Supports Shift+Arrow to switch desktops spatially.
- **Relevance**: MEDIUM -- shows spatial desktop visualization and directional switching via QML.
- **Maintained**: ARCHIVED (April 2022, read-only). Last release v0.9.1 (Jan 2021).
- **Compatibility**: Plasma >= 5.20, X11 only (no Wayland).

### 1.5 Last Used Virtual Desktops
- **URL**: https://store.kde.org/p/2304487
- **What it does**: History-based desktop switching and toggle functionality.
- **Relevance**: LOW -- different navigation paradigm, but shows modern KWin scripting patterns.
- **Maintained**: Recent (Plasma 6).

### 1.6 Grid-Tiling-Kwin
- **URL**: https://github.com/lingtjien/Grid-Tiling-Kwin
- **KDE Store**: https://store.kde.org/p/1198671
- **What it does**: Auto-tiles windows in a grid. Has shortcuts for switching desktops (Meta+Right/Left) and screens (Meta+Up/Down).
- **Relevance**: MEDIUM -- demonstrates directional desktop switching via shortcuts in a KWin script.

---

## 2. KDE Plasma Widgets for Virtual Desktops

### 2.1 Virtual Desktop Bar (Plasma 6 - actively maintained)
- **URL**: https://github.com/lenonk/virtual-desktop-bar
- **What it does**: Clean, text-based virtual desktop switcher widget. Replaces default Pager. Supports dynamic desktop management.
- **Relevance**: LOW for navigation logic, but useful as a UI companion to display custom desktop topology.
- **Maintained**: YES -- actively maintained (v1.0.2, Feb 2026). 546 commits.
- **Compatibility**: Plasma 6 + Wayland ONLY. **Not compatible with Plasma 5.24.**
- **Limitation**: No 2D spatial layout; horizontal switcher only.

### 2.2 Virtual Desktop Bar (Plasma 5 versions)
- **URL**: https://github.com/AllJavi/virtual-desktop-bar (with image support)
- **URL**: https://github.com/joserebelo/plasma-virtual-desktop-bar
- **What they do**: Pager alternatives for Plasma 5 with text labels, dynamic desktop management.
- **Relevance**: LOW -- UI widgets, not navigation logic.
- **Maintained**: AllJavi version last updated Aug 2022; limited maintenance.

### 2.3 Default Pager Widget
- **URL**: https://userbase.kde.org/Plasma/Pager
- **What it does**: Standard KDE pager showing rectangular grid of desktops.
- **Relevance**: LOW -- only supports rectangular grid (1xN or custom rows x columns).

---

## 3. KWin Scripting API -- Key Capabilities for Custom Implementation

### 3.1 Official Documentation
- **API Reference**: https://develop.kde.org/docs/plasma/kwin/api/
- **Tutorial**: https://develop.kde.org/docs/plasma/kwin/
- **Unofficial docs**: https://zeroxoneafour.github.io/kwin-scripting-docs/workspace.html

### 3.2 Relevant API Surface

**Properties (workspace object):**
- `workspace.desktops` -- array of all VirtualDesktop objects
- `workspace.currentDesktop` -- currently active desktop (READ/WRITE -- this is the key to switching)
- `workspace.desktopGridSize` -- QSize of the desktop grid layout
- `workspace.desktopGridWidth` / `workspace.desktopGridHeight` -- grid dimensions

**Methods:**
- `workspace.createDesktop(position, name)` -- create new virtual desktop
- `workspace.removeDesktop(desktop)` -- remove a virtual desktop

**Signals:**
- `workspace.currentDesktopChanged(oldDesktop)` -- emitted on desktop switch
- `workspace.desktopsChanged()` -- emitted when desktops added/removed
- `workspace.desktopLayoutChanged()` -- emitted when grid layout changes

**Slot functions (for built-in navigation):**
- `workspace.slotSwitchDesktopNext()` / `slotSwitchDesktopPrevious()`
- `workspace.slotSwitchDesktopRight()` / `slotSwitchDesktopLeft()`
- `workspace.slotSwitchDesktopUp()` / `slotSwitchDesktopDown()`

**Directional helpers (EffectsHandler, may not be available in all script contexts):**
- `desktopToRight(desktop, wrap)`, `desktopToLeft(desktop, wrap)`
- `desktopAbove(desktop, wrap)`, `desktopBelow(desktop, wrap)`

**Shortcut registration:**
- `registerShortcut(name, description, keySequence, callback)` -- registers a global shortcut

### 3.3 Script Structure

```
myscript/
├── contents/
│   ├── code/
│   │   └── main.js
│   ├── config/
│   │   └── main.xml       (optional: user-configurable settings)
│   └── ui/
│       └── config.ui       (optional: settings UI)
└── metadata.json
```

**metadata.json:**
```json
{
    "KPlugin": {
        "Name": "Spatial Desktop Navigation",
        "Description": "Custom 2D spatial virtual desktop navigation",
        "Icon": "preferences-system-windows",
        "Id": "spatial-desktop-nav",
        "Version": "1.0"
    },
    "X-Plasma-API": "javascript",
    "X-Plasma-MainScript": "code/main.js",
    "KPackageStructure": "KWin/Script"
}
```

**Installation (Plasma 5):**
```bash
plasmapkg2 --type kwinscript -i ./myscript/
# Then enable in System Settings > Window Management > KWin Scripts
# Or:
kwriteconfig5 --file kwinrc --group Plugins --key spatial-desktop-navEnabled true
qdbus org.kde.KWin /KWin reconfigure
```

**Testing:**
```bash
plasma-interactiveconsole --kwin    # Live script console
journalctl -f QT_CATEGORY=js QT_CATEGORY=kwin_scripting  # Debug output
```

---

## 4. DBus Interface for Virtual Desktops

### 4.1 VirtualDesktopManager Interface
- **Service**: `org.kde.KWin`
- **Path**: `/VirtualDesktopManager`
- **Interface**: `org.kde.KWin.VirtualDesktopManager`

**Properties:**
- `count` (uint, read-only) -- number of desktops
- `current` (string, read/write) -- ID of current desktop
- `rows` (uint, read/write) -- number of rows in the grid layout
- `navigationWrappingAround` (bool, read/write) -- whether navigation wraps
- `desktops` (array, read-only) -- list of desktop info tuples

**Useful commands:**
```bash
# List all desktops
qdbus org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.desktops

# Get current desktop ID
qdbus org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.current

# Switch to a specific desktop by ID
qdbus org.kde.KWin /VirtualDesktopManager org.freedesktop.DBus.Properties.Set org.kde.KWin.VirtualDesktopManager current "desktop-id-here"

# Get/set number of rows
qdbus org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.rows
```

### 4.2 Alternative: xdotool / wmctrl (X11 only)
```bash
# Switch to desktop N (0-indexed)
xdotool set_desktop N
wmctrl -s N

# Get current desktop
xdotool get_desktop
wmctrl -d
```

These are simpler but less integrated than KWin scripting. They work for external scripts (bash/python) that drive the navigation logic.

---

## 5. The Custom KWin Script Approach (Recommended)

Since no existing tool provides free-form spatial desktop navigation, a custom KWin script is the most practical approach. Here is a conceptual design:

### 5.1 Core Concept

Define a spatial map as a JavaScript object where each desktop has neighbors for each direction:

```javascript
// Example: Desktop 2 has sub-desktops 2.1 (above), 2.3 (below), 2.4 (below-left)
// Layout:
//
//         [2.1]
//   [1] -- [2] -- [3]
//    [2.4] [2.3]
//

var spatialMap = {
    "Desktop 1":   { right: "Desktop 2", left: null, up: null, down: null },
    "Desktop 2":   { right: "Desktop 3", left: "Desktop 1", up: "Desktop 2.1", down: "Desktop 2.3" },
    "Desktop 3":   { right: null, left: "Desktop 2", up: null, down: null },
    "Desktop 2.1": { right: null, left: null, up: null, down: "Desktop 2" },
    "Desktop 2.3": { right: null, left: "Desktop 2.4", up: "Desktop 2", down: null },
    "Desktop 2.4": { right: "Desktop 2.3", left: null, up: "Desktop 1", down: null },
};

function findDesktopByName(name) {
    var all = workspace.desktops;
    for (var i = 0; i < all.length; i++) {
        if (all[i].name === name) return all[i];
    }
    return null;
}

function navigateSpatial(direction) {
    var current = workspace.currentDesktop;
    var mapping = spatialMap[current.name];
    if (mapping && mapping[direction]) {
        var target = findDesktopByName(mapping[direction]);
        if (target) {
            workspace.currentDesktop = target;
        }
    }
}

registerShortcut("SpatialNavUp", "Spatial: Go Up", "Meta+Alt+Up", function() { navigateSpatial("up"); });
registerShortcut("SpatialNavDown", "Spatial: Go Down", "Meta+Alt+Down", function() { navigateSpatial("down"); });
registerShortcut("SpatialNavLeft", "Spatial: Go Left", "Meta+Alt+Left", function() { navigateSpatial("left"); });
registerShortcut("SpatialNavRight", "Spatial: Go Right", "Meta+Alt+Right", function() { navigateSpatial("right"); });
```

### 5.2 Enhancements
- Make the spatial map configurable via `contents/config/main.xml` + `readConfig()`
- Add "move window to spatial neighbor" shortcuts
- Add wrap-around behavior (optional)
- Add visual feedback via notifications

### 5.3 Companion Visual Widget
A custom Plasma QML widget could visualize the non-rectangular layout as a mini-map on the panel, highlighting the current desktop position. This would be a separate Plasma applet project.

---

## 6. KDE Activities as a Hierarchical Mechanism

### 6.1 How Activities Work
- **URL**: https://blogs.kde.org/2026/01/17/streamline-plasma-with-activities-to-be-more-focused-and-productive/
- Activities are "higher in hierarchy" than virtual desktops. An Activity CONTAINS virtual desktops.
- Each Activity has its own set of windows, widgets, wallpapers, and linked files.
- Virtual desktops are shared across Activities (same count), but windows are Activity-specific.

### 6.2 Relevance to Sub-Desktop Concept
Activities could serve as the "parent" layer:
- Activity "Project A" contains desktops 1, 2, 3
- Activity "Project B" contains desktops 1, 2, 3
- Switching Activity = switching the "parent desktop group"

However, Activities are NOT spatially arranged. There is no directional navigation between Activities. They are switched by name/list, not by direction. This makes them unsuitable as the primary mechanism for spatial sub-desktop navigation.

### 6.3 Potential Hybrid Approach
- Use Activities for truly separate workflows (e.g., "Work" vs "Personal")
- Use the custom KWin spatial script for sub-desktop navigation WITHIN an activity
- This gives two levels of hierarchy: Activity (coarse) > Spatial Desktop Map (fine)

---

## 7. Other Desktop Environments / Window Managers for Reference

### 7.1 AwesomeWM -- TagGrid (Closest Concept)
- **URL**: https://github.com/AfoHT/taggrid
- **Grid Navigation Gist**: https://gist.github.com/raksooo/f42b26924e05d32186124aa89de9f9f4
- **What it does**: Arranges tags (workspaces) in an m x n grid with directional navigation. Uses modular arithmetic for wrap-around.
- **Relevance**: **HIGH conceptually** -- the Lua grid navigation code is a clean reference implementation that can be adapted to KWin JavaScript. However, it is still grid-based, not free-form.
- **Maintained**: taggrid last updated Feb 2020 (dormant).

**Key code pattern from the AwesomeWM gist:**
```lua
function grid(direction)
  local rows, columns = 3, 3
  local i = awful.tag.getidx() - 1
  action = {
    ["down"]  = (i + columns) % (rows * columns) + 1,
    ["up"]    = (i - columns) % (rows * columns) + 1,
    ["left"]  = (math.ceil((i+1)/columns)-1)*columns + ((i-1) % columns) + 1,
    ["right"] = (math.ceil((i+1)/columns)-1)*columns + ((i+1) % columns) + 1,
  }
  local tag = awful.tag.gettags(mouse.screen)[action[direction]]
  if tag then awful.tag.viewonly(tag) end
end
```

### 7.2 Hyprland
- **URL**: https://wiki.hypr.land/Configuring/Dispatchers/
- **Plugins**: https://github.com/zakk4223/hyprWorkspaceLayouts
- **What it does**: Wayland compositor with per-workspace layouts, dispatcher system for workspace switching.
- **Relevance**: MEDIUM -- supports custom workspace navigation via dispatchers, but tied to Hyprland (not KDE).
- **Maintained**: YES, actively developed.
- **Notable**: hyprscrolling plugin implements column-based spatial navigation. hyprexpo provides workspace overview grid.

### 7.3 Niri
- **URL**: https://github.com/niri-wm/niri
- **What it does**: Scrollable-tiling Wayland compositor. Windows on infinite horizontal strip; workspaces arranged vertically.
- **Relevance**: LOW-MEDIUM -- novel spatial paradigm (infinite scrolling), but fundamentally different from the requested free-form 2D layout.
- **Maintained**: YES, actively developed.

### 7.4 i3 / Sway
- **URL**: https://i3wm.org / https://swaywm.org
- **Relevance**: LOW for the specific request. Workspaces are numbered and switched by number, not spatially. No native 2D spatial navigation between workspaces.
- **KDE integration**: i3 can replace KWin on X11 (see https://github.com/heckelson/i3-and-kde-plasma). Not available on Wayland.

### 7.5 Herbstluftwm / BSPWM
- **URL**: https://herbstluftwm.org / https://github.com/baskerville/bspwm
- **Relevance**: LOW for desktop navigation. Their binary tree layouts apply to WINDOW tiling within a workspace, not to workspace topology.
- **Interesting concept**: herbstluftwm's frame tree uses spatial focus navigation (left/down/up/right) within the binary tree of windows. This concept of "navigate to the nearest frame in direction X" could inspire spatial desktop navigation.

### 7.6 Compiz (Historical)
- **URL**: http://wiki.compiz.org/Plugins/Wall
- **What it does**: Desktop Wall plugin arranges viewports in a 2D grid with horizontal+vertical navigation. Expo plugin shows all viewports. Viewport Switcher allows number-based switching.
- **Relevance**: MEDIUM historically -- Compiz's viewport model was a true 2D grid. However, still rectangular, not free-form. And Compiz is effectively dead.

### 7.7 GNOME -- Workspace Matrix Extension
- **URL**: https://github.com/mzur/gnome-shell-wsmatrix
- **GNOME Extensions**: https://extensions.gnome.org/extension/1485/workspace-matrix/
- **What it does**: Arranges GNOME workspaces in a 2D grid with thumbnails.
- **Relevance**: MEDIUM conceptually -- proves that a desktop environment extension can transform linear workspaces into a 2D grid. But still rectangular.

### 7.8 Cinnamon -- 2D Workspace Grid
- **URL**: https://cinnamon-spices.linuxmint.com/applets/view/116
- **What it does**: Native-ish 2D workspace grid with up/down/left/right navigation.
- **Relevance**: LOW -- built into Cinnamon, not portable to KDE.

---

## 8. Tiling Scripts (Related but Different Problem)

These focus on window tiling within a desktop, not desktop-to-desktop navigation:

### 8.1 Bismuth (ARCHIVED)
- **URL**: https://github.com/Bismuth-Forge/bismuth
- **Status**: Archived June 2024. Last release v3.1.4 (Sep 2022).
- **Compatibility**: Plasma 5.20+. Does NOT support Plasma 6.
- **Relevance**: LOW -- window tiling, not desktop navigation.

### 8.2 Polonium
- **URL**: https://github.com/zeroxoneafour/polonium
- **Status**: Active, successor to Bismuth for Plasma 6.
- **Relevance**: LOW -- window tiling, not desktop navigation.

### 8.3 Krohnkite
- **URL**: https://github.com/esjeon/krohnkite
- **Status**: Active.
- **Relevance**: LOW -- dynamic tiling within desktops.

---

## 9. KDE Design Proposals and Bug Reports

### 9.1 T13582 -- Virtual Desktops Workflow Reimagined
- **URL**: https://phabricator.kde.org/T13582
- **Summary**: Proposal to improve default VD shortcuts (Meta+Ctrl+Arrows for navigation). Suggests 4 default desktops, better grid layout. Config-only changes, no code needed.
- **Status**: Open, no implementation follow-up.
- **Relevance**: LOW -- focuses on defaults/UX, not custom spatial topology.

### 9.2 Bug 475077 -- Non-Linear Desktop Arrangements
- **URL**: https://bugs.kde.org/show_bug.cgi?id=475077
- **Summary**: Desktop Bar disappeared in Overview for non-linear (multi-row) layouts in Plasma 6.0. Fixed in Plasma 6.2.
- **Relevance**: MEDIUM -- confirms KDE supports multi-row grid layouts and the developers consider non-linear arrangements a valid use case.

### 9.3 T10573 -- Virtual Desktops & Desktop Grid Suggestions
- **URL**: https://phabricator.kde.org/T10573
- **Relevance**: LOW -- general UX suggestions for Kubuntu defaults.

---

## 10. Summary: Feasibility Assessment

| Approach | Feasibility | Effort | Gets You To... |
|----------|-------------|--------|-----------------|
| **Custom KWin script (lookup table)** | **HIGH** | Medium | Exactly what you want: free-form 2D spatial navigation with sub-desktops |
| KWin script + custom QML pager widget | HIGH | High | Full solution with visual mini-map |
| DBus scripting (bash/python + qdbus) | HIGH | Low-Medium | Works but less integrated; needs external shortcut binding |
| KDE standard grid (rows x columns) | HIGH | Low | Rectangular grid only, no sub-desktops |
| Activities + Virtual Desktops hybrid | MEDIUM | Low | Two-level hierarchy but no spatial navigation between activities |
| Replace KWin with i3 on X11 | MEDIUM | High | Full control but lose KDE integration; X11 only |
| Switch to AwesomeWM + taggrid | LOW | Very High | Best native 2D grid support but loses entire KDE desktop |

### Recommended Path

1. **Primary**: Write a custom KWin script that defines a spatial map (neighbor lookup table) and registers 4 directional shortcuts. This is straightforward (~50-100 lines of JavaScript) and uses well-documented APIs.

2. **Enhancement**: Add a configuration file or UI so the spatial map can be edited without modifying code.

3. **Visual**: Optionally create a Plasma QML widget that displays the spatial layout as a mini-map on the panel.

4. **Hierarchy**: Use KDE Activities for the coarse level (e.g., "Work" / "Personal") and the spatial desktop map for fine-grained navigation within each activity.

---

## 11. Key Reference Links

### Official KDE Documentation
- KWin Scripting API: https://develop.kde.org/docs/plasma/kwin/api/
- KWin Scripting Tutorial: https://develop.kde.org/docs/plasma/kwin/
- KWin Scripting API 4.9 (legacy): https://techbase.kde.org/Development/Tutorials/KWin/Scripting/API_4.9
- Using Other WMs with Plasma: https://userbase.kde.org/Tutorials/Using_Other_Window_Managers_with_Plasma

### Most Relevant GitHub Projects
- Swap/Add/Remove Virtual Desktop Shortcuts: https://github.com/clementballot38/virtual_desktop_tools
- Dynamic Workspaces: https://github.com/maurges/dynamic_workspaces
- Cycle Non-Empty Desktops: https://github.com/shaansubbaiah/kwin-cycle-non-empty-desktops
- Move Window & Focus to Desktop: https://github.com/sanderboom/kwin-move-window-and-focus-to-desktop
- Virtual Desktop Bar (Plasma 6): https://github.com/lenonk/virtual-desktop-bar
- AwesomeWM TagGrid: https://github.com/AfoHT/taggrid

### KDE Store
- KWin Scripts category: https://store.kde.org/browse?cat=210
- KWin category: https://store.kde.org/browse/cat/349/order/latest/

### Community Discussions
- KDE Phabricator T13582: https://phabricator.kde.org/T13582
- KDE Bug 475077: https://bugs.kde.org/show_bug.cgi?id=475077
- KDE Discuss - Dynamic Workspaces: https://discuss.kde.org/t/dynamic-workspaces-virtual-desktops/9281
