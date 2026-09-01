#!/bin/bash -le

mkdir -p "$HOME"/.local/share

mkdir build
cd build
export TZ="America/Los_Angeles"
export PATH="$PATH:/usr/bin/core_perl"
export CTEST_OUTPUT_ON_FAILURE=On
export XDG_RUNTIME_DIR="$(mktemp -d)"
git config --global --add safe.directory /github/workspace
# Arch does not yet package the Gwenhywfar GTK4 frontend. Keep this job
# explicit until gwengui-gtk4 is available in the official repositories.
cmake /github/workspace -DWITH_PYTHON=ON -DWITH_AQBANKING=OFF -DCMAKE_BUILD_TYPE=debug -G Ninja
find /github/workspace/gnucash -type f \( -name '*.ui' -o -name '*.glade' \) -exec gtk4-builder-tool validate {} \;
ninja
trap 'cp Testing/Temporary/LastTest.log /github/workspace/LastTest.log 2>/dev/null || true' EXIT
dbus-run-session -- ninja check
