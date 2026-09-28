# Hand aim jitter: pose timing and tremor

The owner's report from the Quest 3 session of 2026-09-26 (Virtual Desktop, 90 Hz, `ETERNALVR_AIM=hand`):
"my gun/aim was jittery", then first-hand: jittery all the time, low in amplitude, not enough to affect aim,
noticed mostly because of the white dot. Two candidate causes:

- **(A) Tremor and tracking noise**, magnified by the reticle 10 m out along the hand's ray.
- **(B) Pose timing**: the hands are located on the game thread for the head record's pose time
  (predictedDisplayTime plus one period); the compositor corrects the head for the time a frame is really
  shown, not the gun drawn into it.

Data: `<workspace>\runs\session2-20260926-124137\` (`eternalvr-20260926-124139-43960.log`,
`eternalvr-frames-43960.csv`: 115,458 XR frames that showed a head-tracked view, 1,337 s). The analysis is
`tools/frames/aim_jitter.py` (numpy).

## What the table says

That table had no pose columns, so (A) could not be measured from it; only the timing could. The game ran
at 44 to 70 views per second for 810 s (61% of the session), at 85 to 120 for 310 s and above 120 for
180 s.

How late each view was first shown, against the time its poses were predicted for
(`display_minus_pose_time_ms` at the first XR frame showing it):

| Game views/s | Time | XR frames with a new view | Late at first showing, p5 / p50 / p95 | Every frame, mean |
|---|---|---|---|---|
| 44-70 | 810 s | 60% | 11.1 / 22.2 / 33.3 ms | 29.7 ms |
| 70-85 | 30 s | 78% | 0.0 / 11.1 / 24.5 ms | 16.6 ms |
| 85-120 | 310 s | 96% | 0.0 / 11.1 / 22.2 ms | 11.7 ms |
| above 120 | 180 s | 99% | 0.0 / 0.0 / 11.1 ms | 6.2 ms |

- The prediction target itself is steady: it moves in whole display periods between views (median step
  11.1 ms, p95 25.3 ms). No jitter comes from the target.
- It is short, though: predicting one period ahead fits a game running above 120 frames per second. At
  44-70 the typical frame is shown two periods after the time it was predicted for, so the gun is drawn
  where the hand was about 22 ms (up to 33 ms) before, and a view is repeated on 40% of XR frames: the
  displayed lag changes by a period on 80% of consecutive frames (24% above 120). That is lag, plus judder
  while the hand moves; with the hand still it is invisible.
- The code agreed on one time for the gun: the camera hook locates the hands at the head record's pose time
  (`beginGameView(record.poseTime, ...)`), and the viewmodel hook, the fire hook and the aim angles read
  those poses. **The reticle did not**: it was a quad in the hand's aim space, placed by the compositor with
  the live pose at each XR frame's display time. So the dot was on another timeline from the gun and the
  shots: at 60 degrees per second and 22 ms apart it led the barrel by 1.3 degrees (23 cm at 10 m), and it
  showed the runtime's raw pose noise at 90 Hz.

## A against B

The complaint is (A): constant, small, and seen on the dot. A 1 degree dot 10 m out is 175 mm across; hand
tremor and controller noise of a few tenths of a degree (the usual figures, not measured here) move it by
a sizeable part of its own size, while the gun at arm's length moves by millimetres. (B) is real but a
different symptom: lag and judder while the hand moves, worst below 70 frames per second.

## What changed

- **Aim smoothing (A)**: a one-euro filter on the weapon hand's aim orientation
  (`features/input/aim_smoothing.hpp`, `ETERNALVR_AIM_SMOOTHING`, default 0.3), applied where the hands are
  located for the game view, before the viewmodel, the shots, the aim angles and the reticle read the ray.
  At 0.3: a still hand's noise to about a third (RMS), 8 ms of lag at 57 degrees per second, 3 ms in a
  quick flick (170 degrees per second), 24 ms (0.27 degrees) in a slow 11 degrees per second drift; a 20
  degree step is 72% there on the first frame and within a degree after three.
- **The reticle on the gun's ray**: the dot is placed in LOCAL from the shown frame's own (smoothed) ray,
  so it sits where that frame's gun points and its shots go. It moves at the game's frame rate, like the
  gun. Without a ray for the shown frame it falls back to the aim space.
- **Pose lead (B), opt-in**: `ETERNALVR_POSE_LEAD=1` predicts the head and hands later by the measured
  lateness of first showings (`xr_math/display_lead.hpp`, at most two periods; the 10 s `xr:` line shows
  it). Off by default: the complaint is (A), and predicting further makes the runtime extrapolate the hand,
  and its noise, further ahead.
- **The frame table** gains `lead_ms`, the shown view's head orientation and the weapon hand's aim
  orientation as used and as tracked, so `tools/frames/aim_jitter.py` can measure (A) on the next session:
  the angular jitter of the head and of the aim ray (tracked and smoothed) in still 100 ms windows, RMS and
  p95, in degrees.

## For the next session

Run with hand aim and `ETERNALVR_LOG_DIR`, hold the gun still on a target for a few seconds, then
`tools/frames/aim_jitter.py <eternalvr-frames-*.csv>`. The dot should look steady at rest and not trail in
quick flicks; if it still shimmers, try `ETERNALVR_AIM_SMOOTHING=0.5`; if it drags, 0.15; 0 turns it off
for comparison. For (B), compare a sweep at 50-60 frames per second with and without `ETERNALVR_POSE_LEAD=1`.
