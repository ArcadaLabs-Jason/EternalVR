# Shadows popping in as the head turns: what Route S changes, and the flat game against VR

The known issue "Shadows can pop in on some walls as you turn your head" came from the seventh headset session
(2026-09-27, release candidate c5f8d15 = main fc8ee49): one capture (20260927-230958, on the owner's PC) of a wall
fragment whose shadow came in late. No rig run had compared VR with the flat game for it. This page reads the
engine's shadow path for what two renders per tick change (section 1), and measures turning on the rig against the
flat game's render (sections 2 and 3). RVAs are in Steam build 25216728.

Result: on the test PC the shadows behave in each eye as they do in the flat game's render, in the current build
and in the reported one. Nothing VR-specific was found, so nothing was changed.

## 1. The shadow path under Route S

Route S renders the game's view slot 0 twice per tick, eye L then eye R. Every render runs the view's shadow setup
job (0x1CF0240), which calls the per-light record function 0x1CFE2B0 for each shadowed light.

- **Which lights get a shadow update.** With `r_shadowUseTileBinningVisibility` 1 (the default; cvar object
  0x66EF470, read at 0x1CF036D) the setup skips a spot or point light whose bit is clear in the view's
  visible-light set. The set is a GPU readback from the tile binning: per view slot a two-buffer object
  (`[[0x66E3B88] + slot * 0xA8 + 0xA0]`, current index at +0x10, stepped by 0x1BFD780) that the render-view
  function (0x1C54650) steps once per render (0x1C54B2F), the setup reads and clears (0x1CF031A to 0x1CF0461) and
  the binning pass writes (bound at 0x1CFBC50). Stepped per render, each Route S eye reads the buffer its own
  previous render filled: a light that comes into an eye's view gets its first shadow update one tick later. The
  flat game reads the buffer filled two frames before. [code, the latency inferred from the order of the calls]
- **Sun cascades in turn.** `r_amortizedSunCascades` (default 1; 0x66EF370, read at 0x1CFE4DA) updates cascade 0
  and the accurate slices (`r_shadowNumAccurateSunSlices`, 0x66E26C0) every render and one of cascades 1 to 3 by
  the shadow cache's frame count mod 3 (cache +0x30, stepped once per setup at 0x1CF02B9, so once per render),
  shifted per light unless `r_skipShadowParallelTickOffset` (0x66EDF90). A cascade that is not due keeps its cache
  entry (its dynamic flag cleared at 0x1CFF263). `r_forceUpdateAllSunCascades` 1 (0x66EF3F0, 0x1CFE74E) updates all
  four every render. Under Route S a far cascade is redrawn every 1.5 ticks, by eye L and eye R in turn; flat,
  every third frame. [code]
- **Slice culling** against the view (`r_shadowSkipSliceViewCull` 0, 0x66EDD10, at 0x1CFE86D) uses the rendering
  view's own frustum, so each eye culls with its own. [code]
- **Counted per render:** the stale ages (`r_shadowMaxStaleFrames`, `perf-cpu-cvars.md` section 3) and the mip
  flicker limiter's history of 16 renders (shadow data + 0x398 per face, 0x1CFE96A and 0x1CFEC8E), so 8 ticks of
  both eyes. The caster LOD check (0x1CA3752) draws a caster that this render's eye did not see at LOD 4, against
  that eye's own visibility counter. [code]

None of these makes a shadow later in ticks than in the flat game in frames; the ones counted per render run
twice as fast per tick. What the eyes share is the cache content: a slice or cascade one eye redrew is sampled by
the other in the same tick, which would matter only for content fitted to one eye's view.

## 2. Rig method

A second test PC (RTX 3080 Ti, i7-9700K), OpenXR-Simulator, the Release layer of main 77bb3610; the game's settings as
this PC has them (`r_shadowAtlasHeight` 8192, ray tracing on); per-eye TAA, foveation off; 1024x1104 per eye
(one e1m3 sweep repeated at 2048x2208). Fixed exposure (`+r_hdrAutoExposureRatio 0`: with auto exposure a jump
re-exposes the whole image for about a second) and both arms held out of view through the test controller input
(after a jump the arms swing back into place). Dense eye captures (`ETERNALVR_CAPTURE_EYES=<dir>,2`; the writer
keeps up with one pair in about 6 ticks). Variants:

| Variant | What renders |
|---|---|
| Route S | the default: two renders per tick, each eye its own view and asymmetric projection |
| same view | `ETERNALVR_STEREO_SAME_VIEW=1`: two renders per tick, both on the game's own view and the flat game's symmetric projection |
| mono | head-tracked mono (Route S off): one render per tick, the flat game's render path; in-headset captures, one per held view, the game yaw from each capture's text |
| all sun cascades | Route S with `r_forceUpdateAllSunCascades 1` (`ETERNALVR_DEBUG_CVARS`) |

Scenes: e1m3 (checkpoint `cp_03_shoot_gate`, the fxd tour's spots: corridors with point lights, a snowy yard),
Reclaimed Earth (e5m2, the balcony, pond and green streak spots: sun on walls and decks). Tests:

- **Jumps:** `setviewpos` yaw jumps of 90 degrees held 1.5 s (Earth 3 spots, e1m3 6 spots, twice round). Each
  frame of a hold against the hold's settled image in 16x16 blocks (1/32 of the width), split into blocks brighter
  than settled (something that darkens later, as a shadow coming in does) and darker, by ticks after the jump.
  Eye L against eye R of the same pair (eye R warped onto eye L by optical flow after shifting it by the two
  projections' offset), and in the same-view render eye L against eye R as they are.
- **Sweeps:** yaw steps of 6 degrees held 0.6 s from 0 to 180 degrees (e1m3 4 spots, Earth 3 spots). Each held
  view's settled image is resampled into one world-fixed window; per world pixel the range over the views that see
  it (second highest minus second lowest) measures how much its shading depends on where it is in the view. Route S
  eye L and eye R against the same-view and mono renders; differences counted where they cover areas wider than
  5 pixels (thin lines are the eyes' 32 mm offset from the turning point, parallax at near edges).
- **Turning:** the simulated head sways 40 degrees of yaw and 10 of pitch every 5 s (`ETERNALVR_TEST_HEAD_SWAY`);
  eye L against eye R warped, as above.
- **The reported build:** fc8ee49 built and run with the same head sway at the e1m3 and Earth starts (it has no
  `ETERNALVR_DEBUG_COMMANDS`, so no jumps or sweeps).

Scripts and runs: `tmp-vr/rs/shadowpop/` (`chain4.sh` jumps, `chain5.sh` and `chain9.sh` sway, `chain6.sh` and
`chain7.sh` sweeps, `holds3.py`, `lr2.py`, `wsweep.py`, `wsweep_caps.py`, `wcmp.py`), runs `shpop-*`.

## 3. Results

**Jumps** (runs `shpop-earth{def,sv}-h`, `shpop-m3{def,sv}-h`; blocks per frame of 1088, by ticks after the jump):

| Run | brighter 8-15 / 16-31 / 32+ ticks, eye L | eye R | darker 8-15 / 16-31 / 32+, eye L |
|---|---|---|---|
| Earth, Route S | 25.0 / 25.4 / 5.3 | 24.0 / 24.3 / 4.1 | 30.9 / 23.6 / 6.2 |
| Earth, same view | 25.5 / 29.4 / 6.8 | 25.6 / 29.3 / 6.7 | 47.4 / 51.0 / 26.2 |
| e1m3, Route S | 136.9 / 137.9 / 30.2 | 138.0 / 131.4 / 27.2 | 71.9 / 51.8 / 10.0 |
| e1m3, same view | 134.8 / 115.0 / 35.0 | 135.0 / 114.9 / 35.1 | 77.7 / 47.4 / 9.6 |

What settles after a jump is the same in both variants and both eyes: glass and ray-traced reflections (about 24
ticks), the whole image in the e1m3 yard for about 30 ticks (fog and volumetric light, by the look of it), edges
along steps and deck rims for about 14 ticks (screen-space ambient occlusion or contact shadows, by the look of
it), particle effects. Eye L against eye R: Route S warped, differing blocks per pair p50 0, p90 4 (Earth) and
p50 0, p90 17 (e1m3, the arms swinging in and near-edge parallax); same view as they are, p50 0, p99 1 (Earth) and
p99 0 (e1m3), max 29 and 12: the two renders of one view agree from the first frame after a jump. Forcing every
sun cascade each render (`r_forceUpdateAllSunCascades 1`, Earth) left the settled images unchanged and the
settling time the same (median 30 to 36 ticks after a jump with and without, both eyes).

**Sweeps** (world pixels whose view-direction range exceeds the reference's by more than 24, in areas wider than
5 px):

| Scene, spot | Route S eye L vs same view | eye R vs same view | Route S vs mono | same view vs mono |
|---|---|---|---|---|
| e1m3 1 | 0.33% | 0.52% | 0.29% / 0.50% | 0.00% |
| e1m3 2, 3, 4 | 0.00 to 0.30% | 0.00 to 0.41% | | |
| e1m3 1 to 4 at 2048x2208 | 0.00 to 0.37% | 0.00 to 0.20% | | |
| Earth 1, 2, 3 | 0.00% | 0.00 to 0.16% | 0.00% / 0.15% (spot 2) | 0.00% |

Every such area looked at is a near edge (floor rims, steps, a railing) shifted by the eye's offset. The surfaces
whose shading does depend on the view direction (a hologram, reflective floors, a room fading in at the end of a
corridor) depend on it the same way in all four variants; the same-view and mono range maps agree with each
other, so two renders per tick do not change it either.

**Turning** (eye L against eye R warped, differing blocks per pair): Earth p50 2, p90 19 (`shpop-wearthdef-c`),
e1m3 p50 4, p90 19 (`shpop-wm3def-g`); the worst pairs are flow failures on near objects and smoke and a floor
highlight that is view-dependent in reality. No shadow differed between the eyes in any pair looked at.

**The reported build** (upper half of the image only: that build's arms stay in view): e1m3 start
p50 4, p90 7, max 10 (fc8ee49) against p50 3, p90 7, max 39 (main; the worst a near container edge); Earth start
p50 0, max 3 against p50 0, max 10. No shadow differed between the eyes in that build here either.

Contact sheet: `tmp-vr/rs/shadowpop/shadowpop-contact-sheet.jpg` (an Earth jump in eye L, eye R and the same-view
render, and the e1m3 sweep's range maps of the four variants).

## 4. What this leaves

- The reported wall was not found again: it may have been in a scene not tried here (only e1m3 and Reclaimed Earth
  could be stepped through; at e1m1_intro's first arena the view is forced and `setviewpos` does not turn it, and
  the Taras Nabad checkpoint teleports did not move the player), or one of the things that settle after a turn in
  the flat game too: ambient occlusion and contact shadows are worked out on screen, so a crease or a step's shadow
  fills in over a few frames once it comes into view.
- The headset was not used; a player's report of a shadow that comes in late in one eye only, with the place, is
  what would reopen this.
- The jumps and sweeps hold the head still between steps; a turn faster than the simulator's sway (about 50 degrees
  a second) was not tried.
