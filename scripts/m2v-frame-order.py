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


def align(v, a):
    return (v + a - 1) // a * a


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("hw", help="raw canvas NV12M dump")
    ap.add_argument("ref", help="software decode, plain yuv420p/nv12")
    ap.add_argument("--size", default="320x240")
    ap.add_argument("--window", type=int, default=2)
    ap.add_argument("--tolerance", type=float, default=0.5,
                    help="max luma MAD of an in-place match (default 0.5)")
    a = ap.parse_args()
    w, h = (int(x) for x in a.size.split("x"))
    stride = align(w, 64)
    luma = align(stride * align(h, 64), 64 * 1024)
    size, rsize = luma + luma // 2, w * h * 3 // 2

    sw = open(a.ref, "rb").read()
    ref = [sw[k * rsize:k * rsize + w * h:4] for k in range(len(sw) // rsize)]
    raw = open(a.hw, "rb").read()
    n = len(raw) // size
    off = []
    for i in range(n):
        base = i * size
        pic = b"".join(raw[base + y * stride:base + y * stride + w]
                       for y in range(h))[::4]
        d, k = min((sum(abs(x - y) for x, y in zip(pic, ref[k])) / len(pic), k)
                   for k in range(max(0, i - a.window),
                                  min(len(ref), i + a.window + 1)))
        if k != i or d > a.tolerance:
            off.append((i, k, round(d, 3)))
    print(f"hw_frames={n} ref_frames={len(ref)} not_in_place={off}")
    return 1 if off or n != len(ref) else 0


if __name__ == "__main__":
    raise SystemExit(main())
