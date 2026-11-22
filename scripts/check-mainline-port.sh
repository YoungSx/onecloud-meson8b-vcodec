#!/usr/bin/env bash
# Verify that patches/research/ applies, in order, to a pristine kernel tree.
# Usage: bash scripts/check-mainline-port.sh /path/to/linux-7.3.4
# Only the touched files are copied, so the tree itself is never modified.
set -euo pipefail

tree=$(realpath "${1:?usage: $0 <pristine kernel source tree>}")
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
[[ -x $tree/scripts/checkpatch.pl ]] ||
    { echo "no scripts/checkpatch.pl in $tree" >&2; exit 1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

patches=("$root"/patches/research/[0-9][0-9][0-9][0-9]-*.patch)
[[ ${#patches[@]} -gt 0 ]] || { echo "no research patches" >&2; exit 1; }

# Files a patch creates have no source in the tree, so they are only copied
# when they exist and created by the patch otherwise.
files=$(sed -n 's|^+++ b/||p' "${patches[@]}" | sort -u)
while read -r f; do
    if [[ -f $tree/$f ]]; then
        mkdir -p "$work/$(dirname "$f")"
        cp "$tree/$f" "$work/$f"
    elif ! grep -qs "^--- /dev/null$" "${patches[@]}"; then
        echo "missing in tree: $f" >&2
        exit 1
    fi
done <<< "$files"

cd "$work"
for patch in "${patches[@]}"; do
    name=$(basename "$patch")
    if sed -n '/^diff --git/,$p' "$patch" | patch -p1 -F0 --no-backup-if-mismatch; then
        echo "applied $name"
    else
        echo "FAILED $name" >&2
        exit 1
    fi
    # FILE_PATH_CHANGES only asks whether MAINTAINERS covers new files;
    # this driver's directory is already listed there as a whole.
    out=$(sed -n '/^diff --git/,$p' "$patch" |
          "$tree/scripts/checkpatch.pl" --no-tree --no-signoff --strict \
          --ignore FILE_PATH_CHANGES -) || true
    if ! grep -q "0 errors, 0 warnings" <<< "$out"; then
        echo "checkpatch: $name" >&2
        grep -E "^#|errors" <<< "$out" >&2
        exit 1
    fi
done

echo "PASS: ${#patches[@]} patches apply to $(basename "$tree") in order"
