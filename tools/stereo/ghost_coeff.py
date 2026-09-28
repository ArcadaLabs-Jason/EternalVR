#!/usr/bin/env python3
"""How much of one eye's image shows up in the other at the same pixels (docs/VR_STEREO.md, Ghost check).

Under Route S the eyes are rendered one after the other into the same targets, so an effect that reads the
previous frame (TAA's history, for example) reads the other eye's image. That leaves a faint copy of the
other eye's view in each eye, displaced by the eye frustums' shift: the ghost of the owner's first headset
session. The eyes see different directions at the same pixel, so their structure is unrelated there; any
correlation at zero shift is such a copy.

For each captured pair (ETERNALVR_CAPTURE_EYES) both images are band-passed (a difference of Gaussians,
which keeps edges and texture but drops dither noise and the overall brightness) and eye L is regressed on
eye R at the same pixels: coefficient = <L, R> / <R, R>, and the same the other way round. A control
regresses on eye R shifted by 200 pixels. Without a ghost both coefficients are near the control (about
0.01 or less); the owner's TAA ghost measured 0.11 to 0.13.

  python tools/stereo/ghost_coeff.py <folder> [--pairs 12] [--sigma 2 10]

Needs numpy. Reads the 8-bit PNGs the layer writes.
"""
import argparse
import glob
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from eye_diff import read_png  # noqa: E402


def luminance(path):
    width, height, channels, data = read_png(path)
    rgb = np.frombuffer(bytes(data), dtype=np.uint8).reshape(height, width, channels)[:, :, :3]
    return rgb.astype(np.float64) @ np.array([0.299, 0.587, 0.114])


def gaussian_blur(image, sigma):
    """Gaussian blur through the FFT (periodic edges, which the band-pass tolerates)."""
    h, w = image.shape
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.rfftfreq(w)[None, :]
    kernel = np.exp(-2.0 * (np.pi * sigma) ** 2 * (fx * fx + fy * fy))
    return np.fft.irfft2(np.fft.rfft2(image) * kernel, s=image.shape)


def band_pass(image, small, large):
    return gaussian_blur(image, small) - gaussian_blur(image, large)


def coefficients(left, right, small=2.0, large=10.0, control_shift=200):
    """(eye R's share in eye L, eye L's share in eye R, control) for one pair of luminance images."""
    lb = band_pass(left, small, large)
    rb = band_pass(right, small, large)
    shifted = np.roll(rb, control_shift, axis=1)
    return (float((lb * rb).sum() / (rb * rb).sum()), float((lb * rb).sum() / (lb * lb).sum()),
            float((lb * shifted).sum() / (shifted * shifted).sum()))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('folder')
    parser.add_argument('--pairs', type=int, default=12, help='the newest N pairs (default 12)')
    parser.add_argument('--sigma', type=float, nargs=2, default=(2.0, 10.0), metavar=('SMALL', 'LARGE'))
    args = parser.parse_args(argv)
    lefts = sorted(glob.glob(os.path.join(args.folder, '*-L.png')))[-args.pairs:]
    if not lefts:
        print(f'no *-L.png pairs in {args.folder}', file=sys.stderr)
        return 2
    results = []
    for left in lefts:
        right = left[:-len('-L.png')] + '-R.png'
        if not os.path.exists(right):
            continue
        c = coefficients(luminance(left), luminance(right), *args.sigma)
        results.append(c)
        print(f'{os.path.basename(left)[:-6]}: eye L <- R {c[0]:.3f}, eye R <- L {c[1]:.3f}, control {c[2]:.3f}')
    mean = np.mean(results, axis=0)
    print(f'mean of {len(results)} pair(s): eye L <- R {mean[0]:.3f}, eye R <- L {mean[1]:.3f}, '
          f'control {mean[2]:.3f}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
