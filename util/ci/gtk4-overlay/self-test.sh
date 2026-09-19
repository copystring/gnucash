#!/usr/bin/env bash
set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly helper="$script_dir/prepare.sh"
readonly action="$script_dir/../actions/patched-gtk4/action.yaml"
readonly arch_entrypoint="$script_dir/../actions/archlinux-test/entrypoint.sh"
readonly temp_root="$(mktemp -d "${RUNNER_TEMP:-/tmp}/gtk4-overlay-self-test.XXXXXX")"
trap 'rm -rf -- "$temp_root"' EXIT

entry="$temp_root/entry"
contract="$temp_root/contract"
mkdir -p "$entry/lib"
printf 'platform=test\nsource_commit=abc\n' >"$contract"
printf 'DSO sentinel\n' >"$entry/lib/libgtk-4.so.1"
dso_sha="$(sha256sum "$entry/lib/libgtk-4.so.1" | awk '{print $1}')"
{ cat "$contract"; printf 'dso_sha=%s\n' "$dso_sha"; } >"$entry/manifest.txt"
bash "$helper" verify-record "$entry" "$contract"

printf 'mutation\n' >>"$entry/lib/libgtk-4.so.1"
if bash "$helper" verify-record "$entry" "$contract"; then
    echo 'mutated DSO was accepted' >&2
    exit 1
fi
printf 'DSO sentinel\n' >"$entry/lib/libgtk-4.so.1"
printf 'unexpected=entry\n' >>"$entry/manifest.txt"
if bash "$helper" verify-record "$entry" "$contract"; then
    echo 'mutated manifest was accepted' >&2
    exit 1
fi

check_patch_hash()
{
    local patch="$1" expected="$2"
    [[ "$(sha256sum "$patch" | awk '{print $1}')" == "$expected" ]]
    [[ "$({ cat "$patch"; printf mutation; } | sha256sum | awk '{print $1}')" != "$expected" ]]
}
check_patch_hash "$script_dir/gtkcolumnview-focus-column-ref.patch" \
    '23046af144974f7a91d6a2cdad14f9de6764a667077bbb9dc6fd1a0611b3d5f9'
check_patch_hash "$script_dir/gtkwindow-deferred-focus-ref.patch" \
    '061dbca5582157bab664a9e7805d42f0cd83ec5f8d61470da2ad4705217623d8'

set +e
bash -c 'set -euo pipefail; key="$(bash -c "exit 23")"; printf "%s\n" "$key"' \
    >"$temp_root/fingerprint-output"
fingerprint_status=$?
set -e
[[ "$fingerprint_status" -eq 23 && ! -s "$temp_root/fingerprint-output" ]]
grep -Fq 'key="$(bash "$GITHUB_ACTION_PATH/../../gtk4-overlay/prepare.sh" fingerprint)"' "$action"

grep -Fq '#include <gtk/gtk.h>' "$helper"
! grep -Fq '#include <gtk/gtkversion.h>' "$helper"
grep -Fxq 'set -e' "$arch_entrypoint"
echo 'GTK overlay self-test passed'
