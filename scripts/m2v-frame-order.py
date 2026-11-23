#!/usr/bin/env python3
"""Say which software picture each picture of a canvas dump is.

m4v-frame-compare.py reduces a frame to an 8x8 grid of integer means. On the
MPEG-2 B-frame stream two adjacent pictures share the same grid, so a dump
that lost picture 148 reads back as one that lost 149. This compares the
visible luma itself (every 4th byte) against the software pictures within
+-WINDOW positions and prints every capture that is not in its own place or
whose best match is worse than rounding.

    m2v-frame-order.py dump.nv12 sw.yuv --size 320x240
"""
import argparse
import importlib.util
import math
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'frame_compare', Path(__file__).with_name('m4v-frame-compare.py'))
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("hw", help="raw canvas NV12M dump")
    ap.add_argument("ref", help="software decode, plain yuv420p/nv12")
    ap.add_argument("--size", default="320x240")
    ap.add_argument("--window", type=int, default=2)
    ap.add_argument('--canvas-align', type=int, choices=(32, 64), default=32,
                    help='capture alignment; use 64 for historical dumps')
    ap.add_argument("--tolerance", type=float, default=0.5,
                    help="max luma MAD of an in-place match (default 0.5)")
    a = ap.parse_args()
    try:
        w, h = (int(x) for x in a.size.split("x"))
    except ValueError:
        ap.error('--size must be WIDTHxHEIGHT')
    if w < 2 or h < 2 or w % 2 or h % 2 or a.window < 0 or \
            not math.isfinite(a.tolerance) or a.tolerance < 0:
        ap.error('need positive even dimensions, nonnegative window and finite tolerance')

    ref = [plane[::4] for plane in compare.luma_frames(Path(a.ref).read_bytes(), w, h, False)]
    pictures = compare.luma_frames(Path(a.hw).read_bytes(), w, h, True, a.canvas_align)
    n = len(pictures)
    off = []
    for i, picture in enumerate(pictures):
        pic = picture[::4]
        candidates = range(max(0, i - a.window), min(len(ref), i + a.window + 1))
        matches = [(sum(abs(x - y) for x, y in zip(pic, ref[k])) / len(pic), k)
                   for k in candidates]
        if not matches:
            off.append((i, None, None))
            continue
        # Keep an equally good in-place match for visually identical adjacent pictures.
        d, k = min(matches, key=lambda match: (match[0], match[1] != i, match[1]))
        if k != i or d > a.tolerance:
            off.append((i, k, round(d, 3)))
    print(f"hw_frames={n} ref_frames={len(ref)} not_in_place={off}")
    return 1 if off or not n or not ref or n != len(ref) else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        raise SystemExit(f'FAIL: {error}')
