# KWin Tiling Landscape — Plasma 5 vs Plasma 6

## Current Setup

- KWin 5.24.7, Qt 5.15.3, X11 mode
- Script: `kwin-script-tiling` v2.4.0 by Fabian Homborg (faho)
- Repo: https://github.com/faho/kwin-tiling — **archived Nov 2022, unmaintained**
- KDE Store: https://store.kde.org/p/1112554 — last updated Sep 2019

## Plasma 5 Tiling Options

| Project | Status | Style | Notes |
|---------|--------|-------|-------|
| **kwin-script-tiling** (faho) | Archived Nov 2022 | Half/Spiral/Blade/i3 layouts | The one we're running. Has `Qt.include()` scoping bugs on Qt 5.15. No upstream to send fixes to. |
| **Krohnkite** (esjeon) | Original repo dormant (last release Feb 2022) | dwm-inspired | Inspired Bismuth. Works on Plasma 5. |
| **Bismuth** (Bismuth-Forge) | Archived Jun 2024 | dwm-inspired | Built on Krohnkite. Plasma 5 only. Was the most popular option before Plasma 6. |

## Plasma 6 Tiling Options

### Built-in KWin 6 Tiling

KWin 6 added a "Custom Tiling" feature, but it is **not automatic tiling**:
- Lets you define custom tile zones and snap windows to them
- Does NOT auto-tile new windows
- Does NOT auto-adjust layouts when windows are added/removed
- Closer to Windows FancyZones than to i3/dwm
- Plasma 6.4 added per-virtual-desktop tile configurations
- Plasma 6.6.1 improved key-repeat behavior

Not a replacement for scripts like kwin-script-tiling if you want auto-tiling.

### Third-Party Scripts for Plasma 6

| Project | Status (as of Mar 2026) | Style | URL |
|---------|------------------------|-------|-----|
| **Krohnkite** (Plasma 6 port) | Active, v0.9.9.2 (Jul 2025) | dwm-inspired, automatic tiling | https://store.kde.org/p/2144146/ |
| **Fluid-tile** (Serroda) | **Actively maintained** (last commit Mar 2026) | Extends KWin's native tile system with auto-tiling | https://codeberg.org/Serroda/fluid-tile |
| **Polonium** (zeroxoneafour) | Archived Nov 2025 | Bismuth successor for Plasma 6/Wayland | https://github.com/zeroxoneafour/polonium |

### Recommended for Plasma 6

- **Fluid-tile** — most actively maintained, leverages KWin's native tile zones
- **Krohnkite** (Plasma 6 port) — most established, dwm-style

## Breaking Changes: Plasma 5 → Plasma 6

Any KWin script written for Plasma 5 requires significant porting for Plasma 6:

| Change | Detail |
|--------|--------|
| `Qt.include()` deprecated | Must rewrite to ES modules (`import`/`export`). This is what causes the `workspace is not defined` bug in Qt 5.15 and is fully removed in Qt 6. |
| `client` → `window` API rename | `workspace.clientList()` → `workspace.windowList()`, `clientAdded` → `windowAdded`, all `client*` signals/properties renamed |
| Metadata format | `metadata.desktop` → `metadata.json` with `"KPackageStructure": "KWin/Script"` |
| QML changes | `variant` behaves like `var`, implicit string conversions removed, relative URL resolution changed |
| Wayland default | Plasma 6 defaults to Wayland. X11-only code paths need Wayland equivalents. |

The `workspace` global still exists in Plasma 6 as the primary WM interface.

## Lineage

```
mgottschlag/kwin-tiling (original)
  └─ faho/kwin-tiling (v2.x, Plasma 5) ← what we run
       └─ Krohnkite (esjeon, dwm-inspired)
            ├─ Bismuth (Plasma 5, archived)
            ├─ Polonium (Plasma 6, archived)
            └─ Krohnkite Plasma 6 port (active)

Separate lineage:
  Fluid-tile (uses KWin 6 native tile zones, active)
```
