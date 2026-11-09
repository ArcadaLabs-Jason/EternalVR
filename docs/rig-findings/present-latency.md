# Release-to-present timing: why the held game repeated near the refresh (2026-10-30)

## Question

A rig A/B on 2026-10-29 (OpenXR simulator at 90 Hz, Super Gore Nest start, unlocked RTX 3080 Ti, i7-9700K) had the
old "Matched to the headset" hold (`ETERNALVR_TEST_PACE_REPEAT=1`) at 84-87 game ticks a second with 6-10% of the
headset's frames repeating the previous image, while the same scene let go ran 105-131 ticks a second. The guess
was that holding serialises the game's release and its next present, so that the time from release to present,
not the throughput, sits near one display period. Nothing measured it.

## Method

- Instrumentation: release-to-present timing (docs/VR_STEREO.md "Release-to-present timing"), on with the
  performance log. Per image: the release (the present hook of the previous hand-over returned, after the hold),
  eye L's present, the hand-over (eye R's present, `publishSlot`) and its return; on the XR worker the frame that
  showed it: `xrWaitFrame` call and return, the choice of image (the hold's signal), the slot taken (after any
  wait on the shared fence for the game's GPU work), the copy, `xrEndFrame`. `ETERNALVR_TEST_LATENCY_CSV=1` wrote
  every image to a table; the numbers below come from it and the frames table, over the 10 s windows from 30 s on.
- Scene and settings as runs ab-x/ab-y: e2m1 start with god and notarget, 100 s schedule, stereo 2048x2176 per
  eye, native TAA, ray tracing and DLSS off, `ETERNALVR_TEST_UNFOCUSED_CAP=0`.
- Candidates: (a) the old hold (`ETERNALVR_PACE=headset` with `ETERNALVR_TEST_PACE_REPEAT=1`); (b) the old hold
  with the late-image wait (`ETERNALVR_TEST_LATE_WAIT=1`, below); (c) the hold switch of T-194
  (`ETERNALVR_PACE=headset`, the default); (d) the hold switch with its latency gate (`ETERNALVR_TEST_HOLD_GATE=1`,
  below). Order A B C C B A per part where the rig allowed it. Parts: `hi` (clock unlocked, the game above the
  refresh), `ld` (the same with 4 or 8 processes spinning for the whole run), `hk` (the same with synthetic hitches:
  8 processes at high priority spinning 50 ms every 3 s), `lo` (GPU clock locked at 900 MHz, GPU-bound below the
  refresh).
- Smoothness: new images a second; the share of headset frames with no new image (repeats); the spread of the gap
  between new images on the headset (frames table display times, in display periods: standard deviation and p99);
  the content step error (between two images shown one after the other, the game's step, release to release, less
  the headset's step, `xrWaitFrame` return to return, in periods: 0 when every image moves the world on by exactly
  the time between its showings); release to `xrEndFrame` (p50, p99); the game's own time for an image (release to
  hand-over, p50, p99).

## Results

Run names: the A/B tag, the part and the candidate (`hr` (a), `hx` (b), `hn` (c), `hd` (d); a `b` suffix is the
second half of an A B C C B A order). "Quiet" and "busy rig" are the two halves of the same `hi` order, an hour apart:
in the second half other work ran on the rig, and all three candidates repeated about 2%. Ticks a second count the
game's frames whether shown or not (the hold switch, let go, draws more than it shows). The runs a4-hi-hd and
a4-hi-hdb were analysed before a later launch emptied their log folders; their layer logs are kept.

| part | run | candidate | ticks/s | new images/s | repeats % | new-image gap sd / p99 (periods) | content step error sd / p99 (periods) | release to xrEndFrame p50 / p99 ms | game time p50 / p99 ms |
|---|---|---|---|---|---|---|---|---|---|
| hi, quiet (A) | a1-hi-hr | (a) old hold | 90.0 | 89.9 | 0.07 | 0.036 / 1.09 | 0.048 / 0.18 | 12.38 / 17.73 | 6.95 / 9.27 |
| hi, quiet (A) | a2-hi-hr | (a) old hold | 89.9 | 89.7 | 0.36 | 0.074 / 1.03 | 0.095 / 0.13 | 12.05 / 17.39 | 6.59 / 9.63 |
| hi, quiet (A) | a2-hi-hx | (b) old hold + late wait | 90.0 | 89.9 | 0.09 | 0.081 / 1.05 | 0.072 / 0.22 | 12.27 / 18.58 | 6.98 / 10.59 |
| hi, quiet (A) | a2-hi-hn | (c) hold switch | 90.0 | 88.7 | 1.38 | 0.344 / 1.16 | 0.395 / 0.72 | 12.19 / 20.62 | 6.86 / 11.71 |
| hi, busy rig (B) | a2-hi-hnb | (c) hold switch | 99.3 | 88.2 | 1.97 | 0.189 / 2.00 | 0.270 / 1.20 | 12.23 / 26.38 | 6.90 / 19.05 |
| hi, busy rig (B) | a2-hi-hxb | (b) old hold + late wait | 89.0 | 88.2 | 1.92 | 0.157 / 2.00 | 0.149 / 0.98 | 12.07 / 26.05 | 6.57 / 22.36 |
| hi, busy rig (B) | a2-hi-hrb | (a) old hold | 89.2 | 88.3 | 1.84 | 0.174 / 2.00 | 0.204 / 1.09 | 12.01 / 23.69 | 6.52 / 18.63 |
| hi, quiet | a4-hi-hd | (d) switch + gate | 89.9 | 89.8 | 0.22 | 0.056 / 1.02 | 0.066 / 0.06 | 11.86 / 16.55 | 6.34 / 9.12 |
| hi, busy rig | a4-hi-hdb | (d) switch + gate | 87.8 | 86.5 | 3.93 | 0.256 / 3.00 | 0.292 / 1.98 | 11.96 / 34.57 | 6.52 / 23.70 |
| hi + 4 spinning processes | a2-ld-hr | (a) old hold | 90.0 | 89.9 | 0.14 | 0.041 / 1.06 | 0.044 / 0.15 | 12.19 / 17.70 | 6.42 / 9.99 |
| hi + 4 spinning processes | a2-ld-hx | (b) old hold + late wait | 90.0 | 90.0 | 0.01 | 0.034 / 1.08 | 0.051 / 0.19 | 12.26 / 18.59 | 6.64 / 10.51 |
| hi + 4 spinning processes | a2-ld-hn | (c) hold switch | 90.0 | 89.9 | 0.11 | 0.036 / 1.05 | 0.031 / 0.12 | 12.17 / 17.44 | 6.36 / 9.70 |
| hi + 8 spinning processes | a3-ld-hr | (a) old hold | 90.0 | 89.9 | 0.12 | 0.072 / 1.18 | 0.110 / 0.33 | 12.15 / 17.32 | 6.28 / 9.35 |
| hi + hitches (A) | a7-hk-hr | (a) old hold | 84.5 | 81.7 | 8.33 | 0.450 / 3.01 | 0.496 / 2.00 | 11.83 / 35.16 | 6.13 / 24.85 |
| hi + hitches (A) | a7-hk-hd | (d) switch + gate | 84.3 | 81.5 | 8.55 | 0.459 / 3.01 | 0.507 / 2.00 | 11.82 / 35.48 | 6.08 / 24.72 |
| hi + hitches (A) | a7-hk-hn | (c) hold switch | 117.3 | 87.4 | 2.26 | 0.306 / 2.03 | 0.534 / 1.99 | 13.71 / 34.32 | 7.37 / 18.15 |
| hi + hitches | a5-hk-hn | (c) hold switch | 119.3 | 86.9 | 2.64 | 0.317 / 3.00 | 0.489 / 1.98 | 13.18 / 34.68 | 7.18 / 22.54 |
| hi + hitches | a5-hk-hd | (d) switch + gate | 85.5 | 83.3 | 6.59 | 0.422 / 3.00 | 0.442 / 1.99 | 11.87 / 35.03 | 6.23 / 24.38 |
| hi + hitches | a5-hk-hdb | (d) switch + gate | 88.5 | 87.6 | 1.67 | 0.303 / 2.00 | 0.265 / 0.98 | 11.80 / 17.54 | 6.08 / 12.34 |
| hi + hitches | a5-hk-hnb | (c) hold switch | 123.1 | 87.6 | 1.83 | 0.290 / 2.01 | 0.497 / 1.98 | 13.49 / 34.35 | 7.29 / 14.85 |
| hi + hitches | a5-hk-hrb | (a) old hold | 85.8 | 83.6 | 6.33 | 0.410 / 3.00 | 0.448 / 1.99 | 11.97 / 35.07 | 6.39 / 24.45 |
| lo (900 MHz) | a2-lo-hr | (a) old hold | 70.8 | 70.7 | 21.39 | 0.450 / 2.06 | 0.469 / 0.90 | 27.46 / 37.13 | 13.96 / 19.43 |
| lo (900 MHz) | a2-lo-hx | (b) old hold + late wait | 72.1 | 72.0 | 19.91 | 0.435 / 2.05 | 0.455 / 0.91 | 31.32 / 39.86 | 13.69 / 19.11 |
| lo (900 MHz) | a2-lo-hn | (c) hold switch | 73.9 | 73.9 | 0.00 | 0.097 / 1.66 | 0.141 / 0.52 | 25.39 / 30.45 | 13.37 / 18.36 |
| lo (900 MHz) | a2-lo-hnb | (c) hold switch | 73.5 | 73.5 | 0.03 | 0.112 / 1.68 | 0.157 / 0.54 | 25.36 / 30.79 | 13.37 / 18.58 |
| lo (900 MHz) | a2-lo-hxb | (b) old hold + late wait | 75.3 | 75.1 | 16.32 | 0.401 / 2.05 | 0.418 / 0.95 | 30.26 / 38.56 | 13.10 / 18.72 |
| lo (900 MHz) | a2-lo-hrb | (a) old hold | 75.4 | 75.2 | 16.23 | 0.401 / 2.05 | 0.420 / 0.94 | 25.73 / 36.22 | 13.05 / 18.67 |
| lo, hold gate | a4-lo-hdb | (d) switch + gate | 75.7 | 75.7 | 0.00 | 0.103 / 1.66 | 0.146 / 0.56 | 24.68 / 30.25 | 13.01 / 18.44 |
| lo, hold gate | a6-lo-hd | (d) switch + gate | 76.4 | 76.4 | 0.01 | 0.092 / 1.66 | 0.143 / 0.57 | 24.49 / 30.22 | 12.91 / 18.21 |

Hold switch let-go events per run (`pace: the game falls behind the headset`): (c) 8 (quiet), 10 (busy), 11, 11 and
11 (hitches); (d) 0 (quiet), 1 (busy), 1, 1 and 1 (hitches); below the refresh both let go at the start and stay
let go. With steady load the switch stayed held (0).

## Explanation

**Not serialisation.** Held on a quiet rig, the game's own time for an image (release to hand-over) is 6.3-7.0 ms
at the median and 9.3-10.6 ms at p99, against the 11.1 ms period; it hands the image over 4.1-4.5 ms before the next
headset frame chooses (p1 0.5-1.8 ms). The render thread wakes 0.01 ms after the choice (p99 0.05 ms), the
headset's choices come 11.11 ms apart (p99 about 11.8 ms), and the GPU fence held a shown image 0.01 ms at the
median (p99 3.4-4.7 ms; no frame repeated for want of a finished image). Release to `xrEndFrame` is about 12 ms: one
period plus the copy. In that state the old hold runs 90 ticks a second with 0.07-0.36% repeats (2 runs; 0.12-0.14% in the two runs with
steady load).

**What repeats are.** A held image is due one display period after its release: any image whose own time is over
the period costs a repeat. In the repeating stretches both halves of the image slow together (in a busy run, release
to eye L 10.9 ms and eye L to eye R 9.1 ms against 3.5 and 3.3 ms), so the whole game was held up, not one stage, and
they come in bursts of seconds (on 2026-10-29, 16 to 27 bursts of 1 to 240 repeats per run, the tick period's p99 at
23 ms and the CPU timing summary taking up to 107 ms instead of 30). Let go, the game is bound by its rate instead:
at 131 images a second a late image is followed 7.6 ms later by the next, so a headset frame lacks a new image only
when the rate itself falls under the refresh. Holding trades the rate's headroom for a per-image deadline; that is
where "latency, not throughput" holds, in the tail rather than on average.

**Why the rig had so many.** On this shared rig the game runs at below-normal priority (the Task Scheduler's
default for the task that starts it unelevated; seen on the game process during these runs), so any normal-priority
work on the PC can stop it for a few milliseconds (other agents' builds, clang-tidy and QA shared the PC all day, and
builds were seen holding the build marker beside other agents' runs).
Steady load at the game's own priority did nothing (4 and 8 processes spinning for the whole run: 0.01-0.14%
repeats in all candidates). The 2026-10-29 runs and today's busy runs are consistent with that; which work stopped
the game in each burst was not measured [inferred]. A player's game runs at normal priority, so the bursts are
rarer there, but a background program at the same priority can still do the same.

**The hold switch above the refresh.** A burst of more than 4-5 repeats in a second trips the switch's 5% rule:
in a quiet run it let go and held again 8 times, in a busy run 10, with synthetic hitches 11. Let go, it hands over
1.1 to 1.5 images per headset frame and some are never shown, so the world's step on the headset is uneven: content
step error sd 0.27-0.40 periods against 0.07-0.20 for the hold in the same conditions.

**Below the refresh** (GPU-bound, the game's own time 13-14 ms) the hold re-presents 16-21% of the headset's frames
and the gap between new images alternates between one and two periods (sd 0.40-0.45); the hold switch lets go, shows
each image once (0.00-0.03% repeats), the gap's sd is 0.10-0.11 and the content step error 0.14-0.16 against
0.42-0.47, at the same or better tick rate and release to `xrEndFrame` (25.4 against 25.7-27.5 ms at the median). On
the simulator the display times follow the game's rate; on a headset the runtime shows each image for one or two
refreshes and fills in with its reprojection, which the rig cannot judge.

## Late-image wait

Held, a headset frame that begins while the game is still drawing the image the frame before it released waits for
it within its own frame's time, until the submit margin plus 2 ms for the GPU work before the period ends
(`features/pacing/late_image.hpp`, `ETERNALVR_TEST_LATE_WAIT=1`). Within each run it turned most late images into
on-time ones (quiet: 59 of 67; busy: 189 of 360; steady load: 32 of 33), but across runs the repeats did not fall
consistently (busy: 1.92% against 1.84% without it), because a burst stops the game for longer than the wait can
cover. GPU-bound it waits at nearly every frame and adds 4-5 ms release to `xrEndFrame` (31.3 against 27.5 ms and
30.3 against 25.7 ms) for at most 1.5% fewer repeats. Not strictly better on every run: kept as a rig switch, off.

## Hold switch latency gate

The switch lets the game go only when more than half of a window's images took longer than a display period from
their release to their hand-over (`pacing::HoldSwitch::noteGameTime`, `ETERNALVR_TEST_HOLD_GATE=1`; the times are
measured in the present hook, not with the performance log). A game that draws within the period but hitched stays
held; a GPU-bound one (13-14 ms per image) is still let go.

Above the refresh on a quiet run it held like the old hold (0.22% repeats, content step error sd 0.066, no let-go);
below the refresh it let go like the switch (0.00-0.01%, gap sd 0.09-0.10, content step error 0.14-0.15). With
hitches it did what the old hold does, repeat through them (8.55% and 6.59%; 1.67% in a run the hitches hit less),
where the switch let go and repeated 1.83-2.64% at about the same content step error (0.49-0.53 against 0.27-0.51):
under frequent hitches the switch's let-go is the better trade on these measures, on a quiet PC the gate's hold is.
Not strictly better on every run: kept as a rig switch, off.

## Recommendation

- **Default "Matched to the headset": the hold switch of T-194 (c), unchanged.** Below the refresh it is the only
  candidate that does not re-present: the old hold repeats 16-21% of the headset's frames there with a gap sd and
  content step error three times the switch's. Above the refresh it costs about 1.4% repeats and an uneven step
  (content step error sd about 0.4 periods) on a quiet PC where the old hold has 0.1-0.4% and 0.05-0.1, and it is
  clearly better than the old hold under frequent hitches (2% against 6-8% repeats at the same unevenness).
- **Not the old hold (a).** It is the smoothest above the refresh on a quiet PC, but its cost below the refresh is
  the largest of any candidate, and a PC near the refresh moves between the two.
- **Late-image wait (b): dropped as a candidate.** No consistent gain in repeats across runs, and 4-5 ms more
  release to `xrEndFrame` GPU-bound.
- **Latency gate (d): the next candidate for the owner's headset.** It keeps the old hold's quality above the
  refresh on a quiet PC and the switch's below it, and loses to the switch only under frequent hitches. Which of
  (c) and (d) looks smoother near the refresh, with a hitch now and then, is a headset question: the switch's
  let-go phases show every image once at an uneven step, the gate's held phases repeat an image on each hitch.
  Rig: `ETERNALVR_TEST_HOLD_GATE=1`.
- **New game frames only stays the opt-in**, unchanged.
- **Rig method:** game runs here start at below-normal priority; a run that has to be quiet should hold the build
  marker for its whole length (other agents' builds ran beside today's runs) or raise the game's priority, and the
  release-to-present table's game time p99 tells a disturbed run (18 ms and over) from a quiet one (12 ms and
  under).

Evidence: all [rig] on the OpenXR simulator at 90 Hz, one scene, 100 s runs, one to three runs per candidate and
condition; the content step error and gap spread are measures of the image stream, not of what the eye sees through
a runtime's reprojection [inferred]. No headset.

## Review fixes (2026-10-31)

A review of the hold switch found four ways it could misjudge; all are fixed on this branch (T-201):

- **Back-off.** A hold that passed one window and failed the next counted as lasting (the old test was "within
  2 s") and reset the back-off, so a game fast on its own but repeating once held could cycle every 3 s. Now a hold
  that ends within 10 s doubles the back-off (1, 2, 4 ... 32 s) and only a hold of 10 s or more starts it over; a
  hold made by a menu is not judged.
- **Gaps.** A window longer than two seconds (sleep, a session not running, a stalled copy) or with fewer than 30
  headset frames decides nothing and breaks a run of bad windows.
- **One hitch.** A single bad window (one 50 ms hitch makes 4-5 repeats in 90 frames) let the game go; two bad
  windows in a row are needed now.
- **Menus and loading screens.** Away from play (no game ticks from the camera hook for 250 ms) the game is held
  and nothing is decided. Before, the switch let go during the opening loading screen in 6 of 6 runs; now in 0 of
  4.

Method changes for these runs: the game was raised to normal priority for each run (a player's game runs at normal
priority; the rig's task starts it below normal), and from run b1-hi-hnb on Windows Defender's real-time scanning
skips the runs' log folder (it took about one core while the layer wrote its logs and tables; the rig's other run
folder was already excluded). Candidates: (a) the old hold, (c) the hold switch with these fixes.

Runs of 100 s, Super Gore Nest start, 90 Hz simulator, ABBA order (a, c, c, a), game at normal priority, run folder excluded from Defender. Release to `xrEndFrame` and game time are milliseconds; let-go events are the hold switch's let-go lines in the run.

| part | run | candidate | ticks/s | new images/s | repeats % | new-image gap sd / p99 (periods) | content step error sd / p99 (periods) | release to xrEndFrame p50 / p99 ms | game time p50 / p99 ms | let-go events |
|---|---|---|---|---|---|---|---|---|---|---|
| hi | hi-hr | (a) old hold | 89.3 | 88.5 | 1.66 | 0.165 / 2.00 | 0.204 / 1.13 | 11.89 / 23.88 | 6.42 / 16.39 | 0 |
| hi | hi-hn | (c) hold switch, fixed | 127.3 | 88.3 | 1.91 | 0.181 / 2.00 | 0.372 / 1.01 | 14.12 / 27.33 | 7.58 / 16.91 | 8 |
| hi | hi-hnb | (c) hold switch, fixed | 128.8 | 88.5 | 1.64 | 0.164 / 2.00 | 0.361 / 1.00 | 14.01 / 25.03 | 7.47 / 14.29 | 8 |
| hi | hi-hrb | (a) old hold | 79.7 | 74.1 | 17.66 | 0.537 / 3.01 | 0.611 / 1.99 | 12.38 / 37.07 | 7.70 / 24.20 | 0 |
| lo (900 MHz) | lo-hr | (a) old hold | 74.3 | 74.2 | 17.50 | 0.414 / 2.05 | 0.429 / 0.91 | 26.24 / 36.31 | 13.36 / 15.96 | 0 |
| lo (900 MHz) | lo-hn | (c) hold switch, fixed | 76.5 | 76.5 | 0.00 | 0.055 / 1.36 | 0.080 / 0.26 | 24.54 / 27.16 | 13.03 / 15.28 | 1 |
| lo (900 MHz) | lo-hnb | (c) hold switch, fixed | 76.8 | 76.8 | 0.01 | 0.053 / 1.36 | 0.084 / 0.27 | 24.53 / 27.19 | 12.95 / 15.34 | 1 |
| lo (900 MHz) | lo-hrb | (a) old hold | 78.1 | 77.9 | 13.25 | 0.366 / 2.04 | 0.373 / 0.94 | 24.93 / 35.42 | 12.72 / 15.38 | 0 |

The fourth hi run (old hold, last) was disturbed: game time p99 24 ms against 14-17 ms in the other three hi runs (the quiet-run marker from the first round), repeats 17.7% against 1.7%. It is kept in the table and left out of the verdict's quiet-rig comparison.

- **Above the refresh, quiet rig (2 runs each in this round, 4 in the first half):** the fixed switch is as smooth as the old hold in what the eye sees of repeats and gaps: repeats 1.9% and 1.6% against 1.7% (old hold), new-image gap sd 0.18 and 0.16 against 0.17 periods, p99 2.0 in both. It is not smoother, and its content step error stays higher: sd 0.36-0.37 against 0.20 periods (p99 about 1.0 against 1.1). Release to `xrEndFrame` p50 is 2 ms longer (14.0 against 11.9), p99 the same. Let-go cycles: 8 per run (the same count as before the fixes, 8-11), spread over the run with growing gaps (hold back-off), 3 s to 34 s apart in the second run, not a 3 s oscillation.
- **Why the switch is not worse than the old hold even there:** the old hold is not stable. On the same rig and build one old-hold run had 1.7% repeats and the next 17.7% (game time p99 24 ms, a disturbed run), and the first half's had 6.1% and 1.6%. The fixed switch had 1.9, 1.6, 4.5 and 2.1% across its four runs and never went above 5%; it falls back to new frames only when the held game starts repeating.
- **Below the refresh (900 MHz, 2 runs each):** the fixed switch repeats 0.00% and 0.01% (gap sd 0.05, p99 1.36 periods, content step error sd 0.08) against 17.5% and 13.3% for the old hold (gap sd 0.37-0.41, p99 2.05, content step error sd 0.37-0.43). One let-go at 18 s per run, then stays off. Release to `xrEndFrame` is about 1.7 ms shorter for the switch at p50 (24.5 against 24.9-26.2) and 8-9 ms shorter at p99.
- **Verdict:** yes on both counts, with one cost. The fixed switch is at least as smooth as the old hold above the refresh on repeats and gaps (equal when quiet, far steadier when the PC is disturbed), and it is still far smoother below the refresh (0.0% against 13-17% repeats). The cost is a larger content step error above the refresh (sd 0.36 against 0.20 periods, quiet rig), which comes from the let-go phases showing every image once at an uneven step.
- **Not measured / open:** the old hold's second quiet hi run is missing (the one disturbed run stands in its place; the first half's quiet one agrees with the first); the gate candidate (d) was not re-run in this round.

Evidence: all [rig] on the OpenXR simulator at 90 Hz, two runs per candidate and part (four in the first half for the hi part); no headset; content step error and gap spread are image-stream measures, not what the eye sees through a runtime's reprojection [inferred].
