#!/usr/bin/env bash
# dev/setup-kwin-build.sh
#
# Sets up the KWin 5.24.7 build environment for plasma-spatial-workspaces development.
#
# Clones KWin v5.24.7, installs build dependencies, and builds a custom binary at
# ~/.local/kwin-dev. This custom binary is used to test spatial workspace patches
# without modifying the system KWin installation.
#
# Usage:
#   ./dev/setup-kwin-build.sh [--clean]
#
# Options:
#   --clean   Remove existing clone and build dirs before starting
#
# Prerequisites:
#   - Ubuntu 22.04 (Jammy) with deb-src entries in /etc/apt/sources.list
#   - sudo access for apt-get
#
# After running, verify the build:
#   ~/.local/kwin-dev/bin/kwin_x11 --version
#   # Should print: kwin 5.24.7
#
#   # To test as a replacement WM (on live X11 session):
#   DISPLAY=:0 ~/.local/kwin-dev/bin/kwin_x11 --replace &

set -euo pipefail

KWIN_VERSION="v5.24.7"
KWIN_REPO="https://invent.kde.org/plasma/kwin.git"
BUILD_DIR="${HOME}/ws/kwin-dev"
INSTALL_PREFIX="${HOME}/.local/kwin-dev"

# Parse args
CLEAN=0
for arg in "$@"; do
  case $arg in
    --clean) CLEAN=1 ;;
    *) echo "Unknown argument: $arg"; exit 1 ;;
  esac
done

echo "==> KWin build environment setup"
echo "    Version:  ${KWIN_VERSION}"
echo "    Build:    ${BUILD_DIR}"
echo "    Install:  ${INSTALL_PREFIX}"

if [[ $CLEAN -eq 1 ]]; then
  echo "==> Cleaning existing dirs..."
  rm -rf "${BUILD_DIR}"
fi

# Step 1: Enable deb-src if not already enabled
echo "==> Enabling deb-src entries..."
sudo sed -i 's/^# deb-src/deb-src/' /etc/apt/sources.list
sudo apt-get update -qq

# Step 2: Install build dependencies
echo "==> Installing KWin build dependencies..."
sudo apt-get build-dep -y kwin

# Step 3: Clone source
if [[ ! -d "${BUILD_DIR}/kwin/.git" ]]; then
  echo "==> Cloning KWin ${KWIN_VERSION}..."
  mkdir -p "${BUILD_DIR}"
  git clone --depth 1 --branch "${KWIN_VERSION}" "${KWIN_REPO}" "${BUILD_DIR}/kwin"
else
  echo "==> KWin source already present at ${BUILD_DIR}/kwin"
fi

# Step 4: Patch version requirement for KDecoration2
# Ubuntu 22.04 ships KDecoration2 5.24.4 alongside KWin 5.24.7; relax the version check.
echo "==> Patching KDecoration2 version requirement..."
sed -i 's/find_package(KDecoration2 ${PROJECT_VERSION} CONFIG REQUIRED)/find_package(KDecoration2 5.24.4 CONFIG REQUIRED)/' \
  "${BUILD_DIR}/kwin/CMakeLists.txt"

# Step 5: Configure
echo "==> Configuring build..."
mkdir -p "${BUILD_DIR}/build"
cmake "${BUILD_DIR}/kwin" \
  -B "${BUILD_DIR}/build" \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -Wno-dev

# Step 6: Build
echo "==> Building (this takes a few minutes)..."
make -C "${BUILD_DIR}/build" -j"$(nproc)"

# Step 7: Install
echo "==> Installing to ${INSTALL_PREFIX}..."
make -C "${BUILD_DIR}/build" install

# Step 8: Verify
echo "==> Verifying build..."
"${INSTALL_PREFIX}/bin/kwin_x11" --version

echo ""
echo "Build complete. Custom kwin_x11 installed at:"
echo "  ${INSTALL_PREFIX}/bin/kwin_x11"
echo ""
echo "To use in development, run kwin with full path:"
echo "  DISPLAY=:0 ${INSTALL_PREFIX}/bin/kwin_x11 --replace"
