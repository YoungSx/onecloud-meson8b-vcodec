#!/usr/bin/env python3
"""Reproduce the vendor microcode layouts, without executing C."""
import argparse
import hashlib
import json
import pathlib
import re
import struct
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent


def _words(text, expected_words, array):
    """Extract the hex words of a pinned vendor u32 array."""
    match = re.search(
        r"const\s+u32\s+" + array + r"\s*\[\s*\]\s*__attribute__\s*"
        r"\(\(aligned\s*\(8\)\)\)\s*=\s*\{([^{}]*)\}",
        text,
    )
    if not match:
        raise ValueError(f"Unexpected {array} declaration")
    tokens = match[1].strip().rstrip(",").split(",")
    # Each pinned vendor array has one trailing zero outside memcpy's length.
    if len(tokens) != expected_words + 1 or any(
        not re.fullmatch(r"\s*0x[0-9a-fA-F]{1,8}\s*", token) for token in tokens
    ):
        raise ValueError("Unexpected microcode length or non-hexadecimal word")
    if int(tokens[-1], 16) != 0:
        raise ValueError("Nonzero vendor sentinel")
    return struct.pack("<" + "I" * expected_words, *(int(t, 16) for t in tokens[:-1]))


def _load_vendor(entry, commit):
    """Fetch (or use the cached copy of) a pinned vendor source file."""
    path = ROOT / "sources/vendor" / entry["path"]
    if not path.exists() and ARGS.fetch:
        path.parent.mkdir(parents=True, exist_ok=True)
        url = f"https://raw.githubusercontent.com/hardkernel/linux/{commit}/{entry['path']}"
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError(f"Source checksum mismatch: {path}")
        path.write_bytes(data)
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != entry["sha256"]:
        raise ValueError(f"Source checksum mismatch: {path}")
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", data.decode(), flags=re.S)
    return _words(text, entry["words"], entry.get("array", "MicroCode"))


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

    for blob, out in outputs:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(blob)
        print(f"{hashlib.sha256(blob).hexdigest()}  {out} ({len(blob)} bytes)")


if __name__ == "__main__":
    main()
