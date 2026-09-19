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
bash -c 'source "$1"; platform=arch-20260913.0.592969; gtk_version=4.22.5; select_source_contract; [[ "$platform" == arch-20260913.0.592969 && "$gtk_commit" == bd25f1e2dc2c2fbf3b8864e61afe642910ba359c && "$gtk_tag_object" == f93ed25dc632f2161fed4aac66602ec2d293317a && "$column_source_sha" == 36a89d49ee871ce33389135b867f15f6ee19e633a479dbb29f6f6829fc13cc13 && "$window_source_sha" == 41f3a813238ca06e79534ad115f8a7c18df8a35b80f62f621df514d9b222d1bc ]]' \
    _ "$helper"
if bash -c 'source "$1"; platform=fedora-42; gtk_version=4.22.5; select_source_contract' \
    _ "$helper" 2>/dev/null; then
    echo 'unsupported platform/version was accepted' >&2
    exit 1
fi

tag_repo="$temp_root/tag-repo"
git init --quiet "$tag_repo"
git -C "$tag_repo" config user.name 'GTK overlay self-test'
git -C "$tag_repo" config user.email 'gtk-overlay@example.invalid'
git -C "$tag_repo" config core.autocrlf false
printf 'tag fixture\n' >"$tag_repo/tracked"
git -C "$tag_repo" add tracked
git -C "$tag_repo" commit --quiet -m fixture
git -C "$tag_repo" tag --annotate 4.22.5 -m fixture
tag_commit="$(git -C "$tag_repo" rev-parse HEAD)"
tag_object="$(git -C "$tag_repo" rev-parse refs/tags/4.22.5)"
bash -c 'source "$1"; gtk_version=4.22.5; gtk_commit="$2"; gtk_tag_object="$3"; verify_source_pin "$4"' \
    _ "$helper" "$tag_commit" "$tag_object" "$tag_repo"
git -C "$tag_repo" tag 4.22.5-lightweight
if bash -c 'source "$1"; gtk_version=4.22.5-lightweight; gtk_commit="$2"; gtk_tag_object="$2"; verify_source_pin "$3"' \
    _ "$helper" "$tag_commit" "$tag_repo" 2>"$temp_root/lightweight-tag-error"; then
    echo 'lightweight GTK tag was accepted as annotated' >&2
    exit 1
fi
grep -Fq 'GTK tag object type mismatch' "$temp_root/lightweight-tag-error"
echo 'GTK overlay self-test passed'
