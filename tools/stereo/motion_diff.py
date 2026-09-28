#!/usr/bin/env python3
"""Compare eye L's and eye R's motion vectors from the layer's motion capture (ETERNALVR_CAPTURE_MOTION,
docs/rig-findings/stereo-motion-capture.md).

Each capture is <stem>-L.raw and <stem>-R.raw (the velocity image as the game holds it, row after row) with
<stem>.txt naming its format and size. For every capture the tool prints, per eye, the mean speed and the
moving pixels (speed above --moving), and "lost": the share of eye L's moving pixels where eye R's velocity is
exactly zero.

  With ETERNALVR_STEREO_SAME_VIEW=1 both eyes render the same view, so the two images should match pixel for
  pixel; a lost share above zero is an object whose motion eye R does not get.
  With per-eye views the frustums differ, so compare the eyes over regions rather than pixels
  (--box-l / --box-r, as y0,y1,x0,x1).

  python tools/stereo/motion_diff.py <folder> [--moving 1e-5] [--box-l y0,y1,x0,x1 --box-r y0,y1,x0,x1]

Needs numpy.
"""
import argparse
import glob
import os
import re

import numpy as np

FORMATS = {83: np.float16, 103: np.float32}  # VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R32G32_SFLOAT


def sidecar(path):
    """Format and size per eye from a capture's .txt file: {'L': (format, width, height), ...}."""
    eyes = {}
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = re.match(r'eye ([LR]): .*, VkFormat (\d+), (\d+)x(\d+), \d+ bytes', line)
            if m:
                eyes[m.group(1)] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
    return eyes


def load(path, fmt, width, height):
    dtype = FORMATS.get(fmt)
    if dtype is None:
        raise ValueError('VkFormat %d is not handled' % fmt)
    return np.fromfile(path, dtype=dtype).astype(np.float32).reshape(height, width, 2)


def box(text):
    y0, y1, x0, x1 = (int(v) for v in text.split(','))
    return np.s_[y0:y1, x0:x1]


def compare(left, right, moving, box_l=None, box_r=None):
    """Mean speed and moving pixels per eye, and the share of eye L's moving pixels eye R has at exactly 0."""
    a = left[box_l] if box_l is not None else left
    b = right[box_r] if box_r is not None else right
    speed_l = np.hypot(a[..., 0], a[..., 1])
    speed_r = np.hypot(b[..., 0], b[..., 1])
    result = {
        'mean_l': float(speed_l.mean()),
        'mean_r': float(speed_r.mean()),
        'moving_l': int((speed_l > moving).sum()),
        'moving_r': int((speed_r > moving).sum()),
        'lost': None,
    }
    if speed_l.shape == speed_r.shape:
        still = speed_l > moving
        result['lost'] = float(((speed_r == 0) & still).sum() / max(1, still.sum()))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('folder')
    parser.add_argument('--moving', type=float, default=1e-5)
    parser.add_argument('--box-l')
    parser.add_argument('--box-r')
    args = parser.parse_args()
    box_l = box(args.box_l) if args.box_l else None
    box_r = box(args.box_r) if args.box_r else box_l
    for txt in sorted(glob.glob(os.path.join(args.folder, '*.txt'))):
        eyes = sidecar(txt)
        if set(eyes) != {'L', 'R'}:
            continue
        stem = txt[:-4]
        left = load(stem + '-L.raw', *eyes['L'])
        right = load(stem + '-R.raw', *eyes['R'])
        r = compare(left, right, args.moving, box_l, box_r)
        lost = '-' if r['lost'] is None else '%.2f' % r['lost']
        print('%s  L %.2e (%d moving)  R %.2e (%d moving)  lost %s' %
              (os.path.basename(stem), r['mean_l'], r['moving_l'], r['mean_r'], r['moving_r'], lost))


if __name__ == '__main__':
    main()
