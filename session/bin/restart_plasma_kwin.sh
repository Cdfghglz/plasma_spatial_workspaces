#!/bin/bash
# Force-restart KDE Plasma shell and kwin compositor.
# Uses kill -9 to ensure leaked/hung processes are cleaned up.

export DISPLAY=:0

# Backup spatial maps before killing kwin (prevents tombstone loss)
GUARD="$HOME/.local/kwin-dev/bin/spatial-map-guard"
[ -x "$GUARD" ] && "$GUARD" backup

echo "Killing plasmashell and kwin_x11..."
killall -9 plasmashell kwin_x11 2>/dev/null

# Restore tombstones if system kwin corrupted them
[ -x "$GUARD" ] && "$GUARD" restore
sleep 1

echo "Cleaning phantom desktop containments from plasma config..."
# Plasmashell accumulates desktop/folder containments for non-existent screens
# after repeated kwin restarts. Remove any with lastScreen >= number of monitors.
PLASMA_CFG="$HOME/.config/plasma-org.kde.plasma.desktop-appletsrc"
if [ -f "$PLASMA_CFG" ]; then
    NUM_SCREENS=$(xrandr --listmonitors 2>/dev/null | head -1 | awk '{print $2}')
    NUM_SCREENS=${NUM_SCREENS:-2}
    python3 -c "
import re, sys
with open('$PLASMA_CFG') as f:
    text = f.read()
# Parse containment sections
containments = {}
current_id = None
for line in text.split('\n'):
    m = re.match(r'^\[Containments\]\[(\d+)\]$', line)
    if m:
        current_id = m.group(1)
        containments[current_id] = {}
        continue
    if current_id and re.match(r'^\[', line):
        if not line.startswith(f'[Containments][{current_id}]'):
            current_id = None
        continue
    if current_id and '=' in line:
        k, v = line.split('=', 1)
        containments[current_id][k] = v
# Find phantom containments (desktop/folder on screens >= NUM_SCREENS)
remove_ids = set()
for cid, d in containments.items():
    plugin = d.get('plugin', '')
    ls = d.get('lastScreen', '0')
    if ('desktop' in plugin or 'folder' in plugin):
        if ls.isdigit() and int(ls) >= $NUM_SCREENS:
            remove_ids.add(cid)
if not remove_ids:
    sys.exit(0)
# Remove matching sections
lines = text.split('\n')
out = []
skip = False
for line in lines:
    m = re.match(r'^\[Containments\]\[(\d+)\]', line)
    if m and m.group(1) in remove_ids:
        skip = True
        continue
    elif re.match(r'^\[', line) and skip:
        if not any(line.startswith(f'[Containments][{rid}]') for rid in remove_ids):
            skip = False
    if not skip:
        out.append(line)
result = re.sub(r'\n{3,}', '\n\n', '\n'.join(out))
with open('$PLASMA_CFG', 'w') as f:
    f.write(result)
print(f'  Removed {len(remove_ids)} phantom containments (screens >= $NUM_SCREENS)')
" 2>/dev/null || true
fi

echo "Ensuring compositing is enabled in config..."
kwriteconfig5 --file kwinrc --group Compositing --key Enabled true

echo "Starting kwin_x11..."
# Use the patched kwin with correct plugin path to avoid vtable mismatch
KWIN_BIN="${KWIN_BIN:-$HOME/.local/kwin-dev/bin/kwin_x11}"
if [ -x "$KWIN_BIN" ]; then
    echo "  Using patched kwin: $KWIN_BIN"
    # Must set QT_PLUGIN_PATH so the patched KWinX11Platform.so is loaded
    # instead of the system one (vtable mismatch from added virtual methods)
    export QT_PLUGIN_PATH="$HOME/.local/kwin-dev/lib/x86_64-linux-gnu/plugins:${QT_PLUGIN_PATH:-}"
    # Log to the journal, never to this terminal: a paused or stuck terminal
    # (Ctrl+S) blocks every write and freezes the whole desktop.
    setsid systemd-cat -t kwin-dev "$KWIN_BIN" --replace &
else
    echo "  Using system kwin"
    setsid systemd-cat -t kwin kwin_x11 --replace &
fi
sleep 2

echo "Re-enabling compositing (in case crash-loop disabled it)..."
qdbus org.kde.KWin /Compositor resume 2>/dev/null

echo "Starting plasmashell..."
systemd-cat -t plasmashell kstart5 plasmashell &
sleep 3

echo "Done. Status:"
ps aux | grep -E '(kwin_x11|plasmashell)' | grep -v -E '(grep|kioslave)' | awk '{printf "  PID: %s, RAM: %s MB, CPU: %s%%, CMD: %s\n", $2, int($6/1024), $3, $11}'
