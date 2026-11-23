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
