# Alternate eyes: one eye per game tick for slower processors (`ETERNALVR_ALTERNATE_EYES=1` or `auto`)

Route S renders eye L and then eye R in every game tick (`docs/VR_STEREO.md`), and the tick is a serial
chain on the processor: a stereo render costs about what a whole mono frame costs, and the game logic outside
the renders is small (`perf-stereo-cpu.md`). A processor that cannot finish two renders within the headset's
frame period drops the whole game below the headset's rate. With alternate eyes each game tick renders one
eye: eye L, then eye R in the next tick, and so on. It is an option for slower processors, **off by default**
(the owner: "Ya it can be an option"); the launcher's Advanced tab has it as "Alternate eyes". Nothing here has
run on the rig or in a headset yet (section 7 is the recipe). RVAs are in Steam build 25216728.

Prior art: Luke Ross's REAL VR mods render alternate eyes (each eye updated every other frame, one eye about
11 ms stale at 90 Hz; below 90 fps fast motion ghosts), and UEVR's AFR mode is the same idea with known eye
desync. Both costs apply here.

## 1. What renders each tick

| Tick | Render | Eye | Its previous render |
|---|---|---|---|
| t | the engine's own chain | L | L of t - 2 |
| t + 1 | the engine's own chain | R | R of t - 1 |
| t + 2 | the engine's own chain | L | L of t |

The frame-end wrapper (`seq_hooks.cpp`) no longer starts an eye R render; every render is the engine's own
frame with a game tick before it. Which eye a render draws is decided by `stereo_seq::EyeAlternator`
(`src/stereo_seq/alternate_eyes.*`, used through `vkcore/seq_alternate.*`): **eye R when the render frame
right before this one was drawn as eye L in stereo, else eye L**. So eye R only ever follows its eye L, as
under Route S, and any mono render (a menu, a loading screen, a tick whose tags need a new base, a tripped
multiplayer guard) starts the alternation again with eye L. The decision is keyed by the render frame counter
(renderSystem + 0x10, raised by the render-frame job before the world-views pass), so every hook of one render
gets the same answer: the previous-matrix store (once per world view, before the per-eye hook), the per-eye
hook, the post-latch hook and the frame end. The frame end records the outcome before it runs the original
job, which clears the render-frame guard, so the next render is decided after it.

`seqRenderEye()` is the eye the running render draws (under Route S it is `seqChainEye()`). `seqChainEye()`
itself keeps its meaning, "inside the nested eye R render", which never happens with alternate eyes on: the
moved-flag repair of eye R's re-render of eye L's tick (`stereo-moved-flag.md`) then leaves every render as the
engine made it, which is right, because every render now has its own game tick. The previous model matrix
takes the eye's own last render instead (section 9.1).

The per-eye hook (`presenter_seq.cpp`, `onSeqEyeView`) asks the stereo readiness for both eyes (the eye tags
in step, the guard armed), takes the view record of the render's own game frame (by the axis the camera hook
wrote, as eye L does under Route S) and writes that eye's pose, projection and flags. The eye tag then carries
that eye and that game frame.

## 2. What the headset gets

The projection layer carries one pose per view, so each eye can be submitted with the pose it was rendered
with; the compositor's reprojection then corrects the older eye for the head's rotation since (as it corrects
any late frame). So every present shows the **fresh eye beside the other eye's newest image**, each with its
own pose and FOV:

- `stereo_seq::AlternatePairing` decides per present: `Hold` (no partner yet: store the image, show nothing),
  `Publish` (the other eye's image from the tick before is held: show the pair), `ShowMono` (menus, loading),
  `Drop` (the per-eye hook did not write the view). A held image must be the other eye's and at most 2 game
  frames older, else it is given up: a single eye's image is never shown to both eyes.
- The ring slot the next present completes always holds the other eye's newest image. On `Publish` the
  present's image is copied into its half of that held slot, which is then handed to the XR worker, and **in
  the same command buffer** into its half of a new slot that is held for the next present (`CopyTarget::
  carrySlot`, `presenter_copy.cpp`; `presenter_alt.cpp`). One extra eye-sized image copy per present on the
  GPU (about 0.05 ms at 2064 x 2100 on an RTX 4080, as the mirror's copies measured), no extra submit. With
  three ring slots the held slot, the newest published one and the one the worker reads can all be in use; a
  present that finds no free slot for the carry still shows its pair, and the next present starts again with
  `Hold` (the headset repeats the last pair for one frame).
- `stereo_seq::composeHalves` builds the pair's view record: the fresh tick's record, with the held half's eye
  (pose, FOV, offset, axis) taken from the record of the tick it was rendered in. `fillProjectionViews`
  already submits `eyes[i].pose` and `eyes[i].fov` per half.

The UI layer's GUI target is drawn in every tick now (each render is a full engine frame) and goes with every
published slot. The desktop mirror, the eye capture and the motion capture work as under Route S: the window
gets the mirrored eye's presents, a captured pair is eye L of tick t and eye R of tick t + 1, and the motion
capture takes both eyes' velocity images at eye R's copy (the sidecar's ticks then differ by one).

## 3. Per-eye temporal state

The renderer sees the same order of renders as under Route S (L, R, L, R), with a game tick before every
render instead of every second one. What is keyed by the render order or the eye tag stays per eye unchanged:

| State | Mechanism | With alternate eyes |
|---|---|---|
| Camera previous matrices | `PrevMatrixBook` keeps each eye's own last latch (hook after RVA 0x1CE2340) | each eye's previous matrices are its own render two game frames back: its camera motion vectors span its own two ticks (tested) |
| TAA / DLSS history | accumulation pairs picked by the tag's eye and its per-eye count (`pickAccumulation`) | unchanged: each eye reads what it wrote at its last render (tested) |
| Jitter phase | one phase per tick (`taaSubSample`) | `alternateSubSample`: half the game frame, so each eye goes through every phase (tested) |
| TAA reset | `TaaResetPlanner` (both eyes of a tick) | `AlternateTaaReset`: reset until the alternation has run three renders without a break (the engine's flag covers its next three renders, so both eyes) |
| DLSS twin (eye R) | reset unless eye R evaluated the previous game frame | reset unless eye R evaluated two game frames back (`eyeFrameStep`) |
| Exposure | eye R skips its update and reads eye L's last (`ExposurePlanner`) | unchanged: eye R shows eye L's exposure of the tick before (tested) |
| Scattering history | per eye by the render's tag (`ScatterHistory`) | unchanged |
| World GUIs | eye R draws eye L's commit of the same tick | not needed (every render commits its own); the check only acts on a commit one render behind for eye R, which alternate renders do not have |
| Moved flags | eye R's re-render keeps eye L's | not applied (`seqChainEye` is never Right): the engine's own per-tick behaviour |
| Previous model matrix (ordinary entities) | eye R's re-render keeps eye L's (`keep_prev`) | each commit leaves the matrix of two renders back, the eye's own last render (section 9) |

**Object motion is the exception** (section 9 has the analysis and the part fixed). Moving objects get their
previous transforms from two places. Ordinary entities carry a previous model matrix that each commit sets to
the current one before computing the new one; with alternate eyes that was the render before, the other eye's,
one game frame back. Each commit now leaves the entity's matrix of two renders back instead, the eye's own last
render (`stereo_seq/alternate_prev.*`, in `keep_prev_hooks.cpp`). Skinned meshes (and the ring's other users)
take theirs from the object-transform ring (`stereo-object-motion.md`), which has three slots and reads the
render before; giving each eye its own last render there needs a fourth slot (section 9.2), so their motion
vectors still cover one tick of object motion against two of camera motion, and TAA smears fast skinned demons
a little. **Anti-aliasing Off** avoids that entirely (no temporal history).

## 4. Expected saving

Per tick the processor does one render instead of two. From `perf-stereo-cpu.md` (i7-9700K, RTX 3080 Ti,
1280 x 1400 per eye): mono 275 frames/s, Route S 143 ticks/s, a stereo tick 2.5 times a mono frame's CPU
samples, a stereo render about 1.25 mono frames. So an alternate tick costs about half a Route S tick:
**about 45 to 50% less CPU per tick**, and about twice the tick rate on a CPU-bound machine. What that buys:

- The headset gets a new image (one fresh eye) about twice as often. On a processor that makes 50 Route S
  ticks per second, alternation makes about 90 to 100: every 90 Hz headset frame gets a fresh eye instead of
  a repeated pair every other frame.
- Game input and the game's own tick run at the doubled rate; each eye's image is one render old, not two.
- Each eye still updates at half the tick rate, about the Route S pair rate: the saving is in how often the
  headset gets something new, not in how often each eye does. With the game capped (the game's own frame
  limit at the headset's rate), the same picture rate costs about half the processor time.
- The GPU also does one render per tick instead of two.

Costs: the eyes are one tick apart in time. A moving object is where it was 1 tick ago in the held eye, a
horizontal disparity that reads as a small false depth (the Pulfrich effect) and, below the headset's rate,
as a doubled image in fast motion. Head rotation is corrected by the compositor for the held eye; head
translation and animation are not. Object motion under TAA (section 3).

## 5. Variants

- **Adaptive: built, `ETERNALVR_ALTERNATE_EYES=auto` (section 10).** Route S while the processor keeps up
  with the headset, alternation while it does not.
- **Simple fixed alternation (`=1`)**, to measure the saving and see how it looks in a headset.
- Not built: both eyes every Nth tick even while alternating, to bound the eyes' drift in time for moving
  objects.

## 6. Logs and counters

- Start-up: `seq: frame-end job wrapped; per-eye previous matrices on; alternate eyes (no eye R render)`,
  `stereo: alternate eyes on: each game tick renders one eye (ETERNALVR_ALTERNATE_EYES=1), ...` and `seq: Route
  S on: one eye per tick (alternate eyes); ...`.
- The first per-eye views: `seq: eye left of game frame N ...`, then `seq: eye right of game frame N+1 ...`
  (Route S logs both eyes with the same game frame).
- Every 10 s in the `seq:` block: `seq: alternate eyes: A eye L / B eye R render(s); P shown (a fresh eye beside
  the other eye's newest), H held without a partner, M mono; D dropped without a view, G held image(s) given
  up, S not stored; the held eye X game frame(s) older on average (at most Y since the start)`. In steady play
  A and B are equal, P is about A + B, H, G and S near zero and X 1.00. The first `seq: last 10.0 s:` line
  shows `1.00 per game frame`, 0 stereo ticks and 0 eye R frame ends; `previous matrices ... rewritten` stays
  at one per render; the eye tags stay in sync.
- `rates:`: `S stereo pair(s)/s shown` about equal to the tick rate (under Route S the pair rate is the tick
  rate too, but the tick rate is about half).
- `seq-taa:`: accumulation picks eye L and eye R about equal; resets only at starts.
- GPU timing's `stereo tick (eye L + eye R)` line adds eye L of one tick and eye R of the next.

## 7. Rig recipe (not run: the rig was busy)

Build the branch (`tools\evr-build-at.cmd <worktree>`), then each run from its own PowerShell call, e1m2's
start view (`mvvar.ps1` stages the layer, runs e1m2_battle on the simulator with a motion capture every 1000
pairs, and stops the game):

```
$t = '<worktree>'
# 1. Reference: Route S, head still, CPU timing.
& <workspace>\tmp-vr\rs\mvvar.ps1 -Name alt-ref -Seconds 60 -Tree $t -Env @('ETERNALVR_CPU_TIMING=1','ETERNALVR_TEST_HEAD_SWAY=0,0,1')
# 2. Alternate eyes, the same.
& <workspace>\tmp-vr\rs\mvvar.ps1 -Name alt-on -Seconds 60 -Tree $t -Env @('ETERNALVR_ALTERNATE_EYES=1','ETERNALVR_CPU_TIMING=1','ETERNALVR_TEST_HEAD_SWAY=0,0,1')
# 3. Alternate eyes with a head sway and eye captures (the pairs, the ghost check).
& <workspace>\tmp-vr\rs\mvvar.ps1 -Name alt-sway -Seconds 60 -Tree $t -Env @('ETERNALVR_ALTERNATE_EYES=1','ETERNALVR_TEST_HEAD_SWAY=25,6,3','ETERNALVR_CAPTURE_EYES=<workspace>\tmp-vr\rs\alt-sway-eyes,120')
# 4. Alternate eyes with TAA off (no object smear), for the CPU and the look.
& <workspace>\tmp-vr\rs\mvvar.ps1 -Name alt-noaa -Seconds 60 -Tree $t -Env @('ETERNALVR_ALTERNATE_EYES=1','ETERNALVR_STEREO_TAA=0','ETERNALVR_CPU_TIMING=1')
```

Logs are in `<workspace>\tmp-vr\rs\<name>-logs`, motion captures in `<name>-mv`. Pass:

- **The alternation** (run 2): the start-up lines of section 6; `seq: eye left of game frame N` then `seq: eye
  right of game frame N+1`; every steady 10 s window: `seq: alternate eyes:` with eye L and eye R renders
  equal, shown about their sum, held/given up/not stored near zero, held age 1.00; `1.00 per game frame`,
  `0 stereo tick(s)`, `0 eye R frame end(s)`; eye tags in sync with one drain at start; `previous matrices`
  rewritten about once per render; no `stall:` lines beyond the start; no `eye tags out of sync`.
- **The CPU saving** (runs 1 and 2, `cpu:` lines): `tick period` about half of run 1's; `frontend per tick`
  with `eye R render` 0 calls; `the whole process's CPU time per tick` about half of run 1's (per second
  about the same while both are CPU-bound); `rates:` ticks/s about double and pairs/s about equal to it.
  Record the numbers in `perf-baselines.md`.
- **Motion vectors per eye** (runs 1 and 2, head still, `python tools\stereo\motion_diff.py
  <workspace>\tmp-vr\rs\alt-on-mv`): each capture's `.txt` names eye L's tick one below eye R's; the
  static scenery's mean speed in both eyes near zero, as in run 1 (previous matrices from the other eye would
  show a uniform horizontal field of about the eyes' disparity in every pixel); the swaying banner and any
  demon moving in both eyes.
- **The pairs** (run 3): `python tools\stereo\eye_diff.py <workspace>\tmp-vr\rs\alt-sway-eyes
  --disparity` shows the usual negative infinity shift plus the head's turn over one tick; `python
  tools\stereo\ghost_coeff.py ... --pairs 12` stays near the control (no cross-eye history).
- With the option off (run 1) every line is as on main.

## 8. What only a headset can judge

- Whether one-tick-old eyes are comfortable at the rates a slower processor reaches (45 to 90 fresh images per
  second), and how visible the doubled look of fast demons and strafing is.
- Whether moving objects look at the wrong depth (the eyes one tick apart) and whether TAA's object smear is
  acceptable, against anti-aliasing Off.
- Whether the compositor's correction of the held eye looks steady on the Quest 3 through VDXR and SteamVR
  (both get each eye's own pose).
- Whether the HUD panel and the menus stay steady (the GUI now changes every tick).

## 9. Object motion with alternate eyes

A render's TAA history and previous camera matrices are its eye's last render, two renders back (the render
order is L, R, L, R in every mode). Object motion vectors must cover the same time. They come from two
places (`stereo-moved-flag.md` section 5, `stereo-object-motion.md`, and the ring probe of 2026-09-28: the
ring's upload covers only the top of the entity table, the entities that use the ring; the rest take the
per-view path):

| Path | Where the previous transforms come from | With alternate eyes |
|---|---|---|
| Ordinary entities (per-view path) | the entity's previous model matrix, `[RWD + 0xF0]`, which each commit (0x1C8ADE0) sets to the current one `[RWD + 0xC0]` before computing the new one | fixed: the matrix of two renders back (9.1) |
| Ring users (skinned meshes, joint offsets and model matrices) | the object-transform ring's slot of the render before | still the render before, one tick (9.2) |

### 9.1 The previous model matrix of two renders back (`stereo_seq/alternate_prev.*`, `keep_prev_hooks.cpp`)

The previous matrix a commit in render n should leave is the entity's current matrix as it was at the end of
render n - 2. The engine's copy gives the current matrix now, which is that one unless the entity was
committed in render n - 1. So the hook on the commit's copy (RVA 0x1C8AE60, the one `keep_prev` uses) keeps,
per entity, the render of its latest commit and the current matrix that commit replaced (the end of the render
before it), and `planPrevious` decides:

- committed in render n - 1: put that held matrix back as the previous one after the commit (`Restore`);
- committed earlier (or never): the engine's copy is already right (`Engine`);
- committed already in render n (a recommit): keep the previous matrix the first commit left (`Keep`).

Renders are numbered by the frame-end wrapper (`seq_alternate::countRender`, every frame end, eye R's nested
one included): a render's commits run between the frame end before it and its own. The table holds 65,536
entities (5 MB, allocated only with alternate eyes on or auto); larger indices keep the engine's behaviour.
Under Route S's nested eye R (with auto) the same rule gives what `keep_prev` gives (eye R keeps the previous
matrix eye L's commit left, tested), so with alternate eyes on or auto it replaces `keep_prev` for every
render; with the option off `keep_prev` runs unchanged. `ETERNALVR_STEREO_KEEP_PREV=0` turns both off,
`=count` only counts. Tests: `tests/stereo_seq/alternate_prev_tests.cpp` (every pattern of zero, one or two
commits per render leaves the end of render n - 2).

Log, every 10 s: `seq-prev: alternate eyes: R commit(s) given the matrix of two renders back (committed in
the render before), E left as the engine's (not committed since), K second commit(s) in one render keeping
the first one's, U past the table; previous matrices put back P`. In steady alternation with moving demons R
is large, K small, U 0, and P = R + K. A large K would mean a render's commits straddle a frame end (the
numbering assumption failed): then turn it off (`ETERNALVR_STEREO_KEEP_PREV=0`) and report.

### 9.2 The object-transform ring needs a fourth slot

Each render n would read slot(n - 2) (its eye's last render) and write slot(n). Render n + 1 then reads
slot(n - 1), must keep slot(n) for render n + 2, and may not write slot(n - 2): render n read it one render
before, and an upload one render after a read lands before that read's GPU work (the race of
`stereo-object-motion.md` 2.1). So render n + 1 needs a fourth slot; with four, slot(n + 1) = slot(n - 3),
last read by render n - 1, two renders before, as in mono. With the engine's three slots no allocation can
give each eye its own last render: the `ObjectRing` keeps the engine's choice (the render before), and the mixed
Route S and alternating sequences of auto keep its upload rule (tested).

What a fix needs, in order of preference:

- **Two more ring slots per kind.** The ring's buffers are three engine buffer objects per kind at render
  system + 0xC38620 (joint offsets) and + 0xC38638 (model matrices), slot * 8. The renderer's start-up
  (0x1CE5D10, the loop from RVA 0x1CE65DA to 0x1CE66A6, three times) makes each one so (static analysis of
  build 25216728, 2026-09-29):
  - `obj = alloc(0x68, 0x45)` (0x357240: size, memory tag), `ctor(obj)` (0x1BFB170), stored at + 0xC38620 +
    slot * 8, then `create(obj, data, count, elementSize, (short)8, (char)0, (char)4, (int)0x44, 0.5f)`
    (0x1BFB320; bytes = count * elementSize): joint offsets with `data` 256 KiB of zeros on the stack, count
    0x10000, element 4 bytes (a uint32 per entity); model matrices with `data` null, count 0x40000, element 16
    bytes (64 bytes per entity).
  - Shutdown (0x1CE78E0, from RVA 0x1CE7CB7) frees each with `dtor(obj)` (0x1BFB1B0) and a sized delete
    (0x26EC104, 0x68).
  - The parameter setup reads them at RVA 0x1C54A2D, 0x1C54A73 (current) and 0x1C54ABA, 0x1C54B01 (previous),
    each `mov r8, [rax + r8 * 8 + slot base]` then `add r8, 0x18`; the upload at RVA 0x1C00C53 (`mov rdi`,
    joint offsets) and 0x1C00C5B (`mov r12`, model matrices).

  A fourth and fifth slot would be two more objects per kind made the same way (on the thread and at the time
  the renderer allows buffer creation: not known yet), and hooks after those six loads that put a new slot's
  object in place of the loaded one (the counter hooks keep choosing among the engine's three). `ObjectRing`
  would take a slot count (read slot(n - 2), write the least recently used slot last read two or more renders
  ago). Open points for the rig: whether creating buffers after start-up is safe, whether the parameter
  binding caches descriptors per buffer object, and the upload's full-range window after an allocation change
  (`analysis/demon/NOTES.md` in the rig's work folder, lead 2), which a new slot may miss (entities outside the
  uploaded range do not use the ring).
- **A velocity fix-up pass** (UEVR's approach): between the velocity pass and TAA, for pixels whose motion is
  not the camera's alone, v' = 2 v - v_camera, with v_camera from the depth and the eye's two camera matrices.
  It also covers anything else that steps per render (the swaying cloth banner of `stereo-motion-capture.md`),
  but it needs a compute pass inside the engine's frame and the velocity image's exact convention.

## 10. Adaptive alternate eyes (`ETERNALVR_ALTERNATE_EYES=auto`)

Route S while the processor keeps up with the headset's display rate, alternation while it does not; the
launcher's Advanced row "Alternate eyes" is Off (the default), Auto ("only when your processor cannot keep
up") or On. Code: `stereo_seq/adaptive_eyes.*` (the decision, tested), `vkcore/seq_alternate.*` (its use),
`seq_hooks.cpp` (the frame-end wrapper), `presenter_alt.cpp` (the pairs).

### 10.1 Where it switches

A pair's way is chosen once, at its eye L: the first ask of an eye L render (the previous-matrix store, the
per-eye hook or the frame end, all keyed by the render frame) takes the decision's current answer. Both eyes
in the tick: eye L's frame end hands eye L over with `pairInTick`, tells the alternator the pair is done (the
next render starts a pair with eye L) and renders eye R nested, exactly as Route S does. One eye: as with
alternate eyes on. The render order stays L, R, L, R across a switch, so nothing is reset:

| State | Across a switch |
|---|---|
| Previous camera matrices (`PrevMatrixBook`) | per eye, its own last latch: one or two game frames back, whichever it was |
| TAA and DLSS history (`pickAccumulation`) | per eye, by the tag's eye and count: unchanged |
| TAA reset | `AlternateTaaReset(sameTickPairs)`: an eye R of its eye L's own game frame continues the run (tested: no reset after the first two renders through any mix) |
| Jitter phase | Route S's per tick for a both-eyes tick, alternation's per eye for an alternating one (a switch repeats or skips one phase) |
| DLSS twin (eye R) | reset unless eye R evaluated one or two game frames back (`resetTwin(min, max)`) |
| Exposure, scattering history | by the tag's eye: unchanged |
| Object ring | picks by eye and tick; the upload rule holds through any mix (tested) |
| Previous model matrix | the rule of 9.1 for every render, nested eye R included |
| Moved flags | the nested eye R keeps eye L's, as under Route S (`seqChainEye`) |
| Present pairing | a `pairInTick` eye L is held for its own eye R (the other eye's older image released, not counted as given up), which is shown with it at age 0; alternating eyes pair as before (tested) |

The per-eye hook tells a nested eye R by `seqChainEye()`; `seqRenderEye()` gives Right inside the nested render
without asking the alternator.

### 10.2 The decision (`AdaptiveEyes`)

The wall time between the engine chain's frame ends is a tick (the nested eye R, when there is one, inside
it; its own wall time is measured around the nested render). The headset's display rate comes from the XR
worker's `predictedDisplayPeriod` (90 Hz until it has one). Ticks are measured in quarter-second windows:

- **Both eyes per tick, to alternating:** ticks per second below 0.97 times the display rate for 1 s without
  a break.
- **Alternating, back to both eyes:** the estimated Route S rate, the alternating rate divided by (1 + eye R's
  share), above 1.15 times the display rate for 3 s without a break. Eye R's share is its nested render's
  time over the rest of a Route S tick, measured while pairing (0.8 until then, clamped to 0.25 to 2).
- Going back to alternating within 10 s of pairing again doubles the next 3 s wait, up to 24 s; a later
  switch starts again at 3 s.
- Mono time (menus, loading screens, drains) starts the counts again; a tick of the other way (the last one
  before a switch applies, or a Route S tick whose eye R did not render) is not measured.

The estimate assumes an alternating tick is a Route S tick without its eye R. When the game's own frame limit
holds the alternating ticks at the display rate, the estimate is too high (the idle time counts as work) and
auto stays alternating; nothing sets such a limit today (the rig's alternating ticks ran at twice the rate).

### 10.3 Logs

- Start-up: `stereo: adaptive eyes on (ETERNALVR_ALTERNATE_EYES=auto): ...`, `seq: Route S on: both eyes per
  tick or one, as the processor keeps up (adaptive eyes); ...`, `seq: frame-end job wrapped; ...; adaptive
  eyes (eye R nested while the processor keeps up)`, and `seq-prev: ... alternate eyes: each commit leaves
  the current matrix of two renders back as the previous one`.
- Every switch: `seq: adaptive eyes: alternating from the pair after render frame N: both eyes per tick made
  X tick(s)/s for T s, below the headset's H Hz (eye R's render S of the rest of a tick)`, and back: `seq:
  adaptive eyes: both eyes per tick again from the pair after render frame N: alternating made X tick(s)/s,
  both eyes per tick estimated at E for T s, above the headset's H Hz (...)`.
- Every 10 s: `seq: adaptive eyes: last 10.0 s: A s both eyes per tick (X tick(s)/s), B s alternating (Y
  tick(s)/s), M s mono; N switch(es) to alternating, K back; now <way>; headset H Hz; both eyes per tick
  <measured|estimated> E tick(s)/s; eye R's render S of the rest of a tick; back to both eyes after W s above
  H' Hz`, and in the `seq:` block after the alternate-eyes line: `seq: adaptive eyes: P stereo tick(s) with
  both eyes (eye R nested); L eye L image(s) of them held for their eye R, Q shown with it (...)`. L and Q
  are about P in steady play. The alternate-eyes line's eye L count includes the both-eyes ticks' eye L; its
  eye R count only the alternating ones.

### 10.4 Test knob: `ETERNALVR_TEST_CPU_LOAD_MS`

`<ms>[,<seconds on>,<seconds off>]` busy-waits at every render's frame end (eye R's nested one included), so
the rig's fast processor behaves like a slower one; with the two periods the load goes on and off, so one run
shows both switches. Never set by the launcher; logged at start-up as `test: CPU load ...`.

### 10.5 Rig recipe (not run yet)

```
$t = '<worktree>'
# A. Auto on the fast processor: stays both eyes per tick (the rig makes about 140 Route S ticks/s).
& <workspace>\tmp-vr\rs\mvvar.ps1 -Name ad-fast -Seconds 60 -Tree $t -Env @('ETERNALVR_ALTERNATE_EYES=auto','ETERNALVR_CPU_TIMING=1','ETERNALVR_TEST_HEAD_SWAY=0,0,1')
# B. Auto with a load that goes on and off: 8 ms per render for 30 s, then none for 30 s.
& <workspace>\tmp-vr\rs\mvvar.ps1 -Name ad-wave -Seconds 150 -Tree $t -Env @('ETERNALVR_ALTERNATE_EYES=auto','ETERNALVR_TEST_CPU_LOAD_MS=8,30,30','ETERNALVR_CPU_TIMING=1','ETERNALVR_TEST_HEAD_SWAY=10,3,4')
# C. The same load always on, Route S and alternate eyes on: the rates auto should match.
& <workspace>\tmp-vr\rs\mvvar.ps1 -Name ad-load-rs -Seconds 60 -Tree $t -Env @('ETERNALVR_TEST_CPU_LOAD_MS=8','ETERNALVR_CPU_TIMING=1')
& <workspace>\tmp-vr\rs\mvvar.ps1 -Name ad-load-on -Seconds 60 -Tree $t -Env @('ETERNALVR_ALTERNATE_EYES=1','ETERNALVR_TEST_CPU_LOAD_MS=8','ETERNALVR_CPU_TIMING=1')
# D. The previous model matrix of two renders back (9.1), alternate eyes on, head still, against it off.
& <workspace>\tmp-vr\rs\mvvar.ps1 -Name ad-prev -Seconds 60 -Tree $t -Env @('ETERNALVR_ALTERNATE_EYES=1','ETERNALVR_TEST_HEAD_SWAY=0,0,1')
& <workspace>\tmp-vr\rs\mvvar.ps1 -Name ad-prev0 -Seconds 60 -Tree $t -Env @('ETERNALVR_ALTERNATE_EYES=1','ETERNALVR_STEREO_KEEP_PREV=0','ETERNALVR_TEST_HEAD_SWAY=0,0,1')
# E. The option off: every line as on main.
& <workspace>\tmp-vr\rs\mvvar.ps1 -Name ad-off -Seconds 60 -Tree $t -Env @('ETERNALVR_CPU_TIMING=1')
```

With 8 ms per render a Route S tick carries 16 ms of load (under about 55 ticks/s, below 90 Hz) and an
alternating one 8 ms (about 90 to 100 ticks/s), while the Route S estimate stays below 103.5, so auto
alternates while the load is on and pairs again about 3 s after it goes off. Pass:

- **A:** no `seq: adaptive eyes: alternating` line; every 10 s summary `0 switch(es)`, `now both eyes per
  tick`, the measured rate above 90; `P stereo tick(s) with both eyes` about equal to `Q shown with it` and to
  `L ... held for their eye R`; eye tags in sync; the `cpu:` tick period as run E's.
- **B:** `test: CPU load 8.00 ms ...` at start-up; about 1 s into each load period a switch to alternating,
  and about 3 s into each quiet period a switch back; one flip each way per period (no flapping); no `eye tags
  out of sync`, no `stall:` beyond the start, `seq-taa:` resets only at the start, DLSS twin resets 0 (with
  DLSS); the alternating stretches' rates about run C's alternate rate, the paired stretches' about run A's.
- **C:** the rates the two ways make under the load (the thresholds above assume about 55 and 95).
- **D:** `seq-prev: alternate eyes:` every 10 s with most commits `given the matrix of two renders back`, few
  `second commit(s) in one render`, 0 past the table; `python tools\stereo\motion_diff.py
  <workspace>\tmp-vr\rs\ad-prev-mv` against `ad-prev0-mv`: ordinary moving entities' mean speed
  about twice run ad-prev0's (two ticks of motion, as the camera's), static scenery unchanged near zero,
  skinned demons unchanged (the ring, 9.2).
- **E:** no `adaptive`, `alternate` or `test:` lines; `seq-prev:` in its Route S form.
