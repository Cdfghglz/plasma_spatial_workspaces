#!/bin/bash
# deploy.sh — install the desktop customisation from this repo by SYMLINKING every
# file/dir into the place the session reads it from. The repo is the only copy:
# edit here, never in ~/.local/share/kwin/scripts & co.
#
#   ./deploy.sh            link everything (backs up anything that is not already our link)
#   ./deploy.sh --check    report drift only, change nothing (exit 1 if anything is off)
#
# Does NOT build or restart anything:
#   KWin C++        tools/psw build   (builds build/kwin, installs to ~/.local/kwin-dev)
#   ActivityJumper  see README "ActivityJumper"
#   KWin scripts    take effect on the next kwin restart (never unload/reload a live script)
#   systemd units   systemctl --user daemon-reload
set -u
REPO="$(cd "$(dirname "$(realpath "$0")")" && pwd)"
MODE="${1:-link}"
BACKUP="$HOME/.local/share/kde_gt-deploy-backup/$(date +%Y%m%d-%H%M%S)"
drift=0

# repo-relative source  ->  absolute target
MAP=(
  "kwin-scripts/desktopchangeosd            $HOME/.local/share/kwin/scripts/desktopchangeosd"
  "kwin-scripts/desktopchangeosd-spatial    $HOME/.local/share/kwin/scripts/desktopchangeosd-spatial"
  "kwin-scripts/spatial-desktop-nav         $HOME/.local/share/kwin/scripts/spatial-desktop-nav"
  "kwin-scripts/kwin-script-tiling          $HOME/.local/share/kwin/scripts/kwin-script-tiling"
  "kwin-scripts/alwaysopenonfocusedscreen   $HOME/.local/share/kwin/scripts/alwaysopenonfocusedscreen"
  "plasmoids/pager                          $HOME/.local/share/plasma/plasmoids/org.kde.plasma.pager"
  "session/bin/kwin-dev-start               $HOME/.local/bin/kwin-dev-start"
  "session/bin/kwin-resume-watcher.sh       $HOME/.local/bin/kwin-resume-watcher.sh"
  "session/bin/kwin-patched-watchdog.sh     $HOME/.local/bin/kwin-patched-watchdog.sh"
  "session/bin/disk-space-alert.sh          $HOME/.local/bin/disk-space-alert.sh"
  "session/bin/aj-pin-monitor.sh            $HOME/.local/bin/aj-pin-monitor.sh"
  "session/bin/spatial-rofi-switcher        $HOME/.local/bin/spatial-rofi-switcher"
  "tools/psw                                $HOME/.local/bin/psw"
  "session/bin/fix_kwin_compositing.sh      $HOME/scripts/fix_kwin_compositing.sh"
  "session/bin/restart_plasma_kwin.sh       $HOME/scripts/restart_plasma_kwin.sh"
  "session/bin/restart-plasma.sh            $HOME/scripts/restart-plasma.sh"
  "session/bin/xephyr-test-kwin.sh          $HOME/scripts/xephyr-test-kwin.sh"
  "session/bin/spatial-map-guard            $HOME/.local/kwin-dev/bin/spatial-map-guard"
  "session/systemd/kwin-patched-watchdog.service   $HOME/.config/systemd/user/kwin-patched-watchdog.service"
  "session/systemd/kwin-patched-watchdog.timer     $HOME/.config/systemd/user/kwin-patched-watchdog.timer"
  "session/systemd/kwin-patched-watchdog.service.d/handover-guard.conf  $HOME/.config/systemd/user/kwin-patched-watchdog.service.d/handover-guard.conf"
  "session/systemd/kwin-patched-resume.service     $HOME/.config/systemd/user/kwin-patched-resume.service"
  "session/systemd/disk-space-alert.service        $HOME/.config/systemd/user/disk-space-alert.service"
  "session/systemd/disk-space-alert.timer          $HOME/.config/systemd/user/disk-space-alert.timer"
  "session/plasma-env/fix-meta.sh           $HOME/.config/plasma-workspace/env/fix-meta.sh"
  "session/plasma-env/intel-reemit.sh       $HOME/.config/plasma-workspace/env/intel-reemit.sh"
  "session/plasma-env/kwin-dev-path.sh      $HOME/.config/plasma-workspace/env/kwin-dev-path.sh"
  "session/plasma-env/qsg-render-loop.sh    $HOME/.config/plasma-workspace/env/qsg-render-loop.sh"
  "activityjumper/build/ActivityJumper      $HOME/.local/bin/ActivityJumper"
)

say() { printf '%-8s %s\n' "$1" "$2"; }

for entry in "${MAP[@]}"; do
  read -r rel target <<<"$entry"
  src="$REPO/$rel"
  if [ ! -e "$src" ]; then
    say MISSING "$rel (not in repo / not built)"; drift=1; continue
  fi
  if [ -L "$target" ] && [ "$(readlink -f "$target")" = "$(readlink -f "$src")" ]; then
    [ "$MODE" = --check ] && say ok "$target"
    continue
  fi
  drift=1
  if [ -e "$target" ] || [ -L "$target" ]; then
    if [ -L "$target" ]; then state="link elsewhere -> $(readlink "$target")"
    elif diff -rq "$src" "$target" >/dev/null 2>&1; then state="copy, identical"
    else state="copy, DIFFERS from repo"; fi
  else
    state="absent"
  fi
  if [ "$MODE" = --check ]; then say DRIFT "$target ($state)"; continue; fi
  if [ -e "$target" ] || [ -L "$target" ]; then
    mkdir -p "$BACKUP$(dirname "$target")"
    mv "$target" "$BACKUP$target"
  fi
  mkdir -p "$(dirname "$target")"
  ln -s "$src" "$target"
  say linked "$target  (was: $state)"
done

# Generated file: user D-Bus service for ActivityJumper (overrides /usr/share).
svc="$HOME/.local/share/dbus-1/services/org.kde.ActivityJumper.service"
want="$(sed "s#@HOME@#$HOME#" "$REPO/session/dbus/org.kde.ActivityJumper.service.in")"
if [ "$(cat "$svc" 2>/dev/null)" != "$want" ]; then
  drift=1
  if [ "$MODE" = --check ]; then say DRIFT "$svc"; else
    mkdir -p "$(dirname "$svc")"; printf '%s\n' "$want" > "$svc"; say wrote "$svc"
  fi
elif [ "$MODE" = --check ]; then say ok "$svc"; fi

# Root-owned login wrapper: report only.
if ! cmp -s "$REPO/session/system/usr-local-bin-kwin_x11" /usr/local/bin/kwin_x11; then
  drift=1
  say SUDO "/usr/local/bin/kwin_x11 differs; install with:"
  echo "         sudo install -m755 $REPO/session/system/usr-local-bin-kwin_x11 /usr/local/bin/kwin_x11"
elif [ "$MODE" = --check ]; then say ok "/usr/local/bin/kwin_x11"; fi

[ "$MODE" != --check ] && [ -d "$BACKUP" ] && echo "backups of replaced files: $BACKUP"
exit $drift
