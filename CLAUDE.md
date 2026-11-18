# Agent instructions — kde_gt (Plasma/KWin desktop customisation on xps)

Read `README.md` first: component map, build/restart commands, log locations, known issues.

Rules:
1. This repo is the single source, installed by `./deploy.sh` (symlinks for plain files, copies for KDE
   packages: KWin scripts and the pager — KPackage ignores symlinked dirs). Never edit
   `~/.local/share/kwin/scripts`, `~/.local/bin`, `~/scripts` or `~/.config/systemd/user` copies
   directly; if `./deploy.sh --check` shows drift, bring the change into the repo first.
2. Before touching anything live, take the user's state seriously: they are working in this session.
   Tests that switch desktops/activities move their screen — say so and restore afterwards.
3. KWin restarts: only `touch $XDG_RUNTIME_DIR/kwin-dev-handover; ~/.local/bin/kwin-dev-start`, then
   `~/scripts/fix_kwin_compositing.sh` after ~20 s. Never reload a single KWin script in a running KWin.
   Never restart a systemd unit whose cgroup contains kwin_x11.
4. KWin C++: `kwin/` is a submodule (branch `psw/spatial-mode`). When rebuilding only libkwin, do not
   change class layouts (no new data members / virtuals).
5. Commit in the submodule first, then bump the pointer here. Push with the `Cdfghglz` gh account
   (`gh auth switch --user Cdfghglz`); `rastislav-marko-pr` has no write access.
6. Crash/log evidence is often in `~/.xsession-errors`, not the journal (see README).
7. Record new incidents/gotchas in README "Known issues" in the same commit as the fix.
