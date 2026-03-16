#!/bin/bash
# Spatial Desktop Fuzzy Switcher via rofi
# Bound to Meta+Alt+D
#
# Lists all virtual desktops by name, pipes to rofi for fuzzy matching,
# then switches to the selected desktop via qdbus.

# Get current desktop ID
CURRENT_ID=$(qdbus org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.current 2>/dev/null)

# Get all desktops as DBus struct array, extract names and IDs
DESKTOPS=$(qdbus org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.desktops 2>/dev/null)

# Fallback: use workspace desktop names via xdotool
if [ -z "$DESKTOPS" ]; then
    # Get count and iterate
    COUNT=$(xdotool get_num_desktops 2>/dev/null)
    if [ -z "$COUNT" ]; then
        notify-send "Spatial Nav" "Cannot query desktops"
        exit 1
    fi

    NAMES=""
    for i in $(seq 0 $((COUNT - 1))); do
        NAME=$(xdotool get_desktop_name "$i" 2>/dev/null)
        NAMES="${NAMES}${NAME}\n"
    done

    SELECTED=$(echo -e "$NAMES" | sed '/^$/d' | rofi -dmenu -i -p "Desktop" -matching fuzzy)
    if [ -n "$SELECTED" ]; then
        for i in $(seq 0 $((COUNT - 1))); do
            NAME=$(xdotool get_desktop_name "$i" 2>/dev/null)
            if [ "$NAME" = "$SELECTED" ]; then
                xdotool set_desktop "$i"
                break
            fi
        done
    fi
    exit 0
fi

# Parse qdbus output - desktop names
# qdbus returns desktop info; we need to extract names
# Use wmctrl as a more reliable approach
NAMES=$(wmctrl -d 2>/dev/null | awk '{for(i=10;i<=NF;i++) printf "%s ", $i; print ""}' | sed 's/ *$//')

if [ -z "$NAMES" ]; then
    # Last resort: just use desktop numbers
    COUNT=$(qdbus org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.count 2>/dev/null)
    NAMES=""
    for i in $(seq 1 "$COUNT"); do
        NAMES="${NAMES}Desktop ${i}\n"
    done
fi

SELECTED=$(echo -e "$NAMES" | sed '/^$/d' | rofi -dmenu -i -p "Switch Desktop" -matching fuzzy -theme-str 'window {width: 30%;}')

if [ -n "$SELECTED" ]; then
    # Find desktop number by name using wmctrl
    TARGET=$(wmctrl -d 2>/dev/null | while read line; do
        NUM=$(echo "$line" | awk '{print $1}')
        NAME=$(echo "$line" | awk '{for(i=10;i<=NF;i++) printf "%s ", $i; print ""}' | sed 's/ *$//')
        if [ "$NAME" = "$SELECTED" ]; then
            echo "$NUM"
            break
        fi
    done)

    if [ -n "$TARGET" ]; then
        wmctrl -s "$TARGET"
    fi
fi
