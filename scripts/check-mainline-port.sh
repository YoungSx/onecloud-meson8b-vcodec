#!/usr/bin/env bash
# Verify that patches/research/ applies, in order, to a pristine kernel tree.
# Usage: bash scripts/check-mainline-port.sh /path/to/linux-7.3.4
# Only the touched files are copied, so the tree itself is never modified.
set -euo pipefail

tree=${1:?usage: $0 <pristine kernel source tree>}
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

patches=("$root"/patches/research/000*.patch)
[[ ${#patches[@]} -gt 0 ]] || { echo "no research patches" >&2; exit 1; }

files=$(sed -n 's|^+++ b/||p' "${patches[@]}" | sort -u)
while read -r f; do
    [[ -f $tree/$f ]] || { echo "missing in tree: $f" >&2; exit 1; }
    mkdir -p "$work/$(dirname "$f")"
    cp "$tree/$f" "$work/$f"
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
    if [[ -x $tree/scripts/checkpatch.pl ]]; then
        out=$(sed -n '/^diff --git/,$p' "$patch" |
              "$tree/scripts/checkpatch.pl" --no-tree --no-signoff --strict -)
        if ! grep -q "0 errors, 0 warnings" <<< "$out"; then
            echo "checkpatch: $name" >&2
            grep -E "^#|errors" <<< "$out" >&2
            exit 1
        fi
    fi
done

echo "PASS: ${#patches[@]} patches apply to $(basename "$tree") in order"
