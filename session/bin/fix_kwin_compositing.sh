#!/bin/bash
# Fix dead compositing (Meta+D / Show Desktop and other effects do nothing).
# KWin sets OpenGLIsUnsafe=true in kwinrc while it initialises OpenGL; if it is
# killed or replaced mid-init the flag sticks and KWin refuses to composite
# until it is cleared. restart-plasma-kwin does not clear it.

export DISPLAY="${DISPLAY:-:0}"
PATCHED="$HOME/.local/kwin-dev/bin/kwin_x11"
LIB_PATH="$HOME/.local/kwin-dev/lib/x86_64-linux-gnu"

compositing_active() {
    [ "$(qdbus org.kde.KWin /Compositor org.kde.kwin.Compositing.active 2>/dev/null)" = "true" ]
}
clear_flag() {
    kwriteconfig5 --file kwinrc --group Compositing --key OpenGLIsUnsafe false
    kwriteconfig5 --file kwinrc --group Compositing --key Enabled true
}

if compositing_active; then
    echo "Compositing is already active - nothing to fix."
    exit 0
fi

echo "OpenGLIsUnsafe=$(kreadconfig5 --file kwinrc --group Compositing --key OpenGLIsUnsafe) - clearing..."
clear_flag

# First try without restarting KWin.
qdbus org.kde.KWin /KWin org.kde.KWin.reconfigure >/dev/null 2>&1
qdbus org.kde.KWin /Compositor org.kde.kwin.Compositing.resume >/dev/null 2>&1
sleep 2

if ! compositing_active; then
    echo "Still off - restarting KWin (plasmashell is left alone)..."
    GUARD="$HOME/.local/kwin-dev/bin/spatial-map-guard"
    [ -x "$GUARD" ] && "$GUARD" backup
    if [ -x "$PATCHED" ]; then
        "$HOME/.local/bin/kwin-dev-start"
    else
        setsid systemd-cat -t kwin kwin_x11 --replace &
    fi
    for _ in $(seq 1 20); do
        sleep 1
        compositing_active && break
    done
fi

if compositing_active; then
    echo "Compositing is active again."
else
    echo "Compositing is still OFF: $(qdbus org.kde.KWin /Compositor org.kde.kwin.Compositing.compositingNotPossibleReason 2>/dev/null | sed 's/<[^>]*>/ /g')"
    echo "OpenGLIsUnsafe=$(kreadconfig5 --file kwinrc --group Compositing --key OpenGLIsUnsafe) (true again = OpenGL init is really failing)"
    exit 1
fi
