#!/usr/bin/env python3
"""Line a Meson8b capture dump up against a software decode, frame by frame.

MPEG-4 part 2 hardware and software output are not bit-identical (IDCT
implementation), so exact hashes never match. Each frame is reduced to the mean
luma of an 8x8 grid over the visible area instead, which survives that
difference and still tells a recovered picture from a repeated one.

The driver hands userspace whole canvas-aligned NV12M frames: luma is
ALIGN(ALIGN(w,64)*ALIGN(h,64), 64K) bytes, chroma half of that, rows padded out
to the 64-aligned stride. A software decode is plain yuv420p.

    # on the box, hash the dump into a small text file
    m4v-frame-compare.py --signature --canvas dump.nv12 > hw.txt
    # on the host, against a software decode of the same stream
    m4v-frame-compare.py hw.txt ref.yuv --size 320x240

Prints the reference frame each capture lines up with, then a summary. Exit
status is 1 unless every capture frame matched a distinct reference frame in
order. `--self-test` checks the geometry on a synthetic canvas.
"""
import argparse
import sys


def align(v, a):
    return (v + a - 1) // a * a


def canvas_planes(w, h):
    """Return (luma_bytes, chroma_bytes) of one canvas frame per plane."""
    luma = align(align(w, 64) * align(h, 64), 64 * 1024)
    return luma, luma // 2


def frame_size(w, h, canvas):
    luma, chroma = canvas_planes(w, h)
    return luma + chroma if canvas else w * h * 3 // 2


def grid_signature(plane, w, h):
    """Mean of each cell of an 8x8 grid over a w-wide, h-tall luma plane."""
    out = []
    for gy in range(8):
        y0, y1 = gy * h // 8, (gy + 1) * h // 8
        for gx in range(8):
            x0, x1 = gx * w // 8, (gx + 1) * w // 8
            total = 0
            for y in range(y0, y1):
                total += sum(plane[y * w + x0:y * w + x1])
            out.append(total // ((y1 - y0) * (x1 - x0)))
    return out


def luma_frames(data, w, h, canvas):
    """Each frame's visible luma plane, canvas rows and stride removed."""
    size = frame_size(w, h, canvas)
    stride = align(w, 64) if canvas else w
    return [b"".join(data[i * size + y * stride:i * size + y * stride + w]
                     for y in range(h))
            for i in range(len(data) // size)]


def signatures(data, w, h, canvas):
    return [grid_signature(p, w, h) for p in luma_frames(data, w, h, canvas)]


def band_errors(a, b, w, h, bands):
    """Mean absolute luma error of `bands` equal row bands of two planes."""
    out = []
    for k in range(bands):
        y0, y1 = k * h // bands, (k + 1) * h // bands
        lo, hi = y0 * w, y1 * w
        diff = sum(abs(x - y) for x, y in zip(a[lo:hi], b[lo:hi]))
        out.append(diff // (hi - lo))
    return out


def read_signatures(path, w, h, canvas):
    """Signatures from a raw frame file, or from lines of 64 numbers."""
    data = open(path, "rb").read()
    size = frame_size(w, h, canvas)
    if data and len(data) % size == 0:
        return signatures(data, w, h, canvas)
    try:
        text = data.decode()
    except UnicodeDecodeError:
        raise SystemExit(
            f"{path}: {len(data)} bytes is not a whole number of "
            f"{size}-byte {'canvas' if canvas else 'plain'} frames, and it is "
            f"not a signature file either")
    out = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        cells = [int(x) for x in line.split() if x.lstrip("-").isdigit()]
        if len(cells) == 64:
            out.append(cells)
    return out


def distance(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) // len(a)


def self_test(w=320, h=240):
    """A synthetic canvas with known content must read back as its own grid."""
    luma_bytes, chroma_bytes = canvas_planes(w, h)
    stride = align(w, 64)
    canvas = bytearray(luma_bytes + chroma_bytes)
    for y in range(h):
        canvas[y * stride:y * stride + w] = bytes((x + y) & 0xFF for x in range(w))
    frames = luma_frames(bytes(canvas), w, h, canvas=True)
    got = grid_signature(frames[0], w, h)
    plain = bytes(((x + y) & 0xFF) for y in range(h) for x in range(w))
    want = grid_signature(plain, w, h)
    assert got == want, (got[:4], want[:4])
    # A whole-frame mean can hide a defect confined to a few bands, so the
    # per-band error must see one: zero everywhere but the top band.
    bad = bytearray(plain)
    top = (h // 4) * w
    for i in range(top):
        bad[i] = (bad[i] + 40) & 0xFF
    bands = band_errors(bytes(bad), plain, w, h, 4)
    assert bands[0] > 20 and bands[1:] == [0, 0, 0], bands
    print("self-test ok: canvas dump reads back as its own grid, "
          "per-band error localises a band defect")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("hw", nargs="?")
    ap.add_argument("ref", nargs="?")
    ap.add_argument("--size", default="320x240")
    ap.add_argument("--canvas", action="store_true",
                    help="the hw file is a raw canvas dump, not signatures")
    ap.add_argument("--signature", action="store_true",
                    help="print the hw file's signatures and stop")
    ap.add_argument("--tolerance", type=int, default=2,
                    help="max mean luma difference for a match (default 2)")
    ap.add_argument("--bands", type=int, default=0,
                    help="also print the hw/ref error of N row bands, from the "
                         "raw canvas dump; a grid mean over the whole frame "
                         "hides a defect that only covers part of it")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    w, h = (int(x) for x in a.size.split("x"))
    if a.self_test:
        self_test(w, h)
        return 0
    if a.signature:
        if not a.hw:
            ap.error("--signature needs a file")
        data = open(a.hw, "rb").read()
        for sig in signatures(data, w, h, a.canvas):
            print(" ".join(str(c) for c in sig))
        return 0
    if not a.hw or not a.ref:
        ap.error("hw and ref are required unless --self-test/--signature")

    hwp, refp = None, None
    if a.bands and a.canvas:
        hwp = luma_frames(open(a.hw, "rb").read(), w, h, True)
        refp = luma_frames(open(a.ref, "rb").read(), w, h, False)
    hwc = [grid_signature(p, w, h) for p in hwp] if hwp else \
        read_signatures(a.hw, w, h, a.canvas)
    refc = [grid_signature(p, w, h) for p in refp] if refp else \
        read_signatures(a.ref, w, h, False)

    used, first_bad = set(), None
    for i, sig in enumerate(hwc):
        # Greedy: each reference answers for one capture. An extra capture past
        # the end has only used references left, and it is exactly the case the
        # summary exists to explain, so fall back to the nearest of all rather
        # than printing nothing.
        best = None
        for j, rsig in enumerate(refc):
            if j in used:
                continue
            d = distance(sig, rsig)
            if best is None or d < best[0]:
                best = (d, j)
        reuse = best is None
        if reuse:
            for j, rsig in enumerate(refc):
                d = distance(sig, rsig)
                if best is None or d < best[0]:
                    best = (d, j)
        if best and best[0] <= a.tolerance and not reuse:
            print(f"hw {i:3d} -> ref {best[1]:3d} (delta {best[0]})")
            used.add(best[1])
            if hwp and best[1] < len(refp):
                bands = band_errors(hwp[i], refp[best[1]], w, h, a.bands)
                print(f"        bands {bands}")
        else:
            near = "" if best is None else f", nearest ref {best[1]} delta {best[0]}"
            print(f"hw {i:3d} -> no match{near}")
            if hwp and best is not None and best[1] < len(refp):
                bands = band_errors(hwp[i], refp[best[1]], w, h, a.bands)
                print(f"        bands {bands} (nearest)")
            first_bad = i if first_bad is None else first_bad

    missing = [j for j in range(len(refc)) if j not in used]
    extra = len(hwc) - len(used)
    print(f"SUMMARY hw_frames={len(hwc)} ref_frames={len(refc)} "
          f"matched={len(used)} unmatched={extra} first_unmatched={first_bad} "
          f"missing_ref={missing[:16]}{'...' if len(missing) > 16 else ''}")
    return 0 if not missing and extra == 0 else 1


if __name__ == "__main__":
    sys.exit(main())