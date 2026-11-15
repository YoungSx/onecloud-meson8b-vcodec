#!/usr/bin/env python3
"""Reproduce the vendor microcode layouts, without executing C."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import struct
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent


def _words(text, expected_words, array, sentinel=True):
    """Extract the hex words of a pinned vendor u32 array."""
    match = re.search(
        r"const\s+(?:u32\s+" + array + r"\s*\[\s*\]\s*__attribute__\s*"
        r"\(\(aligned\s*\(8\)\)\)|unsigned\s+" + array + r"\s*\[\s*\])"
        r"\s*=\s*\{([^{}]*)\}",
        text,
    )
    if not match:
        raise ValueError(f"Unexpected {array} declaration")
    tokens = match[1].strip().rstrip(",").split(",")
    # Most pinned vendor arrays carry one trailing zero outside memcpy's
    # length; the mpeg4 arrays end on a real word instead.
    expected = expected_words + 1 if sentinel else expected_words
    if len(tokens) != expected or any(
        not re.fullmatch(r"\s*0x[0-9a-fA-F]{1,8}\s*", token) for token in tokens
    ):
        raise ValueError("Unexpected microcode length or non-hexadecimal word")
    if sentinel:
        if int(tokens[-1], 16) != 0:
            raise ValueError("Nonzero vendor sentinel")
        tokens = tokens[:-1]
    return struct.pack("<" + "I" * expected_words, *(int(t, 16) for t in tokens))


def _fetch(url, attempts=7):
    """Download a pinned file, retrying the transient failures GitHub returns
    for unauthenticated raw fetches (429 rate limiting, 5xx).

    CI runners share GitHub's egress and routinely hit 429 while fetching the
    vendor sources, so authenticate when a token is available and back off for
    about a minute (1+2+4+8+16+32 s) before giving up."""
    headers = {}
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"
    for attempt in range(attempts):
        try:
            request = urllib.request.Request(url, headers=headers)
            with urllib.request.urlopen(request, timeout=60) as response:
                return response.read()
        except urllib.error.HTTPError as error:
            if error.code != 429 and error.code < 500:
                raise
            if attempt == attempts - 1:
                raise
        except urllib.error.URLError:
            if attempt == attempts - 1:
                raise
        time.sleep(2 ** attempt)


def _load_vendor(entry, commit):
    """Fetch (or use the cached copy of) a pinned vendor source file."""
    path = ROOT / "sources/vendor" / entry["path"]
    if not path.exists() and ARGS.fetch:
        path.parent.mkdir(parents=True, exist_ok=True)
        url = f"https://raw.githubusercontent.com/hardkernel/linux/{commit}/{entry['path']}"
        data = _fetch(url)
        if hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError(f"Source checksum mismatch: {path}")
        path.write_bytes(data)
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != entry["sha256"]:
        raise ValueError(f"Source checksum mismatch: {path}")
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", data.decode(), flags=re.S)
    return _words(text, entry["words"], entry.get("array", "MicroCode"),
                  entry.get("sentinel", True))


def main():
    global ARGS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true", help="Download missing pinned source files")
    ARGS = parser.parse_args()
    manifest = json.loads((ROOT / "firmware/sources.json").read_text())

    # H.264: five MicroCode[] arrays concatenated -> 16K IMEM + 20K swap.
    h264 = manifest["h264"]
    blob = b"".join(_load_vendor(e, manifest["commit"]) for e in h264["files"])
    if len(blob) != 36 * 1024 or hashlib.sha256(blob).hexdigest() != h264["output_sha256"]:
        raise ValueError("H.264 firmware output checksum or layout mismatch")
    outputs = [(blob, ROOT / "firmware/meson/vdec/meson8b_h264.bin")]

    # MPEG-1/2: single vmpeg12_mc[] array, zero-padded to one 16K IMEM image
    # (vendor memcpy's MC_SIZE bytes regardless of the array's real length).
    mpeg12 = manifest["mpeg12"]
    blob = _load_vendor(mpeg12, manifest["commit"]).ljust(16 * 1024, b"\0")
    if hashlib.sha256(blob).hexdigest() != mpeg12["output_sha256"]:
        raise ValueError("MPEG-1/2 firmware output checksum or layout mismatch")
    outputs.append((blob, ROOT / "firmware/meson/vdec/meson8b_mpeg12.bin"))

    # AVC encoder: MicroCode[] of h264_enc_mix_dump_dblk.h (the vendor M8B
    # FULL-mode default, mix_dump_mc_dblk with enable_dblk=1), zero-padded to
    # one 16K IMEM image (vendor amvenc_loadmc memcpy's MC_SIZE bytes).
    avc_enc = manifest["avc_enc"]
    blob = _load_vendor(avc_enc, manifest["commit"]).ljust(16 * 1024, b"\0")
    if hashlib.sha256(blob).hexdigest() != avc_enc["output_sha256"]:
        raise ValueError("AVC encoder firmware output checksum or layout mismatch")
    outputs.append((blob, ROOT / "firmware/meson/venc/meson8b_avc_enc.bin"))

    # MPEG-4 part 2: vmpeg4_mc_5[] (the vendor MPEG4_5 variant used for
    # DivX/Xvid content), zero-padded to one 16K IMEM image.
    mpeg4 = manifest["mpeg4"]
    blob = _load_vendor(mpeg4, manifest["commit"]).ljust(16 * 1024, b"\0")
    if hashlib.sha256(blob).hexdigest() != mpeg4["output_sha256"]:
        raise ValueError("MPEG-4 firmware output checksum or layout mismatch")
    outputs.append((blob, ROOT / "firmware/meson/vdec/meson8b_mpeg4.bin"))

    # VC-1: the MicroCode[] body of the wrapper header (vc1_mc.c's
    # #define MicroCode vc1_mc include), zero-padded to one 16K image.
    vc1 = manifest["vc1"]
    blob = _load_vendor(vc1, manifest["commit"]).ljust(16 * 1024, b"\0")
    if hashlib.sha256(blob).hexdigest() != vc1["output_sha256"]:
        raise ValueError("VC-1 firmware output checksum or layout mismatch")
    outputs.append((blob, ROOT / "firmware/meson/vdec/meson8b_vc1.bin"))

    # MJPEG: vmjpeg_mc[], zero-padded to one 16K image.
    mjpeg = manifest["mjpeg"]
    blob = _load_vendor(mjpeg, manifest["commit"]).ljust(16 * 1024, b"\0")
    if hashlib.sha256(blob).hexdigest() != mjpeg["output_sha256"]:
        raise ValueError("MJPEG firmware output checksum or layout mismatch")
    outputs.append((blob, ROOT / "firmware/meson/vdec/meson8b_mjpeg.bin"))

    # JPEG encoder: jpeg_encoder_mc[] (the m8b ucode tree carries an
    # identical copy), zero-padded to one 16K IMEM image.
    jpeg_enc = manifest["jpeg_enc"]
    blob = _load_vendor(jpeg_enc, manifest["commit"]).ljust(16 * 1024, b"\0")
    if hashlib.sha256(blob).hexdigest() != jpeg_enc["output_sha256"]:
        raise ValueError("JPEG encoder firmware output checksum or layout mismatch")
    outputs.append((blob, ROOT / "firmware/meson/venc/meson8b_jpeg_enc.bin"))

    hevc = manifest["hevc"]
    blob = _load_vendor(hevc, manifest["commit"]).ljust(16 * 1024, b"\0")
    if hashlib.sha256(blob).hexdigest() != hevc["output_sha256"]:
        raise ValueError("HEVC firmware output checksum or layout mismatch")
    outputs.append((blob, ROOT / "firmware/meson/vdec/meson8b_hevc.bin"))

    for blob, out in outputs:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(blob)
        print(f"{hashlib.sha256(blob).hexdigest()}  {out} ({len(blob)} bytes)")


if __name__ == "__main__":
    main()
