#!/bin/bash
# Reset kwin to clean stock state after running custom spatial kwin.
# Clears spatial config, fixes grid layout, restarts kwin_x11.
#
# Usage: ./reset-kwin.sh [rows]
#   rows: desired grid rows (default: auto from kwinrc Rows or 2)

set -euo pipefail

KWINRC="$HOME/.config/kwinrc"
ROWS="${1:-}"

echo "=== Current state ==="
echo "X11 layout: $(xprop -root _NET_DESKTOP_LAYOUT 2>/dev/null || echo 'N/A')"
echo "kwinrc:"
grep -E "Number|Rows|SpatialMode" "$KWINRC" 2>/dev/null | sed 's/^/  /'
echo "Spatial keys: $(grep -c 'Spatial_' "$KWINRC" 2>/dev/null || echo 0)"

# Read desktop count
COUNT=$(kreadconfig5 --file kwinrc --group Desktops --key Number --default 4)

# Determine rows
if [ -z "$ROWS" ]; then
    ROWS=$(kreadconfig5 --file kwinrc --group Desktops --key Rows --default 2)
fi
COLS=$(( (COUNT + ROWS - 1) / ROWS ))

echo ""
echo "=== Cleaning spatial config ==="

# Remove SpatialMode
kwriteconfig5 --file kwinrc --group Desktops --key SpatialMode --delete 2>/dev/null || true
echo "  Removed SpatialMode"

# Remove all Spatial_* keys
grep -oP 'Spatial_[^=]+' "$KWINRC" 2>/dev/null | while read -r key; do
    kwriteconfig5 --file kwinrc --group Desktops --key "$key" --delete 2>/dev/null || true
done
echo "  Removed Spatial_* keys"

# Ensure Rows is set correctly
kwriteconfig5 --file kwinrc --group Desktops --key Rows "$ROWS"
echo "  Set Rows=$ROWS"

echo ""
echo "=== Setting X11 layout: ${COLS}x${ROWS} for $COUNT desktops ==="
# orientation=0 (horizontal), columns, rows, starting_corner=0
xprop -root -format _NET_DESKTOP_LAYOUT 32c -set _NET_DESKTOP_LAYOUT "0,$COLS,$ROWS,0"
echo "  Done: $(xprop -root _NET_DESKTOP_LAYOUT)"

echo ""
echo "=== Restarting kwin_x11 ==="
# Send reconfigure first (picks up kwinrc changes)
qdbus org.kde.KWin /KWin reconfigure 2>/dev/null || true
sleep 1

# Replace kwin to force full re-init of layout
kwin_x11 --replace &
disown
sleep 2

echo ""
echo "=== Final state ==="
echo "X11 layout: $(xprop -root _NET_DESKTOP_LAYOUT 2>/dev/null || echo 'N/A')"
echo "PID: $(pgrep -x kwin_x11 || echo 'not running')"
echo "Grid: ${COLS}x${ROWS} ($COUNT desktops)"
echo ""
echo "Done. Slide and grid effects should now match."
