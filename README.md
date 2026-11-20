# kde_gt — START HERE

Everything that customises the Plasma 5.24 / X11 desktop on **xps** lives in this repo:
a patched KWin (spatial per-activity desktop grid), KWin scripts, the pager plasmoid,
KActivityJumper, and the session plumbing that keeps the patched KWin running.

**Rule: this repo is the only source.** `./deploy.sh` installs it: plain files are symlinks into the
repo; KDE packages (KWin scripts, pager plasmoid) are **copies**, because KPackage ignores symlinked
package dirs (KWin then silently loads its built-in OSD and skips the rest). Edit here, re-run
`./deploy.sh`, commit, push. `./deploy.sh --check` must report no drift.

## Map

| Component | Source here | Live location | Takes effect |
|---|---|---|---|
| Patched KWin (C++) | `kwin/` — submodule, `Cdfghglz/kwin` branch `psw/spatial-mode` on top of `v5.24.7` | `~/.local/kwin-dev/` (install prefix) | `tools/psw build`, then KWin restart |
| Desktop-change OSD (Alt+Space sticky OSD + launcher) | `kwin-scripts/desktopchangeosd` | `~/.local/share/kwin/scripts/` (overrides KWin's built-in) | KWin restart |
| Spatial navigation | `kwin-scripts/spatial-desktop-nav` | same | KWin restart |
| Tiling (Fabian Homborg 2.4.0 + local fixes 1–10) | `kwin-scripts/kwin-script-tiling` | same | KWin restart |
| Other KWin scripts | `kwin-scripts/alwaysopenonfocusedscreen`, `desktopchangeosd-spatial` (disabled) | same | KWin restart |
| Pager plasmoid | `plasmoids/pager` | `~/.local/share/plasma/plasmoids/org.kde.plasma.pager` | plasmashell restart |
| KActivityJumper (Meta+U/I/O/M/,/. jumpTo, Meta+Space jumpBack, Meta+P pin) | `activityjumper/` — submodule, `Cdfghglz/KActivityJumper` branch `fix-lock-release` | `~/.local/bin/ActivityJumper` via `~/.local/share/dbus-1/services/` | kill daemon; next D-Bus call restarts it |
| Session plumbing | `session/bin`, `session/systemd`, `session/plasma-env` | `~/.local/bin`, `~/scripts`, `~/.local/kwin-dev/bin`, `~/.config/systemd/user`, `~/.config/plasma-workspace/env` | see below |
| Login KWin wrapper (root-owned) | `session/system/usr-local-bin-kwin_x11` | `/usr/local/bin/kwin_x11` | `sudo install -m755 …` (deploy.sh prints it) |

Not in this repo: shortcuts (`~/.config/khotkeysrc`, `kglobalshortcutsrc`), `kwinrc`,
spatial maps (`~/.config/spatial-desktop-nav-<activity>.json`), jumper destinations
(`~/.ActivityJumper/jumperDestinationConfig.config`). Those are snapshotted by
`spatial-map-guard` to `/media/20.04/home/cdf/.kde-config-snapshots/`.

## How the session runs

- Login: Plasma starts `/usr/local/bin/kwin_x11` → exec's the patched `~/.local/kwin-dev/bin/kwin_x11`
  with `QT_PLUGIN_PATH` and `QV4_FORCE_INTERPRETER=1`.
- **All other KWin starts go through `session/bin/kwin-dev-start`**: a transient systemd unit
  (`kwin-dev-<ns>.service`, `KillMode=process`), so restarting any script/service never kills the WM.
- `kwin-patched-resume.service` → `kwin-resume-watcher.sh`: on resume, touches
  `$XDG_RUNTIME_DIR/kwin-dev-handover`, replaces KWin via kwin-dev-start, runs `fix_kwin_compositing.sh` 20 s later.
- `kwin-patched-watchdog.timer` (30 s) → `kwin-patched-watchdog.sh`: if no patched KWin, start one
  (skips during a handover); runs `spatial-map-guard backup`.
- `disk-space-alert.timer` (2 min): desktop notification before `/` fills (a full disk zeroes KDE configs).

## Common tasks

```bash
./deploy.sh --check                 # installed == repo?
./deploy.sh                         # (re)link everything

# KWin C++
cmake -S kwin -B build/kwin -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_INSTALL_PREFIX=$HOME/.local/kwin-dev -DBUILD_TESTING=ON   # once
tools/psw build                     # build + install to ~/.local/kwin-dev
#   quick: cmake --build build/kwin --target kwin -j16 && cp build/kwin/bin/libkwin.so.5.24.7 ~/.local/kwin-dev/lib/x86_64-linux-gnu/
tools/psw check                     # running kwin uses the patched libs?

# Restart KWin safely (after C++ or KWin-script changes)
touch $XDG_RUNTIME_DIR/kwin-dev-handover; ~/.local/bin/kwin-dev-start; sleep 25; ~/scripts/fix_kwin_compositing.sh

# ActivityJumper
cmake -S activityjumper -B activityjumper/build -DCMAKE_BUILD_TYPE=Release   # once
make -C activityjumper/build ActivityJumper && pkill -x ActivityJumper        # pins are in memory: re-pin after

# Config snapshots
~/.local/kwin-dev/bin/spatial-map-guard check | list | restore --restart
```

## Where the logs are

| What | Where |
|---|---|
| KWin started by kwin-dev-start / watchdog / resume | `journalctl --user _PID=$(pgrep -x kwin_x11)` |
| KWin after a **KCrash auto-restart** (`--crashes N`), crash backtraces (`=== PSW CRASH BACKTRACE ===`) | `~/.xsession-errors` (not the journal) |
| KWin script `print()` / QML errors | same as KWin |
| OSD self-heal diagnostics | journal, grep `desktopchangeosd:` |
| Tiling script ghost pruning | journal, grep `TILING: pruning ghost tile` |
| Activity-switch desktop rescue (Fix 11) | journal, grep `Activity switch:` |
| ActivityJumper pins | `journalctl --user _PID=$(pgrep -x ActivityJumper)` (`pin: added/removed/consumed`) |
| Jumper key presses + pin/desktop/activity trace | `~/.local/state/aj-pin-monitor.log` (unit `aj-pin-monitor`, started ad hoc) |
| Key presses from khotkeys | `~/.xsession-errors` (`D-Bus call: … ActivityJumper`) |

## Known issues and gotchas (read before changing anything)

- **Never unload/reload a KWin script in a running KWin** (`unloadScript` + `loadDeclarativeScript`):
  old handlers stay connected; KWin crashed on the next Show Desktop (2026-11-08). Edit, then restart KWin.
- **Never `systemctl --user restart` a unit that owns KWin.** KWin must run in its own `kwin-dev-*` unit.
- **Don't add data members to KWin classes and rebuild only libkwin**: plugins built against the old header
  read inline members at wrong offsets. Use file-local statics, or do a full `psw build`.
- **QML JIT crash**: Qt 5.15 JIT SIGSEGVs in `Array.indexOf → strictEqual` (tiling script). Mitigation
  `QV4_FORCE_INTERPRETER=1` everywhere (kwin-dev-start and the login wrapper).
- **Resume race**: replaced KWin crashes on exit (RenderLoop double free), KCrash restarts it, races the new
  one → `OpenGLIsUnsafe=true` → dead compositing / Meta+D. Fix: `fix-kwin-compositing`.
- **Full disk zeroes configs** (Qt 5.15 QSaveFile commits 0-byte files): spatial-map-guard snapshots,
  disk-space-alert, and the kwin saveJson flush fix (`docs/history/e1-savejson-full-disk.patch`).
- **OSD dialogs go 0×0** after monitor hotplug (PlasmaQuick resizes `mainItem` imperatively and breaks the
  bindings); self-heal rebuilds them. Proper fix (Layout.* sizing) not done yet.
- **i915 GPU hangs** in plasmashell (Mesa #11429); `INTEL_DEBUG=reemit` via `session/plasma-env`.
- **Don't run `activityjumper/install`**: it does `sudo make install` over /usr.
- **KDE packages must be real directories**, not symlinks (see Rule above).
- **Desktop grid overlays** (rename pencil, ×, +) are interactive scenes on the first screen only; other
  screens draw copies and clicks there are mapped onto the first-screen overlay (`mapToPrimaryOverlay`).
- **ActivityJumper history inside one activity** (2026-11-20): since the 2025-08 "auto-pin before
  jumping away" change, every jumper key pressed *inside* KOM pinned the spot left, so Meta+Space walked
  back through every KOM desktop visited. (The 2020 build on 20.04 had no auto-pin, hence "it used to
  work".) Now jumps within an activity don't pin and replace the previous destination key of that
  activity, and Meta+Space goes to the most recent entry in *another* activity (pin, lock or key),
  dropping the in-activity entries above it. With nothing outside the activity it steps back as before.
- **`tobiifreed.service` start job hangs**, which keeps `default.target` from ever being reached; never
  order desktop units `After=default.target`.

## History

- `docs/history/tiling-fixes.md` — tiling script fixes 1–10, debug procedure
- `docs/history/fix10-activity-ghosts.diff`, `fix11-activity-switch-desktop.diff`, `e1-savejson-full-disk.patch`
- `docs/history/kwin-patches-pre-fork/` — the patch series before the fork existed
- `docs/legacy/` — the Gas Town-era setup docs (paths there are outdated)
- Archive of the retired Gas Town workspace, `kde_custom_tiling` POC and `kwin_fixes`:
  `/media/20.04/home/cdf/archive/desktop-2026-11-18/`
