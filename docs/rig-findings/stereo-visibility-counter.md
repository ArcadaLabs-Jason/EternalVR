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
- Effects (particles, flares, beams, ribbons) one eye sees are not drawn there, as before 0.1.22. Section 5 has
  what that does in play with the particle sync on.
- Which type values ordinary models carry was checked only for the e1m1 shells (type 4).

Full notes in the analysis write-up.

## 5. Effects at the strip edge with the particle sync on (rig, 2026-11-08)

A second test PC (RTX 3080 Ti, OpenXR-Simulator, 2048 x 2208 per eye, eye L -54/+40 and eye R -40/+54 degrees), the
particle sync on (T-230, `origin/batch-0200` 3f2129ab), static head through `setviewpos` yaw steps, foveation off.
Runs `tmp-vr/rs/edgefx-*`, scripts and checks in `tmp-vr/rs/edgefx/` (`chain1.sh` to `chain7.sh`, `sweep.py`,
`stripstat.py`).

**The gate.** A build of this tree with the gate's 10 s line split by effect type (held back for one eye: a hit
the engine's test restarted that the two-render test would have continued, with a first-visible delay above 0):

- Particle systems are held back for one eye in every scene tried: median 2,700 to 6,600 gate hits per 10 s (up to
  15,000) in e1m3 (six spots, twelve yaws each), the e1m1_intro first arena and while firing there, a few to about
  ten systems per render. All at the main gather (0x1C775E4), none with flag bit 8 (with it the main gather goes on
  all the same, 0x1C77670). Batch-0200's own layer logs the same scale (`N of them effects, not widened`, 900 to
  13,800).
- Flares, beams and ribbons: 0 or 1 per run. No scene tried had a flare in a strip, so they are untested.
- No effect type had a first-visible delay of 0 (the main gather draws those whatever the count).

**What it shows.** The same sweeps with particle systems widened as models are (a test build: widened only while
the particle sync shares one ring step per tick, so the one eye that draws a system binds the slot its last
generation wrote; patch kept at `tmp-vr/rs/edgefx/particle-widening.patch`): the held count went to 0 and up to
14,600 particle-system counts a 10 s window went on through the other eye's render, yet the pictures did
not change. Pixels differing by more than 16 between the off and the widened run, over the poses both have, in
each eye's outer strip against a centre band of the same width (upper 70% of the image):

| Scene (poses) | Eye R strip / centre | Eye L strip / centre | Noise: batch-0200 against the off run |
|---|---|---|---|
| e1m3 spots 1-2 (23) | 0.41% / 0.68% | 0.35% / 0.68% | 0.35% / 1.18%, 0.42% / 1.26% |
| e1m3 spots 3-4 (24) | 0.50% / 1.14% | 0.89% / 0.76% | |
| e1m3 spots 5-6 (24) | 0.02% / 0.38% | 0.02% / 0.02% | |
| e1m3 lava pit with steam, 2-degree yaw steps (22) | 0.24% / 1.15% | 0.34% / 1.12% | |
| e1m1_intro arena (7) | 0.87% / 1.13% | 0.30% / 0.19% | 1.68% / 3.17%, 1.09% / 0.92% |

The strip differences stay about at or under the centre band's and the batch-0200 repeat's, and every strip
difference looked at (the hologram at the Praetor token, the lava's surface, a moving arm, a swinging cage) is animation that
differs between runs, not something one run lacks. Shotgun impacts aimed 47 degrees to each side (into one eye's
strip; on the left partly in both) left the same decals and glowing holes in both runs and no spark or puff in
either run's captures, which came 0.3 s or more after a shot.
The storm deck (e4m1_rig, two spots) is dominated by rain and lightning differences between runs and says nothing.

So with the particle sync on, the particle systems the gate holds back at the strip edges are, in every scene
tried, ones that drew nothing these captures show; no effect was seen missing in one eye. Not tried: lens flares at a
strip edge, effects in a headset, other maps. The widening was not kept: no visible gain to set against a change
of what the engine draws. Not checked by the test build either: whether the update lists a render consumes are the
gather's of the same render or of the one before (the issue #18 write-up infers the one before); with the one
before, a widened GPU-staged system one eye sees would be drawn in that eye without its GPU draw list entries (the
0.1.34 tiles); no tiles showed in the 93 widened e1m3 poses.
