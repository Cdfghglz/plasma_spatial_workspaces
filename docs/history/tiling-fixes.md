> Moved from ~/ws/kwin_fixes (2026-11-18). Paths below that mention ~/ws/kwin_fixes or editing
> ~/.local/share/kwin/scripts directly are historical: the code now lives in kwin-scripts/kwin-script-tiling
> and is deployed as a symlink by deploy.sh.

# kwin-script-tiling Local Fixes

Applied 2026-03-20 to `~/.local/share/kwin/scripts/kwin-script-tiling/contents/code/`

Originals backed up to `~/ws/kwin_fixes/original_code/`
Full diff: `~/ws/kwin_fixes/patches.diff`

## Bug 1: `workspace` undefined in callbacks (CRITICAL)

**Symptom:** `ReferenceError: workspace is not defined` (3164 occurrences).
Script enters zombie state where Meta+F, desktop switching, tile updates,
and focus tracking all silently fail.

**Root cause:** `Qt.include()` scoping in Qt 5.15 causes the `workspace`
KWin global to become undefined inside callback closures after initialization.

**Fix:** Capture `workspace` in a shared variable `var _ws = workspace;`
declared in `signal.js` (the first `Qt.include()`'d file). Variables declared
in included files are visible to all other included files via `Qt.include()`
shared scope. Then replace all `workspace.` references with `_ws.` across
all JS files.

**Key insight:** `Qt.include()` scoping does NOT make parent-file `var`
declarations visible to included files, and QML-injected globals like `KWin`
are not available at file-load time (only inside functions called from QML
context). However, `var` declarations in included files DO share scope with
other included files. So the capture must happen inside an included file
(signal.js), not in the parent (tilingmanager.js).

**Important:** The RestartKwinScripts tool does not clear KWin's JS engine
cache. A full `kwin_x11 --replace` is required after modifying script files.

**Files changed:**
- `signal.js` — added `var _ws = workspace;` at the top (captures at include time)
- `tilingmanager.js` — replaced ~60 `workspace.` references with `_ws.`
- `tilelist.js` — replaced 2 references, added timer null-guards at line 129/131
- `tile.js` — replaced 1 reference (line 455)
- `util.js` — replaced 4 references (getTilingArea, nextScreenInDirection)

## Bug 2: `keepBelow` not restored during `activate()`

**Symptom:** After switching desktops away and back, some windows lose
`keepBelow=true` and float above the tiled layout. They look correct
positionally but are no longer managed.

**Root cause:** `deactivate()` sets `keepBelow=false` for all tiles.
`activate()` relies on `setClientGeometry()` to restore it, but that
function has 8 early-return guards (tile.js:198-229) that can skip the
`keepBelow=true` assignment on line 234.

**Fix:** In `tiling.js` `activate()`, explicitly restore `keepBelow=true`
and `tile.active=true` for all non-maximized tiles before calling
`_updateAllTiles()`.

**File changed:** `tiling.js` lines 174-180

## Bug 3: Move interrupted by desktop switch leaves `keepBelow=false`

**Symptom:** If a desktop switch happens while a window is being moved,
`keepBelow` is permanently stuck at `false` and `_moving`/`_resizing`
flags remain `true`.

**Root cause:** `onClientFinishUserMovedResized` (tile.js:404) had
`if (!this.active) return;` as the first line, which skipped both
flag cleanup and `keepBelow` restoration when the tile was deactivated
during the move.

**Fix:** Removed the early return. Now always clears `_moving`/`_resizing`
flags and sets `client.keepBelow = this.active` (true if tile active,
false if inactive — consistent with `deactivate()` semantics). Signal
emissions are still guarded by `this.active`.

**File changed:** `tile.js` lines 404-418

## Bug 4: Typo in bogus client check

**Symptom:** `setClientGeometry` incorrectly skips clients where
`minSize.height == maxSize.width` (cross-dimension comparison) instead
of detecting fixed-size clients where `minSize.height == maxSize.height`.

**Root cause:** Typo — `client.maxSize.width` appeared twice on line 228.

**Fix:** Changed second `client.maxSize.width` to `client.maxSize.height`.

**File changed:** `tile.js` line 228

## Bug 5: QML timer corrupted after hibernate/wake

**Symptom:** After hibernating and waking the machine, geometry updates stop
working. `~/.xsession-errors` shows:
`TypeError: Property 'stop' of object TypeError: Type error is not a function`
at `tilelist.js:129`.

**Root cause:** `timerGeometryChanged` is a QML `Timer` object defined in
`main.qml` and passed into `TileList` as `self.timer`. After hibernation,
the QML engine invalidates the object. The JS reference is still truthy so
`if (self.timer)` passes, but calling `.stop()` or `.start()` throws TypeError.

**Fix:** Guard calls with `typeof self.timer.stop === 'function'` before
invoking. When the timer is broken, fall back to calling `self.updateGeometry()`
immediately (no debouncing, but correct).

**File changed:** `tilelist.js` lines 129-133

## Bug 6: Ghost tile / empty window slot after hibernate/wake

**Symptom:** After hibernating and waking, one tiling slot is empty (a window
slot exists in the layout but no window occupies it). `~/.xsession-errors`
shows many `removeTile: No such tile` messages.

**Root cause:** During KWin session restore, `windowClosed` fires for windows
being torn down and recreated. The removal chain calls
`TilingManager._onTileRemoved(tile)`, which routes to `Tiling.removeTile` by
looking up `tile.getDesktop()` / `tile.getScreen()`. These values are stale
after session restore — KWin may have already reassigned the client — so the
wrong `Tiling` instance is targeted. The tile stays in the correct `Tiling`
with a now-deleted client. On the next `_updateAllTiles()` the ghost tile
takes up a layout slot but `setClientGeometry` early-returns for the deleted
client, leaving a permanent empty slot.

**Fix:** Applied the same stale-routing guard to all four `removeTile` call
sites in `tilingmanager.js`:

1. `_onTileRemoved` (line 808) — search all layouts instead of routing by
   `tile.getDesktop()`/`tile.getScreen()`.
2. `_changeTileLayouts` (line 1015) — called by `_onTileDesktopChanged`,
   `_onTileScreenChanged`, `_onTileActivitiesChanged`. Check `indexOf` before
   removal; if tile not found in any `oldLayout`, fall back to searching all
   layouts.
3. `_onTileMovingEnded` (line 981) — guard `startLayout.removeTile` with
   `indexOf` check; fall back to all-layout search.

In all cases: pre-check `layout.tiles.indexOf(tile) > -1` before calling
`removeTile`, so only the layout that actually contains the tile is affected.
If the tile isn't in the expected layout, search all layouts.

**File changed:** `tilingmanager.js` lines 808-819, 840, 989-992, 1015-1042

## Bug 8: `KWin` global becomes undefined after hibernate/wake

**Symptom:** After hibernation, `~/.xsession-errors` shows
`ReferenceError: KWin is not defined` at `tilingmanager.js:0` and
`ignored.js:65`. Also `TypeError: Cannot call method 'setKeepBelow' of undefined`
at `tiling.js:216`.

**Root cause:** Same as Bug 1 — `Qt.include()` scoping causes QML-injected
globals to become undefined in callback closures. Bug 1 fixed `workspace` by
capturing it as `var _ws = workspace;` in `signal.js`. But `KWin` (used for
`readConfig`, `MaximizeArea`, `PlacementMaximizing`, etc.) was not captured.

After hibernate, `KWin` becomes undefined, causing:
- `ignored.isIgnored()` fails → windows that should float get tiled
- `readConfig()` calls fail → config defaults (floaters, gaps, placement) wrong
- `clientArea()` calls via `KWin.MaximizeArea` fail → geometry calculations break
- `setKeepBelow` on undefined tile objects

**Fix:** Captured `KWin` as `var _kwin = KWin;` in `signal.js` alongside `_ws`.
Replaced all 64 `KWin.` references with `_kwin.` across 8 JS files.

**Files changed:** `signal.js`, `tilingmanager.js` (34), `tile.js` (11),
`tiling.js` (8), `util.js` (7), `tilelist.js` (1), `halflayout.js` (1),
`layout.js` (1), `ignored.js` (1)

## Bug 9: Tiling breaks when external monitor disappears after hibernate

**Symptom:** After hibernate/wake, if KWin loses the external monitor
(xrandr shows only eDP-1), the tiling script logs
`requesting unexisting screen -1` and windows lose keepBelow / get wrong
geometry. Tiles that cached `_currentScreen = 1` (the now-gone monitor)
fail all geometry calculations.

**Root cause:** `util.getTilingArea(screen, desktop)` calls
`_ws.clientArea(PlacementArea, screen, desktop)` without validating
`screen`. When a monitor disappears, tiles still reference the old screen
index, which is now out of range.

**Fix:** Two guards:
1. `util.getTilingArea` (util.js) — clamp `screen` to `[0, numScreens)`
   before calling `clientArea`. Windows on a vanished screen get tiled on
   screen 0 instead of failing.
2. `tile.js setClientGeometry` — clamp `client.screen` to valid range
   before storing in `_currentScreen`.

**Files changed:** `util.js` line 144, `tile.js` line 235

## Bug 7: Tile positions not preserved across KWin restarts

**Symptom:** After `kwin_x11 --replace`, windows land in random tile positions
(e.g., the master window ends up as a slave). The window that was the left-column
main tile may land in the right column.

**Root cause:** Tile positions are tracked via `client.tiling_tileIndex` — a JS
property on the KWin client object. This is NOT persisted as an X window property,
so it's lost on KWin restart. With `placement=2` ("Start at the end") in the
config, tiles without a saved `tileIndex` are appended to the layout in whatever
order `clientAdded` fires, which is non-deterministic.

**Fix:** In `Tiling.addTile` (tiling.js), when no `tileIndex` is saved and no
x/y position is provided, insert tiles sorted by geometry rather than appending
blindly. Sort by X (left before right = master before slave), then by Y (top
before bottom for same-column slaves). Window geometry is preserved across KWin
restarts by the X server, so the pre-restart positions are still available.

This only activates when `tileIndex == -1` (no saved index). During normal
operation, the saved `tileIndex` from `syncCustomProperties` is used as before.

**File changed:** `tiling.js` lines 110-115

## Testing

After applying fixes, reload the script:
```bash
# Option 1: Use RestartKwinScripts app
# Option 2: Manual reload
qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript kwin-script-tiling
qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript \
    ~/.local/share/kwin/scripts/kwin-script-tiling/contents/ui/main.qml \
    kwin-script-tiling
qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.start
# Option 3: Restart KWin
kwin_x11 --replace &
```

Verify:
```bash
# Should show 0 new "workspace is not defined" errors
tail -50 ~/.xsession-errors | grep -c "workspace is not defined"

# Check windows are tiled (BELOW state present)
DESK=$(xprop -root _NET_CURRENT_DESKTOP | awk '{print $NF}')
xprop -root _NET_CLIENT_LIST_STACKING | tr ',' '\n' | while read wid; do
  wid=$(echo $wid | tr -d ' ')
  desk=$(xprop -id $wid _NET_WM_DESKTOP 2>/dev/null | awk '{print $NF}')
  if [ "$desk" = "$DESK" ]; then
    name=$(xprop -id $wid WM_NAME 2>/dev/null | sed 's/.*= //' | tr -d '"')
    state=$(xprop -id $wid _NET_WM_STATE 2>/dev/null | sed 's/.*= //')
    echo "$wid | $name | state=$state"
  fi
done
```

To revert:
```bash
cp ~/ws/kwin_fixes/original_code/*.js \
   ~/.local/share/kwin/scripts/kwin-script-tiling/contents/code/
```

## Fix 10 (2026-10-19): ghost tiles from other activities (holes / shifted tiles)

**Symptom:** on some desktop/screen the tiled layout has empty slots and the
remaining windows are squeezed or shifted (seen on PB / desktop 8 "Twister",
lower screen eDP-1). The empty slots are exactly where windows of *other*
activities (KOM, COD) on the same desktop+screen sit.

**Cause:** `_onTileActivitiesChanged` only collected "old" layouts at the tile's
*current* desktop/screen. After a resume restart (kwin is replaced on every
resume) a tile's desktop/screen can be stale, and windows are tiled at startup
before KWin knows their activities, so they first land in the current activity's
layout. When the activity later became known, the copy in the previous
activity's layout wasn't found, and `_changeTileLayouts`' all-layouts fallback
was skipped because the new layout counted as "found" (`removedFromAny = true`).
The tile then stayed in the wrong activity's layout forever, as an invisible tile.

**Fix (tilingmanager.js):**
- `_onTileActivitiesChanged` searches every layout (all activities, desktops,
  screens) for the tile.
- New `_pruneForeignTiles(activity, desktop)`: removes tiles whose window's live
  `client.activities` excludes that activity (empty = all activities, kept), and
  resyncs the tile's cached list. Runs on every activity switch (all desktops)
  and every desktop switch (that desktop only), so any ghost from any path heals
  the next time you switch away and back. Logs `TILING: pruning ghost tile ...`.

Diff: `fix10-activity-ghosts.diff`. Backup of all files before the change:
`backup-2026-10-19-activity-ghosts/` (also `~/.local/share/kwin/scripts-backup/tiling-activity-ghosts-2026-10-19/`).

**Debug aid:** the script has a "TILING: Dump Clients" action, which prints every
activity/desktop/screen layout to the journal:
`qdbus org.kde.kglobalaccel /component/kwin org.kde.kglobalaccel.Component.invokeShortcut "TILING: Dump Clients"`.

**Reload gotcha (confirmed again):** `unloadScript` + `loadDeclarativeScript`
from a fresh path loads the new code, but the old instance's signal handlers stay
connected and keep firing (TypeError `readConfig is not a function` from the old
path). A full kwin restart is required: `touch $XDG_RUNTIME_DIR/kwin-dev-handover;
~/.local/bin/kwin-dev-start`, then `~/scripts/fix_kwin_compositing.sh` after ~20 s.

## 2026-10-28: KWin SIGSEGVs in tiling-script JS (follow-up to Fix 10)

Six in-use crashes 10-19..10-28, all in Qt 5.15 JIT code calling `Array.prototype.indexOf` ->
`QV4::RuntimeHelpers::strictEqual` on a corrupt array element. Only this script uses Array.indexOf.
They began ~10 h after Fix 10, which added all-layout `indexOf` scans on every desktop/activity switch.
Mitigation/test: `QV4_FORCE_INTERPRETER=1` (JS JIT off) in `~/.local/bin/kwin-dev-start`.
If crashes persist with the JIT off, reduce Fix 10's `_pruneForeignTiles` to activity switches only, or drop it.
Crash logs after a KCrash restart go to `~/.xsession-errors`, not the journal.

## Fix 11 (2026-11-18): activity switch leaves you on a desktop outside the new activity's layout

Symptom: switching activity from a desktop that only exists in the current activity (e.g. PB desktop 21 -> KOM)
kept desktop 21 current in KOM: outside KOM's spatial map, so no neighbours, no OSD highlight, no windows.
Cause: the patched VirtualDesktopManager rebuilt the grid on Activities::currentChanged but never touched
the current desktop. Fix (kwin C++, ~/ws/kwin-dev/kwin/src/virtualdesktops.cpp): remember the current desktop
per activity (file-local static, so the class layout/ABI is unchanged); on a switch, if the current desktop is
not in the new activity's map, go (deferred, one event-loop tick) to that activity's last desktop, else its
first mapped desktop. Logs `Activity switch: desktop N not in activity map, moving to M`.
Diff: fix11-activity-switch-desktop.diff. Pre-fix sources + libkwin: kwin-src-backup-2026-11-18/.
Not yet ported to the mayor/rig repo (same as e1-savejson patch).
