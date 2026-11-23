# Parser fixture

`hevc-1080p-sps.hevc` contains only the SPS NAL unit from the generated 1080p HEVC acceptance sample. It describes coded 1920×1088, visible 1920×1080, DPB 5 and reorder 2. The host test also checks truncation and invalid prefixes.

`h263-baseline-128x96.h263` contains the first two synthetic I/P pictures generated
with official FFmpeg 6.1.1: `testsrc2=size=128x96:rate=30`, `-frames:v 3 -c:v h263
-f h263`. It is an 8271-byte codec test input, SHA256
`50b21c54c6f88481c424759b6477f19983149e93c8b64667e1e95c2974f6b240`.
No external media is included; the generated test pattern is distributed under
the repository GPL-2.0 license. `test_h263_vlc.c` checks the firmware intermediate
stream lengths and FNV hashes independently obtained from libamcodec's
`h263vld()` at `4eebd67b142837fbb418abf5390060d4639440ba`, plus truncation,
output capacity, geometry and state isolation. Intermediate output is not stored
as a fixture. Neither result executes hardware or proves decoded pixels.


`h263-plus-164x100.h263` contains three synthetic I/P pictures from official
FFmpeg 6.1.1: `testsrc2=size=164x100:rate=25`, `-frames:v 3 -c:v h263p
-threads 1 -g 30 -q:v 10 -f h263`. It exercises custom dimensions, custom clock
and the P-picture rounding bit. Size 4558 bytes; SHA256
`dd95332b0205759976f983f12e43a8b572729a458ca230a9ca8b2f8443bf22ed`.
The generated pattern uses the repository GPL-2.0 license. The C test checks
intermediate lengths/hashes from the same pinned libamcodec reference, derives
a UFEP=0 continuation without changing its macroblocks, and exercises a bounded
1916x1080 INTER picture with a GOB header. The header/format tests cover rejected
tools, truncation, session state, geometry and capacity. No output media or
hardware acceptance results are fixtures.
