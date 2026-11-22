# Official FFmpeg regression, 2026-11-22

OneCloud S805, original Linux 5.15.79-meson, 64 MiB CMA. Tests temporarily
load idle candidate modules and restore the installed ones. No boot kernel,
DT, CMA or production container switch is performed.

## Encoder and quality

Encoder SHA-256:
`e72e3a6697b5f7296520f8b339b7c7e6af9147b299d0639e92c5e42128f4747b`.
The original unmodified production FFmpeg 7.0.2 runs the transcodes and
software validation.

[Continuous regression](continuous.json): 1813.749 seconds, 99 iterations,
11880 pictures. Every 120-picture output was counted and independently
software-decoded with no error. All 99 samples of four HTTP entry points and
Arcane passed. The original encoder was restored. This is a bounded soak,
not indefinite or arbitrary-stream acceptance.

[Quality matrix](QUALITY.json): each sample contains 90 pictures at 640×360,
30 fps. `run-quality.py` generates the same reference, compares all 90 decoded
pictures using official FFmpeg PSNR, and records the driver counters. Values
below are mean per-picture **luma** PSNR, not a full-image quality score.

| Pattern | Target Mbps | Actual Mbps | Mean Y PSNR dB |
| --- | ---: | ---: | ---: |
| static | 0.8 | 0.711 | 43.972 |
| static | 1.6 | 1.457 | 50.466 |
| static | 3.0 | 1.968 | 55.166 |
| motion | 0.8 | 1.314 | 36.699 |
| motion | 1.6 | 1.576 | 38.584 |
| motion | 3.0 | 2.956 | 39.853 |
| scene | 0.8 | 1.900 | 34.089 |
| scene | 1.6 | 1.943 | 35.204 |
| scene | 3.0 | 2.974 | 37.878 |

Hard low targets saturate; easy static pictures can undershoot at QP0 because
VBR adds no filler. These are measurements, not a strict CBR or arbitrary
peak guarantee. No new control algorithm was inferred from just nine samples.

## Decoder and diagnostic corrections

The candidate arms parser completion and queued-input state before the fetch
kick. `tests/test_esparser_order.py` reproduces an immediate IRQ using the
actual driver function. The diagnostic client now counts nonempty pictures
and uses each codec's picture boundaries rather than H.264 rules for all
formats. Build it statically for the older target libc.

[PARSER.json](PARSER.json) preserves the initial failed test: default
multithreaded rawvideo/framemd5 output truncated to 146 pictures after an
initial complete run; the newly built diagnostic client also failed to start
because it was dynamically linked against GLIBC_2.34. Neither result is a
successful legacy-codec acceptance.

[Original-module comparison](ORIGINAL-COMPARE.json) reproduces 145/146-picture
prefixes without the candidate, with FFmpeg warning that all capture buffers
are held by userspace. Passthrough timestamps alone do not fix it. Setting
`-filter_threads 1` globally and `-threads:v 1` on rawvideo output avoids
retaining all capture buffers. This uses standard options and changes no
FFmpeg code. It must not be conflated with the older rare 299/300 event.

[Single-thread original comparison](TUNED-COMPARE.json) passes 3 × 300
complete picture hashes. [Candidate H.264 rerun](PARSER-FINAL.json) passes
15 × 300 complete picture hashes with the same standard options, original
modules restored and production start time unchanged. Its HEVC attempts
report an unavailable wrapper in the host build, rather than a silicon
failure. Its legacy runs precede the diagnostic EOS/LAST correction and
must not establish a fixed tail-count limit.

## EOS/LAST and HEVC cross-check

[Detailed comparison](DRAIN-HEVC.json) uses a statically linked client and
waits for the LAST buffer, since an EOS event can precede its dequeue. The
original and candidate still show MPEG-2 149/150 and 89/90 variability. A
complete 150-picture B-frame run matches every software picture in order;
a complete 90-picture run matches the recorded visible MD5
`f555b2b0373a7a052455c08d24c59391`. Short runs fail those content checks.
MPEG-4 delivers 88/90 and VC-1 six coded pictures in this bounded set.
The report is deliberately **partial**, not a legacy-tail fix.

The same report validates HEVC using an independent **unmodified upstream**
7.0.2 build (host FFmpeg omits that wrapper). SHA-256:
`c22f5876df20b0db5bb878234224f2fcfff55814ca853d2d3262738095d375a0`.
Both 1080p and left-16 crop inputs pass twice, each 30/30 reference hashes.
Original modules are restored, four HTTP probes return 200 and Arcane runs.
`run-drain-hevc.py` records the actual commands and content comparisons.

## Research kernel

[Series check](mainline-series.log) applies all 16 patches without fuzz and
passes strict checkpatch with zero errors/warnings. [Full build](mainline-build.log)
and [metadata](mainline.json) record Linux 7.3.4 multi_v7_defconfig,
944 modules and vmlinux, with no warnings. Decoder module SHA-256:
`0917020ac16254462ade32f72f008e4dc0fd8eec91bfa47624d2802200d32aeb`.
This is compile-level evidence only. The research series still predates the
runtime independent frame pool; another board/kernel needs the pool port
and actual hardware acceptance.
