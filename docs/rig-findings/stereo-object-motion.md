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

## 2. Fix: `src/stereo_seq/object_prev.*`, `src/vkcore/object_prev_hooks.*` (default on; `ETERNALVR_STEREO_OBJECT_PREV=0` turns it off)

Eye R's own render of the tick before is two renders back, and it is still in the ring: its slot is filled
again only by the render after eye R's. A mid hook on each of the two `lea r8d, [rax + 2]` that pick the
previous slot (RVA 0x1C54A8C for the joints, 0x1C54AD3 for the matrices; signature at RVA 0x1C54A84, both
parameters checked by name) gives eye R's render `counter + 2`, so the engine picks (counter + 4) % 3 =
(counter - 2) % 3. That happens only when eye R's last render was exactly two renders back. After a gap (a mono
frame, a missed tick) the engine's own pick stands for that render.

## 3. Checks (the second test PC, 2026-09-27)

- Unit tests: `tests/stereo_seq/object_prev_tests.cpp`.
- Live, e1m1 intro: `eye R's previous-frame object buffers: 2498 from its own render, 2 left as they were`
  per 10 s. Eye R's counter steps by exactly 2 between its renders, which confirms that the counter is per
  render. The ring's six buffers are unmapped (updated through staging copies).
- Ghost check (1280 x 1400, `tools/ghostrun.ps1`): see the merge message.
- The simulator's start view has no animated enemy in sight, and the sharpness of the swaying or firing gun
  (a view-attached mesh) does not separate the two builds. So the effect on the demon still needs the
  headset.
