#!/bin/bash
# Timestamped trace for debugging ActivityJumper pins (2026-11-18): jumper calls,
# pin-state signals, activity and desktop changes. getPinState calls are NOT matched
# (calls are matched by destination: khotkeys sends them without an interface field):
# this script calls getPinState itself, which would feed back into an endless loop. Output: ~/.local/state/aj-pin-monitor.log
export DBUS_SESSION_BUS_ADDRESS=${DBUS_SESSION_BUS_ADDRESS:-unix:path=/run/user/$(id -u)/bus}
LOG=$HOME/.local/state/aj-pin-monitor.log
declare -A NAMES
while IFS='=' read -r k v; do [[ $k =~ ^[0-9a-f]{8}- ]] && NAMES[$k]=$v; done < <(sed -n '/^\[activities\]/,/^\[/p' ~/.config/kactivitymanagerdrc)
pin() { echo "-"; }  # no getPinState queries: they make the jumper re-check its state (2026-11-18)
desk() { qdbus org.kde.KWin /KWin org.kde.KWin.currentDesktop 2>/dev/null; }
act() { local a; a=$(qdbus org.kde.ActivityManager /ActivityManager/Activities CurrentActivity 2>/dev/null); echo "${NAMES[$a]:-$a}"; }
echo "$(date '+%F %T.%3N') START at $(act):$(desk) pin=$(pin)" >> "$LOG"
dbus-monitor --session \
  "type='method_call',destination='org.kde.ActivityJumper',member='jumpTo'" \
  "type='method_call',destination='org.kde.ActivityJumper',member='jumpBack'" \
  "type='method_call',destination='org.kde.ActivityJumper',member='changePinState'" \
  "type='signal',interface='org.kde.ActivityJumper',member='pinStateChanged'" \
  "type='signal',interface='org.kde.ActivityManager.Activities',member='CurrentActivityChanged'" \
  "type='signal',interface='org.kde.KWin.VirtualDesktopManager',member='currentChanged'" 2>/dev/null |
while IFS= read -r line; do
  case "$line" in
    method\ call*) m=${line##*member=}; pending="CALL $m" ;;
    signal*pinStateChanged*) pending="SIGNAL pinStateChanged" ;;
    signal*CurrentActivityChanged*) pending="ACTIVITY"; ;;
    signal*currentChanged*) pending="DESKTOP" ;;
    *string\ *|*int32\ *)
      if [ -n "$pending" ]; then
        v=$(echo "$line" | sed -E 's/^\s*(string|int32) //; s/"//g')
        echo "$(date '+%F %T.%3N') $pending $v | now $(act):$(desk) pin=$(pin)" >> "$LOG"; pending=""
      fi ;;
    "") ;;
  esac
  if [ -n "$pending" ] && [[ $pending == CALL\ jumpBack* || $pending == CALL\ changePinState* ]]; then
    echo "$(date '+%F %T.%3N') $pending | now $(act):$(desk) pin=$(pin)" >> "$LOG"; pending=""
  fi
done
