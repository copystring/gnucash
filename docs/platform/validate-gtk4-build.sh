#!/bin/bash
# GTK4 Local Build Validation Script
# Run this in an environment with build tools (cmake, ninja, pkg-config, GTK4 deps)
# Requires: Gwenhywfar branch-4 + AqBanking built and installed

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GNUCASH_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${GNUCASH_ROOT}/build-gtk4-test"
INSTALL_DIR="${GNUCASH_ROOT}/inst-gtk4-test"
GWENHYWfar_PREFIX="${INSTALL_DIR}/gwenhywfar"

echo "=== GTK4 Local Build Validation ==="
echo "GnuCash root: $GNUCASH_ROOT"
echo "Build dir: $BUILD_DIR"
echo "Install dir: $INSTALL_DIR"

# Step 1: Build Gwenhywfar GTK4 (from our fork/branch-4 with GTK4 backend)
echo ""
echo "=== Step 1: Build Gwenhywfar GTK4 ==="
GWEN_SRC="${GWEN_SRC:-${GNUCASH_ROOT}/../gwenhywfar-branch-4}"
if [ ! -d "$GWEN_SRC" ]; then
    echo "Gwenhywfar source not found at $GWEN_SRC, cloning fork..."
    git clone --depth 1 --branch cs-202609-gtk4-gui https://github.com/copystring/gwenhywfar.git /tmp/gwenhywfar-gtk4
    GWEN_SRC="/tmp/gwenhywfar-gtk4"
fi
cd "$GWEN_SRC"
if [ ! -f configure ]; then
    autoreconf -fiv
fi
./configure \
  --prefix="$GWENHYWfar_PREFIX" \
  --with-guis="gtk4" \
  --disable-binaries \
  --disable-plugins
make -j"$(nproc)"
make install
echo "Gwenhywfar installed to $GWENHYWfar_PREFIX"

# Step 2: Build AqBanking against Gwenhywfar GTK4
echo ""
echo "=== Step 2: Build AqBanking ==="
AQB_SRC="${AQB_SRC:-${GNUCASH_ROOT}/../aqbanking-master}"
if [ ! -d "$AQB_SRC" ]; then
    echo "AqBanking source not found at $AQB_SRC, cloning..."
    git clone --depth 1 https://github.com/aqbanking/aqbanking.git /tmp/aqbanking-gtk4
    AQB_SRC="/tmp/aqbanking-gtk4"
fi
cd "$AQB_SRC"
if [ ! -f configure ]; then
    autoreconf -fiv
fi
PKG_CONFIG_PATH="${GWENHYWfar_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH:-}" ./configure \
  --prefix="$GWENHYWfar_PREFIX" \
  --with-gwen-dir="$GWENHYWfar_PREFIX"
make -j"$(nproc)"
make install
echo "AqBanking installed to $GWENHYWfar_PREFIX"

# Step 3: Configure GnuCash for GTK4
echo ""
echo "=== Step 3: Configure GnuCash (GTK4) ==="
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

PKG_CONFIG_PATH="${GWENHYWfar_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH:-}" \
cmake -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_PREFIX_PATH="$GWENHYWfar_PREFIX" \
    -DWITH_PYTHON=ON \
    -DGTK_MIN_VERSION=4.10.0 \
    "$GNUCASH_ROOT"

# Step 4: Build GnuCash
echo ""
echo "=== Step 4: Build GnuCash ==="
ninja -j"$(nproc)"

# Step 5: Run Tests (Headless)
echo ""
echo "=== Step 5: Run Tests ==="
# Check if Weston is available for Wayland headless
if command -v weston &> /dev/null; then
    echo "Running tests with Weston (Wayland)..."
    mkdir -p /tmp/weston-xdg
    chmod 700 /tmp/weston-xdg
    XDG_RUNTIME_DIR=/tmp/weston-xdg weston --backend=headless-backend.so --socket=wayland-test &
    WESTON_PID=$!
    sleep 1

    WAYLAND_DISPLAY=wayland-test GDK_BACKEND=wayland \
    CTEST_OUTPUT_ON_FAILURE=On ninja check || true

    kill "$WESTON_PID" || true
elif command -v xvfb-run &> /dev/null; then
    echo "Weston not found, running tests with Xvfb (fallback)..."
    GDK_BACKEND=x11 CTEST_OUTPUT_ON_FAILURE=On xvfb-run -a ninja check || true
else
    echo "WARNING: Neither Weston nor Xvfb available. Running tests without virtual display..."
    CTEST_OUTPUT_ON_FAILURE=On ninja check || true
fi

echo ""
echo "=== Validation Complete ==="
echo "Check build logs above for results."
