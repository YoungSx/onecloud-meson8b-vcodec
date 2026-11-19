#!/usr/bin/env python3
"""Localise how a joined run's pictures differ from the same segment alone.

    hash-diff.py SOLO_DUMP SOLO_HASH JOINED_DUMP JOINED_HASH FIRST

FIRST is the line of the joined hash file at which the repeated segment
starts (50 + 30 = 80 for a 320x240 / 1600x900 / 320x240 stream). Pictures
from there must match the solo run byte for byte; for each that does not,
the byte spans that differ are printed with the plane and row they fall in,
so padding noise can be told from picture content.

Dumps are indexed by line position, which is what FRAME_DUMP writes: the
hash file's first column counts only the pictures that had bytes.
"""
import sys

H = 240
STRIDE = 320              # ALIGN(width, 32)
LUMA = 131072             # amvdec_get_output_size_wh(320, 240)
FRAME_BYTES = LUMA + LUMA // 2


def spans(a, b):
    out = []
    for off in range(0, FRAME_BYTES, 8):
        if a[off:off + 8] == b[off:off + 8]:
            continue
        if out and out[-1][1] == off:
            out[-1][1] = off + 8
        else:
            out.append([off, off + 8])
    return out


def where(start):
    if start >= LUMA:
        plane, base = "chroma", LUMA
    else:
        plane, base = "luma", 0
    row = (start - base) // STRIDE
    rows = H if plane == "luma" else H // 2
    return f"{plane} row {row} {'visible' if row < rows else 'PADDING'}"


def main(solo_dump, solo_hash, joined_dump, joined_hash, first):
    solo = [line.split()[2] for line in open(solo_hash)]
    joined = [line.split() for line in open(joined_hash)]
    first, bad, at = int(first), 0, 0
    with open(solo_dump, "rb") as sd, open(joined_dump, "rb") as jd:
        for pos, (_, nbytes, h) in enumerate(joined):
            size, base = int(nbytes), at
            at += size
            ref_idx = pos - first
            if ref_idx < 0 or ref_idx >= len(solo) or h == solo[ref_idx]:
                continue
            bad += 1
            jd.seek(base)
            data = jd.read(size)
            sd.seek(ref_idx * FRAME_BYTES)
            ref = sd.read(FRAME_BYTES)
            if size != FRAME_BYTES:
                print(f"picture {pos} (solo {ref_idx}): {size} bytes, "
                      f"expected {FRAME_BYTES}")
                continue
            print(f"picture {pos} (solo {ref_idx}):")
            for start, end in spans(data, ref)[:6]:
                print(f"   {where(start)} [{start:#x}..{end:#x})")
    print(f"{bad} picture(s) differ")


if __name__ == "__main__":
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    main(*sys.argv[1:])
