#!/bin/bash
# Restart KDE Plasma (kwin + plasmashell) - useful when the desktop freezes.
# Run from TTY2: Ctrl+Alt+F2, then: ~/scripts/restart-plasma.sh

export DISPLAY=:0

echo "Stopping KDE Plasma..."
killall plasmashell kwin_x11 2>/dev/null
sleep 2

echo "Starting kwin (window manager)..."
kwin_x11 &
sleep 1

echo "Starting plasmashell..."
plasmashell &

echo "Done. Switch back to the desktop with Ctrl+Alt+F1 (or F7)."
