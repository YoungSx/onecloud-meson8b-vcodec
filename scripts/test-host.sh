#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
mkdir -p build/host-tests
python3 scripts/extract-firmware.py --fetch
python3 -m unittest discover -s tests -v
python3 -I scripts/m4v-frame-compare.py --self-test
shellcheck scripts/*.sh
for test in copy window capture pool jpeg esparser_padding hevc_sps hevc_tiles hevc_picture_info h263_header h263_vlc wmv3_header rate_control rate_limit h264_sps ge2d_layout ge2d_scale; do
    cc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
      -fno-pie -no-pie "tests/test_$test.c" -o "build/host-tests/$test" -lm
    if [[ "$test" == hevc_sps ]]; then
        "build/host-tests/$test" tests/fixtures/hevc-1080p-sps.hevc \
          tests/fixtures/hevc-dpb16-sps.hevc
    elif [[ "$test" == h263_vlc ]]; then
        "build/host-tests/$test" tests/fixtures/h263-baseline-128x96.h263 \
          tests/fixtures/h263-plus-164x100.h263
    else
        "build/host-tests/$test"
    fi
done
# The swap body needs arm_neon.h, which an x86 host does not have; the ARM
# cross compiler does, so probe with whatever cc resolves and skip on x86.
if printf '#include <arm_neon.h>\n' | cc -E -x c - >/dev/null 2>&1; then
    cc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Ivdec \
      tests/test_swap_neon.c -o build/host-tests/swap_neon
    build/host-tests/swap_neon
else
    echo 'skip: no arm_neon.h on this host' >&2
fi
for client in encode-test overflow-queue ge2d-copy; do
    cc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined "tests/$client.c" \
      -o "build/host-tests/$client"
done
build/host-tests/encode-test -g -p scene - - - 96 64 70 NV12 90 H264 > /dev/null
python3 scripts/check-docs.py
