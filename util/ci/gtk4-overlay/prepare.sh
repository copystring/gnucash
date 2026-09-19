#!/usr/bin/env bash
set -euo pipefail

# This builds only the version-matched GTK DSO needed by the existing GnuCash
# CTest jobs. It is not a replacement for distribution GTK package testing:
# the proven source profile deliberately disables optional GTK facilities.
readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly column_patch="$script_dir/gtkcolumnview-focus-column-ref.patch"
readonly window_patch="$script_dir/gtkwindow-deferred-focus-ref.patch"
readonly column_patch_sha='23046af144974f7a91d6a2cdad14f9de6764a667077bbb9dc6fd1a0611b3d5f9'
readonly window_patch_sha='061dbca5582157bab664a9e7805d42f0cd83ec5f8d61470da2ad4705217623d8'
readonly -a meson_options=(
    --buildtype=release --wrap-mode=nodownload
    -Dbuild-demos=false -Dbuild-examples=false -Dbuild-tests=false
    -Dbuild-testsuite=false -Ddocumentation=false -Dintrospection=disabled
    -Dx11-backend=true -Dwayland-backend=true -Dbroadway-backend=false
    -Dvulkan=disabled -Dmedia-gstreamer=disabled -Dprint-cpdb=disabled
    -Dprint-cups=disabled -Dcloudproviders=disabled -Dsysprof=disabled
    -Dtracker=disabled -Dcolord=disabled -Daccesskit=disabled
)

sha256()
{
    sha256sum "$1" | awk '{print tolower($1)}'
}

require_equal()
{
    local label="$1" expected="$2" actual="$3"
    if [[ "$actual" != "$expected" ]]; then
        printf '%s mismatch\n  expected: %s\n  actual:   %s\n' \
            "$label" "$expected" "$actual" >&2
        return 1
    fi
}

require_sha256()
{
    local label="$1" path="$2" expected="$3" actual
    actual="$(sha256 "$path")"
    require_equal "$label SHA256" "$expected" "$actual"
}

select_source_contract()
{
    case "$platform:$gtk_version" in
        ubuntu-26.04:4.22.4)
            gtk_commit='7f99ab1a26408b6499a18f353f081e3c0598ea5c'
            gtk_tag_object='442df0886e47cd15ecbdd267e706d08ea517eada'
            column_source_sha='36a89d49ee871ce33389135b867f15f6ee19e633a479dbb29f6f6829fc13cc13'
            window_source_sha='59c0c837334e3e0039c2799fb5df04827b9f78bd4518c65d25ae66afd28d41be'
            ;;
        arch-*:4.22.5)
            gtk_commit='bd25f1e2dc2c2fbf3b8864e61afe642910ba359c'
            gtk_tag_object='f93ed25dc632f2161fed4aac66602ec2d293317a'
            column_source_sha='36a89d49ee871ce33389135b867f15f6ee19e633a479dbb29f6f6829fc13cc13'
            window_source_sha='41f3a813238ca06e79534ad115f8a7c18df8a35b80f62f621df514d9b222d1bc'
            ;;
        *)
            printf 'Unsupported GTK overlay platform/version: %s (%s)\n' \
                "$platform" "$gtk_version" >&2
            return 1
            ;;
    esac
}

select_contract()
{
    local arch_gtk_package
    . /etc/os-release
    platform="$ID-${VERSION_ID:-rolling}"
    architecture="$(uname -m)"
    gtk_version="$(pkg-config --modversion gtk4)"
    select_source_contract || return
    if [[ "$ID" == arch ]]; then
        arch_gtk_package="$(pacman -Q gtk4)"
        if [[ "$arch_gtk_package" != 'gtk4 1:4.22.5-1' ]]; then
            printf 'Unsupported Arch GTK package: %s\n' "$arch_gtk_package" >&2
            exit 1
        fi
    fi
    [[ "$architecture" == x86_64 || "$architecture" == aarch64 ]]
    require_sha256 'ColumnView patch' "$column_patch" "$column_patch_sha"
    require_sha256 'GtkWindow patch' "$window_patch" "$window_patch_sha"
}

verify_source_pin()
{
    local source="$1" head tag_object tag_type tag_commit
    head="$(git -C "$source" rev-parse 'HEAD^{commit}')"
    tag_object="$(git -C "$source" rev-parse "refs/tags/$gtk_version")"
    tag_type="$(git -C "$source" cat-file -t "refs/tags/$gtk_version")"
    tag_commit="$(git -C "$source" rev-parse "refs/tags/$gtk_version^{commit}")"
    require_equal 'GTK checkout commit' "$gtk_commit" "$head"
    require_equal 'GTK annotated tag object' "$gtk_tag_object" "$tag_object"
    require_equal 'GTK tag object type' tag "$tag_type"
    require_equal 'GTK tag peeled commit' "$gtk_commit" "$tag_commit"
}

abi_inventory()
{
    local module value cc_machine cc_version
    cc_machine="$(cc -dumpmachine)" || return
    cc_version="$(cc -dumpfullversion)" || return
    printf 'cc=%s-%s\n' "$cc_machine" "$cc_version"
    value="$(ld --version)" || return
    printf 'ld=%s\n' "${value%%$'\n'*}"
    value="$(ldd --version)" || return
    printf 'libc=%s\n' "${value%%$'\n'*}"
    for module in meson ninja python3 pkg-config; do
        value="$($module --version)" || return
        printf '%s=%s\n' "$module" "$value"
    done
    for module in gtk4 glib-2.0 cairo pango harfbuzz gdk-pixbuf-2.0 \
        graphene-gobject-1.0 wayland-client x11 webkitgtk-6.0; do
        value="$(pkg-config --modversion "$module")" || return
        printf '%s=%s\n' "$module" "$value"
    done
    if command -v dpkg-query >/dev/null; then
        dpkg-query -W -f='${binary:Package}=${Version}\n' binutils build-essential gcc \
            libc6-dev libglib2.0-dev libgtk-4-dev libcairo2-dev libdrm-dev \
            libepoxy-dev libfontconfig-dev libfribidi-dev libgdk-pixbuf-2.0-dev \
            libgraphene-1.0-dev libharfbuzz-dev libjpeg-dev libpango1.0-dev \
            libpng-dev libtiff-dev libwayland-dev libwebkitgtk-6.0-dev \
            libxkbcommon-dev libx11-dev libxcursor-dev libxdamage-dev \
            libxext-dev libxfixes-dev libxi-dev libxinerama-dev libxrandr-dev \
            libxrender-dev meson ninja-build pkg-config sassc wayland-protocols
    else
        pacman -Q binutils gcc glibc glib2 glib2-devel gtk4 cairo pango harfbuzz \
            gdk-pixbuf2 graphene libdrm libepoxy fontconfig fribidi libjpeg-turbo \
            libpng libtiff wayland libxkbcommon libx11 libxcursor libxdamage \
            libxext libxfixes libxi libxinerama libxrandr libxrender \
            webkitgtk-6.0 meson ninja pkgconf sassc wayland-protocols
    fi
}

contract_text()
{
    local options_sha inventory_sha
    options_sha="$(printf '%s\n' "${meson_options[@]}" | sha256sum | awk '{print $1}')" || return
    inventory_sha="$(abi_inventory | sha256sum | awk '{print $1}')" || return
    printf 'platform=%s\narchitecture=%s\ngtk_version=%s\nsource_commit=%s\nsource_tag_object=%s\n' \
        "$platform" "$architecture" "$gtk_version" "$gtk_commit" "$gtk_tag_object"
    printf 'column_source_sha=%s\nwindow_source_sha=%s\ncolumn_patch_sha=%s\nwindow_patch_sha=%s\n' \
        "$column_source_sha" "$window_source_sha" "$column_patch_sha" "$window_patch_sha"
    printf 'meson_options_sha=%s\nabi_inventory_sha=%s\n' "$options_sha" "$inventory_sha"
}

fingerprint()
{
    local contract helper_sha
    select_contract
    contract="$(contract_text)" || return
    helper_sha="$(sha256 "${BASH_SOURCE[0]}")" || return
    {
        printf '%s\n' "$contract"
        printf 'helper_sha=%s\n' "$helper_sha"
    } | sha256sum | awk '{print $1}'
}

verify_record()
{
    local entry="$1" expected="$2" dso="$1/lib/libgtk-4.so.1" recorded_sha
    local expected_lines manifest_lines
    test -r "$entry/manifest.txt"
    test -r "$dso"
    expected_lines="$(wc -l <"$expected")"
    manifest_lines="$(wc -l <"$entry/manifest.txt")"
    [[ "$manifest_lines" -eq $((expected_lines + 1)) ]]
    while IFS= read -r line; do grep -Fx -- "$line" "$entry/manifest.txt" >/dev/null; done <"$expected"
    recorded_sha="$(sed -n 's/^dso_sha=//p' "$entry/manifest.txt")"
    [[ -n "$recorded_sha" && "$(sha256 "$dso")" == "$recorded_sha" ]]
}

install_dependencies()
{
    . /etc/os-release
    if [[ "$ID" == ubuntu && "$VERSION_ID" == 26.04 ]]; then
        sudo apt-get install --yes --no-install-recommends build-essential git \
            libglib2.0-dev libgtk-4-dev libcairo2-dev libdrm-dev libepoxy-dev \
            libfontconfig-dev libfribidi-dev libgdk-pixbuf-2.0-dev \
            libgraphene-1.0-dev libharfbuzz-dev libjpeg-dev libpango1.0-dev \
            libpng-dev libtiff-dev libwayland-dev libxkbcommon-dev libx11-dev \
            libxcursor-dev libxdamage-dev libxext-dev libxfixes-dev libxi-dev \
            libxinerama-dev libxrandr-dev libxrender-dev meson ninja-build \
            binutils pkg-config sassc wayland-protocols
    elif [[ "$ID" != arch ]]; then
        printf 'Unsupported GTK overlay distribution: %s\n' "$ID" >&2
        exit 1
    fi
}

build_overlay() (
    local entry="$1" expected="$2" temp_root source build staged jobs
    jobs="${GTK4_OVERLAY_JOBS:-3}"
    [[ "$jobs" =~ ^[1-3]$ ]]
    temp_root="$(mktemp -d "${RUNNER_TEMP:-/tmp}/gnucash-gtk4-overlay.XXXXXX")"
    trap 'rm -rf -- "$temp_root"' EXIT
    source="$temp_root/source"
    build="$temp_root/build"
    staged="$temp_root/staged"
    git -c core.autocrlf=false clone --depth 1 --branch "$gtk_version" \
        https://gitlab.gnome.org/GNOME/gtk.git "$source"
    verify_source_pin "$source"
    require_sha256 'gtkcolumnview.c preimage' \
        "$source/gtk/gtkcolumnview.c" "$column_source_sha"
    require_sha256 'gtkwindow.c preimage' \
        "$source/gtk/gtkwindow.c" "$window_source_sha"
    require_sha256 'ColumnView patch' "$column_patch" "$column_patch_sha"
    require_sha256 'GtkWindow patch' "$window_patch" "$window_patch_sha"
    git -C "$source" apply --check "$column_patch"
    git -C "$source" apply "$column_patch"
    git -C "$source" apply --check "$window_patch"
    git -C "$source" apply "$window_patch"
    git -C "$source" diff --check
    meson setup "$build" "$source" "${meson_options[@]}"
    meson compile -C "$build" -j "$jobs" gtk-4
    mkdir -p "$staged/lib" "$(dirname "$entry")"
    cp -L "$build/gtk/libgtk-4.so.1" "$staged/lib/libgtk-4.so.1"
    { cat "$expected"; printf 'dso_sha=%s\n' "$(sha256 "$staged/lib/libgtk-4.so.1")"; } \
        >"$staged/manifest.txt"
    mv "$staged" "$entry"
)

activate_overlay() (
    local cache_root="${GNC_GTK4_CACHE_ROOT:?}" fp entry expected webkit_dso ldd_output
    local loader_prefix="${RUNNER_TEMP:-/tmp}/gtk4-overlay-loader"
    local -a gtk_cflags
    select_contract
    fp="$(fingerprint)"
    entry="$cache_root/$fp"
    expected="$(mktemp "${RUNNER_TEMP:-/tmp}/gtk4-contract.XXXXXX")"
    trap 'rm -f -- "$expected" "$loader_prefix".*' EXIT
    contract_text >"$expected"
    if [[ -e "$entry" ]]; then verify_record "$entry" "$expected"; else build_overlay "$entry" "$expected"; fi
    verify_record "$entry" "$expected"
    printf 'GTK overlay platform: %s %s, system GTK %s\n' \
        "$platform" "$architecture" "$gtk_version"
    read -r -a gtk_cflags <<<"$(pkg-config --cflags gtk4)"
    printf '#include <gtk/gtk.h>\n#if GTK_MAJOR_VERSION != 4 || GTK_MINOR_VERSION != 22 || GTK_MICRO_VERSION != %s\n#error mismatched GTK headers\n#endif\n' \
        "${gtk_version##*.}" | cc -fsyntax-only "${gtk_cflags[@]}" -x c -
    readelf -d "$entry/lib/libgtk-4.so.1" | grep -E '\(SONAME\).+\[libgtk-4\.so\.1\]'
    ldd_output="$(ldd "$entry/lib/libgtk-4.so.1")"
    ! grep -F 'not found' <<<"$ldd_output"
    python3 - "$entry/lib/libgtk-4.so.1" "$gtk_version" <<'PY'
import ctypes
import os
import sys

gtk = ctypes.CDLL(sys.argv[1], mode=os.RTLD_NOW)
actual = tuple(getattr(gtk, f"gtk_get_{part}_version")() for part in ("major", "minor", "micro"))
expected = tuple(map(int, sys.argv[2].split(".")))
if actual != expected:
    raise SystemExit(f"GTK DSO version mismatch: expected {expected}, loaded {actual}")
print(f"Loaded GTK overlay DSO: {sys.argv[1]} version {'.'.join(map(str, actual))}")
PY
    webkit_dso="$(pkg-config --variable=libdir webkitgtk-6.0)/libwebkitgtk-6.0.so"
    test -r "$webkit_dso"
    rm -f -- "$loader_prefix".*
    env LD_LIBRARY_PATH="$entry/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
        LD_DEBUG=libs LD_DEBUG_OUTPUT="$loader_prefix" \
        python3 -c 'import ctypes, os, sys; ctypes.CDLL(sys.argv[1], mode=os.RTLD_NOW)' "$webkit_dso"
    grep -l -F "calling init: $entry/lib/libgtk-4.so.1" "$loader_prefix".* >/dev/null
    env LD_LIBRARY_PATH="$entry/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
        ldd "$webkit_dso" | grep -F "$entry/lib/libgtk-4.so.1"
    {
        printf 'GNC_PATCHED_GTK4_LIB=%s\n' "$entry/lib"
        printf 'GNC_PATCHED_GTK4_DSO=%s\n' "$entry/lib/libgtk-4.so.1"
        printf 'LD_LIBRARY_PATH=%s%s\n' "$entry/lib" "${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
    } >>"${GITHUB_ENV:?}"
)

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    case "${1:-}" in
        install-dependencies) install_dependencies ;;
        fingerprint) fingerprint ;;
        prepare) activate_overlay ;;
        verify-record) verify_record "${2:?}" "${3:?}" ;;
        *) echo "usage: $0 install-dependencies|fingerprint|prepare|verify-record ENTRY CONTRACT" >&2; exit 2 ;;
    esac
fi
