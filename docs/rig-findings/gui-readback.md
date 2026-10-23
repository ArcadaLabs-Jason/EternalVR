# The GUI readback off the present hook

The GUI target's sparse readback (`vkcore/wheel_probe.hpp`) tells the layer where the game drew the weapon wheel
(docs/VR_MENUS.md, "Where the wheel shows") and, in wrist and weapon mode, the corner blocks the hand HUDs show
(docs/VR_HANDS_HUD.md, "Where the corner blocks are"). Up to 0.1.77 the present hook read each completed
reading's grid out of the host buffer and measured it itself: 0.4 to 1.1 ms on a frame that reads the wheel,
0.3 to 0.7 ms on the frame of each corner reading (every GUI copy until both blocks are found, then every 30th),
a spike in normal play. Now the present hook only records the copy and hands each completed reading to the
readback's own thread (`vkcore/readback_worker.hpp`), which reads and measures it and publishes the result; what
each reading found goes back to the read cadences at a later poll.

Labels: `[rig]` measured on the test PC (RTX 3080 Ti, OpenXR-Simulator), `[code]` read from the code,
`[inferred]` a reading of the two.

## How it was measured

Release builds, Route S with Parallel Eye Rendering, 2048x2208, e1m3 at the gate checkpoint, wrist HUD, god
mode; 130 s from the map start with the wheel held open with the right stick five times for 6 s, GUI captures
every 10th copy. A is origin/main (f07c9ef8) with only the present hook's own time for the readback added to its
two log lines (`WheelProbe::record` plus `poll`, poll counted with the next GUI copy); B is this change, whose
lines carry the same figures. Runs in the order A, B, A, B. In these runs the game made about 40 GUI copies a
second.

## Cost on the present hook

| | A (origin/main), two runs | B (readback thread), two runs |
| --- | --- | --- |
| Wheel up: present hook per frame, average | 96.5, 95.3 us | 8.2, 8.0 us |
| Wheel up: present hook per frame, largest line maximum | 1115, 1010 us | 138, 67 us |
| Corners in play: present hook per GUI copy, average | 11.5, 11.4 us | 2.1, 2.1 us |
| Corners in play: present hook per GUI copy, maximum (lines after the first) | 670, 491 us | 106, 175 us |
| Measuring a wheel reading (A on the present hook, B on its own thread) | 633, 624 us | 636, 626 us |
| Measuring a corner reading | 281, 278 us | 284, 281 us |
| Readings: wheel, corners | 324 and 303, 325 and 303 | 332 and 303, 332 and 303 |

All [rig]. What is left on the present hook in B is mostly recording the copy (the driver's cost of
`vkCmdCopyImageToBuffer` with one region a sampled row: 288 rows for the wheel at this size, 230 for the two
corners): 46 us a wheel reading on average (66 at most), 38 us a corner reading (174 at most) [rig]. A pays the
same for it, inside its figures [inferred: the same code records the copy]. Handing a reading over (the timestamp
query, a lock and a wake-up) and taking the results back are the rest: a few microseconds a frame. The first
corner line of a session also holds the host buffer's creation and the thread's start (278 and 412 us at most in
the two B runs) [rig].

The GPU copies are unchanged: about 200 us a wheel reading and 100 us a corner reading in both [rig].

## Latency

The worker publishes a reading 0.3 ms (corners) to 0.65 ms (wheel) after the present hook handed it over, which
is where origin/main measured it: B's lines give 24.4 ms from the copy's record to the hand-off and 25.0 ms to the
publication for the wheel, 23.8 and 24.1 ms for the corners, at most 30.5 ms. In GUI copies the hand-off comes 1
copy after the reading's own and the publication 2; the second is the copy the same present records after its
poll, while the worker measures [rig, code]. The XR worker reads the newest measurement once a headset frame, so
the extra fraction of a millisecond moves a reading to the next headset frame only when it lands in that gap
[inferred]. No present found every slot taken (0 reads skipped for no free slot in all B lines) [rig].

## Same results

- The wheel's rectangle: in all four runs the five openings logged `weapon wheel straight ahead (wrist HUD): GUI
  rect (1217, 716) 720 x 720` the first time and `720 x 744` the other four [rig].
- The corner blocks: every run logged one cut, `health and armor GUI rect (0, 1367) 480 x 313, ammo (1516, 1431)
  532 x 249` [rig].
- The cuts against every GUI capture of the run, checked as QA `hand-hud-large` checks its last five (alpha 16
  or more, 8-connected shapes, the cut logged before the capture; a cut holding nothing drawn or more than 90%
  drawn left out): with the wheel down, 453 to 461 captures checked per run and no shape across a
  cut in any run. With the wheel open, 144 to 147 captures per run and 109 to 111 crossings in A and B alike: the
  open wheel's bottom reaches a few pixels into the top of the ammo cut (the wheel's rectangle ends at y 1460,
  the cut starts at 1431), so the wrist HUD's ammo quad shows a sliver of the wheel's bottom while the wheel is
  up. That is as it was before this change [rig]; the check's control (every cut 60 pixels narrower) finds a
  shape across in every capture [rig].

## Shutdown and the device

`WheelProbe::destroy` (the UI images destroyed: a swapchain change, a ring rebuild, the device going) stops the
thread before it frees the host buffer: the reading being measured finishes, the ones waiting are dropped, and
nothing reads the buffer after it. A new size of GUI target waits until no slot is in flight or held by the
thread. The thread makes no Vulkan call: the present hook invalidates the mapped range (on memory that is not
coherent) and reads the timestamps before it hands a reading over. At process exit the thread is let go without
a lock or a wait (the process's other threads are gone by then) [code]. Unit tests: `readback_worker_tests.cpp`.

Headset only: whether a frame-time difference shows on a real headset at 90 to 120 Hz (the simulator ran about
40 GUI copies a second here).
