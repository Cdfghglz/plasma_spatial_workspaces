# KWin Tiling Script — Window Debugging Procedure

Procedure for diagnosing windows that have broken out of tiling management
in the `kwin-script-tiling` KWin script.

## Quick Reference

The tiling script uses `keepBelow=true` on **managed tiled** windows, and
`keepBelow=false` on **floating/unmanaged** windows. This is counterintuitive:
tiled windows are "below" so that floating windows stay on top.

A window that has "broken" from tiling typically means it lost its `keepBelow`
flag during a desktop switch or move operation, causing it to float above
the other tiled windows and stop receiving geometry updates.

---

## Step 0: Check Script Health First (Most Important)

Before diagnosing individual windows, check if the tiling script itself is
in a healthy state. The script is known to enter a **zombie state** where it
is loaded but its core functions silently fail.

```bash
# Is the script loaded?
qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.isScriptLoaded kwin-script-tiling

# Check for the critical "workspace is not defined" error
grep -c "workspace is not defined" ~/.xsession-errors 2>/dev/null

# Check for timer errors
grep -c "TypeError.*stop.*not a function" ~/.xsession-errors 2>/dev/null

# Check for tile management errors
grep -c "removeTile: No such tile" ~/.xsession-errors 2>/dev/null

# Show recent tiling errors
grep -E "workspace is not defined|removeTile|No rectangle|TypeError.*stop" ~/.xsession-errors 2>/dev/null | tail -20
```

### Interpreting Health Check

**If `workspace is not defined` count is > 0, the script is broken.**

This error means the KWin scripting API's `workspace` global object is
inaccessible inside callback closures. When this happens:

- `Meta+F` (Toggle Floating) **does not work** — it references `workspace.activeClient`
- Desktop switch handling (`_onCurrentDesktopChanged`) silently fails
- Tile geometry updates (`_updateAllTiles`) silently fail
- Focus tracking (`trackFocusChanges`) silently fails
- The geometry-change timer is broken

The script becomes a zombie: loaded, some per-client signal handlers still
fire (they receive the `client` parameter directly), but all functions
that need to query global state via `workspace.*` are dead.

**Root cause:** The script uses `Qt.include()` (deprecated since Qt 5.12)
to load JS files. The `workspace` global is available at initialization
time (during `Component.onCompleted` in main.qml) but becomes undefined
in callback closures later. This is a Qt/QML scoping bug with `Qt.include()`.

**Permanent fix applied (2026-03-20):** Captured `workspace` in a shared
variable `var _ws = workspace;` in `signal.js` (the first included file).
All `workspace.` references replaced with `_ws.` across all JS files.
See `~/ws/kwin_fixes/FIXES.md` for details.

If the error reappears (e.g., after a script update overwrites the patch),
restart KWin to reload:
```bash
# Restart KWin (preserves window positions, clears JS cache)
kwin_x11 --replace &
```

**Important:** The RestartKwinScripts tool does NOT clear the JS engine
cache. A full `kwin_x11 --replace` is required after modifying script files.

---

## Step 1: Gather Environment Info

```bash
# Monitor layout (positions, resolutions)
xrandr --query | grep -E "connected|[0-9]+x[0-9]+"

# Current desktop (0-indexed)
xprop -root _NET_CURRENT_DESKTOP
xprop -root _NET_DESKTOP_NAMES

# KWin version and operation mode
qdbus org.kde.KWin /KWin supportInformation 2>/dev/null | head -20

# Tiling script config
grep -A 20 '\[Script-kwin-script-tiling\]' ~/.config/kwinrc
```

## Step 2: List All Windows on Current Desktop

```bash
# Get current desktop index
DESK=$(xprop -root _NET_CURRENT_DESKTOP | awk '{print $NF}')

# List all windows with geometry, filtering to current desktop
# Columns: window_id, desktop, pid, x, y, w, h, hostname, title
wmctrl -l -G -p | awk -v d="$DESK" '$2 == d || $2 == -1'
```

Key fields from wmctrl output:
- Column 2: desktop index (-1 = sticky/all desktops)
- Columns 4-7: x, y, width, height

## Step 3: Check Window States (Critical Diagnostic Step)

For each window on the current desktop, check `_NET_WM_STATE`:

```bash
DESK=$(xprop -root _NET_CURRENT_DESKTOP | awk '{print $NF}')
xprop -root _NET_CLIENT_LIST_STACKING | tr ',' '\n' | while read wid; do
  wid=$(echo $wid | tr -d ' ')
  desk=$(xprop -id $wid _NET_WM_DESKTOP 2>/dev/null | awk '{print $NF}')
  if [ "$desk" = "$DESK" ] || [ "$desk" = "0xffffffff" ]; then
    name=$(xprop -id $wid WM_NAME 2>/dev/null | sed 's/.*= //' | tr -d '"')
    geo=$(xdotool getwindowgeometry --shell $wid 2>/dev/null \
          | grep -E "X=|Y=|WIDTH=|HEIGHT=" | tr '\n' ' ')
    state=$(xprop -id $wid _NET_WM_STATE 2>/dev/null | sed 's/.*= //')
    echo "$wid | $name | $geo| state=$state"
  fi
done
```

### Interpreting Results

| State | keepBelow? | Position | Meaning |
|-------|-----------|----------|---------|
| `_NET_WM_STATE_BELOW` | Yes | Correct grid | Properly tiled and managed |
| `_NET_WM_STATE_BELOW` | Yes | Wrong pos/size | Managed but layout rectangles corrupted |
| No BELOW state | No | Correct grid | **BROKEN** — lost keepBelow, visually correct but unmanaged |
| No BELOW state | No | Arbitrary | Floating (intentional or broken) |
| `_NET_WM_STATE_MAXIMIZED_*` | No | Full screen | Maximized (script sets keepBelow=false) |

**The "broken" windows are typically those WITHOUT `_NET_WM_STATE_BELOW` but at
positions that match the tiling grid.** They look correct visually but are no
longer managed. They will not respond to layout changes or tile operations.

However, if the script is in the zombie state (Step 0), windows WITH
`_NET_WM_STATE_BELOW` can also be at wrong positions because `_updateAllTiles`
fails silently.

## Step 4: Identify the Tiling Grid

For a given monitor, the tiling script (HalfLayout by default) creates:
- Left half: x=0, width=screen_width/2
- Right half: x=screen_width/2, width=screen_width/2

With multiple windows on the right, they stack vertically, dividing the right
half into equal-height segments.

Compare each window's geometry against the expected grid positions.

## Step 5: Check Stacking Order

```bash
DESK=$(xprop -root _NET_CURRENT_DESKTOP | awk '{print $NF}')
xprop -root _NET_CLIENT_LIST_STACKING | tr ',' '\n' | while read wid; do
  wid=$(echo $wid | tr -d ' ')
  desk=$(xprop -id $wid _NET_WM_DESKTOP 2>/dev/null | awk '{print $NF}')
  if [ "$desk" = "$DESK" ] || [ "$desk" = "0xffffffff" ]; then
    name=$(xprop -id $wid WM_NAME 2>/dev/null | sed 's/.*= //' | tr -d '"')
    state=$(xprop -id $wid _NET_WM_STATE 2>/dev/null | sed 's/.*= //')
    echo "$wid | $name | state=$state"
  fi
done
```

Windows with `keepBelow` are at the bottom of the stacking order.
Windows without it are on top.

## Step 6: Use the Built-in Dump Command

The tiling script has `Meta+Shift+Escape` bound to "TILING: Dump Clients"
which prints the internal tile list. **This also references `workspace`
internally (in `_dumpClients` at tilingmanager.js:1242), so it may fail
in the zombie state.** Check output in:

```bash
# For X11 sessions
tail -50 ~/.xsession-errors

# For systemd-managed sessions
journalctl --user -u plasma-kwin_x11 --since "30 seconds ago" 2>/dev/null
```

---

## Root Causes (Ordered by Likelihood)

### 1. PRIMARY: `workspace` becomes undefined in callbacks (ZOMBIE STATE)

**Evidence:** `~/.xsession-errors` contains `ReferenceError: workspace is not defined`

The script loads JS files via `Qt.include()` (deprecated Qt API). The
`workspace` KWin scripting global is available during initialization
(in `Component.onCompleted`) but becomes undefined inside callback
closures at some point after init. This may be triggered by garbage
collection, QML engine internal state changes, or Plasma component
restarts (plasmashell crash/restart, kwin reconfiguration).

**What breaks:**
- `_onCurrentDesktopChanged` (tilingmanager.js:1040) — accesses
  `workspace.currentDesktop`, silently throws and returns
- `_updateAllTiles` → `resizeScreen` → `util.getTilingArea` (util.js:145)
  — accesses `workspace.clientArea()`
- `trackFocusChanges` (tilelist.js:419) — accesses `workspace.clientList()`
- `Toggle Floating / Meta+F` (tilingmanager.js:243) — accesses
  `workspace.activeClient`
- `timerGeometryChanged` handler (tilelist.js:129) — timer object becomes
  inaccessible, `self.timer.stop()` throws TypeError

**Cascade effect:**
1. Desktop switch fires `currentDesktopChanged` signal
2. `_onCurrentDesktopChanged` tries `workspace.currentDesktop` → throws
3. `deactivate()` on old desktop never runs → keepBelow states are unchanged
4. `activate()` on new desktop never runs → tiles never repositioned
5. Over time, tiles drift as individual signal handlers (which DON'T need
   `workspace`) still fire and partially update state

**Fix applied (2026-03-20):** Captured `workspace` in `var _ws = workspace;`
in `signal.js` (first included file). All `workspace.` refs replaced with
`_ws.` across tile.js, tilelist.js, tiling.js, tilingmanager.js, util.js.
See `~/ws/kwin_fixes/FIXES.md` and `~/ws/kwin_fixes/patches.diff`.

**Qt.include() scoping lessons learned:**
- `var` declarations in the parent file (tilingmanager.js) are NOT visible
  to `Qt.include()`'d files — JS hoisting does not apply across includes
- QML-injected globals (`KWin`, `workspace`) are NOT available at file-load
  time — only inside functions called from QML context
- `var` declarations in included files DO share scope with other included files
- Therefore the capture must be in the first included file (signal.js)

### 2. SECONDARY: `setClientGeometry` early-return guards

When the script IS healthy, windows can still lose tiling through
`setClientGeometry` (tile.js:196-331). This function sets `keepBelow=true`
on line 234, but 8 early-return guards (lines 198-229) run first:

```
if (!client)                                    → null client
if (client.tiling_resize)                       → script already resizing
if (client.fullScreen)                          → fullscreen mode
if (this.maximized)                             → STICKY: tile maximized flag
if (!this.hasClient(client))                    → wrong tile reference
if (client.deleted)                             → window being destroyed
if (client.move || !client.resizeable || ...)   → mid move/resize
if (this._moving || this._resizing || !this.active) → tile flags stuck
```

During `deactivate()`→`activate()` (desktop switch), if any guard fires,
`keepBelow` remains `false` (set during deactivate) and is never restored.

**Fix applied (2026-03-20):** `activate()` in tiling.js now explicitly
restores `keepBelow=true` for all non-maximized tiles before calling
`_updateAllTiles()`, bypassing the early-return guards.

### 3. TERTIARY: Move interrupted by desktop switch

1. User starts moving window → `keepBelow=false` (tile.js:380)
2. Desktop switches → `tile.active=false` (tiling.js:187)
3. Move finishes → `onClientFinishUserMovedResized` checks
   `if (!this.active) return;` (tile.js:378) → skips `keepBelow=true`
4. `keepBelow` permanently stuck at false

**Fix applied (2026-03-20):** `onClientFinishUserMovedResized` no longer
early-returns. Always clears `_moving`/`_resizing` flags and sets
`client.keepBelow = this.active`.

---

## Quick Fixes

### If script is in zombie state (workspace undefined):
```bash
# Restart KWin (preserves windows)
kwin_x11 --replace &
```

### If script is healthy but a window broke:
- `Meta+F` twice on the broken window (toggle float off/on)
- Or `Meta+Shift+F11` twice (toggle tiling for entire desktop)

### Neither works:
Manually set keepBelow and position via xdotool/wmctrl:
```bash
# Find the broken window
WID=$(xdotool selectwindow)

# Set keepBelow
xdotool windowactivate $WID
wmctrl -i -r $WID -b add,below

# Move to a tiling position (example: right-top of 2560x1440 screen)
wmctrl -i -r $WID -e 0,1281,0,1279,479
```

---

## Script Source Location

```
~/.local/share/kwin/scripts/kwin-script-tiling/contents/code/
  tile.js          - Individual tile management, keepBelow logic
  tilelist.js      - Global tile tracking, signal connections
  tiling.js        - Per-screen layout, activate/deactivate
  tilingmanager.js - Top-level manager, keyboard shortcuts, desktop switching
  ignored.js       - Ignored window list and float rules
  halflayout.js    - Default half-screen layout algorithm
  util.js          - Geometry utilities, getTilingArea (uses workspace.clientArea)
```

## Key Code Paths

```
Desktop switch:
  workspace.currentDesktopChanged signal
  → TilingManager._onCurrentDesktopChanged (tilingmanager.js:1039)
    → old layouts .deactivate() → sets keepBelow=false, tile.active=false
    → new layouts .activate() → _updateAllTiles() → setClientGeometry() → keepBelow=true

Window added:
  workspace.clientAdded signal
  → TileList.addClient (tilelist.js:246) → _addTile() → tileAdded signal
  → TilingManager._onTileAdded (tilingmanager.js:721) → layout.addTile()
  → _updateAllTiles()

Toggle Floating (Meta+F):
  workspace.activeClient → check tiling_floating
  → if floating: set false, TileList.addClient()
  → if tiled: set true, TileList.untileClient() → keepBelow=false, removeClient()

User move/resize:
  clientStartUserMovedResized → keepBelow=false (tile.js:380)
  clientStepUserMovedResized → emit moving/resizing step signals
  clientFinishUserMovedResized → keepBelow=true (tile.js:415) [ONLY if tile.active]
  → TilingManager._onTileMovingEnded → swap or transfer tile
```
