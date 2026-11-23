#!/usr/bin/env python3
"""Line a Meson8b capture dump up against a software decode, frame by frame.

MPEG-4 part 2 hardware and software output are not bit-identical (IDCT
implementation), so exact hashes never match. Each frame is reduced to the mean
luma of an 8x8 grid over the visible area instead, which survives that
difference and still tells a recovered picture from a repeated one.

The current driver returns NV12M with 32-byte stride alignment, luma sized
ALIGN(ALIGN(w,32)*ALIGN(h,32),64K), and half-sized chroma. Historical 64-byte
layouts require --canvas-align 64. A software decode is plain yuv420p by default.
Raw comparisons check every visible Y/U/V sample as well as picture identity;
signature-only comparisons cannot establish pixel correctness.

    # on the box, hash the dump into a small text file
    m4v-frame-compare.py --signature --canvas dump.nv12 > hw.txt
    # on the host, against a software decode of the same stream
    m4v-frame-compare.py hw.txt ref.yuv --size 320x240

Prints the reference frame each capture lines up with, then a summary. Exit
status is 1 unless every capture frame matched a distinct reference frame in
order. `--self-test` checks the geometry on a synthetic canvas.
"""
import argparse
import math
from pathlib import Path
import sys


def align(v, a):
    return (v + a - 1) // a * a


def canvas_planes(w, h, alignment=32):
    """Return (luma_bytes, chroma_bytes) of one canvas frame per plane."""
    luma = align(align(w, alignment) * align(h, alignment), 64 * 1024)
    return luma, luma // 2


def frame_size(w, h, canvas, alignment=32):
    luma, chroma = canvas_planes(w, h, alignment)
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


def luma_frames(data, w, h, canvas, alignment=32):
    """Each frame's visible luma plane, canvas rows and stride removed."""
    size = frame_size(w, h, canvas, alignment)
    if len(data) % size:
        raise ValueError(f'truncated raw output: {len(data)} bytes is not a multiple of {size}')
    stride = align(w, alignment) if canvas else w
    return [b"".join(data[i * size + y * stride:i * size + y * stride + w]
                     for y in range(h))
            for i in range(len(data) // size)]


def signatures(data, w, h, canvas, alignment=32):
    return [grid_signature(p, w, h) for p in luma_frames(data, w, h, canvas, alignment)]


def visible_planes(data, w, h, canvas, alignment=32, pixel_format='yuv420p'):
    """Y/U/V samples in visible raster order; capture canvas output is NV12M."""
    luma = canvas_planes(w, h, alignment)[0] if canvas else w * h
    stride = align(w, alignment) if canvas else w
    size = frame_size(w, h, canvas, alignment)
    y_frames = luma_frames(data, w, h, canvas, alignment)
    frames = []
    for i, y in enumerate(y_frames):
        start = i * size + luma
        if canvas or pixel_format == 'nv12':
            uv = b''.join(data[start + row * stride:start + row * stride + w]
                          for row in range(h // 2))
            u, v = uv[::2], uv[1::2]
        else:
            u = data[start:start + w * h // 4]
            v = data[start + w * h // 4:start + w * h // 2]
        frames.append((y, u, v))
    return frames


def plane_metrics(a, b):
    if not a or len(a) != len(b):
        raise ValueError('pixel planes must be nonempty and have equal lengths')
    differences = [abs(x - y) for x, y in zip(a, b)]
    mad = sum(differences) / len(a)
    mse = sum(d * d for d in differences) / len(a)
    psnr = math.inf if not mse else 10 * math.log10(255 * 255 / mse)
    return mad, psnr


def band_errors(a, b, w, h, bands):
    """Mean absolute luma error of `bands` equal row bands of two planes."""
    out = []
    for k in range(bands):
        y0, y1 = k * h // bands, (k + 1) * h // bands
        lo, hi = y0 * w, y1 * w
        diff = sum(abs(x - y) for x, y in zip(a[lo:hi], b[lo:hi]))
        out.append(diff // (hi - lo))
    return out


def read_signatures(path, w, h, canvas, alignment=32):
    """Signatures from a raw frame file, or from lines of 64 numbers."""
    data = Path(path).read_bytes()
    size = frame_size(w, h, canvas, alignment)
    if canvas or data and len(data) % size == 0:
        return signatures(data, w, h, canvas, alignment)
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
        try:
            cells = [int(x) for x in line.split()]
        except ValueError as error:
            raise ValueError('malformed signature row') from error
        if len(cells) != 64 or any(not 0 <= cell <= 255 for cell in cells):
            raise ValueError('signature row must contain 64 values in 0..255')
        out.append(cells)
    return out


def distance(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) // len(a)


def self_test(w=320, h=240):
    """A synthetic canvas with known content must read back as its own grid."""
    luma_bytes, chroma_bytes = canvas_planes(w, h)
    stride = align(w, 32)
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
    ap.add_argument('--canvas-align', type=int, choices=(32, 64), default=32,
                    help='capture stride/height alignment; 64 for historical dumps')
    ap.add_argument('--ref-format', choices=('yuv420p', 'nv12'), default='yuv420p')
    ap.add_argument('--max-mad', type=float, default=2,
                    help='maximum mean absolute sample error of each raw Y/U/V plane')
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
    try:
        w, h = (int(x) for x in a.size.split("x"))
    except ValueError:
        ap.error('--size must be WIDTHxHEIGHT')
    if w < 8 or h < 8 or w % 2 or h % 2 or a.tolerance < 0 or \
            not math.isfinite(a.max_mad) or a.max_mad < 0 or not 0 <= a.bands <= h:
        ap.error('need even dimensions >= 8, nonnegative tolerances and bands <= height')
    if a.self_test:
        self_test(w, h)
        return 0
    if a.signature:
        if not a.hw:
            ap.error("--signature needs a file")
        data = open(a.hw, "rb").read()
        if not data:
            raise ValueError('empty raw output')
        for sig in signatures(data, w, h, a.canvas, a.canvas_align):
            print(" ".join(str(c) for c in sig))
        return 0
    if not a.hw or not a.ref:
        ap.error("hw and ref are required unless --self-test/--signature")

    hwp, refp, hw_pixels, ref_pixels = None, None, None, None
    if a.canvas:
        hw_data, ref_data = Path(a.hw).read_bytes(), Path(a.ref).read_bytes()
        hw_pixels = visible_planes(hw_data, w, h, True, a.canvas_align)
        ref_pixels = visible_planes(ref_data, w, h, False, pixel_format=a.ref_format)
        hwp = [planes[0] for planes in hw_pixels]
        refp = [planes[0] for planes in ref_pixels]
    hwc = [grid_signature(p, w, h) for p in hwp] if hwp else \
        read_signatures(a.hw, w, h, a.canvas, a.canvas_align)
    refc = [grid_signature(p, w, h) for p in refp] if refp else \
        read_signatures(a.ref, w, h, False)

    used, first_bad, previous, out_of_order, pixel_errors = set(), None, -1, [], []
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
            if best[1] <= previous:
                out_of_order.append(i)
            previous = best[1]
            if hw_pixels is not None:
                metrics = [plane_metrics(hw_pixels[i][plane], ref_pixels[best[1]][plane])
                           for plane in range(3)]
                print('        ' + ' '.join(f'{name}:MAD={mad:.3f},PSNR={psnr:.3f}'
                                          for name, (mad, psnr) in zip('YUV', metrics)))
                if any(mad > a.max_mad for mad, _ in metrics):
                    pixel_errors.append(i)
            if a.bands and hwp and best[1] < len(refp):
                bands = band_errors(hwp[i], refp[best[1]], w, h, a.bands)
                print(f"        bands {bands}")
        else:
            near = "" if best is None else f", nearest ref {best[1]} delta {best[0]}"
            print(f"hw {i:3d} -> no match{near}")
            if a.bands and hwp and best is not None and best[1] < len(refp):
                bands = band_errors(hwp[i], refp[best[1]], w, h, a.bands)
                print(f"        bands {bands} (nearest)")
            first_bad = i if first_bad is None else first_bad

    missing = [j for j in range(len(refc)) if j not in used]
    extra = len(hwc) - len(used)
    print(f"SUMMARY hw_frames={len(hwc)} ref_frames={len(refc)} "
          f"matched={len(used)} unmatched={extra} first_unmatched={first_bad} "
          f"missing_ref={missing[:16]}{'...' if len(missing) > 16 else ''} "
          f"out_of_order={out_of_order} pixel_errors={pixel_errors} "
          f"pixel_checked={hw_pixels is not None}")
    return 0 if hwc and refc and not missing and extra == 0 and \
        not out_of_order and not pixel_errors else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        sys.exit(f'FAIL: {error}')
