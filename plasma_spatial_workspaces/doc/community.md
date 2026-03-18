# PSW Community References

Upstream KDE discussions relevant to Plasma Spatial Workspaces.

---

## 1. KDE Discuss: Automatic Virtual Desktops Management

**URL**: https://discuss.kde.org/t/automatic-virtual-desktops-management/15776/8

**Summary**: Forum thread discussing automatic virtual desktop creation/deletion
(GNOME-style auto-lifecycle where a fresh desktop appears when all are occupied
and empty ones are reclaimed). Multiple users express desire for 2D desktop
navigation and spatial grid layouts.

**Relevance to PSW**:
- PSW is a superset of what this thread requests. The thread focuses on
  auto-lifecycle (create/destroy desktops dynamically), while PSW provides the
  spatial navigation graph that makes 2D layouts meaningful.
- Auto-lifecycle could be built on top of PSW's spatial map: add a desktop to
  the graph when all are occupied, remove when empty, preserving neighbor
  relationships.
- Thread confirms user demand for features PSW already implements (directional
  navigation, grid layouts beyond flat rows).

---

## 2. KDE MR !6922: Auto-Arrange Desktop Grid View

**URL**: https://invent.kde.org/plasma/kwin/-/merge_requests/6922
**Note of interest**: https://invent.kde.org/plasma/kwin/-/merge_requests/6922#note_1262322

**Summary**: Merge request by Blazer Silving (filed Dec 2024, still unmerged as
of Oct 2025) to reintroduce the Plasma 5 ability for the Desktop Grid effect to
compute its own layout independently of the system pager. In Plasma 6, the
Desktop Grid was merged into Overview and lost independent layout control. If
you have 9 desktops in 1 row (for touchpad swiping), the grid also shows them
as one tiny row. This MR adds an "auto-arrange" option so the grid can display
as 3x3 while the pager stays 1-row.

**Key discussion points**:
- Maintainer Vlad Zahorodnii asked: "Does it make sense to support this in kwin
  core instead?" -- aligns with PSW's approach of spatial relationships at the
  compositor level rather than per-effect workarounds.
- Debate over decoupling grid visual layout from `VirtualDesktopGrid` and
  whether it would break gestures (it doesn't -- gestures follow the pager
  until Overview is fully expanded).
- Note #1262322 (by Samuele Zappala, July 2025) links the Discuss thread above,
  argues this MR is a good incremental step while leaving full 2D navigation as
  a future independent feature.
- MR is stalled. Maintainers have not engaged further. Author considering
  forking Overview into a standalone effect.

**Relevance to PSW**:
- The core tension this MR addresses (single global VirtualDesktopGrid
  controlling both navigation topology AND visual layout) is exactly what PSW
  solves architecturally with an explicit spatial neighbor graph.
- PSW ported to Plasma 6 would supersede this MR entirely.
- Validates user demand for PSW's feature set.
- Vlad's suggestion to do it "in kwin core" is precisely PSW's design.

---

## 3. Related KDE Bugs

- **Bug #482418**: Desktop Grid should have its own layout independent of pager
  (the bug that MR !6922 addresses).
