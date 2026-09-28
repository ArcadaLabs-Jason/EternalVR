#!/usr/bin/env python3
"""Compare the eye pairs the layer captures (ETERNALVR_CAPTURE_EYES, docs/VR_STEREO.md).

Each pair is <stem>-L.png and <stem>-R.png. For every pair the tool prints how many pixels differ, the
largest and mean channel difference and the bounding box of the differing pixels, and with --disparity the
horizontal shift that best matches the two eyes in a band across the image centre.

  S1 (ETERNALVR_STEREO_SAME_VIEW=1): both eyes render the same view, so any difference is engine state that
  steps per render (particles, water, animated materials, exposure). Expect 0, or small local boxes.
  S2 (per-eye views): the eyes differ everywhere; the disparity is a few pixels at most for distant geometry
  and grows for near geometry, with eye L's content shifted right relative to eye R's.

  python tools/stereo/eye_diff.py <folder> [--threshold 8] [--disparity] [--diff-out <folder>]

Reads 8-bit RGB or RGBA PNGs (any filter); uses numpy when it is installed, pure Python otherwise (slow on
full-size images).
"""
import argparse
import glob
import os
import struct
import sys
import zlib

try:
    import numpy as np
except ImportError:  # pragma: no cover - the rig's venv has numpy
    np = None


def read_png(path):
    """Returns (width, height, channels, bytes) of an 8-bit RGB/RGBA PNG, rows tightly packed."""
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG')
    at, idat, width, height, channels = 8, [], 0, 0, 0
    while at < len(data):
        length, kind = struct.unpack('>I4s', data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if kind == b'IHDR':
            width, height, depth, colour, _, _, interlace = struct.unpack('>IIBBBBB', body)
            if depth != 8 or colour not in (2, 6) or interlace:
                raise ValueError(f'{path}: only 8-bit RGB/RGBA, not interlaced')
            channels = 3 if colour == 2 else 4
        elif kind == b'IDAT':
            idat.append(body)
        elif kind == b'IEND':
            break
        at += 12 + length
    raw = zlib.decompress(b''.join(idat))
    stride = width * channels
    out = bytearray(stride * height)
    prev = bytearray(stride)
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, channels, bytes(out)


def write_png(path, width, height, rgb):
    """8-bit RGB PNG of tightly packed rows (filter 0, zlib level 6)."""
    rows = b''.join(b'\x00' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))

    header = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(rows, 6)) +
                chunk(b'IEND', b''))


def rgb_of(image):
    width, height, channels, data = image
    if channels == 3:
        return data
    return bytes(b for i, b in enumerate(data) if i % 4 != 3)


def compare(left, right, threshold):
    """Differences of two same-size images: dict with counts, maxima and the bounding box."""
    (w, h, _, _), (w2, h2, _, _) = left, right
    if (w, h) != (w2, h2):
        raise ValueError(f'sizes differ: {w}x{h} and {w2}x{h2}')
    a, b = rgb_of(left), rgb_of(right)
    if np is not None:
        d = np.abs(np.frombuffer(a, np.uint8).astype(np.int16) - np.frombuffer(b, np.uint8).astype(np.int16))
        d = d.reshape(h, w, 3).max(axis=2)
        mask = d > threshold
        ys, xs = np.nonzero(mask)
        box = (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())) if len(xs) else None
        return {'differing': int(mask.sum()), 'pixels': w * h, 'max': int(d.max()), 'mean': float(d.mean()),
                'box': box, 'diff': d}
    differing, dmax, total, box = 0, 0, 0, None
    per_pixel = []
    for p in range(w * h):
        v = max(abs(a[3 * p + c] - b[3 * p + c]) for c in range(3))
        per_pixel.append(v)
        total += v
        dmax = max(dmax, v)
        if v > threshold:
            differing += 1
            x, y = p % w, p // w
            box = (x, y, x, y) if box is None else (min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y))
    return {'differing': differing, 'pixels': w * h, 'max': dmax, 'mean': total / (w * h), 'box': box,
            'diff': per_pixel}


def disparity(left, right, search=96):
    """Horizontal shift s (pixels) minimising |L(x) - R(x + s)| over a band across the image centre."""
    if np is None:
        return None
    w, h = left[0], left[1]

    def grey(img):
        rgb = np.frombuffer(rgb_of(img), np.uint8).reshape(h, w, 3).astype(np.float32)
        return rgb.mean(axis=2)

    band = slice(h * 2 // 5, h * 3 // 5)
    gl, gr = grey(left)[band], grey(right)[band]
    best, best_err = 0, None
    for s in range(-search, search + 1):
        lo, hi = max(0, -s), min(w, w - s)
        err = float(np.abs(gl[:, lo:hi] - gr[:, lo + s:hi + s]).mean())
        if best_err is None or err < best_err:
            best, best_err = s, err
    return best, best_err


def pairs(folder):
    for left in sorted(glob.glob(os.path.join(folder, '*-L.png'))):
        right = left[:-6] + '-R.png'
        if os.path.exists(right):
            yield os.path.basename(left[:-6]), left, right


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('folder')
    ap.add_argument('--threshold', type=int, default=8, help='channel difference counted as a change')
    ap.add_argument('--disparity', action='store_true', help='estimate the horizontal shift between the eyes')
    ap.add_argument('--search', type=int, default=96, help='largest shift tried by --disparity (pixels)')
    ap.add_argument('--diff-out', help='folder for amplified difference images')
    args = ap.parse_args(argv)
    found = 0
    for stem, lpath, rpath in pairs(args.folder):
        found += 1
        left, right = read_png(lpath), read_png(rpath)
        r = compare(left, right, args.threshold)
        share = 100.0 * r['differing'] / r['pixels']
        line = (f"{stem}: {r['differing']} of {r['pixels']} pixels differ ({share:.3f}%), max {r['max']}, "
                f"mean {r['mean']:.3f}, box {r['box']}")
        if args.disparity:
            d = disparity(left, right, args.search)
            line += '' if d is None else f', disparity {d[0]} px (error {d[1]:.2f})'
        print(line)
        if args.diff_out:
            os.makedirs(args.diff_out, exist_ok=True)
            w, h = left[0], left[1]
            if np is not None:
                grey = np.clip(r['diff'].astype(np.int32) * 8, 0, 255).astype(np.uint8)
                rgb = np.repeat(grey[:, :, None], 3, axis=2).tobytes()
            else:
                rgb = bytes(min(255, v * 8) for v in r['diff'] for _ in range(3))
            write_png(os.path.join(args.diff_out, stem + '-diff.png'), w, h, rgb)
    if not found:
        print(f'no *-L.png / *-R.png pairs in {args.folder}')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
