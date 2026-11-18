#!/bin/bash
# Launch a nested KDE/KWin session in Xephyr for safe testing.
# Usage: ./xephyr-test-kwin.sh [kwin_binary]
# Stop:  kill the Xephyr process or run: pkill -f "Xephyr :5"

KWIN="${1:-$HOME/.local/kwin-dev/bin/kwin_x11}"
XEPHYR_DISPLAY=:5
SIZE="1280x720"
LOG="/tmp/xephyr-kwin-test.log"

# Clean up previous session
pkill -f "Xephyr $XEPHYR_DISPLAY" 2>/dev/null
sleep 0.5

echo "Starting Xephyr on $XEPHYR_DISPLAY ($SIZE)..."
DISPLAY=:0 Xephyr "$XEPHYR_DISPLAY" -screen "$SIZE" -ac -resizeable &
XEPHYR_PID=$!
sleep 2

if ! kill -0 "$XEPHYR_PID" 2>/dev/null; then
    echo "ERROR: Xephyr failed to start"
    exit 1
fi

# Start a nested D-Bus session — use 'wait' instead of 'read' to keep alive
echo "Starting nested session with KWin: $KWIN"
DISPLAY="$XEPHYR_DISPLAY" dbus-run-session bash -c "
    export DISPLAY=$XEPHYR_DISPLAY

    # Start KWin
    $KWIN &>$LOG &
    KWIN_PID=\$!
    sleep 3

    if ! kill -0 \$KWIN_PID 2>/dev/null; then
        echo 'ERROR: KWin crashed on startup. Check $LOG'
        exit 1
    fi

    echo 'KWin running (PID '\$KWIN_PID')'

    # Launch xterm
    xterm -geometry 80x24 &

    echo '=== Xephyr test session active ==='
    echo 'Click Xephyr window → Ctrl+Shift to capture keyboard → Meta+S to test'
    echo 'To stop: pkill -f \"Xephyr :5\"'

    # Wait for KWin to exit (keeps dbus-run-session alive)
    wait \$KWIN_PID 2>/dev/null
" &

DBUS_PID=$!

# Move Xephyr window to current desktop after it settles
sleep 5
XWIN=$(DISPLAY=:0 wmctrl -l | grep -i xephyr | awk '{print $1}')
if [ -n "$XWIN" ]; then
    DISPLAY=:0 wmctrl -i -a "$XWIN" 2>/dev/null
    echo "Xephyr window raised."
fi

echo "Session running."
echo "  Xephyr PID: $XEPHYR_PID"
echo "  Stop with:  pkill -f 'Xephyr :5'"
