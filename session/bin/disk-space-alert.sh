#!/bin/bash
# disk-space-alert — desktop notification before / fills up.
#
# Run by disk-space-alert.timer every 2 min. A 100%-full root disk silently wipes
# KDE config files (Qt 5.15 QSaveFile bug, see ~/.local/kwin-dev/bin/spatial-map-guard),
# and has recurred from gt tmp_pack_* files and runaway logs.
#
# Alerts (rate-limited):
#   warning   free < WARN_GB, or free will run out within ~2h at the current rate
#   critical  free < CRIT_GB, or out within ~20 min  (repeats every 10 min)
# The notification names the biggest files written in the last hour.

MOUNT=/
WARN_GB=${WARN_GB:-15}
CRIT_GB=${CRIT_GB:-4}
STATE="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/disk-space-alert"
mkdir -p "$STATE"

avail_mb=$(df -Pm "$MOUNT" | awk 'NR==2 {print $4}')
now=$(date +%s)

# Fill rate from the previous sample (MB/min, positive = filling).
rate=0
if [ -f "$STATE/last" ] && read -r prev_t prev_mb < "$STATE/last" && [ "$now" -gt "$prev_t" ]; then
    rate=$(( (prev_mb - avail_mb) * 60 / (now - prev_t) ))
fi
echo "$now $avail_mb" > "$STATE/last"

eta_min=""
[ "$rate" -gt 0 ] && eta_min=$(( avail_mb / rate ))

level=""
if [ "$avail_mb" -lt $((CRIT_GB * 1024)) ] || { [ -n "$eta_min" ] && [ "$eta_min" -lt 20 ]; }; then
    level=critical; every=600
elif [ "$avail_mb" -lt $((WARN_GB * 1024)) ] || { [ -n "$eta_min" ] && [ "$eta_min" -lt 120 ]; }; then
    level=warning; every=3600
fi

if [ -z "$level" ]; then
    rm -f "$STATE"/notified-*
    exit 0
fi

stamp="$STATE/notified-$level"
if [ -f "$stamp" ] && [ $((now - $(stat -c %Y "$stamp"))) -lt "$every" ]; then
    exit 0
fi
touch "$stamp"

# Biggest files modified in the last hour (bounded search, read-only).
culprits=$(timeout 25 find /home/cdf /tmp /var/tmp -xdev -type f -size +500M -mmin -60 \
    -printf '%s\t%p\n' 2>/dev/null | sort -rn | head -3 |
    awk -F'\t' '{printf "%.1fG %s\n", $1/2^30, $2}')
packs=$(du -cb /home/cdf/ws/panza_stack/gt/.git/objects/pack/tmp_pack_* 2>/dev/null | awk 'END {print $1+0}')

msg="Only $((avail_mb / 1024)) GB free on $MOUNT"
[ -n "$eta_min" ] && msg="$msg, filling at ${rate} MB/min (full in ~${eta_min} min)"
msg="$msg. A full disk wipes KDE configs."
[ "$packs" -gt $((1 << 30)) ] && msg="$msg\ngt tmp_pack_* files: $((packs >> 30)) GB (safe to delete if no process holds them)"
[ -n "$culprits" ] && msg="$msg\nRecently written:\n$culprits"

urgency=1; [ "$level" = critical ] && urgency=2
echo "disk-space-alert: $level: $msg"
gdbus call --session --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.Notify \
    "disk-space-alert" 0 "drive-harddisk" "Disk space $level" "$(printf '%b' "$msg")" \
    "[]" "{'urgency': <byte $urgency>}" 0 >/dev/null 2>&1
