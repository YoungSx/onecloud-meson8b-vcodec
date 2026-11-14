# S805 encoder feasibility audit — 2026-11-14

[Conclusion and extension paths](../../docs/encoding-feasibility.md).
The original Amlogic S805 datasheet was read at PDF pages 7 and 9 from the
pinned mirror recorded in [sources.json](sources.json). H.265 is described
for decoding; the independent encoder formats are JPEG and H.264 (1080p30).
The mirror PDF SHA-256 was checked; no full proprietary PDF is republished.

The Hardkernel commit is 5aed95d35d252cafc75ce613a3a0052285662de2, root tree
2bc2ea309cc6d86d24ea006819c39b194aa61dce. The complete recursive tree was not
truncated and contained 51,448 blobs. The exact path query and matching
encoder/microcode inventory are in [vendor-inventory.json](vendor-inventory.json).
Nine inspected vendor files match both their recorded SHA-256 and Git blob
objects from that commit. The later GXL HEVC encoder reference matches its
Gitiles blob; it describes a separate WAVE420L register/IRQ interface.

No S805 HEVC encoding host/firmware path was found. This does not claim a
formal proof against all hypothetical CPU/hardware hybrid algorithms;
it establishes that a usable native HEVC encoder cannot currently be
implemented by adding platform data or swapping the decoder firmware.

The source inspection also finds MFDIN RGB/YUV input, fixed-slice and
macroblock-QP controls worth further development for AVC/JPEG. Register
presence is not hardware acceptance; each needs stream/content, layout,
error recovery and service/rollback tests before advertising support.

Verification: 12 Python tests, four C ASan/UBSan programs, ShellCheck and
36 local Markdown-file link checks passed. Driver/firmware/overlay code remains
unchanged; no new registers were tested on the production device.
