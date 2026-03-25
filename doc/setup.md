# Plasma Spatial Workspaces — Setup & Operations

## Directory Layout

```
~/ws/kde_gt/                          # Gas Town HQ
├── CLAUDE.md                         # Agent instructions
├── doc/
│   ├── plan.md                       # Feature specification
│   └── setup.md                      # This file
├── mayor/                            # Mayor agent workspace
│   └── rig -> plasma_spatial_workspaces/mayor/rig/
├── plasma_spatial_workspaces/        # Project directory
│   └── mayor/rig/                    # PSW rig (own git repo)
│       ├── src/                      # Patched KWin C++ source
│       ├── patches/                  # Patch files against stock KWin 5.24
│       ├── plasma_spatial_workspaces/scripts/  # KWin scripts (spatial-desktop-nav)
│       └── dev/psw                   # Build/deploy tool
└── config.yaml
```

## Build Pipeline

### Source and Build Paths

| What | Path |
|------|------|
| Rig source | `~/ws/kde_gt/plasma_spatial_workspaces/mayor/rig/src/` |
| Build tree source | `~/ws/kwin-dev/kwin/src/` |
| Build directory | `~/ws/kwin-dev/build/` |
| Install prefix | `~/.local/kwin-dev/` |
| Patched binary | `~/.local/kwin-dev/bin/kwin_x11` |
| Patched plugins | `~/.local/kwin-dev/lib/x86_64-linux-gnu/plugins/` |
| Deployed QML scripts | `~/.local/share/kwin/scripts/` |

### Build & Deploy

```bash
# 1. Copy rig source to build tree (psw build does this internally)
cp mayor/rig/src/virtualdesktops.cpp ~/ws/kwin-dev/kwin/src/

# 2. Build and install
psw build        # cmake --build + install to ~/.local/kwin-dev/

# 3. Deploy (restarts kwin + plasmashell with correct QT_PLUGIN_PATH)
psw deploy       # Syncs QML, kills kwin/plasmashell, restarts with patched binary

# 4. Verify
psw check        # Confirms patched .so loaded, no system lib conflicts
```

### Critical: QT_PLUGIN_PATH

The patched KWin adds virtual methods to `EffectsHandler`. If the system
`KWinX11Platform.so` is loaded instead of the patched one, you get a
**vtable mismatch SIGSEGV**. Always set:

```bash
export QT_PLUGIN_PATH=~/.local/kwin-dev/lib/x86_64-linux-gnu/plugins:$QT_PLUGIN_PATH
```

The `restart_plasma_kwin.sh` script and `psw deploy` handle this automatically.

## Persistence & Recovery

### Watchdog Timer (systemd user)

A timer checks every 30 seconds that the patched kwin is running. If the
session falls back to system kwin (after a crash, sleep/wake, etc.), the
watchdog replaces it:

```
~/.config/systemd/user/kwin-patched-watchdog.timer    # 30s polling
~/.config/systemd/user/kwin-patched-watchdog.service   # Replace if system kwin
~/.config/systemd/user/kwin-patched-resume.service     # Post-sleep recovery
```

```bash
# Check status
systemctl --user status kwin-patched-watchdog.timer

# Manual trigger
systemctl --user start kwin-patched-watchdog.service
```

### Crash Recovery

If kwin crashes:
1. KWin auto-restarts with `--crashes N` flag
2. If crash protection blocks compositing (`OpenGLIsUnsafe=true`), reset:
   ```bash
   kwriteconfig5 --file kwinrc --group Compositing --key OpenGLIsUnsafe false
   qdbus org.kde.KWin /KWin reconfigure
   ```
3. The watchdog timer ensures patched kwin replaces system kwin within 30s

### Manual Recovery

```bash
# Full redeploy
psw deploy

# Quick restart (no rebuild)
~/scripts/restart_plasma_kwin.sh

# Just replace system kwin with patched
QT_PLUGIN_PATH=~/.local/kwin-dev/lib/x86_64-linux-gnu/plugins \
  ~/.local/kwin-dev/bin/kwin_x11 --replace &
```

## Spatial Map Configuration

### File Locations

| File | Purpose |
|------|---------|
| `~/.config/spatial-desktop-nav.json` | Default spatial map (all 10 desktops) |
| `~/.config/spatial-desktop-nav-<activity-uuid>.json` | Per-activity override |
| `~/.config/kwinrc` `[Desktops]` | `SpatialMode=true` enables spatial navigation |

### Map Format

```json
{
    "<desktop-uuid>": {
        "above": "<neighbor-uuid or empty>",
        "below": "<neighbor-uuid or empty>",
        "left": "<neighbor-uuid or empty>",
        "right": "<neighbor-uuid or empty>"
    },
    "removed": ["<tombstoned-desktop-uuid>"]
}
```

The `"removed"` array tombstones desktops excluded from this activity's
layout. Tombstoned desktops are skipped in grid building and navigation.

### Desktop UUID Mapping

Query live:
```bash
qdbus --literal org.kde.KWin /VirtualDesktopManager \
  org.kde.KWin.VirtualDesktopManager.desktops
```

### Grid Diagnostics

```bash
# Per-activity grid (what effects/OSD use)
qdbus org.kde.KWin /VirtualDesktopManager \
  org.kde.KWin.VirtualDesktopManager.spatialGridColumns
qdbus org.kde.KWin /VirtualDesktopManager \
  org.kde.KWin.VirtualDesktopManager.spatialGridRows
qdbus org.kde.KWin /VirtualDesktopManager \
  org.kde.KWin.VirtualDesktopManager.spatialGridLayout

# X11 property (MAX across all activities, used by pager)
xprop -root _NET_DESKTOP_LAYOUT
```

## Patched Components

### C++ (compiled into libkwin.so)

- **`virtualdesktops.cpp/h`** — `VirtualDesktopSpatialMap`, `VirtualDesktopGrid::updateFromSpatialMap()`, per-activity maps, tombstones, `_NET_DESKTOP_LAYOUT`
- **`effects.cpp/h`** — `EffectsHandlerImpl::isSpatialMode()`, `desktopGridCoords()`, `desktopExists()`, `desktopAboutToBeRemoved()`
- **`desktopgrid.cpp`** — Desktop grid effect with spatial layout, tile overlays, inline rename
- **`workspace_wrapper.cpp/h`** — QML API: `spatialMode`, `spatialNeighborUp/Down/Left/Right()`, `desktopGridWidth/Height`

### QML (hot-deployable via psw deploy)

- **`desktopchangeosd/osd.qml`** — Desktop change OSD popup with spatial grid BFS
- **`spatial-desktop-nav/spatial.js`** — Per-activity desktop names, spatial navigation script
- **`desktopgrid/tile_overlay.qml`** — Desktop grid tile overlay with rename UI

### Patches (in `rig/patches/`)

- `b2-effects-handler-spatial.patch` — Adds spatial mode API to effects handler interface

## Known Issues

- **Screen disconnect crash** (hq-55z, P1): Disconnecting second monitor crashes patched kwin
- **Multi-rename in single grid session** (hq-tp5n, P2): Only one rename persists per desktop grid session
- **Tombstone loss on system kwin fallback**: When system kwin runs, it overwrites spatial maps without tombstone data
