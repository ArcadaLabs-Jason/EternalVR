# Models seen by one eye only were never drawn under Route S

A headset session (2026-10-04, v0.1.21) showed pickups disappearing and a breakable wall's glow cut off at
the right edge of the right eye. RVAs are in Steam build 25216728.

## 1. What the rig showed

In the first arena of `e1m1_intro` (static head, `setviewpos` yaw steps), a box of shotgun shells:

- drawn in a mono run at about 50 degrees to the right, missing in eye R, whose view reaches 54 degrees;
- drawn in eye R only once it was inside eye L's view too (eye L reaches 40 degrees to the right); on the
  other side, missing in eye L at -46 degrees (outside eye R's -40);
- drawn in both eyes when both render the game's own view (`ETERNALVR_STEREO_SAME_VIEW=1`).

So each eye drew the model only when the other eye's frustum held it as well. None of the culling switches
changed it (each held through `ETERNALVR_DEBUG_CVARS` and read back): `r_skipModelGPUCulling 1`,
`r_skipModelCPUCulling 1`, `r_skipSurfaceCPUCulling 1`, `r_useUmbraCulling 0`, `r_usePosedVisibilityCheck 0`,
`g_enable_animated_visibility_box 0`, `g_enable_visibility_Box 0`, `r_useComputeSkinning 0`,
`r_skipSkinnedModels 1` (the shells still drew: not skinned), nor `discontinuousViewPosition` on both eyes.
A hanging cage (rigid, animated) drew in eye R's outer strip.

## 2. Cause

Every non-static model has a `firstVisibleFrameCount` (`idRenderModelParms` g + 0x68, default 2 written at
0x18D8FB1; the commit at 0x1C8B6B1 packs it into bits 28..30 of the model's flag qword, flag bit 0 `isStatic`
exempts the model). The gathers add a model to a view only after it has passed culling in more consecutive
renders of that view slot than that count:

```
0x1C775E4  mov   eax, [rcx + 0x2BC1F4]          ; the view's render counter
0x1C775EA  cmp   [rcx + rdx*4 + 0x1A81F8], eax  ; lastVisible >= counter: counted in the previous render?
0x1C775F1  jl    restart                         ;   no: the count restarts
0x1C775FB  inc   eax
0x1C775FD  mov   [rcx + rdx*4 + 0x1A81F8], eax  ; lastVisible = counter + 1
0x1C7760A  mov   [rcx + rdx*4 + 0x1E81F8], r8d  ; the consecutive count
0x1C7761D  cmp   r8d, (flags >> 28) & 7          ; more than firstVisibleFrameCount: drawn
```

(main gather job 0x1C76C80, after the frustum test, Umbra and the per-render dedupe; the same gate twice in the
second gather 0x1C78D50 at 0x1C79137 and 0x1C79442). The state S = `[[0x66E3B88] + 0x220 + viewIndex * 8]`
is per view slot, and the render thread steps its counter once per render (`inc dword [r8 + 0x2BC1F4]` at
0x1C5DF63 in 0x1C5DED0, from the render-thread frame 0x1CD8380 at 0x1CD84D1). Both Route S eyes render view
slot 0, so each render's previous one is the other eye's: a model inside one eye's frustum only has its count
restarted at 1 on every render and stays under the gate. The game-side "was it rendered" query 0x18E7C70
(corpse removal) and a shadow LOD check (0x1CA3752) read the same counter.

## 3. Fix: `src/vkcore/vis_gate_hooks.*`, `src/stereo_seq/vis_gate.hpp` (default on; `ETERNALVR_STEREO_VIS_GATE=0` turns it off)

A mid hook on each gate's counter load (0x1C775E4; 0x1C79131 and 0x1C7943C) does the load and the compare
itself and resumes on the engine's own path: the count load when the count goes on, the `inc eax` when it
restarts (the count register is already 0 there). The test is widened from the previous render to the last two
(`lastVisible >= counter - 1`), so under Route S a model counted by either eye in the previous tick goes on;
a model in one eye's view only gains one count per tick and passes the gate after three ticks, the engine's
own delay. The engine's stores (`lastVisible = counter + 1`, the count) and the counter itself are unchanged.
In a mono frame the wider test only lets a model that dropped out for exactly one render keep its count.
Alternate eyes (one eye per tick) had the same problem and gets the same fix. Parallel Eye Rendering renders the
eyes as two view slots, each with its own counter, so it is unaffected. The log counts, every 10 s, the counts
that went on only through the wider test, those the engine's test passed, and the restarts.

Only models are widened, never effects. At each gate rbx holds the model's flags; bits 25-27 are its type, and
types 0-3 (particles, flares, beams, ribbons) keep the engine's one-render test.

- **Why not effects.** 0.1.22 widened every gate and added two companions: occlusion query copies without
  `VK_QUERY_RESULT_WAIT_BIT` (0x1C32F20; the flag stores at 0x1C32FAF and 0x1C33039) and a particle
  `UpdateInView` check over the last two renders (0x195523D). Flares are the only users of that query pool (the
  slot allocator 0x1C343C0 has one caller, the flare `UpdateInView`): each takes two slots per render. A flare
  drawn in one eye's render only left queries pending that the other eye's copy waited on forever (the game's queue
  stalled a few seconds after the first teleport, XR still at 90 Hz). Copied without the wait, an unfinished query
  kept another flare's count instead (slot numbers restart every frame), and a flare could flash at full brightness
  for a frame: public issue #18, lightning in Exultia. The widened particle check also drew a one-eye system from
  another render's ring slot.
- **Rig check (RTX 3080 Ti, 2026-10-05, e1m1 shells in each eye's outer strip, `vis-run.sh`).** Effects not widened
  and the engine's waiting copies back: the shells draw at yaw 45/40/35 and no stall in 130 s. The old widening of
  every gate with the waiting copies back: stalled at 80 s (0 ticks/s). All two-render continues came from the
  main gate (0x1C775E4), half of type 0 and half of type 4.

So the query copies and the particle check are the engine's own again. Effects one eye sees stay unseen there,
as before 0.1.22.

Rejected: skipping the counter's step (`inc dword [r8 + 0x2BC1F4]` at 0x1C5DF63) in eye R's render, so both
eyes share one value per tick. It drew the pickup, and hung like the gates alone (the query copies' wait, found
later), but it also changes what the shadow LOD check and the game's "was it rendered" query read, so the gate
hooks were kept.

## 4. Not covered yet

- The surface-spawned particle slots compare `[model + 0x4AC]` with worldData + 0xBFD8 minus 1 (0x18DA66D) and
  reset every tick under Route S.
- Effects (particles, flares, beams, ribbons) one eye sees are not drawn there, as before 0.1.22: a wall glow at
  the strip edge can be missing in that eye.
- Which type values ordinary models carry was checked only for the e1m1 shells (type 4).

Full notes in the analysis write-up.
