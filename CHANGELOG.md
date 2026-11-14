# Changelog

Versions follow SemVer and Conventional Commits. Until 1.0, releases are
pre-releases tied to the recorded OneCloud kernel and device-tree ABI.

## Unreleased

- Document the S805 HEVC encoding hardware boundary and evidence-backed
  H.264/JPEG extension paths; preserve both README languages and source provenance.

## v0.1.0 — 2026-11-14 (pre-release)

- Reorganize current installation, usage, architecture, build and validation
  guides; provide equivalent English and Simplified Chinese READMEs.
- Add host tests, firmware reproduction, ARM cross-build and package validation
  to CI; version tags prepare checked release drafts and source companions.
- Preserve the 2026-11-14 hardware acceptance and installed `bd1cb79` provenance.

## Initial hardware implementation — 2026-11-14

- Stateful H.264, HEVC Main 8-bit and legacy MPEG/VC-1/MJPEG decoding.
- HCODEC H.264/JPEG encoding, quality/keyframe controls and software CBR.
- 1080p H.264 B-frame input concurrent transcoding under 64 MiB CMA.
- Reversible runtime overlay installation and systemd autoload.
- Independent FFmpeg drain fixes and static v4l2-ctl; original boot files preserved.

See [validation](docs/validation.md) for codec limits and exact evidence.
