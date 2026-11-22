#!/bin/sh
# Run the offline CCF selection model; does not access any codec device.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-sign-compare   -fsanitize=address,undefined -fno-pie -no-pie   "$root/ccf-selection.c" -o "$work/ccf-selection"
"$work/ccf-selection"
