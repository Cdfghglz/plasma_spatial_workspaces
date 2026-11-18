#!/bin/bash
# Ensure the patched kwin-dev build is the running window manager.
# Called every 30 s by kwin-patched-watchdog.timer.
GUARD=$HOME/.local/kwin-dev/bin/spatial-map-guard
PATCHED=$HOME/.local/kwin-dev/bin/kwin_x11
CRASH_DIR=$HOME/.local/kwin-dev/crashes
[ -x "$PATCHED" ] || exit 0

# The timer fires right after resume, exactly when kwin-resume-watcher is
# replacing kwin; it touches this stamp. Every past dead-compositing incident
# (2026-09-28, 10-08, 10-09, 10-12) was the two of them racing.
STAMP="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/kwin-dev-handover"
if [ -e "$STAMP" ] && [ $(( $(date +%s) - $(stat -c %Y "$STAMP") )) -lt 90 ]; then exit 0; fi

PIDS=$(pgrep -x kwin_x11 2>/dev/null)
# A kwin --replace handover (resume watcher, KCrash auto-restart) is in
# progress: several kwins, or the newest one only just started. Starting yet
# another kwin now kills one of them mid OpenGL init, which leaves
# OpenGLIsUnsafe=true in kwinrc and compositing (Meta+D) dead.
if [ "$(echo "$PIDS" | wc -w)" -gt 1 ]; then exit 0; fi
if [ -n "$PIDS" ] && [ "$(ps -o etimes= -p "$PIDS" | tr -d ' ')" -lt 30 ]; then exit 0; fi

if [ -z "$PIDS" ]; then
    [ -x "$GUARD" ] && "$GUARD" restore
    exec "$HOME/.local/bin/kwin-dev-start"
fi
EXE=$(readlink -f "/proc/$PIDS/exe" 2>/dev/null)
if [ "$EXE" != "$PATCHED" ]; then
    [ -x "$GUARD" ] && "$GUARD" backup
    [ -x "$GUARD" ] && "$GUARD" restore
    exec "$HOME/.local/bin/kwin-dev-start"
else
    [ -x "$GUARD" ] && "$GUARD" backup
fi
exit 0
