#!/bin/bash
# Install Spatial Desktop Navigation KWin script
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN_DIR="$SCRIPT_DIR/spatial-desktop-nav"
INSTALL_DIR="$HOME/.local/share/kwin/scripts/spatial-desktop-nav"

echo "=== Spatial Desktop Navigation - Installer ==="

# Check dependencies
echo "Checking dependencies..."
for cmd in qdbus wmctrl rofi; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "  WARNING: '$cmd' not found. Install with: sudo apt install $cmd"
    else
        echo "  OK: $cmd"
    fi
done

# Install KWin script
echo ""
echo "Installing KWin script to $INSTALL_DIR..."
if [ -d "$INSTALL_DIR" ]; then
    echo "  Removing previous installation..."
    rm -rf "$INSTALL_DIR"
fi

mkdir -p "$INSTALL_DIR"
cp -r "$PLUGIN_DIR/metadata.desktop" "$INSTALL_DIR/"
cp -r "$PLUGIN_DIR/contents" "$INSTALL_DIR/"

echo "  Done."

# Install rofi switcher
echo ""
ROFI_DEST="$HOME/.local/bin/spatial-rofi-switcher"
echo "Installing rofi switcher to $ROFI_DEST..."
mkdir -p "$HOME/.local/bin"
cp "$PLUGIN_DIR/spatial-rofi-switcher.sh" "$ROFI_DEST"
chmod +x "$ROFI_DEST"
echo "  Done."

# Enable the KWin script
echo ""
echo "Enabling KWin script..."
kwriteconfig5 --file kwinrc --group Plugins --key spatial-desktop-navEnabled true
echo "  Done."

# Reload KWin
echo ""
echo "Reloading KWin..."
qdbus org.kde.KWin /KWin reconfigure
echo "  Done."

echo ""
echo "=== Installation complete! ==="
echo ""
echo "Shortcuts registered (configure in System Settings > Shortcuts > KWin):"
echo "  Meta+Alt+Arrow      Navigate spatially"
echo "  Meta+Alt+Shift+Arrow  Create desktop in direction"
echo "  Meta+Ctrl+Alt+Arrow   Move window to direction"
echo "  Meta+Alt+G            Toggle schematic overview"
echo ""
echo "Rofi switcher:"
echo "  Bind 'spatial-rofi-switcher' to Meta+Alt+D in System Settings > Shortcuts > Custom"
echo ""
echo "To uninstall:"
echo "  rm -rf $INSTALL_DIR"
echo "  rm -f $ROFI_DEST"
echo "  kwriteconfig5 --file kwinrc --group Plugins --key spatial-desktop-navEnabled false"
echo "  qdbus org.kde.KWin /KWin reconfigure"
