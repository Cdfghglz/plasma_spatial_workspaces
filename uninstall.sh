#!/bin/bash
# Uninstall Spatial Desktop Navigation KWin script
set -e

echo "=== Spatial Desktop Navigation - Uninstaller ==="

# Remove KWin script
INSTALL_DIR="$HOME/.local/share/kwin/scripts/spatial-desktop-nav"
if [ -d "$INSTALL_DIR" ]; then
    echo "Removing KWin script from $INSTALL_DIR..."
    rm -rf "$INSTALL_DIR"
    echo "  Done."
else
    echo "KWin script not found at $INSTALL_DIR (already removed?)."
fi

# Remove rofi switcher
ROFI_DEST="$HOME/.local/bin/spatial-rofi-switcher"
if [ -f "$ROFI_DEST" ]; then
    echo "Removing rofi switcher from $ROFI_DEST..."
    rm -f "$ROFI_DEST"
    echo "  Done."
else
    echo "Rofi switcher not found at $ROFI_DEST (already removed?)."
fi

# Disable the KWin script
echo "Disabling KWin script in kwinrc..."
kwriteconfig5 --file kwinrc --group Plugins --key spatial-desktop-navEnabled false
echo "  Done."

# Reload KWin
echo "Reloading KWin..."
qdbus org.kde.KWin /KWin reconfigure
echo "  Done."

echo ""
echo "=== Uninstall complete! ==="
echo "Your desktop is back to its previous state."
echo "No desktops, shortcuts, or other settings were modified."
