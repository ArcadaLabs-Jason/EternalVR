# Frame work the per-view chain runs twice (Parallel Eye Rendering)

Steam build 25216728, RVAs. Rig: the RTX 3080 Ti test PC, 2026-10-06.

## Symptom

Exploded or stretched geometry: long thin lines and large shards across the image, in either eye, more often
under GPU load. The rig's effect-burst test (e1m1_intro, firing in bursts, a burst of 10 consecutive stereo
frames every 8 s) measures them as one-eye blobs and long one-eye lines. Route S passes it; Parallel Eye
Rendering failed it on every build since the port.

## How it was narrowed

Temporary test hooks on `vkQueueSubmit` (not kept in the tree), two runs each, interleaved with baselines:

- the CPU waiting for the queue to go idle after every submit, or after every graphics submit: passed;
- the per-frame transfer batch (0x1D1F3C0, submitted at 0x1CDB5AF) waiting on the GPU for the graphics work
  submitted before it: about half the faults;
- a full memory barrier in front of every graphics submit, no CPU wait: no change;
- the CPU waiting until at most one or two graphics submits were unfinished: not passed.

So the fault was work of one view landing on state the other view's GPU work still used, not GPU work
overlapping. The object-transform ring (0x1C00B40) is uploaded once per view, but both uploads are
byte-identical (hashed at 0x1C00C2E) and skipping the second changed nothing. The global staging allocator
0x6706700 is reclaimed by fences and safe when filled twice.

## The jobs

The engine's per-view chain (render-view job 0x1C5CD00, chain builder 0x1C5CEA0, compute prepass 0x1C5A750,
binning root 0x1CFA640) queues jobs that act on state of the world's, not the view's. In mono each runs once
per frame; with two views, twice:

- **GPU particle simulation**, 0x1C25DD0 -> 0x1C2A630 (record 0x3986290), queued by the binning root, parameter
  render context + 0x6A7DD0. About 15 dispatches (emit, simulate, compact, indirect arguments) on the world's
  one particle manager (`[world + 0x26B8]`) with the same buffer toggle (`+0x1205C`): particles advance twice,
  the dead and append counters are consumed twice, and view 1's run rewrites the buffers view 0's draws read.
  A large emission (the start of e1m1_intro) turned into cyan shards and lines in both eyes.
- **Geometry-cache upload**, 0x1943730 -> 0x1945520 (record 0x397C9B0), compute prepass, parameter the view's
  MVP-culling context. Each run flips its frame halves at the end (`[0x57AF150]` at 0x1946A3E, the manager's
  group `[[0x5BF13B8] + 0x26EEC]` in 0x1D228F0). Two runs flip back: on the rig every frame's first run found
  the same halves as the frame before, so each frame wrote the half the GPU was still reading.
- **Pending-work drain**, 0x1D26220 -> 0x1D265C0 (record 0x39B3AB0), compute prepass, parameter the
  MVP-culling context. It empties lists shared by both views (object 0x681A4C0) into the context of whichever
  view runs first. Pinned to one view it measured better than with the other view.

The fix (`view_jobs.cpp`): these join skinning, the blended BLAS and the TLAS jobs; view 1's call returns at
once. The MVP-culling context tells the views apart for two of them: view 0's is in the command context
table's cell 0x667F058, view 1's in the slot-1 cell 0x667F060 (`view_contexts.cpp` makes it, the redirects
route view 1 to it).

## Measurements (effect bursts, PE, one-eye blob p99 L/R, steps with a long line L/R)

| Build | Runs | Blob p99 | Long-line steps |
|---|---|---|---|
| before the fix | 12 | 0.04-0.92 | 7-24 per eye |
| the fix | 2 | 0.0095/0.014, 0.022/0.003 | 2/2, 5/1 |
| the fix and the 1x1x1 dispatch 0x1C54620 in view 0 too | 2 | 0.030/0.005, 0.017/0.005 | 5/0, 4/1 |
| the fix without the particle job | 2 | 0.047/0.023, 0.018/0.088 | 4/6, 3/4 (most in the particle burst) |
| CPU waits for each graphics submit (reference) | 2 | 0.003/0.003, 0.003/0.004 | 0/1, 0/1 |
| Route S | 3 | 0.023/0.008, 0.001/0.001, 0.008/0.001 | 1/0, 0/0, 0/0 |

With the fix the remaining long lines are single steps spread over the run.

## Still open

- A few long-line steps remain, more in eye L (view 0); Route S has 0 or 1. The views' chains run in
  parallel and nothing orders view 0's particle simulation or geometry-cache flip before view 1's consumers.
- Eye L changes more between consecutive frames than eye R (flicker ratio R/L 0.13-0.17; Route S 0.79-0.98).
- Other frame state advanced per view that does not draw geometry: the water simulation's ping-pong and
  three-slot ring (0x1CE3A90, world + 0xB4550 + 0xC30 / + 0xC34), the offscreen GUI upload 0x1C58050, the
  async acceleration-structure parameter copy 0x1C590F0 (async ray tracing only). The water one may explain
  reports of an uncomfortable underwater effect; view 1's water now starts from view 0's starting state and
  steps only its own clones (`view_water.hpp`, docs/VR_STEREO.md), not rig-checked yet.
