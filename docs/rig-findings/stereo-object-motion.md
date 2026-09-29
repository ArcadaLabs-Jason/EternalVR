# Eye R smears moving objects under Route S: the previous frame's object buffers

In a Quest 3 test session, eye R drew animated meshes smeared and blocky, in TAA-like streaks, while
eye L was sharp and static scenery matched: the e1m1 giant background demon (R/L sharpness 0.59 to 0.89 on the
demon against 0.9 to 1.1 on the scenery in the same frames), and a zombie's glowing eyes trailing in eye R
only. The tester: "I can feel it more than I can see it. Closing one eye fixes." The analysis compared
disparity-aligned eye pairs from in-game captures (key image `zoom_c14_giant_i.png`). RVAs are in Steam
build 25216728.

## 1. Cause

TAA reprojects its history with motion vectors. For a moving or animated object the motion vector comes from
the object's transforms this frame and last frame: skinning joint offsets and model matrices. The render-view
job's parameter setup (RVA 0x1C54650) binds them from a ring of three buffers per kind (the render system at
`[r12 + 0x4D9DC0]`, joint offsets at + 0xC38620 + slot * 8, model matrices at + 0xC38638 + slot * 8):

| Parameter | Slot |
|---|---|
| `jointOffsetsBuffer`, `modelMatricesBuffer` | counter % 3 |
| `prevJointOffsetsBuffer`, `prevModelMatricesBuffer` | (counter + 2) % 3, the render before |

The counter (`0x1CBB2D0`, `[[frame + 0xF58] + 0xB0]`) goes up by one per render, not per game frame; each
render fills its own slot (RVA 0x1C00B40, through staging copies: the buffers are not mapped). Under Route S a
tick is two renders of one game frame, eye L then eye R:

- eye L's previous is the render before, eye R's of the tick before: the previous game frame, as it should
  be;
- eye R's previous is eye L's render of the same game frame: the same pose, so every moving object has zero
  motion in eye R, and its TAA blends the object's history from where it no longer is.

Camera motion was already per eye (`prev_matrices`, `stereo-temporal.md`), which is why static scenery is
sharp in both eyes. The ghost checks used views without moving objects.

## 2. Fix: each render its own ring slot (default on; `ETERNALVR_STEREO_OBJECT_PREV=0` turns it off)

The slot allocator is `src/stereo_seq/object_prev.*` (class `ObjectRing`, no game code, unit tested); the
hooks that hand its answers to the engine are `src/vkcore/object_prev_hooks.cpp`.

### 2.1 How it got here

The root cause is a race on the object-transform ring: the engine keeps the skinning joint offsets and model
matrices in a ring of three GPU buffers indexed by the per-render counter, and a slot's upload is not ordered
after the GPU work of earlier renders that read it. In mono a slot is written again two renders after its
last read, which the engine's frames in flight allow. Under Route S eye R's previous frame was wrong, so its
moving demons smeared under TAA. Four steps, all on 2026-09-27 and 2026-09-28:

- `0e62150`: eye R's previous-frame picks got `counter + 2`, so eye R read its own render of the tick
  before, two renders back. That left one render between eye R's read and the slot's next upload (the next
  eye L's). With the headset's frames in flight the upload often came first: eye R read the next tick's
  transforms and its motion vectors pointed backwards (same-view captures on the owner's Quest 3, eye R's
  zombie and gun reversed against eye L).
- `7cf1741` (package `0aabf68`): one slot per game tick. Both renders used eye L's slot (eye R uploaded the
  same game frame there) and read the tick before's. The enemies were fixed, but the world strobed in the
  headset: eye R's upload landed while eye L's GPU work still read the slot, and the two renders'
  transforms are not identical.
- `eded94f`: each render's eye is looked up by its own counter instead of the tag in flight (section 2.3).
  Before it only about 40% of renders took the per-tick picks. The strobing did not change.
- `ac01e0d`, with the QA fixes `f45216b` and `4e4df1f` (merge `122dd2d`): every render gets a slot of its
  own. Headset test on the owner's Quest 3, 2026-09-28: no strobing, and demons as sharp in eye R as in eye L.

### 2.2 The slot allocator: `ObjectRing` (`object_prev.hpp`, `object_prev.cpp`)

`ObjectRing::picksFor(eye, tick, counter)` gives each render two counters to hand the engine: `current` for
the upload and the current-frame buffers (the engine takes `current % 3`) and `previous` for the
previous-frame buffers (the engine takes `(previous + 2) % 3`). `remapped` is false when both are the
render's own counter. Every render goes through the ring, mono ones too, and a render without a tag (the
main menu, a load) is asked as `Eye::Mono` with tick 0: the engine's own picks never mix with the ring's.

Each slot records the render that wrote it (eye, tick, counter) and the counter of the latest render that
wrote or read it. Counters are compared by unsigned difference, so the counter may wrap.

The previous frame (`previousSlot`):

- eye R: the tick before's eye R while a slot still holds it, else the tick before's latest render;
- eye L: the tick before's latest render (in steady stereo, its eye R);
- mono, or nothing of the tick before left in the ring: the slot the render just before wrote
  (`lastSlot_`), which is the engine's own choice.

One rule then overrides that choice: the previous frame is always a slot the render just before wrote
(`lastSlot_`) or read (`lastRead_`); when it is neither, the render takes that render's previous frame
(`lastRead_`) instead. The render writes the least recently used slot other than the one it reads, which
is then the third slot, one the render just before did not touch. Without the rule a render between ticks
(untagged, or mono still carrying the last tick, as `seq_hooks` tags mono renders) can leave the tick
before's eye R in that third slot; eye R would read it and write a slot eye L was still using.

In steady stereo eye L keeps one slot and eye R alternates between the other two, and both eyes read the
tick before's eye R:

| Render | Reads | Writes |
|---|---|---|
| tick t, eye L | B (eye R of t - 1) | A |
| tick t, eye R | B | C |
| tick t + 1, eye L | C (eye R of t) | A |
| tick t + 1, eye R | C | B |

Every slot is written again at least two renders after its last read, as in mono, and no render writes a
slot another render of its tick reads. The first stereo tick after mono has nothing of the tick before in
the ring: eye R reads eye L's render of the same tick, so moving objects have no motion in eye R for that
one tick.

Five hooks ask about each render (section 2.3), so the ring remembers its last eight answers by counter and
a render asked again gets the same one. A render whose answer was dropped would be decided again as a new
render. The allocator assumes that renders are first asked in counter order; `picksFor` sets its `inOrder`
flag false when a first ask's counter is not the last one plus 1, and the hooks count those (a render
decided again after its answer was dropped counts there too).

### 2.3 The hooks (`object_prev_hooks.cpp`)

Five mid hooks, each on the instruction that takes the render counter the engine just read (`rax`); the
hook replaces its low 32 bits with the ring's counter:

| Site | Signature | Hook | Instruction | Counter given |
|---|---|---|---|---|
| `jointOffsetsBuffer` | RVA 0x1C549F1 | + 0xF | `mov r8d, eax` | `current` |
| `modelMatricesBuffer` | RVA 0x1C549F1 | + 0x55 | `mov r8d, eax` | `current` |
| `prevJointOffsetsBuffer` | RVA 0x1C54A84 | + 0x8 | `lea r8d, [rax + 2]` | `previous` |
| `prevModelMatricesBuffer` | RVA 0x1C54A84 | + 0x4F | `lea r8d, [rax + 2]` | `previous` |
| the upload (in 0x1C00B40) | RVA 0x1C00C22 | + 0xC | `mov r8d, eax` | `current` |

Each signature must be found once, the counter calls at all three sites must reach the same function in
the code section, and each parameter load must name the expected parameter (the name pointer sits 0x10
before the parameter global); otherwise nothing is installed.

All five or none: `g_live` is set only once all five hooks are in, and until then every hook leaves the
engine's picks alone. A hook that went in before a later one failed cannot be removed, so it stays in and
does nothing. Slots from the ring in some places and the engine's in others would mix frames.

The multiplayer guard: nothing is installed unless `mp_guard::allowsGameTouch()` is true, and each hook asks
it again first and does nothing once the guard has tripped.

The render's eye and tick come from the tag that presents with backend frame counter + 1
(`seqTagForBackendFrame(counter + 1)`): the render counter and the backend frame counter are in step, as
the ring trace showed on every render. Not the tag in flight (`seqTagInFlight`), which reads the backend
frame counter again at the hook: the counter function (RVA 0x1CBB2D0) reads that same counter
(`[[0x66E2C30 + 0xF58] + 0xB0]`), but the render thread's swap moves it on while the render-view job is
still running, often under a headset's load, and a later read then names the next render (the world strobed
when the ring used it). See `stereo-temporal.md`, "Which counter finds a render's tag".

The install line names the five hook RVAs. Every 10 s, counted once per render at the `jointOffsetsBuffer`
hook:

```
seq-objprev: object-transform ring: N render(s) on other slots than the engine's, N on its own, N of them
without a tag, N first asked out of counter order
```

### 2.4 The ring trace (`src/vkcore/ring_trace.*`)

`ETERNALVR_STEREO_RING_TRACE=1` (off by default) records ring events from the five hooks: after the first
20,000 it keeps 600 and logs them once. Each line has the site, the engine's counter, the counter given and
its slot (`% 3`), the chain's eye (`seqChainEye`), the eye of the tag in flight, the eye, tick, render
frame and backend frame of the render's own tag, the thread and the time in ms. It showed that a render's
own tag is the one that presents with backend frame counter + 1, on every render (`eded94f`).

## 3. Checks

- Unit tests, `tests/stereo_seq/object_prev_tests.cpp`. A replay checks every answer: asked again, the same
  counters; no slot written sooner than two renders after its last read. Cases: the steady stereo pattern
  above; no render writes a slot another render of its tick reads; mono renders go round the ring, each
  reading the render before; the first stereo tick after mono; a tick without eye R and an eye R without
  its eye L; renders between ticks (mono with a stale tick, untagged, one between eye L and eye R); every
  mix of up to three renders between ticks (seven kinds of step, 399 sequences); the out-of-order count.
- Live, e1m1 intro (the second test PC, 2026-09-27, with `0e62150`): eye R's counter steps by exactly 2 between its
  renders, which confirms that the counter is per render. The ring's six buffers are unmapped (updated
  through staging copies).
- Headset, the owner's Quest 3, 2026-09-28, with `ac01e0d` and its QA fixes: no strobing, and demons as sharp
  in eye R as in eye L.
- Same-view velocity captures with the fix on and off: `stereo-motion-capture.md`.

Still open:

- The damned souls reaching out of the walls can still differ slightly between the eyes.
- The swaying banner at the top of the `e1m2_battle` start view (Havok cloth) has exactly zero motion in
  eye R. It is not the object ring: see `stereo-motion-capture.md`.
