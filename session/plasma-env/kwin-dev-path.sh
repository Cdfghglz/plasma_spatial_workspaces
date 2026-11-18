#!/bin/sh
# Prepend the patched kwin-dev install prefix so KWin finds its own
# scripts/services (e.g. desktopchangeosd) before the system copy under
# /usr/share/kwin/scripts/.
export XDG_DATA_DIRS="$HOME/.local/kwin-dev/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
