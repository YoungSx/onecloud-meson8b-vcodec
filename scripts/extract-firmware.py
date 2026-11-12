#!/usr/bin/env python3
"""Reproduce the vendor H.264 IMEM and swap-image layout, without executing C."""
import argparse
import hashlib
import json
import pathlib
import re
import struct
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent


def parse_words(text, expected_words):
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    match = re.fullmatch(
        r"\s*const\s+u32\s+MicroCode\[\]\s+__attribute__\s*"
        r"\(\(aligned\s*\(8\)\)\)\s*=\s*\{([^{}]*)\}\s*;\s*",
        text,
    )
    if not match:
        raise ValueError("Unexpected microcode declaration")
    tokens = match[1].strip().rstrip(",").split(",")
    # Each pinned vendor array has one trailing zero outside memcpy's length.
    if len(tokens) != expected_words + 1 or any(
        not re.fullmatch(r"\s*0x[0-9a-fA-F]{1,8}\s*", token) for token in tokens
    ):
        raise ValueError("Unexpected microcode length or non-hexadecimal word")
    if int(tokens[-1], 16) != 0:
        raise ValueError("Nonzero vendor sentinel")
    return struct.pack("<" + "I" * expected_words, *(int(t, 16) for t in tokens[:-1]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true", help="Download missing pinned source files")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "firmware/sources.json").read_text())
    chunks = []
    for entry in manifest["files"]:
        path = ROOT / "sources/vendor" / entry["path"]
        if not path.exists() and args.fetch:
            path.parent.mkdir(parents=True, exist_ok=True)
            url = f'https://raw.githubusercontent.com/hardkernel/linux/{manifest["commit"]}/{entry["path"]}'
            with urllib.request.urlopen(url, timeout=60) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() != entry["sha256"]:
                raise ValueError(f"Source checksum mismatch: {path}")
            path.write_bytes(data)
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError(f"Source checksum mismatch: {path}")
        chunks.append(parse_words(data.decode(), entry["words"]))
    blob = b"".join(chunks)
    if len(blob) != 36 * 1024 or hashlib.sha256(blob).hexdigest() != manifest["output_sha256"]:
        raise ValueError("Firmware output checksum or layout mismatch")
    out = ROOT / "firmware/meson/vdec/meson8b_h264.bin"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    print(f"{hashlib.sha256(blob).hexdigest()}  {out} ({len(blob)} bytes)")


if __name__ == "__main__":
    main()
