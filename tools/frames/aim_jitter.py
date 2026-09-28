#!/usr/bin/env python3
"""Pose timing and aim jitter from a frame table (eternalvr-frames-<pid>.csv, docs/rig-findings/aim-jitter.md).

Usage: aim_jitter.py <eternalvr-frames-*.csv>

Timing: how late each game view is first shown against the time its poses were predicted for
(display_minus_pose_time_ms), split by the game's view rate. Jitter (tables with the aim columns): the
angular noise of the head's forward and of the weapon hand's aim ray, as tracked and as used (smoothed), in
100 ms windows where the ray is roughly still: the RMS of each window's deviation from its own linear trend,
in degrees, reported as the median and the 95th percentile over the windows. Needs numpy.
"""
import sys

import numpy as np

WINDOW_S = 0.1
STILL_DEG_PER_S = 10.0


def forward(q):
    """-Z rotated by unit quaternions q (n x 4, x y z w)."""
    x, y, z, w = q[:, 0], q[:, 1], q[:, 2], q[:, 3]
    return np.stack([-(2 * (x * z + w * y)), -(2 * (y * z - w * x)), -(1 - 2 * (x * x + y * y))], axis=1)


def jitter(t, d):
    """Per-window RMS deviation (degrees) of unit directions d at times t from a linear fit, still windows only."""
    out = []
    start = t[0]
    while start < t[-1]:
        m = (t >= start) & (t < start + WINDOW_S)
        start += WINDOW_S
        if m.sum() < 5:
            continue
        tw, dw = t[m] - t[m].mean(), d[m]
        mean = dw.mean(axis=0)
        mean /= np.linalg.norm(mean)
        # Two tangent axes at the mean direction; small angles in radians along each.
        a = np.cross(mean, [0.0, 1.0, 0.0] if abs(mean[1]) < 0.9 else [1.0, 0.0, 0.0])
        a /= np.linalg.norm(a)
        b = np.cross(mean, a)
        ang = np.degrees(np.stack([dw @ a, dw @ b], axis=1))
        slope = np.polyfit(tw, ang, 1)
        if np.linalg.norm(slope[0]) > STILL_DEG_PER_S:
            continue
        resid = ang - np.outer(tw, slope[0]) - slope[1]
        out.append(np.sqrt(np.mean(np.sum(resid ** 2, axis=1))))
    return np.array(out)


def main(path):
    d = np.genfromtxt(path, delimiter=',', names=True)
    v = d[d['view'] > 0]
    t = (v['display_time_ns'] - v['display_time_ns'][0]) / 1e9
    seq = v['view']
    first = np.r_[True, np.diff(seq) != 0]
    late = v['display_minus_pose_time_ms']
    period = np.median(v['period_ms'])
    print(f'{len(v)} frames with a view over {t[-1]:.0f} s, period {period:.2f} ms')
    print('first showing of a view, late by (ms): p5/p50/p95 by the game\'s view rate')
    for lo, hi in [(0, 70), (70, 85), (85, 120), (120, 10000)]:
        rows = []
        for w0 in np.arange(0, t[-1], 10.0):
            m = (t >= w0) & (t < w0 + 10.0)
            if m.sum() < 100:
                continue
            rate = (seq[m].max() - seq[m].min()) / 10.0
            if lo <= rate < hi:
                rows.append(m)
        if rows:
            m = np.any(rows, axis=0)
            p = np.percentile(late[m & first], [5, 50, 95])
            print(f'  {lo}-{hi} views/s: {len(rows) * 10} s, a new view on {first[m].mean():.0%} of frames, '
                  f'late {p[0]:.1f}/{p[1]:.1f}/{p[2]:.1f}; every frame {late[m].mean():.1f} mean')
    if 'lead_ms' in v.dtype.names:
        print(f'pose lead: median {np.median(v["lead_ms"]):.1f} ms')
    if 'aim_qw' not in v.dtype.names:
        print('no pose columns in this table: no jitter figures')
        return
    tf = t[first]
    for name, prefix in [('head', 'head_q'), ('aim as tracked', 'aim_tracked_q'), ('aim as used', 'aim_q')]:
        q = np.stack([v[first][prefix + c] for c in 'xyzw'], axis=1)
        ok = np.abs(np.linalg.norm(q, axis=1) - 1.0) < 1e-2
        if ok.sum() < 20:
            print(f'{name}: no samples')
            continue
        j = jitter(tf[ok], forward(q[ok]))
        if len(j) == 0:
            print(f'{name}: no still windows')
            continue
        print(f'{name}: {len(j)} still 100 ms windows, jitter RMS median {np.median(j):.3f} deg, p95 '
              f'{np.percentile(j, 95):.3f} deg ({np.median(j) * np.pi / 180 * 10000:.0f} mm at 10 m)')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    main(sys.argv[1])
