# Capability source recheck, 2026-11-23

This is a source investigation and host experiment, with **no new board test**.
The [conclusions](../../docs/capability-recheck.md) distinguish hardware limits,
firmware/driver gaps and client integration. Runtime formats and deployed modules
were not changed.

[sources.json](sources.json) pins 37 downloaded inputs by URL and SHA256. The
Hardkernel tree at `5aed95d35d252cafc75ce613a3a0052285662de2` is not truncated;
its `m8b/ucode` symlink resolves to `../m8/ucode`. Encoder library branch names
were resolved to commits before source inspection. Their unrelated later encoder
IPs must not be described as S805 capabilities.
Current upstream GE2D and the later LibreELEC encoder were also resolved to
commits. Upstream still lacks the needed YUV/scaling/S805 integration; the
later macroblock CBR path is explicitly gated to GXTVBB and newer chips.

Direct corrections:

- `h263_mc` exists inside the MPEG-4 microcode file, and `vmpeg4.c` loads it.
  Official FFmpeg 7.0.2 and 8.1.3 both register/map H.263 V4L2 M2M decoding.
- AVS has microcode and a vendor driver; WMV3 has a mode in the VC-1 driver.
  Missing application/UAPI integration is not proof of absent silicon.
- Vendor AVC has per-instance queues, buffers and hardware scheduling. Multiple
  contexts share one engine; eight vendor slots do not promise eight streams.
- Vendor S805 HEVC has tile geometry, SAO and motion-prediction setup. Runtime
  rejection in this project is an implementation boundary.
- Vendor GE2D has canvas/YUV/NV12/endian configuration. Register bits alone do
  not prove actual VDEC SWAP64 conversion, DMA correctness or speed.
- B-picture constants exist in encoder headers, but an executable B/CABAC path
  was not found. The earlier inference from no separate B command was too strong.
- No applicable S805 HEVC encoder or reliable Main10 path was found. HEVC
  libraries found in this pass target `/dev/HevcEnc` and WAVE hardware.

## Reproduce

```sh
python3 evidence/capability-recheck-2026-11-23/verify.py --fetch
```

An existing cache can omit `--fetch`, or use `--cache /absolute/path`.
The script verifies all input hashes before compiling any vendor source.
It uses the existing firmware parser and independently compiles the C arrays,
extracts actual vendor GE2D functions with register writes replaced by arrays,
and executes the actual vendor tile geometry function. GE2D/tile experiments
run with ASan/UBSan. Generated source, executables, research-only IMEM images
and results stay under ignored `build/`, outside runtime firmware packaging.

[host-results.json](host-results.json) records:

- H.263: 2243 words / 8972 bytes; 16 KiB research image SHA256
  `70476be8dd48d02a9f72f6f9a5a2c79b9c035f7160c32cfcd9b80922ad31d8ce`.
- AVS: 2256 words / 9024 bytes; 16 KiB research image SHA256
  `dc9e1bc115305ba947f27b8231cb7ceef403d6f519650fe88607d30376fbd52f`.
- GE2D: Meson8b preprocessor branch selects source endian bit 7 and destination
  bit 23 without a YUV matrix; vendor NV12 secondary-output setup is reachable.
- Tiles: 30 CTUs / 4 uniform columns yields vendor `7/7/7/9` versus reference
  `7/8/7/8`. The parameter interpretation and actual firmware behavior still
  require a board test. This is not a deployed decoder defect claim.

[Repository host checks](host-tests.log) pass: 57 Python tests, C sanitizer
checks, ShellCheck and links in 76 Markdown files. A deliberately corrupted
source cache is rejected before any compilation. The first repository run
failed only because the new results link preceded copying its artifact; after
publishing that file, the complete `make test` run passed.

The first local GE2D harness omitted matrix constants and failed to compile;
including those definitions from the same pinned vendor header resolved it.
Some raw GitHub downloads returned 429 or stale/missing paths; corrected tree
paths and GitHub raw contents responses supplied the pinned bytes. No missing
download was treated as evidence of absent hardware.

Host checks cannot establish frame content, thermal behavior, CMA concurrency,
endian semantics or production stability. No private addresses, credentials or
production logs are part of these artifacts.
