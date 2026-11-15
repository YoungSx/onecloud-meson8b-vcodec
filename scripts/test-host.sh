#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
mkdir -p build/host-tests
python3 scripts/extract-firmware.py --fetch
python3 -m unittest discover -s tests -v
shellcheck scripts/*.sh
for test in copy window jpeg esparser_padding hevc_sps rate_control h264_sps; do
    cc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
      -fno-pie -no-pie "tests/test_$test.c" -o "build/host-tests/$test" -lm
    if [[ "$test" == hevc_sps ]]; then
        "build/host-tests/$test" tests/fixtures/hevc-1080p-sps.hevc
    else
        "build/host-tests/$test"
    fi
done
cc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined tests/encode-test.c \
  -o build/host-tests/encode-test
build/host-tests/encode-test -g -p scene - - - 96 64 70 NV12 90 H264 > /dev/null
python3 scripts/check-docs.py
