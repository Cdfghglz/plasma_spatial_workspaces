#!/bin/bash
# Watch for system resume via logind DBus signal and restart patched kwin.
# Runs as a long-lived user service.

PATCHED="$HOME/.local/kwin-dev/bin/kwin_x11"
LIB_PATH="$HOME/.local/kwin-dev/lib/x86_64-linux-gnu"
HANDOVER_STAMP="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/kwin-dev-handover"

[ -x "$PATCHED" ] || { echo "Patched kwin not found: $PATCHED"; exit 1; }

echo "kwin-resume-watcher: monitoring logind PrepareForSleep signal..."

gdbus monitor --system \
    --dest org.freedesktop.login1 \
    --object-path /org/freedesktop/login1 2>/dev/null |
while read -r line; do
    if echo "$line" | grep -q "PrepareForSleep.*false"; then
        echo "kwin-resume-watcher: resume detected, restarting kwin in 3s..."
        # Tell kwin-patched-watchdog to stay out of the way during the handover.
        touch "$HANDOVER_STAMP"
        sleep 3
        DISPLAY=:0 "$HOME/.local/bin/kwin-dev-start"
        sleep 4
        DISPLAY=:0 qdbus org.kde.KWin /Compositor org.kde.kwin.Compositing.resume 2>/dev/null
        # The replaced kwin can crash on exit and KCrash restarts it, racing the
        # new one; the loser dies mid OpenGL init and leaves OpenGLIsUnsafe=true.
        # Once things settle, clear that and bring compositing back.
        sleep 20
        DISPLAY=:0 "$HOME/scripts/fix_kwin_compositing.sh"
        echo "kwin-resume-watcher: kwin restarted, compositor resumed."
    fi
done
