# Stereo

`ETERNALVR_MODE=stereo` runs **Route S, synchronized sequential stereo** (D-032, D-037, T-066, T-069):
each game tick is rendered twice, once per eye, from the same tick, and the two images go to the headset
as one projection layer. The design and every address are in `docs/rig-findings/stereo-routes.md`
section 2 (Steam build 25216728). The engine's own two-view path (EngineNativeStereo) is not available on
this build; its findings and the experiment harness that remains are at the end of this document. Parallel Eye
Rendering (`ETERNALVR_PARALLEL_EYES=1`, experimental, off by default) renders both eyes as two views of one
frame instead (section "Parallel Eye Rendering").

Status (2026-09-26): **Route S v1 runs on the rig and is re-tested with the motion controllers** (OpenXR-Simulator): stable, exactly two renders per
game tick, every eye pair complete and in step, each eye with its own pose, projection and weapon
(section "Live results"). **Open:** the HUD and every other 2D overlay are drawn in eye L only; not yet
tried on the Quest 3.

## How Route S works

1. **Eye R re-run.** Every engine render frame ends in the frame-end job (RVA 0x1CBA1C0), which the frame
   middle job queues from a function pointer in `.data` (RVA 0x39A9600). The layer swaps that pointer for
   a wrapper (`src/vkcore/seq_hooks.cpp`). The wrapper runs the original first: that is eye L, the
   engine's own chain, handed to the render thread as usual. On a stereo tick it then calls the render
   system's synchronous render (RVA 0x1CBEA30, vtable slot 0x50; the loading screens render through it
   without a game tick) with the same arguments, which runs the whole frame chain again for eye R and
   ends in the wrapper once more (which then only runs the original). The screenshot request of the frame
   info is cleared for eye R's call so a screenshot is taken once. No code is patched for this; eye R
   renders like a fast next frame that had no game tick in between.
2. **Per-eye view.** The per-eye hook (RVA 0x1C754BC, `stereo_hooks.cpp`) runs in both chains after the
   engine copied the game's head-centred view into the render view and before the latch and the Umbra
   request. It asks which chain it runs in (`seqChainEye`) and writes that eye's origin (the head
   position at `ETERNALVR_WORLD_SCALE` plus the eye's offset), view axis and asymmetric projection
   (`explicitProjectionMatrix` with `useExplicitProjectionMatrix`, honoured by the latch: E2), plus the
   per-view flags: `forceFullResolution` (+0x11) on both eyes, `skipAutoExposureUpdate` (+0x74C) on eye R
   (exposure adapts once per tick, in eye L's frame; the auto-exposure index hook,
   `src/vkcore/exposure_hooks.*`, gives eye R the exposure eye L wrote, in every TAA mode; without that
   hook eye R updates its own, so the two eyes form one adapting chain), `inhibitModelFovScale` (+0x13) and,
   for S5, `discontinuousViewPosition` (+0xC). After each eye's latch (post-latch hook, RVA 0x1C75772) two sets
   of matrices are repaired (`src/stereo_seq/centered_matrix.*`): with `useExplicitProjectionMatrix` set
   the latch builds `centeredViewProjectionMatrix` (+0x296B0) from the explicit matrix, depth rows
   included, and the weapon disappears (S2), so its depth row is read from the centred matrix the
   world-views pass latched from the game's own view and written back; and the four hands-and-guns
   matrices (+0x295B0, +0x29630, +0x29770, +0x297F0), always built from the symmetric weapon FOV, get
   the eye's frustum rows, so the weapon registers with the world in each eye. The eyes come from
   `xrLocateViews` in `LOCAL`, taken relative to the head located in `LOCAL` at the same time
   (OpenXR-Simulator reports `VIEW`-space eye poses with the head's height in them); an eye more than
   15 cm from the head centre makes the tick mono. Eye R uses the record of the game frame eye L was
   drawn for (and only while the render view still carries that frame's axis). The decision is made
   here: eye L's view is written only when the tick can be a stereo one (the multiplayer guard armed, the
   eye tags in step, below); a tick that could be stereo but whose tags need a new base first stays the
   game's own view and is marked wanted, so that the base is taken on that mono frame and the next tick
   pairs. A frame drawn for eye L is therefore never shown without its eye R.
3. **Previous-frame matrices per eye.** Each render frame starts by copying the render view's current
   matrices into its previous-frame fields (RVA 0x1CE2340). With two renders per tick each eye would get
   the other eye's. A hook after that store (RVA 0x1C75D81) keeps the stored bytes for the other eye and
   writes the ones kept for this eye (`src/stereo_seq/prev_matrices.*`): what the store computes in eye R's
   frame is exactly eye L's next previous state, and what it computes in eye L's frame after a stereo
   tick is eye R's. Only the ranges the store writes are touched (0x29480 to 0x298E8, nine ranges,
   checked against the store's code). Kept bytes are used only by the render frame right after the one
   that kept them (renderSystem + 0x10): a frame in between (mono, a loading screen, a map change that
   gives a new view an old one's address) leaves the engine's store as it is.
4. **Eye tags and pairing.** Each frame the wrapper hands to the render thread gets a tag: its eye (L, R
   or mono), its game frame and whether its view was written. The render thread presents frames in
   order, so the present hook takes one tag per present, checked against the backend frame counter
   (renderBackend + 0xB0, raised just before each swap): each tag carries the counter value its present
   must have, counted from a base taken while the render thread was idle. Taking the base means holding
   a frame-end job (of a wanted mono frame, above) until the render thread has presented every frame
   handed to it (`src/stereo_seq/render_idle.*`): the wrapper counts the frames it hands over, and once a
   base exists the next one is taken the moment the backend counter has caught up with that count (a
   slow frame is waited for, however long the counter stands still: a base taken under it would pair two
   different frames). The first base, and one after the counts stopped agreeing, is taken after 100 ms
   without a backend frame. A drain gives up after 250 ms (mono for 2 s, then the next base comes from a
   quiet period; each failure in a row doubles the wait, up to 16 s, and a drain that succeeds starts over,
   `src/stereo_seq/drain_backoff.*`: a render thread that will not go idle costs a 250 ms hitch less and
   less often); at most one drain per second. A disagreement (a tagged
   frame that never presented, a present no tag was queued for, more than 8 queued) drops the tags out of
   step: presents are then shown mono until the next base. The pairing (`src/stereo_seq/eye_pairing.*`)
   copies eye L into the left half of a free ring slot, holds the slot, copies eye R of the same tick into
   the right half and hands the slot to the XR worker; a half whose partner is missing is dropped (never
   shown alone), mono frames go into both halves.
5. **Projection layer.** The ring and the XR swapchain are two eye images wide. A pair is submitted with
   each eye's half, pose and FOV; a mono frame (menus, loading, a mono tick) with the head pose and the
   game's FOV in both halves; the cinema quad shows the left half.

Both presents also reach the game's window, and the window is only a mirror that must not pace the game: the
game renders as fast as it can, or one pair per headset frame under Frame pacing (below). How the layer keeps
the desktop display from pacing it is in Desktop window, below. The ring keeps one
command buffer per slot half: eye R's copy into a slot is recorded while eye L's may still be pending. A pair
whose game frame record is gone is not shown (the headset repeats the last pair).

**Lens flares.** A lens flare (idRenderModelFlare) builds its quads on the CPU in clip space: its update (RVA
0x1936650) projects the flare with the render view's view and projection matrices and writes the quads into
the transparency-quad ring, once per render. Its only caller, the flare job of the world update (RVA
0x18E1670), runs before the screen-views pass, so the matrices were the world-views latch of the game's
head-centred view and both eyes drew every flare at the head's position: a floor flare 28 px apart between
the eyes where the scenery next to it is 251 px apart, so it looked farther than infinity. The layer records
each flare the job updated (its vertex block, quad count and the intensity the prepare left; hooks at RVA
0x18E1704 and 0x18E1716, `src/vkcore/flare_views.cpp`) and after each eye's latch runs the update again with
the eye's render view, over the same block, with the intensity put back and with the two occlusion query
slots the engine's call took in that render (hook at RVA 0x1936731), so nothing is allocated and the queries
the render issues still go with the vertices. Mono renders keep the engine's quads; Parallel Eye Rendering is
left alone (both of its views draw one set of quads). The log has the hooks at start-up (`seq-flares:`), the
first eye with its own flares and every 10 s the flares rebuilt and anything left as the engine wrote it.
`ETERNALVR_FLARES_PER_EYE=0` turns it off.

**CPU particles and effects** (off by default since 0.1.35, `ETERNALVR_STEREO_FX_SYNC=1` turns it on, `=count` only
counts; 0.1.34 had it on, and eye R drew snow and some effects in e1m3 as tiles of their whole sprite sheet, seen in
players' headset captures and never on the storm deck).
Sprites, smoke, sparks and blood go through a ring of three vertex slots: each render draws the vertices the render
before it generated and generates new ones for the render after it. Under Route S that put eye L one tick behind
eye R on every CPU particle. In eye R's render the ring is not advanced (its previous slot, which eye L's frame
middle closed, is opened again for eye R's lens flares and tracers), the particle light pool keeps eye L's count,
and the generation of every particle system and effect eye L generated in the tick is skipped (hooks at RVA
0x18E791C, 0x18E7ABD, 0x195526F and 0x195166E, `src/vkcore/fx_sync_hooks.cpp`). Eye R draws eye L's binding and
shows eye L's particle lights, so both eyes show the tick before, as mono does. A particle system eye L did not
have (seen by eye R only) is generated by eye R as the engine does. All four hooks act or none does; mono,
alternate eyes and Parallel Eye Rendering are left alone. Particle lights are written once per tick, in eye L's
render. With ray tracing on, eye L's particles may be missing from eye R's reflections
(`docs/rig-findings/stereo-fx-lag.md`; a first rig run showed the shapes alike in both eyes and pooled lights
missing in eye R, fixed since). Rig A/B on 2026-10-08 (storm deck, same view, 3-frame bursts): bursts with an eye
L/R difference above 2 went from 11 of 17 to 0 of 17 without firing, lightning-strike onsets and strike-lit
surfaces included, and from 6 of 17 (up to 16.6) to 0 of 17 (at most 1.6) with firing, so muzzle flashes, their
lights and impact effects match; normal stereo keeps its parallax, with no flat sprites. A separate light-update
fix showed no measurable gain on top and is parked. Not tried in a headset yet.
The e1m3 tiles, as read from the code (a first e1m3 rig run had them in eye R with `ETERNALVR_STEREO_FX_SYNC_GPU=0`
and none without it): a particle system can have GPU particle stages, and the GPU particle manager's draw lists and
emitter records are reset every render, eye R's included, then filled only by the particle update's bind and
generation, both of which eye R skipped for what eye L generated. Their instances then went into eye R's GPU step
without this render's data. Eye R now binds the GPU stages of such a particle system itself (0x1C2B670 per enabled
GPU stage, as the bind at RVA 0x1955949) and runs the engine's generation for it, while its CPU stages keep eye L's
binding; every other particle system and effect keeps the reuse (`src/vkcore/fx_gpu_stages.cpp`;
`ETERNALVR_STEREO_FX_SYNC_GPU=0` reuses them too, as 0.1.34 did). If its checks fail nothing is installed. A
read-only hook on the GPU particle step (0x1C28DD0) adds the per-eye counts to the `seq-fx:` line
(`docs/rig-findings/stereo-fx-lag.md`, section 7).

## Stack

Eye R's whole chain runs nested inside eye L's frame-end job on the same job thread's stack (the engine
runs the jobs of a job list it waits on inline; the S2 log shows eye L's per-eye hook, the frame end and
eye R's per-eye hook on one thread), and each job of the chain keeps a job list of about 14 KiB on the
stack (`sub rsp, 0x38C0` in the frame-end job, 0x3870 in render one and the render-frame job). The
engine never nests two chains, and many of its threads are created with 256 KiB stacks. A stack overflow
ends the process without a crash report, a dump or an event log entry: the game's crash handler needs about
33 KiB of stack for its first log line and faults again on an overflowed thread, and the game runs with
`SetErrorMode(3)`, which keeps Windows error reporting quiet. That is what the one silent exit of S2 looked
like. So eye R is rendered only when the stack below eye L's frame end holds the deepest eye R chain
measured so far (at the wrapper, the per-eye hook and the previous-matrix hook of eye R's chain; 64 KiB
before the first measurement) plus 64 KiB (`src/stereo_seq/stack_budget.*`); otherwise the tick stays
mono (eye L's half dropped). The first stereo tick logs the stack left and its size; every 10 s the log
has the deepest eye R chain and the least stack left.

## Fail closed

Route S installs only when every check passes; otherwise the layer logs the reason and runs head-tracked
mono (`seq: ...; stereo off`):

- each of its ten signatures matches once in `.text` (frame-end queue site, frame-end job, its guard
  clear, render one frame, render-frame job, world-views pass, previous-matrix store call, the store,
  the render system object, the render thread's swap);
- the slot the queue site reads lies in a writable, non-executable section and holds the frame-end job;
- the guard clear lies inside the frame-end job;
- the synchronous render queues the same render-frame job the engine's own frames start with;
- the render system's vtable slot 0x50 is the synchronous render;
- the previous-matrix store call lies in the world-views pass and calls the store;
- the camera hook and the per-eye hook are installed; the swap is an interlocked compare-exchange from
  the expected pointer.

At run time a frame-end job for another render system object turns Route S off.

## Multiplayer guard

Every Route S write asks `mp_guard::allowsGameTouch()` (docs/rig-findings/mp-guard.md): the wrapper
only runs the original (no eye R) once the guard is not armed, and asks again right before eye R (a trip
after eye L drops eye L's half), the per-eye hook writes nothing, the
previous-matrix hook leaves the store alone, and the hooks are installed only while the guard is armed.
The pointer swap stays in place after a trip (hooks are never removed live); the wrapper then just
forwards. The engine two-view experiment hooks check the guard the same way; Parallel Eye Rendering's are in
its section. The run-time cvars (TAA,
comfort, CPU Saver, Sharpening, prompts) go back to their values before the layer's first write on a trip;
the window and present set and `r_hdrDisplay` stay (mp-guard.md section 1).

## Cvars (v1)

Temporal effects would mix the eyes (each eye would read the other's history), so v1 turns them off:
`r_TAASafeMode 1` (no temporal accumulation), `r_antialiasing 0` (no TAA or DLSS), `r_jitter 0`,
`rs_enable 0` (no dynamic resolution; `forceFullResolution` is set per view as well) and
`r_swapInterval 0`. They are set **on the command line** by the launcher and the launch helper
(`launch-ht.ps1 -Stereo`, before `-ExtraArgs`, so a later `+r_TAASafeMode 0` wins, as S3 needs). The layer
logs at start-up whether the command line carries the v1 set (`seq: command line cvars:`), and every 10 s
the `r_swapInterval` value the render thread actually reads.

**The command line does not hold them.** The game applies the player's settings after it: read back at run
time through the engine's own setter, the owner's first headset session ran with `r_TAASafeMode 0`,
`r_antialiasing 1` (TAA on), `r_swapInterval 1`, `r_dof 1` and `r_SSR 1`, whatever the command line said.
With TAA on, each eye's TAA reads the other eye's history (backend-frame parity,
`docs/rig-findings/stereo-temporal.md` on the stereo-taa branch), and every eye image carries a faint copy of
the other eye's image at the same pixels: displaced by the eye frustums' shift (about 27 degrees), drifting
against the geometry as the head moves. That was the owner's "faint full copy of the map, off centre"
(section Ghost check below). So under Route S the layer **holds** the part of the v1 set stereo
correctness needs at run time (`src/vkcore/runtime_cvars.*`): `r_TAASafeMode 1` and `r_antialiasing 0`,
written through `idCVar::SetString` (RVA 0x376020, located by signature; the cvars by their registration),
checked at every present and written again if the game puts them back, only while the multiplayer guard
allows touching the game (`cvars: held at run time: ...`, `cvars: r_antialiasing 1 -> 0 (reads 0)`). Nothing
else of the v1 set is forced (SSR and the rest stay as the player set them; the comfort set below is held on its own); the present
interval is the layer's (Desktop window). The writes are not saved: the text configs after such a run carry
neither key, and the next run reads the player's `r_antialiasing 1` again. `ETERNALVR_STEREO_RUNTIME_CVARS=0`
leaves the cvars as the game has them. The single policy for these cvars is
`stereo_seq::stereoRuntimeCvars(StereoTemporal)`: `Off` gives this set, `PerEye` none; a per-eye temporal
module (per-eye TAA) reports itself with `runtime_cvars::setStereoTemporal(PerEye)` and then owns the TAA
cvars, so the two never write against each other.

**The window and present set is held in both temporal modes** (`stereo_seq::stereoWindowCvars`):
`r_fullscreen 0`, `r_swapInterval 0` and `r_windowWidth` / `r_windowHeight` at the size the game was started
with (the command line's when it sets both, else `ETERNALVR_WINDOW`'s width and height). Some load paths apply the player's video mode: in the owner's second headset session a new
campaign re-created the swapchain at 1280x800, 2560x1440, then 3840x2160 (display 100 Hz -> 120 Hz), each
eye rendered 4.1x the pixels, and the stereo ticks fell from 95-190 to 40-66 per second with the XR
compositor below 90. A fresh config's `r_swapInterval` is 2. The presenter logs `presenter: WARNING the
game's swapchain is WxH, not the expected WxH` for any later swapchain of another size than the render size
(while the layer answers the client area) or the first swapchain's. Once the render size is off (the surface
cannot scale to it, as on the AMD driver of a Radeon 890M, whose scaled image range is only the window's
size), `r_windowWidth` / `r_windowHeight` are left to the game and logged (`cvars: r_windowWidth is left at
the game's ...`): the eyes then render at the window's size, and holding the render size made the game
resize its window mid-session, which that driver answered with `VK_ERROR_OUT_OF_DATE_KHR`; the game then froze.

**The comfort set is held in both temporal modes too** (`stereo_seq::stereoComfortCvars`): HDR output, motion
blur and the dash's radial blur (`r_blurRadialScale 0`: the game writes 1 there itself when `r_motionblur` goes
to 0), depth of field, chromatic aberration, vignette, view bob, the view kicks and shakes, the damage tint and
blur, the view effects' screen overlays, the underwater screen warp (`r_waterPostProcess 0`: it moves the
picture the same way in both eyes' screen coordinates, so it has no depth in a headset), the weapon's FOV scale
and the Meathook's single view turn. The game's
own settings override the command line here as well (a rig run with them on the command line still wrote
`pm_noBob 0 -> 1`, `view_damageBlur 1 -> 0` and six more), so in stereo the launcher no longer puts them on the
command line: the layer's hold sets them, from Route S's first present (`presenter_copy.cpp`, before the
runtime's session and the first map), and a multiplayer guard trip gives the player's own values back
(`cvar_book.hpp`). Only `r_hdrDisplay 0` stays on the command line, since the game picks the swapchain's format at
start-up (a trip leaves it, as it leaves the window set). In mono the layer holds none of these, so mono launches
keep them on the command line (`launcher/data/forced-cvars.txt`, `| mono`), all but the underwater warp (one
picture for both eyes, so it stays as the game has it). The launcher's session restore puts
the player's values back in the game's configs either way.

`ETERNALVR_DEBUG_CVARS="name=value;name=value"` writes more cvars the same way for rig experiments;
`name=?` only logs the value the game runs with. Writes from it may be saved by the game (a written
`r_SSR 0` landed in `DOOMEternalConfig.local`), so rig runs restore the configs afterwards as usual.

Two static findings (`docs/rig-findings/stereo-temporal.md`): `+r_jitter 0` has no effect, since the engine
sets `r_jitter` every backend frame to 1 exactly when `r_TAASafeMode` is 0 and an AA mode is on (reading it
back tells whether TAA or DLSS runs); and with eye R skipping its exposure update, eye L's auto exposure
reads a "previous" image nothing writes in steady stereo, so exposure stops adapting (live test T6). The
auto-exposure index hook (`src/vkcore/exposure_hooks.*`) fixes that whenever Route S runs: it needs the eye
tags only, so it holds with per-eye history off as well.

With per-eye history off or failed closed the layer also holds `r_lightScatteringTAA` at run time
(`stereo_seq::stereoScatterFilterCvar`): 1 while the scattering history is per eye (Per-eye temporal
history), else 0. The start-up list says so (`cvars: held at run time: ... r_lightScatteringTAA 1 while
the scattering history is per eye, else 0`; `... 0` when the scattering hooks are not installed, for
example with `ETERNALVR_STEREO_SCATTER_TAA=0`; with per-eye history requested, followed by `(if per-eye TAA
fails closed)`, since per-eye history writes it itself while it runs). `r_SSDOTemporalAA` follows SSDO's
history the same way (`stereo_seq::stereoSsdoFilterCvar`; `r_SSDOTemporalAA 1 while the SSDO history is per
eye, else 0`, or `... 0` without the SSDO hooks, for example with `ETERNALVR_STEREO_SSDO_TAA=0`).

## Per-eye temporal history (default; `ETERNALVR_STEREO_TAA=0` turns it off)

TAA and DLSS per eye, written up in `docs/rig-findings/stereo-temporal.md`: eye R gets its own TAA
accumulation images (built by the engine's own slot builder when the renderer starts, from a hook
installed at `vkCreateInstance`), the two accumulation selectors pick each frame's eye's pair by its eye
tag, both eyes of a tick share one jitter phase, both are reset together when eye R missed a tick (TAA
by the view's reset flag, DLSS by `Reset` on both eyes' evaluations), eye R evaluates a twin DLSS feature
(each evaluation's eye found by its output image), and the temporal effects whose history is still shared
(anti-ghosting mask, SSDO without its per-eye history, depth of field, water, refraction, ray-traced
reflection upscale, dynamic resolution) are switched off (on the command line; the layer writes any that
differ through the engine's cvar setter), and eye R's slot is rebuilt by the slot builder when the engine
resizes its own (resizing eye R's targets one by one hung a 12 GB card, `stereo-temporal.md` 5.1). A missing
piece fails closed: the first stereo tick writes `r_antialiasing 0` and `r_TAASafeMode 1`. While per-eye
history runs, screen-space reflections are held at the player's own `r_SSR` (`ETERNALVR_STEREO_SSR`; before,
the game's own `r_SSR 0` after the start-up `r_TAASafeMode 1` left them off in every session), and the hold
follows the game's Reflections setting whenever that runs (the profile's load, an apply in the video menu):
SSR keeps no history of its own and reads the last frame's colour through the TAA history selector (0x1CBB6C0),
which gives each eye its own (static reading; no headset check yet). Without per-eye history (`ETERNALVR_STEREO_TAA=0`,
failed closed) the game writes `r_SSR 0` on every render itself while `r_TAASafeMode` is 1, and that stays:
the eyes would share that colour. The launcher and
`launch-ht.ps1 -Stereo` put the cvars on the command line (`-StereoV1`: the v1 set and
`ETERNALVR_STEREO_TAA=0`; `-StereoDlss`: `ETERNALVR_STEREO_DLSS=1`). Live on the rig (2026-09-26): ghost
coefficient 0.029 / 0.029 against a control of 0.014, the same as v1 and against 0.21 for the game's shared
TAA; DLSS per eye 0.030 / 0.030; TAA costs about 0.09 ms per eye at 1280x740
(`docs/rig-findings/stereo-temporal.md` section 7).

Auto exposure, the light scattering and SSDO are kept per eye by hooks of their own, which need the eye
tags only. They run whenever Route S runs: with per-eye history on, off (`ETERNALVR_STEREO_TAA=0`, the
launcher's Anti-aliasing Off, `-StereoV1`) or failed closed. The start-up line `seq-exposure: per-eye TAA
...; auto-exposure index per eye hooked, eye R takes eye L's exposure; scattering history per eye hooked;
SSDO history per eye hooked` names the mode.

- **Auto exposure** (`src/vkcore/exposure_hooks.*`, `docs/rig-findings/stereo-temporal.md` 3.3): eye L and
  mono frames alternate their exposure image by their own frame count, and eye R takes the one eye L wrote
  this tick. With per-eye history requested this starts at its first stereo tick, as before. Without it the
  index is held only while eye R skips its update (`ETERNALVR_STEREO_EXPOSURE_ONCE`, on by default); with
  the skip off the engine's own parity already makes both eyes one adapting chain.
- **Light scattering** (default; `ETERNALVR_STEREO_SCATTER_TAA=0` turns it off): the history is per eye, and
  its filter (`r_lightScatteringTAA`) stays on. Eye R gets four volume images of its own, which follow the
  engine's resizes, and each eye's pairs and filter state go into the device context before its render
  (`docs/rig-findings/stereo-scatter.md`). Without the filter the volume's coarse cells crawl as the head
  moves: the "walking texture" in fog and god rays. Per-eye history writes the cvar in its own set; without
  it the layer's run-time set holds it (Cvars (v1)).
- **SSDO** (default; `ETERNALVR_STEREO_SSDO_TAA=0` turns it off; `src/vkcore/ssdo_hooks.*`,
  `src/stereo_seq/ssdo_history.*`): the ambient occlusion's temporal filter (`r_SSDOTemporalAA`) stays on with
  a history per eye. The engine keeps two half-size accumulation targets and the unfiltered one in one array
  of the device context (+ 0x550 .. + 0x560) and writes the target of the backend counter's parity, so under
  Route S each eye read the other eye's occlusion and the filter was held off (static reading of build
  25216728). Eye R gets two targets of its own, made with the engine's (hook at RVA 0x1C1EC1D in the device
  context constructor) and resized with them (RVA 0x1C2199A in the render-size change 0x1C21600). At the entry
  of SSDO's parameter setup (RVA 0x1C71630) the array the render's targets come from is pointed at the eye's
  own pair, with the eye's last written target where the engine reads. The filter state stays the engine's;
  when an eye's own history is stale (its first render, after a resize or a gap, or both eyes after a skipped
  eye R) its last frame is moved back so the engine resets the filter. Mono renders follow eye L's chain; a
  mono render right after eye L's (eye R's render whose tag was not found) starts over instead. Anything
  missing or a size that differs after a resize fails closed: `r_SSDOTemporalAA` stays 0, with one
  `seq-ssdo:` line saying why. While `rs_enable` reads non-zero (dynamic resolution, which the engine applies
  to the target of the counter's parity; per-eye TAA holds it at 0, Anti-aliasing Off does not) the filter is
  held off as well and the renders are left to the engine; when it is 0 again both eyes start over. The first
  stereo tick logs whether the history is ready (`seq-ssdo: the SSDO history per eye is ready at the first
  stereo tick ...`, or `... is not ready ...: <why>`, for example eye R's targets never made). Route S only: Parallel Eye Rendering keeps the launcher's `r_SSDOTemporalAA 0`.
  Static work so far; the rig A/B (noise at a still view against mono, the ghost check) is still to do.

Moving and animated objects get their previous frame per eye too (default; `ETERNALVR_STEREO_OBJECT_PREV=0`
turns it off). The engine keeps each render's joint offsets and model matrices in a ring of three buffers and
reads the previous frame's from the render before. For eye R that is eye L's render of the same frame, so
moving objects had no motion there and TAA smeared them. Eye R now reads its own render of the tick before
(`docs/rig-findings/stereo-object-motion.md`).

### The game's video menu (DLSS entry)

The game's video menu has one anti-aliasing entry, DLSS, with Off (the game's TAA), Performance, Balanced and
Quality (index 0 to 3). The game keeps the index in the player's profile (`advDlssQualityIndex` in
`profile.bin`, synced by Steam Cloud, which the launcher's settings restore never writes back) and fills the
entry from that index, never from the cvars. Its setter (RVA 0x1420FD0) stores the index and writes
`r_antialiasing` (0: 1, else 2) and `r_dlssQuality` (1, 2, 3); the video page calls it for the entry every
time it is applied (RVA 0x15E1D8F in the page apply 0x15E1600), and the profile is saved with it. In
stereo the layer holds `r_antialiasing` and `r_dlssQuality` itself when the launcher's Anti-aliasing is DLSS
or Off, so the entry showed the profile's choice ("Off" with the launcher's DLSS on).

`src/vkcore/dlss_menu_hooks.cpp` (stereo only, from the Route S start; decisions in
`src/stereo_seq/dlss_menu.cpp`):

- **Shown:** a mid hook after the page refresh's call to the index getter (RVA 0x15E8F35, the getter
  0x1415E90 reads settings + 0x122A0) fills the entry with what runs: the launcher's DLSS quality
  (Ultra Performance, which the menu lacks, as Performance; DLAA, held as `r_dlssQuality` 3, as Quality), Off with the launcher's Off, after a failed-closed
  start, or when DLSS fell back to TAA, and the profile's own index with the launcher's TAA (which keeps the
  profile's DLSS, per eye).
- **Applied:** a detour of the setter compares the entry with what was shown. Unchanged: the setter is
  skipped, so the profile keeps its own index and no cvar is rewritten. Changed with the launcher's TAA: the
  setter runs as in the flat game (the layer follows the game's choice). Changed with the launcher's DLSS or
  Off: skipped and logged; the launcher's setting decides in VR and the profile keeps the flat game's choice.
  When DLSS fell back to TAA because eye R's feature could not be created (the entry shows Off), choosing a
  DLSS entry tries that feature again at once with a fresh count of tries (`src/stereo_seq/ngx_twin_retry.hpp`):
  at the launcher's quality with the launcher's DLSS (the setter skipped), as in the flat game with the
  launcher's TAA (the setter runs).
- Writing the settings object's index instead was rejected: every profile save (this page's apply and other
  pages') would store the VR value in `profile.bin`, which nothing puts back after the session.
- Log lines: `dlss-menu: DLSS setter at RVA ...` (located), `dlss-menu: video menu refresh N: the profile's
  DLSS index P, shown S (...)` (the first 20 refreshes and any change), `dlss-menu: video menu applied DLSS
  index I (shown S, the profile's P, ...): unchanged | applied as in the flat game | not used in VR | DLSS
  asked for again`, then `seq-taa: DLSS chosen in the game's video menu: trying eye R's DLSS feature again`.
- Not hooked (a signature or self-check failed, or the multiplayer guard not armed): the entry shows the
  profile's index as before, and an apply writes it through the game's own setter.

## Alternate eyes (`ETERNALVR_ALTERNATE_EYES=1` or `auto`, off by default)

An option for slower processors (the launcher's "Alternate eyes" on the Advanced tab): each game tick renders
one eye in the engine's own chain, eye L and then eye R in the next tick, instead of eye L and a nested eye R
render. The design, the expected saving and the rig recipe are in `docs/rig-findings/alternate-eye.md`
(`src/stereo_seq/alternate_eyes.*`, `src/vkcore/seq_alternate.*`, `src/vkcore/presenter_alt.cpp`). In short:

- The eye of a render is eye R when the render right before was eye L in stereo, else eye L
  (`stereo_seq::EyeAlternator`, keyed by the render frame counter); `seqRenderEye()` gives it to the per-eye
  hook, the post-latch hook and the previous-matrix hook. `seqChainEye()` stays "inside the nested eye R
  render", which never happens with `=1`, so the moved-flag repair leaves every render alone.
- The render order is still L, R, L, R, so each eye's previous matrices, TAA and DLSS history, exposure and
  scattering history stay its own; the jitter phase and the TAA and DLSS resets count each eye's renders two game
  frames apart. Ordinary moving entities get the previous model matrix of two renders back, their eye's own last
  render (`stereo_seq/alternate_prev.*`); skinned meshes take theirs from the object ring's render before (the
  other eye's, one game frame back), which needs a fourth ring slot to fix: under TAA fast demons smear a
  little (the design doc, section 9).
- Every present shows its fresh eye beside the other eye's newest image, each with the pose and FOV it was
  rendered with (the compositor corrects the older one for the head's rotation): the image goes into its half
  of the held ring slot, which is shown, and in the same copy into its half of a new held slot
  (`stereo_seq::AlternatePairing`, `composeHalves`, `CopyTarget::carrySlot`).
- About half the CPU per tick, about twice the tick rate on a CPU-bound machine; each eye updates at half the
  tick rate. Log: `stereo: alternate eyes on: ...` at start-up and `seq: alternate eyes: ...` every 10 s.
- `auto` (the launcher's "Auto (only when your processor cannot keep up)"): each pair renders both eyes in its
  tick (Route S, eye R nested) while the ticks keep up with the headset's display rate, and alternates after
  about 1 s below it; back to both eyes after about 3 s with the estimated Route S rate 15% above it
  (`stereo_seq::AdaptiveEyes`). The way changes only at a pair's eye L and nothing resets. Log: `seq: adaptive
  eyes: ...` at every switch and every 10 s (the design doc, section 10).

## Parallel Eye Rendering (`ETERNALVR_PARALLEL_EYES=1`, off by default, experimental)

Instead of Route S's two renders per tick (eye L's, then eye R's nested in its frame end), the engine renders
both eyes as two render views of one frame: view 0 is eye L, view 1 eye R, and the two views' job chains run on
the job workers at the same time. The renderer of build 25216728 keeps per-view storage for one view, so view 1
gets its own (`docs/rig-findings/perf-multiview-slots.md`, `docs/rig-findings/view-shared-state.md`). The sites
are RVAs: Steam build 25216728 only (the PE timestamp is checked); any other build logs it and runs Route S.
Measured (Release, OpenXR-Simulator 1280x720, three interleaved rounds of 120 s, median game ticks a second
after 60 s): +41.7% in e1m2 and +28.7% in e1m3 on an RTX 3080 Ti with an i7-9700K, +16.7% earlier on the RTX
4080 rig. The GPU's work is not reduced.

**Turning it on.** `ETERNALVR_MODE=stereo` and `ETERNALVR_PARALLEL_EYES=1`; nothing else. The launcher's
"Parallel Eye Rendering (experimental)" (Play tab, stereo only; shown with `ETERNALVR_SHOW_PARALLEL_EYES=1`) sets
exactly that, so a `.cmd` that sets the
two variables runs the same thing. It stays off (one `parallel eyes: off: ...` line, Route S) when
`ETERNALVR_STEREO_EXPERIMENT` is set (the experiment then runs), the UI layer is off (`ETERNALVR_UI_LAYER=0`:
its image hooks give each eye its view's picture) or `ETERNALVR_STEREO_DLSS=1` comes with `ETERNALVR_PE_DLSS=0`
or `ETERNALVR_STEREO_RUNTIME_CVARS=0` (nothing would hold TAA after a fallback; Route S then runs DLSS per eye,
as before DLSS ran under Parallel Eye Rendering). Unset or any value other than
`1`, none of it runs and nothing is logged (`src/vkcore/parallel_eyes_settings.*`, unit-tested).

**DLSS** (`ETERNALVR_STEREO_DLSS=1`, the launcher's DLSS; `src/vkcore/view_dlss.*`, the planner in
`view_dlss_plan.hpp` unit-tested). The engine keeps its DLSS feature on the command context that records the
post-process pass (context +0x190): the pass's DLSS branch (0x1C9B5D0) creates it there (0x1CC5760) and
evaluates it (0x1CC5B30). View 1 records into its own post-process context (category 11 slot 1), so the engine
makes view 1 a feature and a DLSS history of its own; Route S's per-eye twins stay off. The layer holds
`r_antialiasing 2` and the launcher's `r_dlssQuality` (DLAA as under Route S, with a newer DLSS), and:
- one lock around the engine's create, evaluate, release (0x1CC5F00, both views' anti-aliasing passes at a
  switch to TAA) and optimal render size query (0x1CC5D40): they fill or use the engine's one NGX parameter
  block (0x66E8B28), and evaluate counts down `r_dlssForceReset` (0x66E8E30);
- each view's results counted (the engine's own count, 0x66E0D80, is written by both views after evaluate, so
  one view's failures may never reach its 3): 3 failures in a row of either view fall back to TAA in both eyes
  (the engine then releases both features), tried again once after 5 s and at once whenever DLSS is chosen in
  the game's video menu (one automatic try a session: each switch between DLSS and TAA changes the render size
  and so makes view 1's clones again, and a session has only a few clone builds);
- "Reset" (the evaluation's own, written at 0x1CC5CE0 before NGX's helper reads it at 0x1CC7C89) for view 1's
  evaluations while the frames sent lately did not all render view 1 (a loading screen, new clones), and for
  the other view's next evaluation when an engine reset reached one view;
- view 1's feature released (0x1CC5F00 on its context) once view 1 stops for good (a guard trip, the clones
  off) and before NGX shuts down (0x2268FC0);
- a DLSS pass on any other context (the one async compute context; async compute is off) logs and holds TAA
  for the session.
Every site is checked byte by byte with the install's checks: a mismatch keeps Parallel Eye Rendering off with
DLSS (Route S runs DLSS per eye); a hook that fails later holds TAA. The game's video menu shows what runs, as
under Route S. Static reading (2026-10-08): view 1's context +0x190 starts null (0x1C663A0 runs for it and clears
it at 0x1C6651D), async TSSAA stays off without an async queue (0x667EB68 stays -1, 0x1CDE893). Not rig-checked
yet. Log: `view-dlss: DLSS in both views, ...` at the install, `view-dlss: view N's DLSS feature created: ID
(WxH -> WxH)`, every 10 s `view-dlss: evaluates view 0 N (failed N), view 1 N (failed N); features made N, N;
Reset raised by the layer N, N, by the engine N, N; lock waits N (N us); DLSS and TAA switches N; DLSS in both
eyes`, and on a fallback
`view-dlss: view N's DLSS evaluation failed 3 times in a row: TAA in both eyes ...`, `view-dlss: trying DLSS
again ...`, `view-dlss: DLSS in both eyes again after a fallback ...`.

**Install** (`src/vkcore/view_install.cpp`, from the game's `vkCreateInstance` before its present policy is
decided, with the multiplayer guard installed first). Every check of every step comes first, with the memory
the steps take: the sites' bytes, the per-view block's references, the context tables, the 16 code bytes and
the clones' sites. Every range the changes write is made writable before the first change, so a change
cannot fail half-way. A failed check (or a storage hook, which is inert until the end) leaves the game
working as it was, and Route S runs with its present policy. A hook that fails after the first change (the
redirects' or the clones') leaves the changes made before it: Route S does not run on that engine, which
renders view 0 alone for the session, as after a guard trip, shown as mono (one `FAILED` line).

**What the layer holds while it is on** (`src/vkcore/view_slots.*`):

- View 1's per-view storage: `r_maxRenderViews` 2, the static per-view block moved to two entries
  (`view_block.cpp`), a second device context view slot and occlusion-query state, a second render context,
  one dispatcher call per view (`view_slots.cpp`).
- View 1's command contexts (slot 1 of the per-view categories, `view_contexts.cpp`) and the per-view redirects:
  the three four-context categories split two slots per view (16 code bytes of the jobs' fan-out changed),
  per-view Begin Frame resets, jobs that use the renderer's one scratch one view at a time, skinning and the
  ray tracing structures for view 0 only, view 1's light binning after view 0's through job graph edges with a
  bounded wait and a watchdog (`view_redirects.cpp`, `view_binning.cpp`).
- **Compute skinning output, one counter** (`view_skin_alloc.cpp`): each view's surface setup (0x1C00F70) takes
  its skinned surfaces' room in the world geometry manager's compute skinning output with a counter in its own
  render context (+0x4D9F50 + 0x270 / + 0x274), and every view starts its counter at the output's start. View 1
  takes its room from view 0's counter, and the output is made with twice the room (the renderer setup's read
  of `r_worldGeometryManagerCSSkinBufferSize`, 491520 to 983040 positions, within the 20 bits of offset a draw
  surface keeps; the cvar is not written). Without it view 1's csSkinning dispatches (category 2, after view
  0's) overwrote view 0's skinned positions wherever the two views' lists of skinned surfaces differ: eye L's
  arms, weapon, demons and props drawn as spikes and shards (the headset, 2026-10-02; rig runs in e1m1_intro
  with the head and hands still: 42 of 43 frames, none with the change). The two lists matched at the e1m2
  start the rig used before, so nothing showed there. Only in frames that dispatch view 0 (with
  `ETERNALVR_TEST_VIEW_ONLY=1` nothing starts view 0's counter). Log: `view-redirects: compute skinning output
  for both views: 983040 positions ...` at the renderer setup, and with the counts `view-redirects: compute
  skinning: view 1's surfaces took their room from view 0's counter N time(s) ...`.
- **Occlusion query copies without the wait** (`view_query_copy.cpp`): each view's surface setup (0x1C7A920,
  through 0x1C7CF80) marks an occlusion-tested object's two queries pending in one global bit array
  (0x66E3878, plain read-modify-writes; the indices come from a global counter restarted every frame), the
  occlusion pass (0x1C8FA40, category 7) issues them in a pool reset once a frame, and each view's emissive and
  blend job (0x1C62040, category 10) starts by copying every pending query with `VK_QUERY_RESULT_WAIT_BIT`
  (0x1C32F20) and clearing the bits. With two views the bits and the object's indices are shared: a bit can
  outlive its frame, and the next frame's first copy waits on queries reset at that frame's start and not
  issued again. The queue stalls there until Windows resets the GPU (TDR, nvlddmkm 153): on the rig every
  walking run in e1m1_intro, about 12 s into the walk, once an occlusion-tested object comes into range (NV
  diagnostic checkpoints: the last command the GPU reached was view 1's copy of queries 0 and 1 in category 10
  slot 2; view 1 had issued them in the frame before, after both copies). Both copies (0x1C32FAF, 0x1C33039)
  are made without the wait flag while Parallel Eye Rendering runs: queries the GPU has not finished are not
  copied, and the buffer keeps their previous results. Log, with the counts: `view-redirects: occlusion query
  results copied without waiting N time(s)`. The cost: nothing makes a query issued in this frame's occlusion
  pass available by the copy without the wait, and 0x1C32F20 clears the pending bits all the same (0x1C33007).
  Such a query's buffer slot keeps the value an earlier copy wrote there, and as the indices restart every
  frame it may be another object's result: an object can be culled or kept by a stale result for a frame (a
  pop at an occlusion edge). Keeping the wait for the queries pending from this frame only would need the
  frame and the issuing view of each bit (set with plain read-modify-writes by both views' setups) and proof
  that every one was issued before the copy; not done. Checked in the headset.
- **View 1's shadow setup on view 0's shadow cache entries** (`view_shadow_cache.cpp`): each view's shadow setup
  job (0x1CF0240) reads back its visible-light bits, picks each shadowed light's shadow map level and finds or
  makes the light's entry in the shadow atlas cache, releasing the light's entries at the other levels. The cache
  is one engine object ([0x66E2E88]) for both views, and view 1 renders no shadow map tiles (it shades with view
  0's atlas). View 1's setup released view 0's entries wherever the eyes picked other levels, and counted the
  cache's frame twice: both eyes lit darker, worst on eye L's left (rig, e1m1_intro still recipe, mean luminance
  against Route S's same eye at the same game tick: eye L 0.86, eye R 0.82; with the change 0.95 and 0.96; view 0
  alone 0.97). While view 1's setup runs, the cache's begin (0x1D058B0, 0x1D05A30) and frame count (0x1CF02B9)
  are left out, and so are the releases (0x1D05BF0) the setup makes (0x1CF063F) and its per-light record
  function 0x1CFE2B0 makes (0x1CFF16A, 0x1CFF213, 0x1CFF34F), told by their return address; a lookup
  (0x1D05A40) that misses tries the light's other levels: view 1 takes the entry view 0 made. A light view 0 has
  no entry for gets one of its own, and the allocation (0x1D05220) releases an older entry no light used this
  frame when the atlas has no room (0x1D053D8, 0x1D04F83) and allocates again, as the game's does: with that
  release left out too it would find the same entry again and recurse until the stack ran out. Each view's
  light parameter build (0x1CF0B80, the shading's shadow rect and matrix) does not read the setup's records: it
  looks the face's entry up itself from the level byte the setup wrote into the light's entry in the light list
  (+0x6A + face, one list for both views), at that level or coarser, else a dummy entry with a zero rect. After
  view 1's setup each face's level byte is set to the level of the entry its record holds, so view 1's shading
  finds view 0's entry where it is finer than the level view 1 picked. The two views' setups run under one lock,
  view 1's after view 0's: it waits for view 0's at most 2 ms (16 waits in a row that run out stop the waiting
  for the session; on the rig view 0's had always finished, 30000 of 30000 frames), and only in frames that
  dispatch view 0 (with `ETERNALVR_TEST_VIEW_ONLY=1` view 1's setup is the game's own). Log, with the counts:
  `view-redirects: view 1's shadow setup on view 0's shadow cache entries N time(s): view 0's had finished N,
  waited for N, ran out N, did not wait N; N release(s) left out, older entries released for view 1's
  allocations N + N (0x1D053D8, 0x1D04F83), N light(s) found at another level, N face level(s) set to the
  entry's`.
- **Models only view 1 sees are updated** (`view_updates.cpp`): the world update (0x18E5070) runs once a frame,
  for view 0, and its jobs prepare and update (UpdateInView) the particles, flares, beams and ribbons on view
  0's four lists (render view +0x970, 0x2000 model indices each, the counts at +0x20970). Each view's gather
  fills its own lists, and view 1's were never read: a particle system or ribbon in the strip only eye R sees
  (about 40 to 54 degrees right of centre) was not simulated, and eye R drew it with stale geometry or none. A
  hook before the update makes its job counts from the lists (RVA 0x18E51DC) adds view 1's entries that are
  not on view 0's lists to them, within each list's 0x2000 entries. The lists feed only the prepare and the
  updates: what each view draws stays its own gather's. It uses the lists only when both views' gathers were
  set up for this world at its last frame and both have ended: a hook at the end of the gather's last job
  (0x1C79FC0, RVA 0x1C7A8F5) records the view and world frame each render context's gather ended for, waited
  for at most 0.5 ms (16 waits in a row that run out stop the waiting for the session; such frames are left
  out, counted per view).
  Lens flares stay off (`r_skipFlares 1`). Log: `view-updates: hook at RVA 0x18E51DC ...` at start-up, and every
  10 s `view-updates: last 10 s: N frame(s) with view 1's lists added to view 0's ...` with the entries added,
  already on view 0's and over the cap per list, and the frames left out. `ETERNALVR_TEST_PE_UPDATE_UNION=0`
  leaves it out.
- **The water from view 0's starting state** (`view_water.*`): each view's render-view job 0x1C575F0 calls the
  water setup (0x1CE3A90) with its water context (render context + 0x705B78), holding the world's one water
  state (render world + 0xB4550, 0xC90 bytes) at +0xE0. The setup decides from the state which parts of the
  simulation the frame steps (waves' FFT, ripples, caustics: context +0x1125..+0x1127), moves the state's
  indices on (displacement +0xC30, caustics ring +0xC34, ripples +0xC38), binds the grid matrix of the render
  before (+0xC48) and stores its own over it; the view's water job (0x1CE2A30) reads the state again and steps
  what was asked. With two views every frame moved it all on twice, on one state the two views' setups and
  jobs used at the same time: the ripples ran at twice their speed with the eyes a step apart (both views
  stepped the same two images), the caustics ring moved two slots, the displacement index flipped back, and
  each view's grid took the other's matrix as its previous one. Now view 1's setup and job work on a copy of
  the state, put into view 1's context at the setup's entry (the engine stores the world's again before the
  next one): the world's, with the fields the setup decides from (wave parameters, last-step frames, indices,
  ripple camera) from the state view 0's setup started from, and view 1's own grid matrix of its render before
  (only from the frame just before: after a map load or a frame left to the engine, view 0's starting one).
  View 1 then takes view 0's decisions and binds the same ripple and caustics images; at the setup's end its
  ripple and caustics steps are cleared (their images are the world's; view 0 steps them), while its waves
  step stays (the displacement and FFT images are view 1's clones, `kDcCloneFields` and `kImageSlots`). When
  view 0's setup ended with no water in its view (it leaves at once, stepping and moving nothing) while view
  1's sees water (eye R reaches a pool's edge first), view 1's steps stay instead, the fields its setup wrote
  go back into the world's state and its job runs on the world's (checked at view 1's setup end and at its
  job's state read; otherwise nobody would step the ripples and caustics eye R binds). The
  world's state is moved on by view 0 alone, once a frame, so `r_waterInterleaveUpdates` is not held as Route
  S's per-eye water holds it: with one backend frame per game frame view 0's counter alternates its parity as
  in mono. View 1 never remakes the world's grid mesh (+0xC18; the make 0x1CE3430 frees the old one at once):
  its make on the copy waits for view 0's setup and takes the world's mesh, and its job takes it again at its
  state read (0x1CE2B1A). View 1 waits for nothing else, except at its entry for view 0's setup (at most 30
  ms) when the grid mesh or the wave spectrum is to be remade (`r_waterGridResolution` or
  `r_waterQualityFFT` changed: a map's first water frame, a water setting changed); the spectrum rebuild one
  view's setup took is given to the other's job too. When view 0 is not rendered
  (`ETERNALVR_TEST_VIEW_ONLY=1`) or view 0's setup of the next frame already began, view 1's render is left
  to the engine. Static work so far; the rig A/B is still
  to do. Log: `view-water: hooks at RVA 0x1CE3A90 ..., 0x1CE53AB ... and 0x1CE2B1A ...` at start-up, `view-water:
  first view 1 water render on its copy, started before view 0's setup: ...` (and after it, while it ran), and
  every 10 s `view-water: last 10 s: view 1's water from view 0's starting state N time(s) before its setup, N
  after it, N while it ran; left to the engine ...; view 1's ripple / caustics steps left out N / N, ...;
  decided as view 0 N, otherwise N; ...`: in steady play nearly all of view 1's renders start from view 0's
  state and `otherwise` stays 0 (the first frame that differs is logged with both setups' flags and
  indices). `ETERNALVR_TEST_PE_WATER=0` leaves it out.
- **One auto exposure for both eyes** (`exposure_hooks.cpp`, the hook Route S uses, RVA 0x1C98D46 in
  0x1C988E0): view 0 adapts the exposure both eyes share; view 1 skips its update (`skipAutoExposureUpdate`,
  set where its eye pose is written) and its exposure targets are not cloned. The engine picks the image a view
  reads by its slot's last-updated frame count, and view 1's (slot 1) is never written while it skips: view 1
  read image 0 (or the image of its last own update) while view 0 alternates by its backend frame's parity, so
  eye R lagged 0 to 2 frames,
  alternating, and showed a darker or brighter flash in one eye while exposure changed (12 to 14% in Jason's
  captures on the Fortress stairs). The hook gives view 1 view 0's index, the parity of the frame
  (`ETERNALVR_PE_EXPOSURE=same`, the default) or the other one (`prev`: the image view 0 wrote the frame
  before, whatever order the views' post-process work runs in), and sets view 1's skip byte in its render
  context block (+0x4D9C70 + 0x21) where the eye pose write returned early, so that view 1 does not write the
  shared images (the post-process job reads the byte after the hook: inferred from the code, checked by the
  count below). `engine` leaves the engine's index and the skip as they are and only counts (the rig's positive
  control). View 0 is never changed. After a multiplayer guard trip view 1 is left to the engine. Log:
  `pe-exposure: auto-exposure index hooked at RVA 0x1C98D46` and `pe-exposure: view 1 reads the exposure view 0
  writes this frame` at start-up (`NOT hooked: ...` when the site is not found), and every 10 s while view 1 is
  touched `pe-exposure: last 10 s: view 0 N (index not its frame's parity N); view 1 N, the engine's index
  differed from view 0's N, its own update blocked (no skip flag) N, its last own update changed N, its frame
  count as view 0's last N / one ahead N / other N; adapted at once view 0 N / view 1 N; other views N`, and
  for the first 20 renders that adapt their exposure at once `pe-exposure: view V adapts its exposure at once
  (the render's instant flag), backend frame F (N so far)`: the block's byte +0x9D (render context + 0x4D9D0D,
  latched from renderView_t + 0x634) keeps the adaptation factor at 1, a whole-image exposure jump in one frame,
  as `r_hdrAutoExposureInstant 1` does every frame (which game event sets it is not known). `index not its
  frame's parity` stays 0; the
  engine's index differs on about half of view 1's frames whatever the mode (the hook replaces it unless
  `engine`); `no skip flag` counts view 1's frames whose eye pose was not written (with `engine`, `ran (no skip
  flag)`: they ran their own update into the shared images); `its last own update changed` (the engine's index
  for view 1 with its skip flag set is the parity of view 1's last own update) stays 0 unless the engine mode let
  an update run; `other` frame counts stay 0 (`same` then reads the image view 0 writes in the same frame).
- **View 0's screen pass after a swapchain recreate** (`view_swap_guard.cpp`): the last view of a frame runs
  its screen pass (0x1CDF6E0) in the finish job right after the acquire; view 0, which the dispatcher sends
  with "more views follow", runs it earlier, as a job (0x1C58030) or inline (0x1C57572), into the swapchain
  image the last acquire left on the screen target (dc + 0x508, colour at +0x10). When the frame's begin job
  recreates the swapchain (a window resize, an out-of-date swapchain, `r_hdrDisplay` changed: 0x1CDD3C0 ->
  0x1D090E0), the old images are deleted (0x1D09540) before view 0's pass runs, and the pass bound a deleted
  image: the game crashed at DOOMEternalx64vk+0x1C334B9 (seen at start-up, when the second resize ran in a
  frame with views on a worker thread). A hook at the pass's first instruction leaves view 0's pass out
  (resuming at a plain `ret`) when it does not return into the finish job (0x1CDF449) and the target's colour
  image is neither one of the swapchain's (dc + 0xB8 + 0x38, count at + 0x80) nor its fallback ([0x66E31D0]);
  pointers are compared only. That frame's picture of view 0 would have gone into the deleted image, so view
  1's copy of the same frame is not made either, and the present that would pair with it keeps the last pair.
  Its command context (cell [11, 0]) still holds the frame's post-process passes, as with one view, where it
  never holds a screen pass. The pass is also left out while the frame begin's recreate is running (no lock:
  the recreate waits on the window thread). View 0 draws into the image the frame before acquired, so the
  first presents of a new swapchain show images nothing has drawn yet: from each destroy, a present whose eye 0
  would be the presented image keeps the last pair while that image is one no view 0 pass drew since (at most
  4; `view_snapshot.cpp`, `snapshot_ring.hpp`; a pair's eye 0 is its own frame's copy and never one of
  these). A watch (`view_swap_watch.cpp`, its own hooks: the destroy, the frame begin 0x1CD9750
  and its recreate call 0x1CDD5BF) logs `parallel eyes: swapchain destroyed (N time(s)) on thread T;
  recreated by the frame begin job on thread B, a frame with V view(s)` (or `... called directly (return RVA
  ...)`, or `not from a frame begin's recreate`), then for the first view 0 pass checked after it `parallel
  eyes: view 0's first screen pass after swapchain destroy N, on thread T: its colour image P is ...` (not the
  swapchain's, left out; a new swapchain image, kept; a new image at a destroyed one's address, kept), and
  `parallel eyes: view 0's screen pass left out: its swapchain image was destroyed since the last acquire (N
  time(s))` (each up to 32 times), with the counts in the periodic `parallel eyes:` and `eye-snapshot:` lines.
  `ETERNALVR_TEST_PE_SWAP_GUARD=0` leaves the pass in (the watch still logs and holds).
- **Async compute off**: the device setup's one read of `r_enableAsyncCompute` is made 0 (`view_async.cpp`); the
  cvar itself and the player's config are not changed. With async compute on, both views record into the
  engine's one set of async compute contexts: a driver crash in the shell world, a hang a few seconds into a
  map (rig runs pa1, rsa1, rsa2; the headset's black screen). Should the game's device still get a compute-only
  queue, view 1 is not rendered (one line) and both eyes show view 0's image.
- View 1's clones of the screen-sized targets (`view_clones.*`, `view_clone_binds.cpp`) and its own passes where
  the engine has one for the frame: screen pass, environment, light scattering's wait for its volumes, its
  shadow atlas work left out (it shades with view 0's atlas), its light and decal tile list pool
  (`view_one_passes.cpp`). Slot 0's TAA targets (device context +0x60..+0x78: the accumulation buffers and
  distortion) are not cloned: view 1 renders with view slot 1's (about 53 MB). Slot 0's view colour (+0x80,
  `_viewColor0`) stays cloned: view 1 binds it (the clone census, rig run pint-pe-a). An image clone keeps its name from build to build
  (`_evrview1_<engine name>`, `view_clone_make.*`), so a rebuild (a resize, ray tracing turned on or off)
  keeps each clone instead of leaving the old set allocated until the next map load (600-850 MB per rebuild
  before). A build never frees anything (a purge frees the image's state block at once, which a job of the
  frame before may still write): the engine's resize, after its device idle, purges every clone and the ones
  waiting (no longer made, or made beside under another name), and the next build allocates them again.
  The refraction (glass and other refractive surfaces) is cloned too (device context +0x440 .. +0x490, targets +0x5F0 .. +0x610;
  `ETERNALVR_TEST_VIEW_CLONE_SKIP=refract` leaves it shared): the refraction pass (0x1C63DB0) blends each
  refraction update's copy of the scene with the last frame's from a pair of accumulation images per update,
  and with the refraction mask on (the default) the mask pass (0x1C64360) keeps its tiles in a pair as well,
  both picked by the frame counter's parity, the same in both views. With one set the two views wrote the same
  images and each blended its glass with the other view's last frame. The passes read the engine's device
  context; view 1's binds and write marks take its clones, so each view alternates within its own pairs (static
  reading of build 25216728; the census shows whether view 1 binds them). About 20 MB at 2048x2208.
  The glass itself refracts the scene mips (`_viewColorScaled00..40`, global slots 0x66E3210..0x66E3270) and the
  refraction mask, which the transparency pass binds on its own parameter block in the render context (+0x706DD0,
  0x1C63A80 from 0x1C579DA) rather than on a command context's: the transparent draws after the first
  refraction update resolve their materials there (0x1C65650, 0x1C65080). View 1's binds there take its clones
  too (`view-clones: ... image binds cloned ..., binds on the transparency block N`); before, eye R's glass showed eye L's
  picture of the same frame (eye L's gun through the Urdak ring-hub glass, rig 2026-10-09). `tblock` in
  `ETERNALVR_TEST_VIEW_CLONE_SKIP` leaves that block to the engine (as before).
- The eye copy: each eye is its own view's screen pass output (eye 0 view 0's swapchain image, eye 1 view 1's
  image), in a two-eye ring (`presenter_eyes.*`), paired by frame (`view_snapshot.*`, `snapshot_ring.hpp`).
  Each eye is copied into a small ring by a batch after the game's submit of the command buffer its view's
  screen pass is recorded into (view 0's swapchain image is left in PRESENT_SRC, where the game's command
  buffer moved it). With an eye 0 copy the signals of the game's batches from the first one carrying view 0's
  command buffer on move onto the copies' batch (`submit_order.hpp`), so the present of that image and the
  next write after it is acquired again wait for the copy on every path, the presents the presenter does
  not copy included; when they cannot move (another pNext chain, or a later batch of the submit waiting on
  one of them) that eye 0 copy is not made (counted). A frame's two copies share a slot, and a present shows the newest pair both of whose
  copies were submitted, with that frame's view record as its pose, so the eyes are always of one frame. A
  slot's copies wait on the GPU for its last copies and its last read, so 2 pairs are kept (4 images, as many
  as the eye 1 copies before; about 18 MB each at 2056x2216). With no new pair since the last one shown the
  present is not handed to the headset, which keeps the last pair (at most 8 presents in a row); after that,
  before the first pair and on frames without view 1, eye 0 is the presented image and eye 1 view 1's image.
  Eye 1 takes view 1's image only when the frame rendered view 1 (the last three frames sent did, and since
  view 1's clones were last made, `view_frames.hpp`); on frames with view 0 alone (loading screens, the async
  compute safety net) and for three frames after new clones (a resize without a loading screen) there is no
  pair and eye 1 takes the presented image too, never an older frame's or an unwritten one (`eye-copy: eye 1
  takes the presented image: view 1 was not rendered for this frame`). Before the pairs (and with
  `ETERNALVR_TEST_PE_PAIRING=guess`) eye 0 was the presented image and eye 1 the copy of view 1's image a
  present was guessed to go with, from which frame drew the presented image: the guess was a frame off for
  runs of presents whenever the game's phase changed, and a present whose eye 1 would have repeated was
  dropped (30% of a session's presents in the headset, 2026-10-07).
- Its cvars, held as Route S holds its own, from the first present on every present (menus and loading
  screens included): `r_useNewDepthDownscale 0` (with the new downsample view 1 drew tile-shaped black holes
  in e1m3), the launcher's anti-aliasing (`r_antialiasing 1`, with DLSS `r_antialiasing 2` and the launcher's
  `r_dlssQuality` (above), or with Off `r_TAASafeMode 1` and `r_antialiasing 0`; `ETERNALVR_STEREO_RUNTIME_CVARS=0` leaves it as the game has it), Route S's window and
  present set (`r_fullscreen 0`, `r_swapInterval 0`, the launch size, left to the game once the render size is
  off) and its comfort set; the launcher's sharpening, CPU Saver and button prompts as under Route S, and
  `r_SSR 0` with the launcher's Screen-space reflections Off (`runtime_cvars_pe.hpp`). Also
  `r_raytracedReflectionsTemporalUpscaleQuality 0`, the launcher's command line value, which the game's
  Reflections setting writes over at the profile's load (Jason's 10-07 session read 1 from its first
  `game settings:` line on): the ray-traced reflections then traced a quarter of their rays and blended in a
  history, where Route S's per-eye TAA traces them all. This is the shipped behaviour, not a new override:
  Route S with per-eye TAA (the launcher's default anti-aliasing) writes the quality back to 0 on every
  stereo tick (`taa_hooks.cpp`, `stereoTaaForcedCvars`), so Parallel Eye Rendering now renders the
  reflections as Route S does. View 1 binds its own clones of the nine reflection
  images and of the velocity, G-buffer and blend targets (the bind log's `view1 1 clonable 1` rows are the
  engine's image before the swap; view 1's parameter blocks hold other images than view 0's), but at 0 no
  history is read at all, as in Route S. The cost is the full rate of rays. Not the marker of Route S's
  `r_SSR` follow (`ETERNALVR_STEREO_SSR` below): per-eye TAA reads the 1 to 3 the setting writes only under
  Route S, where this set is not held, and never runs on an engine Parallel Eye Rendering changed; under
  Parallel Eye Rendering the setting's `r_SSR` stays as the game writes it. Log: `cvars: held at run time:
  ..., r_raytracedReflectionsTemporalUpscaleQuality 0` and `cvars: r_raytracedReflectionsTemporalUpscaleQuality
  1 -> 0 (reads 0); held in VR` after each of the game's writes (the first 12 logged).
  `ETERNALVR_TEST_PE_RT_UPSCALE_HOLD=0` leaves the game's value (the `held at run time` line then lacks it).
- Route S's own modules stay off: per-eye TAA, the scattering hooks, the exposure hook's eye tags (its site
  serves view 1's index, above), alternate eyes, its present
  policy (the window's present gate, present mode and swapchain image count of `stereo_present.hpp`). Frame
  pacing (`ETERNALVR_PACE=headset`) holds its images to the headset's frames as it does Route S's.
  `ETERNALVR_ALTERNATE_EYES` is ignored (one `parallel eyes: ETERNALVR_ALTERNATE_EYES is ignored ...` line),
  and its `auto` does not turn frame pacing off; the launcher passes `0` and greys Alternate eyes out while
  Parallel Eye Rendering is on.

**Multiplayer guard.** Nothing is installed unless the guard is armed. The dispatcher asks the guard for every
frame and, once it has tripped, renders view 0 alone. Every hook that changes view 1's work asks
`parallelEyesTouch()`: the guard, or after a trip only until the frames the game built with two views before
it are rendered (each must finish the way it started; the stereo layout hook builds no such frame after a
trip and three one-view frames in a row), then false for good. The device setup's async read, the context
resize, the pool sizing, the environment fill and the eye copy ask the guard directly. Three kinds keep
running after a trip, as they must: the storage hooks, which move view index 1's accesses out of the engine's
one-view storage (`r_maxRenderViews` stays 2) and change nothing for view 0; the hooks that keep view 0 on its
two command contexts as the changed fan-out bytes expect; and the locks and the one-view clamp that keep
two-view frames' jobs apart (the render target manager's lock, the serial jobs' locks and the texture
streamer's gather clamp), which are no-ops with one view. The layer never unhooks or unpatches live.

**Test knobs** (rig experiments only). The release layer carries them; each is inert unless its variable is
set, and none is set by the launcher:

| Variable | Effect |
|---|---|
| `ETERNALVR_TEST_VIEW_CLONES=0` | no clones: view 1 draws into the engine's targets (the eye copy is then off) |
| `ETERNALVR_TEST_VIEW_CLONE_SKIP=<items>` | the clones view 1 leaves to the engine, comma-separated; replaces the default `slot0` (slot 0's TAA targets, device context +0x60..+0x78; its view colour +0x80 stays cloned): `slot0`, `dof` (the depth of field targets, only while the layer holds `r_dof 0`), `gui` (the GUI target's colour; its depth stays view 1's), `flares` (`_cineLensflares`), `mblur` (`_velocityTileMax0/1`), `refract` (the refraction's images and targets: its history pairs and the mask chain, glass then blends with the other view's last frame), `tblock` (the transparency pass's own block keeps the engine's scene mips and refraction mask: view 1's glass refracts view 0's picture), a global slot's RVA (`0x66E3180`), `dc+<offset>` (`dc+0x5E0`); `none` clones them all. View 1's final image, screen pass output and water simulation (0x66E30D8..0x66E30E8, dc+0x338..0x360) always stay |
| `ETERNALVR_TEST_VIEW_CLONE_LOG=0` | no census of what view 1 does with its clones (the line per clone after each build stays) |
| `ETERNALVR_TEST_VIEW_CLONE_NAMES=build` | each build names its clones anew, as before: a rebuild leaves the old set allocated until a map load (the clones turn off at the 8th change of the engine's targets) |
| `ETERNALVR_TEST_VIEW_CLONE_REBUILD=<seconds>` | makes the clones again that long after each build (1 to 3600 s, at most 5 times; with stable names each is kept), to measure video memory across rebuilds |
| `ETERNALVR_TEST_EYE_COPY=0`, `1` | `0`: both eyes the presented image; `1`: each view's image before the screen pass (gamma-darker, from the engine's post-process final target, RVA 0x66E3208) |
| `ETERNALVR_TEST_VIEW_OFF=<parts>` | leaves fixes out, comma-separated: `edges` (binning edges), `dc` (view 1's device context copy), `binds` (view 1 binds its clones), `pool` (tile list pool), `shadows` (view 1 renders its own shadow atlas work), `env`, `volumes`, `screen` |
| `ETERNALVR_TEST_VIEW_ONLY=0`, `1` | renders that view alone |
| `ETERNALVR_TEST_PE_UPDATE_UNION=0` | view 1's update lists are not added to view 0's: models only eye R sees are not prepared or updated (as before) |
| `ETERNALVR_TEST_PE_PAIRING=guess` | eye 0 the presented image and eye 1 the copy of view 1's image guessed to go with it, as before the pairs (`frame`, the default, pairs by frame) |
| `ETERNALVR_TEST_PE_PAIR_SLOTS=2`, `3`, `4` | the pairs kept (default 2; each pair two eye images) |
| `ETERNALVR_TEST_PE_EYE1_LAG=1` | a rig control for the eye sync check (not with the guess): each new pair shows eye 1 of the pair shown before it, so eye 1 is a frame late, as eyes out of sync would be; the pair shown last is kept for it (at least 3 pairs kept). Log: `eye-snapshot: eye 1 a frame late (ETERNALVR_TEST_PE_EYE1_LAG=1, ...)` once, and in the 10 s lines `eye 1 a frame late (ETERNALVR_TEST_PE_EYE1_LAG=1) N, two or more frames late N, its own frame's (no pair shown before) N` |
| `ETERNALVR_TEST_PE_DROP=<N>` | a rig control for the judder check (2 to 60, not with the guess): every Nth present with a new pair is not handed to the headset, which keeps the last pair as when no pair is new (the pair stays unread: the next present shows it or a newer one). Log: `eye-snapshot: one present in N with a new pair is left out (ETERNALVR_TEST_PE_DROP=N, ...)` once, `new pairs left out (ETERNALVR_TEST_PE_DROP=N) N` in the 10 s lines and `D` in the trace |
| `ETERNALVR_TEST_PE_REPEATS=show` | with the guess: a present whose eye 1 would repeat its last copy (the eyes a frame apart) is shown instead of dropped |
| `ETERNALVR_TEST_EYE_SNAPSHOT=0`, `1`, `2` | `0`: no copies, each eye from its view's image at present; with the guess `1` always the copy before the matched one, `2` the matched one also when both frames drew the presented image |
| `ETERNALVR_TEST_PE_SWAP_GUARD=0` | view 0's screen pass is not left out when its swapchain image was destroyed since the last acquire: a swapchain recreate in a frame with both views crashes as before (the control run) |
| `ETERNALVR_TEST_PE_WATER=0` | view 1's water setup and job work on the world's water state as before: the simulation steps twice a frame and each view takes the other's grid matrix |
| `ETERNALVR_TEST_PE_RT_UPSCALE_HOLD=0` | `r_raytracedReflectionsTemporalUpscaleQuality` left as the game has it (1 to 3 after the profile's load), not held at 0: the ray-traced reflections' temporal upscale as before |
| `ETERNALVR_TEST_CPU_LOAD_MS=<ms>[,<s on>,<s off>]` | as under Route S (below), a slower processor: Parallel Eye Rendering has no frame-end wrapper, so the load runs once a render frame at its view dispatch, before the views are dispatched. Log: `test: CPU load <ms> ms at every render's view dispatch (Parallel Eye Rendering) ...` |
| `ETERNALVR_TEST_PE_ASYNC=keep` | async compute left as the game has it, to reproduce the hang |
| `ETERNALVR_TEST_BINNING_DELAY=<ms>` | view 0's binning sleeps before its sinks (1 to 100 ms), so view 1 reaches its roots first |
| `ETERNALVR_TEST_CB_CHECK=1` | the command buffer check (`cb_check.hpp`; roughly halves the frame rate); with it `ETERNALVR_TEST_VIEW1_GPU_SKIP` / `_VIEW0_GPU_SKIP=<categories>` drop a view's draws by category and `ETERNALVR_TEST_CALLERS=<category>` logs view 1's draw call stacks (`cb_view_skip.hpp`) |
| `ETERNALVR_TEST_VIEW_LIFT=<view>,<metres>` | raises one view's camera, to tell which eye shows which view |
| `ETERNALVR_TEST_INSTALL_FAIL=check`, `redirects`, `hook` | the install stops as a failure would: `check` after every check, nothing changed (Route S runs); `redirects` in the redirects' hooks, after the block move and view 1's contexts with their counts raised, before the code bytes; `hook` after the code bytes are changed (both view 0 alone for the session) |

**Logs.** At start-up: `parallel eyes: on: both eyes as two views of one render (29 slot sites, 8 occlusion
sites); async compute off; view 1's clones on; eye copy each view's screen pass; parts off
(ETERNALVR_TEST_VIEW_OFF): none`, before it `view-redirects: ... fan-out bytes changed` and `parallel eyes:
r_maxRenderViews 1 -> 2`; at the device setup `parallel eyes: async compute off (the device setup read
r_enableAsyncCompute N as 0)`; then `stereo: Parallel Eye Rendering's eye copy on`, `view-clones: build 0:
...` and `eye-copy: eye 1 takes view 1's image`. Every 10000 two-view frames `parallel eyes: N two-view
frame(s); view 0 alone: ...` with the redirect and binning counts, every minute the clones' counts. Each
build logs every clone (`view-clones: build N clone K: '<engine name>' (<where the engine keeps it>) WxHxD, L
layer(s), M mip(s), format F (<name>), X MB (vk Y MB)`; a build that makes the same set again, as each map
load does, logs `the same N clones as build M`), a summary (`view-clones: build N: I image clone(s), X MB
...`), what it left shared (`... object(s) left shared (ETERNALVR_TEST_VIEW_CLONE_SKIP=...)`) and what it did
by name (`image clones by name: K kept, A allocated again, N new, B made beside one still allocated at
another size; W wait for the engine's next resize to be purged`; at the resize `the engine's resize: P image
clone(s) purged`). At the first minute's report 120 s or more after a build the census follows: `view-clones: census
of build N: clone K '<name>' X MB: stored S, target binds T, image binds I, marks M, as the clone C, other
contexts O` for each clone, then the clones view 1 never used with their size and the clones seen on other
command contexts than view 1's (a clone never used is strong evidence, not proof: the image A/B decides),
and for what the build left out `left out '<name>' (<where>) X MB: view 1 stored S, target binds T, image
binds I, marks M` with `view 1 used U of L left-out object(s) and image(s)` (any use there is view 1
working on view 0's object). After a
multiplayer guard trip: `parallel eyes: the multiplayer guard has tripped: view 0 alone from now on`, then
`parallel eyes: after the multiplayer guard trip no frame holds view 1 any more; ...`. Off: no
line at all without the variable, else `parallel eyes: off: <why>; the standard renderer` or `parallel eyes:
not available for this game version (Game Pass 1.0.56.0, timestamp 0x69BC663D)` (the build's name when EternalVR
knows it). A hook that failed after the engine was changed: `parallel eyes: FAILED after the engine was changed
(above): not on; both eyes show the same image (view 0's) for this session, ...`. The launcher's session summary,
its status line and the report's `last session parallel eyes` say which of these the session logged
(`launcher/src/EternalVR.Launcher.Core/ParallelEyesRun.cs`): on, on and then one image in both eyes (after a
multiplayer guard trip, with view 1's clones off, or with the async compute safety net), off with the reason, not
available, or FAILED. The report's line needs a session with a headset frame logged, and a problem shown during
the session keeps the status line.
The eye copies: `eye-snapshot: on: each eye from a copy of its view's image made after its frame's submit, a
present shows the newest pair of one frame (2 kept)`, at the first copies `eye-snapshot: pairs: eye N's copies
2 images WxH format F, S MB`, when the game's submits move to another queue family (its command buffers are
made again for it once no copy is pending) `eye-snapshot: pairs: eye N's copies move from queue family A to
B`, and every 10 s (as the change since the last line; at vkDestroyDevice the same
lines for the session) `eye-snapshot: last 10 s: N present(s): a new pair N (P%), the last pair kept N (P%), no
pair ...`, `eye-snapshot: last 10 s: eye 0 copies N (no slot free N, ...), ... eye 1 copies N (...)` and
`eye-snapshot: last 10 s: frames with view 0's command buffer submitted before view 1's N, in the same submit
N, after it N, never seen N; presents with the newest frame's view 0 submitted N, not yet N; the frame shown 0
frames behind the newest N, 1 N, 2 or more N, changes N; copies never shown N` (with the guess its own two
lines in place of the first two). When an in-headset capture fires, the last 512 presents follow (on a thread
of their own, once the burst's presents are in): `eye-snapshot: trace of the last N presents, ...` with what
each field is, then `eye-snapshot: trace <present> <seconds> <newest frame>/<frame shown> <outcome, view 0,
order, drawn by> i<image> c<presenter copy> v<view record> | ...`, four presents a line.

**Known limits.**

- Steam build 25216728 only. Anti-aliasing TAA, DLSS or Off: the layer holds `r_antialiasing 1`, with
  `ETERNALVR_STEREO_DLSS=1` `r_antialiasing 2` (DLSS in both views, above), or with `ETERNALVR_STEREO_TAA=0`
  (the launcher's Off) `r_antialiasing 0` and `r_TAASafeMode 1`. With DLSS each eye's feature takes its own
  video memory (about 250 to 370 MB with DLSS 310), as under Route S.
- Rig-checked on the simulator (deaths, a level change, both eyes matching Route S by numbers in e1m2 and e1m3);
  not yet the main menu to campaign flow, pause and Dossier menus, cutscenes or long sessions; the headset only
  in early tries (the black screen there was async compute).
- The 32nd change of the engine's targets (resolution or render scale changes, ray tracing turned on or off;
  the first set of clones counts as one of 32 builds) turns the clones off for the process: view 0 alone from
  then on, both eyes its image (`view-clones: ... clones off, view 0 alone from now on`); a restart brings
  view 1 back. Up to 64 remakes after map loads are fine.
- The in-headset bug capture takes both halves of the ring slot the headset gets: the pair shown.
- After a swapchain recreate the headset keeps the last pair for up to 4 presents (not checked in the
  headset). The swapchain guard also leaves view 0's pass out when the pass would draw into the device
  context's other screen target (+0x588, +0x590, picked at 0x1CDF73F when a renderer flag is set) while the
  swapchain image is stale: one frame without view 0 at such a recreate.
- The two views' pictures can still differ in small ways (faint motion-vector blocks low in view 1 were seen).
- The models only eye R sees are updated with view 0's render view, as all the others: billboards face view
  0's position (32.5 mm away), and with ray tracing on a particle system takes view 0's in-view bit (render
  view +0x20980, read at 0x1953F05), which is clear for it. Not checked in the headset yet.
- Not with foveated rendering: its passes take their eye from Route S's eye tags, which the two views do not
  have, so it would keep every pass at full rate. With `ETERNALVR_PARALLEL_EYES=1` on the build Parallel Eye
  Rendering supports, the layer turns `ETERNALVR_FOVEATION` and the `ETERNALVR_VRS_TEST` experiments off,
  whether or not Parallel Eye Rendering then runs (the multiplayer guard not armed or a failed check leaves
  the standard renderer, still without foveation; `vrs: foveated rendering is off: Parallel Eye Rendering is
  requested (ETERNALVR_PARALLEL_EYES=1 on its build)`). On other builds the variable does nothing and
  foveation stays on. The launcher greys Foveated rendering out and does not set the variable.

## Frame pacing (`ETERNALVR_PACE=headset`, the launcher's default)

Without it the game renders stereo pairs as fast as it can and each XR frame shows the newest finished pair
(`updateImage` takes the ring's newest slot; which one exactly: below). A game faster than the headset (a player's
RTX 5080 drew a median of 142 pairs a second on a 90 Hz Quest 2) gets an uneven pulldown: some headset frames show
a pair one game frame newer than the last, others two. Head rotation stays smooth (the compositor turns every
frame to the head), but the world's animation, locomotion and the gun advance at an irregular cadence that a
native VR game, which renders one frame per `xrWaitFrame`, does not have. The launcher turns pacing on by default
since 0.1.12 (players preferred it in 0.1.11, where it was an option); the layer's own default without the
variable stays off.

**Which image a headset frame shows** (`src/features/pacing/slot_choice.*`, the decision; `takeRenderedSlot` in
`src/vkcore/presenter_ring.cpp`). The game publishes an image when it presents, before its GPU work for it is
done, and runs about a frame ahead, so at the start of a headset frame the newest image is often still
rendering. The worker takes it if it is done; else it waits for it on the shared fence while the headset's frame
has time (until 4 ms before the end of the display period, counted from `xrWaitFrame`'s return, on a
high-resolution timer); else it takes the image published before it if that one was not shown yet, is done and
was not written again; else it shows the last image again. Before 0.1.29 the worker took the newest image and
waited up to two periods for its copy, which under Parallel Eye Rendering (both eyes in one frame) cost a
display slot whenever the frame ran past its period (rig, e1m1 GPU-bound: 2.7% of slots skipped, now 0.5-1.6%;
Route S unchanged). Taking the previous image without waiting kept the slots but showed fewer new images (63-65
of the game's 85-87 a second, against 70-71 with the wait) and older ones (pose age p95 80-91 ms against 58-63).

`ETERNALVR_PACE=headset` (the launcher's "Frame pacing: Matched to the headset", Play tab,
Picture, stereo only) holds the game to one image per headset frame (`src/features/pacing/pace_policy.*`, the
decision and its counters; `src/vkcore/frame_pacing.*`, the glue):

- **Where the game waits.** In the layer's `vkQueuePresentKHR`, after the downstream present returned and the
  hook's own timing closed, with no lock of the layer held, and only after a present that handed an image to the
  XR worker (`publishSlot`): under Route S eye R's present, which completes the pair (eye L's only copies its
  half). The render thread waits there for the headset's next frame; its next frame (eye L of the next tick)
  starts when it comes. The engine's frontend never runs far ahead of its render thread, so the game ticks
  follow at one per headset frame, each started at the same point of the headset's frame loop.
- **The signal.** The XR worker raises it in every frame it renders, in `updateImage` the moment it chooses the
  newest image (after `xrWaitFrame` and `xrBeginFrame`), not when `xrWaitFrame` returns: an image the game
  finishes quickly can then never be taken by the frame that released it, so while the game keeps up every
  image is shown exactly once. It comes before `updateImage` waits for the image's copy (which waits for the
  game's GPU work), so the game's next frame never waits for the previous one's GPU work.
- **Timeout.** A wait ends after two display periods without a new headset frame (one frame the runtime skipped
  still ends it in time), at most 50 ms, and counts as a timeout. Once the headset has begun no frame for ten
  periods, about 110 ms at 90 Hz (not shown, the dashboard, a lost session, shutdown), nothing waits until it
  does again: the game never hangs on the headset. Shorter stalls keep the game held: a streaming runtime
  whose encoder falls behind stalls its loop for 40-100 ms, and with three periods (before 0.1.32) the game
  ran free in every stall, rendering images nobody saw (a Virtual Desktop log: 186 in 10 s at 90 Hz).
- **Behind a runtime's menu.** While the session is VISIBLE, or SYNCHRONIZED once hidden, after it had focus
  (SteamVR's dashboard, Meta's menu), keep-active keeps the game running without input, so the present hook
  holds it to one image per display period (clamped to 1/90 to 1/30 s; 1/72 s while no period is known; not fewer, as a session can stay VISIBLE while the game is what the player sees) on
  its own clock, pacing on or off and whether or not the headset's frames come (`UnfocusedCap`; public issue
  #19). Letting the game pause instead was not used: the dashboard does not take the window's focus, and a
  posted deactivation would change the game's own state (its pause) where the cap only holds frames back. Log: `pace: a runtime menu is over the game: ...` and
  `pace: the menu is gone; ...`.
- **Slower PCs.** A game that cannot keep up with the headset never waits (a headset frame always began while it
  drew), so below the headset's rate nothing changes; the uneven cadence of a game just below the rate (an
  occasional repeated frame) remains.
- **Pose.** The camera hook predicts the head and hands for `predictedDisplayTime` plus one period, plus the
  measured lead under `ETERNALVR_POSE_LEAD` (`xr_math/display_lead.hpp`). The period added is at most 1.5 times
  the headset's refresh period (the runtime's own refresh rate when it gives one, else the base of "Headset
  refresh rate" below): while a runtime throttles it reports 2x or more, and player logs had 22 to 56 ms, so
  the head was predicted that far ahead and every movement overshot (`src/features/pacing/prediction_period.*`;
  `xr: pose prediction: the display period ... is over 1.5x the headset's ...` and `... again`, at most 20
  lines, then a count after each refresh summary). Not with the pose lead on: the lead is measured against
  the reported period, so it would add a held period back and then lag behind the runtime's next change
  (`xr: pose prediction: the pose lead is on, so the display period is added as the runtime reports it`). Paced, every game frame is shown the
  same time after its pose was taken, so the lead converges on that time and each pose is predicted for the
  display time it is shown at. Under `ETERNALVR_PACE=headset` the pose lead is on by default
  (`ETERNALVR_POSE_LEAD=0` turns it off, to compare the two separately).
- **Menus, loading screens, cutscenes, mono.** Every image handed over counts, mono frames included, so menus
  and cutscenes are held to the headset's rate the same way; a loading screen slower than the headset never
  waits. The layer paces whatever mode it runs in when asked; the launcher offers it in stereo only.
- **Alternate eyes.** Not with `ETERNALVR_ALTERNATE_EYES=auto` (the layer logs it and stays off, the launcher
  greys it out; with Parallel Eye Rendering, which ignores alternate eyes, pacing stays on): the adaptive switch decides by the game's tick rate, which pacing holds at the headset's, so
  once alternating it would never measure the headroom to render both eyes per tick again. With `1` each
  present hands over a pair (its fresh eye beside the other's newest), so pacing holds the alternating ticks,
  and each eye, to the headset's rate and half of it.

Log: at start-up `pace: ETERNALVR_PACE=headset: ...` or `pace: off: ...`. Every 10 s after the `rates:` line,
in both modes, `pace: <off|headset>; last 10 s: F headset frame(s): A with no new image, B with one, C with two
or more (N image(s) never shown); X image(s) per headset frame; game frames shown L ms after the time their pose
was predicted for, on average`: paced and keeping up, B is close to F, X is 1.00 and L settles near 0 once the
lead has converged. Paced, a second line: `pace: last 10 s: H image(s) handed over; W wait(s) for the headset's
next frame, average M ms, longest M ms; T timeout(s); G without a wait (a headset frame had begun), I with the
headset not running`. A `stall:` line's hook time does not include the wait.

The game's own cap would be cheaper: `com_adaptiveTickMaxHz` ("max game hz", 1000 by default; the dormant VR
path raises it to at least 120, `docs/rig-findings/stereo-reentry.md`) on the command line, for example
`+com_adaptiveTickMaxHz 90`. It caps the tick rate on the game's own clock, though, not in phase with the
headset's frames and drifting against them, so the uneven pulldown would come back as a slow beat. Not tried.

## Headset refresh rate

The runtime's `predictedDisplayPeriod` is 1/refresh, but several runtimes report a multiple of it while they
hold the game at a fraction of the refresh rate: Virtual Desktop doubles it while SSW is active, SteamVR reports
a measured period that reads 2x or 3x while it throttles or Motion Smoothing runs, Pimax doubles it under Smart
Smoothing. Some headsets also change their refresh rate during play. The 10 s `xr:` line's `display period` is
the latest frame's only, so the layer watches every frame's period (`src/features/pacing/display_period_watch.*`,
the logic; `src/vkcore/presenter_refresh.*`, the glue):

- **Hysteresis.** A new period counts once it holds for 6 frames in a row; one odd frame is no change. Periods
  within 3% of each other are the same period (SteamVR's measured period wanders a little).
- **Base.** The shortest period that held for 3 s: the headset's refresh rate as far as the period shows it.
  A settled period about 2x to 6x the base is the runtime throttling or reprojecting; one that is no multiple of
  it is a refresh change. A real refresh change to exactly half the rate (144 to 72 Hz) cannot be told from
  throttling by the period alone; the runtime's own rate below can.
- **Time.** The time between frames goes to the settled period; gaps over 0.5 s (the session not running) are
  not counted.
- **`XR_FB_display_refresh_rate`.** Enabled only when the runtime lists it (VDXR, SteamVR, WiVRn; Varjo does
  not); never used to request a rate. Once the session runs the layer reads the runtime's refresh rate, reads it
  again at each logged period change (SteamVR sends no change event), and logs the extension's change event.
  Changes are then named against the headset's own refresh period, and a session that ran throttled from its
  start still gets the right base. Without the extension, or when its getter fails (logged once), the period
  alone decides.

Log lines (the time is the log's, so it matches the line's own time stamp):

- `xr: refresh rate 90.0 Hz (XR_FB_display_refresh_rate)` when the session starts; `xr: refresh rate 144.0 ->
  90.0 Hz (XR_FB_display_refresh_rate, read at the display period change)`; `xr: refresh rate 90.0 -> 72.0 Hz
  (XR_FB_display_refresh_rate event)`. Without the extension, once: `xr: XR_FB_display_refresh_rate not offered;
  the refresh rate is read from the display period only`.
- `xr: display period 11.11 ms at 4.2 s (90 Hz)` when the first period settles (`(90 Hz, the headset's refresh
  rate)` with the extension).
- `xr: display period 6.94 -> 13.89 ms at 312.4 s (2x the base 6.94 ms: the runtime is throttling or
  reprojecting)` (with the extension: `2x the headset's 6.94 ms at 144 Hz: ...`), `(back to the base: 144 Hz)`
  (`back to the headset's refresh rate: 144 Hz`), `(a refresh change to 90 Hz)`, `(shorter than any steady period
  before: 144 Hz)` (a session that started throttled, before its base held). The first 200 changes are logged,
  then `xr: display period: 200 lines logged; later changes are only counted`.
- Every 60 s while frames run, and when the XR worker finishes: `xr: refresh summary: base 6.94 ms (144 Hz); 1x
  81.5%, 2x 17.2%, 3x 0.9%, other 0.4% (90 Hz 0.4%); 12 change(s) in 312 s` (`xr: refresh summary at session
  end: ...` for the last). Shares are of the time counted; multiples up to 6x are named, longer periods and
  non-multiples are `other` with up to three rates listed.

The status file (`eternalvr-status.txt`, `src/vkcore/status_file.hpp`) carries the same facts for the
launcher, after its first five keys, each rewritten when it changes: `runtime=` and `system=` (the names the
runtime reports), `recommended=WxH` (the runtime's size per eye), `render=WxH` (each eye's image the headset
gets), `refresh_hz=` (the base, for example `144.0`) and `throttled_share=` (the share at 2x the base or more,
for example `0.181`, with each summary).

## Render size (T-031)

With `ETERNALVR_RENDER_SIZE=auto` (the launcher's default in stereo) each eye renders at the runtime's
recommended view size, scaled into a 2064 x 2208 pixel budget, times `ETERNALVR_RENDER_SCALE`, whatever the
desktop's displays are: the layer answers the game's own `GetClientRect` calls for its swapchain and output
size with that size and creates the swapchain with present scaling, so the real window is only a small
desktop mirror (docs/rig-findings/render-size.md). Quest 3 through VDXR (2496 x 2688 recommended) renders
2056 x 2216 per eye. Without it (`off`, or anything missing: fail closed) each eye renders at the game
window's size, as the mono view does. The ring is twice the eye's width. The eye's projection maps its own
FOV onto the whole image, so any aspect works; the recommended size gives square pixels (a 2560x2100
window gave non-square ones, which the compositor maps back correctly, only the pixel density differing by
axis).

## Settings (environment)

| Variable | Default | Effect |
|---|---|---|
| `ETERNALVR_MODE` | head-tracked | `stereo`: Route S (or an experiment, below) |
| `ETERNALVR_STEREO_SAME_VIEW` | 0 | 1: both eyes render the game's own view (S1); pairs are shown with the head pose |
| `ETERNALVR_CAPTURE_EYES` | unset | `<dir>[,<N>]`: every Nth pair (default 60) written as `eyes-<pid>-p<pair>-t<tick>-L.png` / `-R.png`, uncompressed (one pair is written at a time and compression would take about 0.7 s per pair; the in-headset capture's files are compressed) |
| `ETERNALVR_CAPTURE_BURST` | 1 | `<n>`, 1 to 16: each in-headset capture (docs/VR_CONTROLLERS.md) saves n consecutive frames instead of one: Route S pairs or Parallel Eye Rendering frames (both eyes each; under Parallel Eye Rendering both halves of the ring slot the headset gets) as `-f00-L.png` / `-f00-R.png` and on, or mono frames (head-tracked play, a menu or loading screen) as `-f00-mono.png` and on; the text file has a line per frame (`burst frame NN: stereo pair P, game tick T`; under Parallel Eye Rendering `burst frame NN: game frame record R` and the copy of view 1 that eye R shows; for a mono frame its game frame record). A frame of the other kind (a menu's mono frame in a burst of pairs, a pair in a burst of mono frames), a new render size or a frame given up ends a burst early with the frames it has. The GUI image comes with frame 00 only. Host memory: one buffer per image, about 18 MB at a 2056x2216 render size (36 MB per pair, about 580 MB for 16 pairs), taken for each burst and freed once its frames are written; a burst whose buffers would take more than half the physical memory free, or that cannot have them, is one frame instead. A burst counts each of its frames against the session's 50 frames (`bug_capture::kMaxFramesPerSession`, about 650 MB of PNG at most) and takes no more than are left. One background thread compresses the images in turn, straight from those buffers (about 0.35 s per eye image: about 11 s for 16 pairs, and no other capture starts until it is done); on disk about 12 MB per pair. The launcher's Export report takes a burst's text file and GUI image, then its frames in order while they fit in its 20 MB: at that render size the first frame |
| `ETERNALVR_STEREO_PREV_MATRICES` | 1 | 0: no previous-matrix hook (the eyes share the engine's) |
| `ETERNALVR_STEREO_FIX_CENTERED` | 1 | 0: leave the centred matrix as the latch builds it (the weapon disappears) |
| `ETERNALVR_STEREO_VSYNC` | 0 | 1: keep the game's FIFO present mode (tick rate capped at half the refresh) |
| `ETERNALVR_STEREO_SWAP_IMAGES` | 4 | the game's swapchain image count under Route S (0: the game's own, 2) |
| `ETERNALVR_WINDOW_PRESENTS` | gated on NVIDIA | `all`: every present reaches the game's window (no image is handed back); `gated`: hand images back on any GPU |
| `ETERNALVR_MIRROR` | left | what the game's window shows during stereo pairs: `left`, `right` (one tick late) or `off` (black); while a menu is up over the game, the menu panel's image (Desktop window, Menus in the window) |
| `ETERNALVR_RENDER_SIZE` | off (launcher: auto) | `auto`: each eye at the runtime's recommended size (budget 2064 x 2208 pixels) times the scale; `WxH`: that size; `off`: the window's size (docs/rig-findings/render-size.md) |
| `ETERNALVR_RENDER_SCALE` | 1.0 | 0.5 to 2.0, multiplies the `auto` size (above 1 past the budget) |
| `ETERNALVR_MIRROR_WINDOW` | unset | `x,y,width,height`: the game window's client area once the render size is on (the desktop mirror); `ETERNALVR_WINDOW` is where it goes before and when the render size turns off |
| `ETERNALVR_MIRROR_DISPLAY` | launcher | with the render size: the display the mirror window goes on, centred in its work area: `primary`, a number (`1` = the first display Windows lists; the log lists them as `mirror: display N ...`) or a desktop point `x,y` (the display holding it); `launcher` keeps `ETERNALVR_MIRROR_WINDOW` as it is |
| `ETERNALVR_MIRROR_SIZE` | launcher's | with the render size: the mirror window's client area, `WxH` (64 to 8192) or a scale of the launcher's size (0.25 to 4); fitted to its display. `fill`: the window covers the whole display `ETERNALVR_MIRROR_DISPLAY` names (its full area, taskbar included; `launcher` means the display holding the launcher's rectangle) without a frame, for others watching; the image is stretched into it: the whole eye image with `ETERNALVR_MIRROR_CROP=full` (visibly squashed, the eye image is taller than a monitor), the crop's band with `16:9` (exact on a 16:9 monitor, slightly stretched on another shape). Never raised above the taskbar or activated, so the taskbar can stay on top on a display that has one. Log: `mirror: window fills display N at x,y WxH (borderless, stretched\|cropped)` |
| `ETERNALVR_MIRROR_CROP` | full | with the render size: `16:9`, `16:10` (or `W:H`) shows only the eye image's centred band, in a window of that shape (the band stretched over the image in the ring copy, one blit per present the window shows; the swapchain stretched into the window); `full` shows the whole eye image in a window of the eye's shape: the size asked for (`ETERNALVR_MIRROR_SIZE` or the launcher's) is cut to the largest rectangle of the eye image's aspect inside it (a 1280x1400 eye in 1280x720 gives 658x720), at the same top-left corner, or centred where `ETERNALVR_MIRROR_DISPLAY` centred it, so the image fills the window without bars from the first frame. Not with `fill` (the display's shape). Log: `mirror: the window takes the eye's shape: WxH at x,y (WxH eye inside WxH, ...)` |
| `ETERNALVR_MIRROR_FRONT` | 1 | 0: the game window is not brought to the front once when its surface is made (never activated either way) |
| `ETERNALVR_UI_CROP` | 1 | 0: the UI quad and the menu panel show the whole GUI target instead of its 16:9 band |
| `ETERNALVR_UI_WASH` | 1 | 0: the HUD quad shows the GUI target's full-screen additive wash (the red low-health vignette) as the game drew it (docs/rig-findings/ui-layer.md, section 12) |
| `ETERNALVR_STEREO_RUNTIME_CVARS` | 1 | 0: do not hold `r_TAASafeMode 1` / `r_antialiasing 0` at run time; with Parallel Eye Rendering its anti-aliasing (`r_antialiasing 1`, or the Off set) is not held either (one `cvars: Parallel Eye Rendering's anti-aliasing is left as the game has it` line) |
| `ETERNALVR_PACE` | off | `headset`: one image per headset frame, the game's render thread waiting after each pair for the headset's next frame (at most two display periods); the pose lead then defaults on (Frame pacing) |
| `ETERNALVR_CPU_SAVER` | unset | `name=value;...` held at run time like the stereo set: the cvars of the launcher's texture streaming and CPU Saver items that are on (`launcher/data/cpu-saver.txt`, docs/rig-findings/perf-cpu-cvars.md); a value `<=N` is a cap, written only while the cvar's float value is above N; a cvar the stereo sets hold keeps their value |
| `ETERNALVR_STEREO_SSDO` | unset (= 1) | 1 or unset: `r_SSDO 1` held at run time under Route S (the launcher passes the player's own value from their config, 1 unless it sets 0). The game turns SSDO off itself after `r_TAASafeMode 1` (0x1C6FCC0), which Route S holds at start-up, so before this every Route S session ran without it and each eye's outer strip read 15 to 20% too bright against a mono reference in shadowed corners. SSDO's own temporal filter runs only with its history per eye (`ETERNALVR_STEREO_SSDO_TAA`), else it stays off (`r_SSDOTemporalAA 0`), so neither eye reads the other's history (with the filter off: sway ghost check, no cross-eye ghost; about +0.5 ms GPU per stereo pair on an RTX 3080 Ti at 2048x2208). 0: held at 0 the same way. Both follow the game's Directional Occlusion setting: a mid hook at its setter's entry (0x1420F20, called by the profile's load, the overall preset and the video menu's apply; level 0 writes `r_SSDO 0`, levels 1 to 6 `r_SSDO 1`) reads the level, and the hold takes it from then on (`cvars: the game's Directional Occlusion setting ran (level 3): r_SSDO 1`, `cvars: r_SSDO held at 0 from now on (the game's Directional Occlusion setting)`; without the setter found, `cvars: r_SSDO is held at its launch value ...`). The knock-on never calls the setter, so it is still written over. A change made in the game's menu during a VR session stays: the launcher's restore keeps the `r_SSDO` the game saves when the status file says `ssdo_follow=1` (the setter ran and per-eye TAA was on; not with anti-aliasing Off, where the knock-on writes 0 on every render against the hold) and the game saved the `ssdo_value` held (left out of the config: its default, 1), else puts it back as before (`LaunchPlan.KeptKeys`, `LayerStatusFile.Followed`). A setter not found in another build (`cvars: r_SSDO is held at its launch value, not at the game's Directional Occlusion setting ...`) leaves `ssdo_follow=0`, so a launch value never sticks in the config. Any other value: not held (one `cvars: r_SSDO is left as the game has it` line) |
| `ETERNALVR_STEREO_SSR` | unset (= 1) | 1 or unset: `r_SSR 1` held while per-eye TAA or per-eye DLSS runs, by its per-tick check and only once its `r_TAASafeMode 0` holds (the launcher passes the player's own value from their config, 1 unless it sets 0, as Reflections Low does; `seq-taa: r_SSR held at 1 (ETERNALVR_STEREO_SSR=1; per-eye TAA; follows the game's Reflections setting)`, `=unset` without the variable); 0: held at 0 the same way. Both follow the game's Reflections setting (0x1421DC0), which writes `r_SSR` with `r_raytracedReflectionsTemporalUpscaleQuality` (Low: `r_SSR 0` with 3; Medium: `r_SSR 1` with 2; High and above: `r_SSR 1` with 1): the forced set holds that quality at 0 (from the command line's `+r_raytracedReflectionsTemporalUpscaleQuality 0`; its default 1 would read as Medium or higher until the profile's load), so the check reads it first on each tick, and a 1 to 3 there means the setting ran since the last tick (the profile's load before the first stereo tick, or the player's apply in the video menu); its `r_SSR` is held from then on (`seq-taa: r_SSR held at 0 from now on (the game's Reflections setting: off, Low)`). The safe mode knock-on writes only `r_SSR 0`, so it is still undone. A config without `r_SSR` cannot tell Low from Medium (both save `r_SSRQuality 0`), so the profile's load decides. off (the launcher's Screen-space reflections Off): held at 0 whatever the game's setting, under Parallel Eye Rendering too (its run-time set). The same knock-on as SSDO: the game writes `r_SSR 0` on every render while `r_TAASafeMode` is not 0 (0x1C6FCC0, 0x1C71630), so before this Route S sessions ran without screen-space reflections. SSR keeps no history of its own; it reads the last frame's colour through the TAA history selector (0x1CBB6C0), which per-eye TAA gives each eye's own (static reading, not rig-checked yet). Not held with per-eye TAA off (`ETERNALVR_STEREO_TAA=0`) or failed closed, where that colour would be shared (`seq-taa: r_SSR is not held: per-eye TAA is off (ETERNALVR_STEREO_TAA=0)` or `... failed closed`, and `seq-taa: r_SSR no longer held: per-eye TAA failed closed` when it fails closed later). A Reflections change made in the game's menu during a VR session stays: the hold follows it, and the launcher's restore keeps the `r_SSR` the game saves when the status file says `ssr_follow=1` (the hold followed the game's setting with per-eye TAA on; `LaunchPlan.KeptKeys`) and the game saved the `ssr_value` held (a key left out of the config counts as its default, 1), else puts it back as before: a launch or knock-on value saved before the follow, as after a crash, is put back. The upscale quality is read and written back to 0 in one step on each tick, apart from the rest of the forced set, so a menu apply landing between the two is not lost. Under Parallel Eye Rendering the layer reads the variable only for off (per-eye TAA is a Route S module); Parallel Eye Rendering does not hold `r_TAASafeMode 1` with its TAA, so the player's `r_SSR` stays there anyway. Any other value: not held (`seq-taa: r_SSR is left as the game has it (ETERNALVR_STEREO_SSR=...)`) |
| `ETERNALVR_SHARPENING` | unset | a number from 0 to 10 (the launcher's Sharpening: 0, 1, 2 or 3): `r_sharpening`, the game's post-process sharpening, held at run time and compared as a float (the game's menu sets fractions such as 1.99); unset leaves the player's own setting. With DLSS 2.5.1 and later it is the only sharpening: NGX logs that DLSS's own is deprecated and disabled |
| `ETERNALVR_DEBUG_CVARS` | unset | `name=value;...` written at run time; `name=?` only logs (rig experiments); `<=N` is a cap as above; an entry replaces the CPU Saver's value for the same cvar |
| `ETERNALVR_VRS_TEST` | unset | experiments, over `ETERNALVR_FOVEATION`: `2x2` or `4x4` on every pass, `eyetest` (eye L's left half and eye R's right half at 4x4: which eye each pass got shows in `ETERNALVR_CAPTURE_EYES` captures; the rig QA's `foveation-eyes` scenarios measure it with `tools/rig/qa/qa-blockiness.ps1`), `fovea` (the regions of `ETERNALVR_VRS_FOVEA=<full>,<half>` degrees, default 24,40) |
| `ETERNALVR_TEST_VRS_PARITY` | 0 | 1, a rig test with foveated rendering or the `eyetest` experiment: render passes whose counters do not agree also take the frame their command buffer's recording or parity guesses (`vrs: ETERNALVR_TEST_VRS_PARITY=1: ...`). The guesses can give a pass the other eye's pattern (`src/stereo_seq/pass_frames.hpp`); never set by the launcher |
| `ETERNALVR_VRS_TINT` | 0 | 1, with foveated rendering or an `ETERNALVR_VRS_TEST` experiment: a 4x4 dot every 32 pixels of the headset's eye images where that eye's rate image is at half rate (yellow) or quarter rate (red), to see the regions in the headset. It shows the eye's pattern, not each pass's rate: an eye image is dotted only when some of that eye's render passes got a rate image since its previous one, and passes kept at full rate (frame not known, the GUI target, other targets) are dotted where they draw. The captures and the desktop mirror show the game's image without them (`src/vkcore/vrs_marks.cpp`) |
| `ETERNALVR_PRESENT_IMMEDIATE` | 0 | 1: immediate present mode in any mode (mono frame-rate references) |
| `ETERNALVR_STEREO_FULL_RES` | 1 | `forceFullResolution` on both eyes |
| `ETERNALVR_STEREO_EXPOSURE_ONCE` | 1 | `skipAutoExposureUpdate` on eye R while the auto-exposure index hook gives it eye L's exposure (without the hook eye R updates its own); 0: eye R updates its own, and without per-eye history the engine's index is kept (one chain of both eyes) |
| `ETERNALVR_STEREO_DISCONTINUOUS` | 0 | 1: `discontinuousViewPosition` on both eyes (S5) |
| `ETERNALVR_STEREO_BIN_TILES` | 1 | the light and decal binning's tile grid set from each view's own projection (docs/rig-findings/stereo-bin-tiles.md); 0: the engine's symmetric grid, whose lit areas end in tile-shaped steps in the headset |
| `ETERNALVR_STEREO_INHIBIT_MODEL_FOV` | 1 | `inhibitModelFovScale` on each eye's view, and the hands-and-guns matrices in the eye's frustum |
| `ETERNALVR_STEREO_TAA` | 1 | per-eye temporal history (TAA and DLSS; section "Per-eye temporal history"); 0: v1, no temporal accumulation (auto exposure, the light scattering and SSDO stay per eye) |
| `ETERNALVR_STEREO_DLSS` | 0 | 1: DLSS per eye instead of TAA (`r_antialiasing 2`); under Parallel Eye Rendering each view's own feature (`ETERNALVR_PE_DLSS=0` keeps Route S for DLSS instead) |
| `ETERNALVR_STEREO_DLSS_QUALITY` | unset | with DLSS per eye: `quality`, `balanced`, `performance`, `ultra_performance` (or `3` to `0`), the `r_dlssQuality` held while DLSS runs; `dlaa`: DLSS at the full render size (render size = output size). The game maps `r_dlssQuality` 0 to 3 only (RVA 0x1CC5D40, 0x1CC5760; any other value is Balanced), so DLAA holds `r_dlssQuality` 3 and the layer sets NGX's `PerfQualityValue` to DLAA (5) on every write of it (`src/vkcore/dlss_dll.cpp`): the game's optimal render size, its feature and eye R's twin all become DLAA. Only with `ETERNALVR_DLSS_DLL` of DLSS 3.1 or later; otherwise DLSS runs at Quality and `dlss:` says why. Unset: the player's own quality |
| `ETERNALVR_DLSS_DLL`, `_PRESET`, `_ROUTE` | unset | a newer `nvngx_dlss.dll` of the player's own (DLSS 310: the transformer model) and its render preset, for both eyes' features; loaded from its own folder, never copied into the game's (`docs/rig-findings/dlss-dll.md`) |
| `ETERNALVR_STEREO_FX_SYNC` | 0 | 1: CPU particles and effects alike in both eyes (off by default since 0.1.35: snow and some effects broke in eye R in e1m3): eye R draws eye L's particle vertices and lights and generates only what eye L did not (How Route S works, CPU particles and effects; `docs/rig-findings/stereo-fx-lag.md`); `count`: nothing changes, what would be reused is counted; unset or 0: no game code patched, eye L one tick behind on CPU particles |
| `ETERNALVR_STEREO_FX_SYNC_GPU` | 1 | with `ETERNALVR_STEREO_FX_SYNC=1`: eye R binds the GPU particle stages of the particle systems eye L generated and generates those itself (`docs/rig-findings/stereo-fx-lag.md`, section 7); 0: they are reused like the rest, as in 0.1.34 (eye R drew them as tiles in e1m3), for A/B |
| `ETERNALVR_STEREO_OBJECT_PREV` | 1 | eye R's moving objects take their previous frame from eye R's own render of the tick before (`docs/rig-findings/stereo-object-motion.md`); 0: from eye L's render of the same frame (no motion, smeared by TAA) |
| `ETERNALVR_STEREO_VIS_GATE` | 1 | models one eye sees are drawn: the first-visible gate takes either eye's last render for models; particles, flares, beams and ribbons keep the engine's test (`docs/rig-findings/stereo-visibility-counter.md`); 0: the engine's own test, so a pickup in one eye's outer strip only is not drawn |
| `ETERNALVR_FLARES_PER_EYE` | 1 | lens flares built again from each eye's own view after its latch (How Route S works, Lens flares); 0: the engine's quads from the head-centred view, at the same place in both eyes (farther than infinity) |
| `ETERNALVR_ALTERNATE_EYES` | 0 | 1: one eye per game tick, eye L then eye R; auto: only while the ticks fall behind the headset (section "Alternate eyes"; `docs/rig-findings/alternate-eye.md`) |
| `ETERNALVR_FOVEATION` | unset | `subtle`, `balanced`, `aggressive` or `maximum` (the launcher's Foveated rendering, experimental): fixed foveated rendering through `VK_NV_shading_rate_image`, full rate within the region of 30, 24, 18 or 12 degrees around head-forward in each eye, half rate within the region of 16 degrees more (12 for `maximum`, so quarter rate from the region of 24 degrees), quarter rate outside (`src/vkcore/vrs_nv.cpp`). A region has the area of the cone of its angle on the eye's tangent plane and reaches the same fraction of the way from head-forward to every edge of the eye's image, so the reduced-rate band takes the same share of the way to the edge on the nasal side and the top as on the temporal side and the bottom (`src/features/foveation/foveation_region.cpp`); in a symmetric square FOV it is the cone's circle. Each eye's shape (its FOV and its orientation in the head) comes from stereo frames whose eyes pass the plausibility checks, the FOV check of `docs/VR_HEAD_TRACKED.md` among them (`ETERNALVR_FOV_CHECK`); a different shape that holds for 90 frames in a row replaces it and the rate images are made again, at most 8 times in a process (`src/features/foveation/eye_shape_latch.cpp`; `vrs: eye N's shape changed: ...`). Each render pass gets the pattern of the eye of the backend frame it is recorded for, and only where that frame is sure: the backend counter at the pass equals the counter the render-view job read for its render. Every other pass stays full rate. What its command buffer's recording since its `vkBeginCommandBuffer` or its parity (once two recordings in a row agreed two frames apart) would guess is counted, with the recordings a later agreement contradicts and the parity breaks, but not used: those guesses can name the other eye's frame (`src/stereo_seq/pass_frames.hpp`, `src/vkcore/vrs_command_buffers.cpp`; `vrs: render pass frames: ...` every 200,000 passes; on the rig every pass in the map agreed). A pass's eye so comes only from a frame its two counters agree on; with `ETERNALVR_TEST_VRS_PARITY=1` (a rig test) the guesses are used too. NVIDIA RTX only: other cards log it as unsupported and render normally. Off with Parallel Eye Rendering (one line; "Parallel Eye Rendering", Known limits). Only render targets in the eye's space are foveated: the eye image and any copy of it scaled by one factor down to 1/8 (DLSS's smaller render size, the half size buffers; `src/features/foveation/eye_targets.cpp`); shadow maps and other targets stay full rate. Mono frames (the cinema screen, menus) stay full rate. The game's menus and HUD (render passes into its GUI target, found by the UI layer) stay at full rate too (`src/vkcore/vrs_gui.cpp`); without the UI layer they are foveated like the eye image |
| `ETERNALVR_TEST_DLSS_TWIN_FAIL` | unset | test knob: a count from 1 to 100; eye R's first tries at its own DLSS feature fail without a create (result 0xBAD00000), to exercise the fallback to TAA, the tries after it and the menu's try (`docs/rig-findings/stereo-temporal.md`, Fail closed) |
| `ETERNALVR_TEST_CPU_LOAD_MS` | unset | test knob: `<ms>[,<s on>,<s off>]` busy-waits at every render's frame end (under Parallel Eye Rendering once a render frame at its view dispatch), as a slower processor (`alternate-eye.md` 10.4) |
| `ETERNALVR_TEST_PRESENT_OUT_OF_DATE` | unset | test knob: `<seconds>`: the first game present that reaches the driver that long after the first one returns `VK_ERROR_OUT_OF_DATE_KHR` to the game (the driver took it), once, so the game's swapchain recreate and the layer's hand-back of held images (Desktop window) run on any GPU; VR on only |
| `ETERNALVR_STEREO_SCATTER_TAA` | 1 | the light scattering's temporal filter per eye (`docs/rig-findings/stereo-scatter.md`); 0: the filter held off in stereo |
| `ETERNALVR_STEREO_SSDO_TAA` | 1 | SSDO's temporal filter (`r_SSDOTemporalAA`) per eye under Route S (section "Per-eye temporal history", SSDO); 0: the filter held off in stereo (`seq-ssdo: off (ETERNALVR_STEREO_SSDO_TAA=0); ...`) |
| `ETERNALVR_STEREO_EXPERIMENT` | unset | `left-eye` or `two-views`: the EngineNativeStereo experiments instead of Route S (Parallel Eye Rendering stays off with either) |
| `ETERNALVR_PARALLEL_EYES` | unset | `1` in stereo: Parallel Eye Rendering, both eyes as two views of one frame (section "Parallel Eye Rendering"), on Steam build 25216728 only; any other build, the UI layer off or DLSS with `ETERNALVR_PE_DLSS=0` keep Route S, and with a stereo experiment the experiment runs (logged). Async compute off, view 1's clones and the eye copy come with it. The launcher's "Parallel Eye Rendering (experimental)", off by default |
| `ETERNALVR_PE_EXPOSURE` | `same` | under Parallel Eye Rendering, the auto-exposure image view 1 reads: `same` the one view 0 writes this frame, `prev` the one view 0 wrote the frame before, `engine` the engine's own index (the parity of view 1's last own update, image 0 until it runs one) and view 1's update skipped only where its eye pose was written (the rig's positive control); section "Parallel Eye Rendering", one auto exposure for both eyes |
| `ETERNALVR_STEREO_EYE_POSES`, `_JITTER_COPY`, `ETERNALVR_TEST_WEAPON_FOV` | | experiments only (below) |
| `ETERNALVR_GPU_TIMING` | 0 | `sample` (or `sampled`): GPU timestamps around the submit batches of 3 frames in every 45, each eye's GPU busy time in a 10 s line (below); `1`, `on` or `true`: every frame, with the full summary and CSV; anything else: off; any mode |

## Logs and counters

Everything Route S logs starts with `seq:` (and `capture:`). At start-up: the settings, the command line
cvars, each hook point with its RVA, `frame-end job wrapped`, then `Route S on`. When stereo starts:
`render thread idle at backend frame N`, then `stereo tick for game frame ...` (first three). Every 10 s:

- `last 10.0 s: G game frame(s), R render frame(s) (X per game frame), B backend frame(s), T stereo
  tick(s), E eye R frame end(s)`: in steady stereo X is 2.00, B equals R, and T equals E and G.
- `pairs P complete of S started, M mono; dropped halves ...; pair(s) without a view record (not shown)`.
- `eye tags in sync: ... matched, ... untagged; out of sync ... (missing present) / ... (untagged frame)
  / ... (overflow) / ... (rebase); ... drain(s), ... failed, ... on a quiet period only, D ms in total,
  longest L ms; frames without a backend frame ...; previous matrices ... rewritten / ... kept / ... put
  back; r_swapInterval V`. D and L are the time the drains held the game's frontend (a hitch in the
  headset). "Put back" counts alternate-eye eye R renders that stayed mono after their previous matrices
  were rewritten: they render with what the engine stored.
  A failed drain logs `the render thread did not present every frame within 250 ms; mono for W ms (N
  failed drain(s) in a row)`.
- `eye R skipped ... (render-frame guard busy) / ... (multiplayer guard) / ... (stack); eye R chain at
  most N KiB deep, least stack left at eye R M KiB`.
- `seq-exposure: auto-exposure index held for A eye L or mono / B eye R render(s); C render(s) whose tag in
  flight names another eye`: A and B are above 0 in stereo whatever the TAA mode (both 0: the hook is not
  installed or not holding, see the `seq-exposure:` start-up line; without per-eye history and with
  `ETERNALVR_STEREO_EXPOSURE_ONCE=0` that is by design).
- `seq-ssdo: SSDO history per eye: A eye L / mono and B eye R render(s); restarts F first, Z after a resize, G
  after a gap, M eye R missed a tick, U untagged after eye L, D after dynamic resolution; V other view(s) and
  R dynamic resolution render(s) left to the engine; C render(s) whose tag in flight names another eye; targets
  WxH, eye R's WxH / WxH; r_SSDOTemporalAA N`: in steady stereo A equals B, the restarts come only at the
  start, a resize and a skipped eye R, U and R are 0, the three sizes are equal and N is 1. At start-up
  `seq-ssdo: SSDO targets (RVA ...), setup (RVA ...) and resize (RVA ...) hooked`, `seq-ssdo: eye R's SSDO
  targets made with device context ...` and the first four renders (`seq-ssdo: render N for eye X: writes ...,
  reads ...`, with `history reset: ...` where it starts over).
- With `ETERNALVR_STEREO_FX_SYNC=1` or `=count` only, `seq-fx: eye R reused eye L's particles N time(s) (P
  particle system(s), E effect(s)), light pool kept K; eye R generated M / Q itself (eye L had not) and U GPU-staged
  particle system(s) eye L had (S GPU stage(s) bound); eye L's particle systems with GPU particles W; generated by
  eye L A / B, eye R C / D (particle systems / effects); ring advances eye L F, eye R G; GPU particle manager per
  render (emitter records / draw list / light atlas): eye L r / d / a, eye R r / d / a (n / m GPU steps)`: in steady
  stereo N and K are close to F, G is 0, M and Q stay small, W equals U (U is 0 with
  `ETERNALVR_STEREO_FX_SYNC_GPU=0`) and eye R's GPU averages are close to eye R's in a `=count` run, not necessarily
  to eye L's (`docs/rig-findings/stereo-fx-lag.md`, section 5).
- With alternate eyes: `alternate eyes: A eye L / B eye R render(s); P shown ..., H held without a partner, ...;
  the held eye X game frame(s) older on average` (the first line then shows 1.00 renders per game frame and no
  stereo ticks).

Beside them (the 10 s blocks): `rates: game P present(s)/s, T tick(s)/s, S stereo pair(s)/s shown; XR F
frame(s)/s, N new image(s)/s; copy wait A ms average, M ms longest; newest image wait W ms average, L ms
longest`, the game's own rates next to the runtime's (a headset's frame counter shows the XR rate, not the
game's), how long the XR worker waited for its copy between xrBeginFrame and xrEndFrame, and how long it waited
for the newest image's frame to finish rendering (Which image a headset frame shows); the `xr:` line counts the
repeats with no finished image to take (still rendering, or being written), the newest images taken and the waits
for one; `window: last 10 s: A acquire(s), average/max ms; C present call(s),
average/max ms` (the time the driver's acquire and present take); `window: ... present(s) to the window,
... of the other eye and ... too soon handed back; display H Hz; ... handed back, ... failed, ... held`;
`mirror: the window shows left ...`; `pace:` (the headset's cadence and, with `ETERNALVR_PACE=headset`, the
waits; Frame pacing); `xr: refresh summary:` every 60 s and `xr: display period A -> B ms at T s` at each
change (Headset refresh rate); and `cvars:` for every run-time cvar write. Once no game present has come for 5 s or
more while the headset runs (checked with the rates line), `present: the game has stopped presenting: no game
present for N s ...` gives the newest failed present result and the images the window gate holds (or that
the presenter's lock is busy), and `present: the game presents again after N s ...` follows if they return. The eye capture logs each
eye L image's alpha (`eye L alpha min, mean, % below 255`): the projection layer is layer 0, which some
runtimes blend by its alpha.

The 10 s `xr:` line ends with `runtime gaps over 2 periods N, longest X ms`: how many intervals between consecutive
xrWaitFrame predicted display times were longer than twice the display period, and the longest interval, so
runtime stalls show even when the average rate looks close to the display's. After it, `xr: inside the runtime
last 10 s: xrWaitFrame A ms average, M ms longest; xrWaitSwapchainImage ... per call (N call(s)); xrEndFrame ...`
gives the time the XR worker spent inside those calls (the swapchain waits are each call for the game's image and
the UI image; xrWaitFrame normally blocks for most of a display period, which is the runtime's own pacing).

The in-headset capture logs `capture: saved eye L/R + UI to <base>-*.png (+ .txt), capture N: S of F frame(s),
T ms after the trigger pull` (F is 1, or a burst's frames; S of them were saved). With
`ETERNALVR_CAPTURE_BURST`: `capture: n consecutive frame(s) per capture (ETERNALVR_CAPTURE_BURST)` at the first
capture (`... is not 1 to 16; one frame per capture` for a bad value); `capture: a burst needs M MB of host
memory, more than half the F MB free; one frame instead` or `capture: K host buffer(s) of B bytes could not be
made; one frame instead` when the burst's memory cannot be had; `capture: L frame(s) left this session; this
burst takes L` near the session's limit. At shutdown, `capture: N not saved: the presenter stopped before its
frames were copied` (or `before its write started`), or for a burst still being written (the writer stops before
its next image and the buffers are freed at once) `NOT all saved (stopped at shutdown)` with the frames saved,
which its text file also gives.

The per-eye hook's post-latch check (`stereo: latched view ... equals the matrix written`, and its 10 s
summary `eye views L / R, latched ... (N differ ...)`) confirms each eye's projection reached the latch.

`tools/stereo/eye_diff.py <dir> [--disparity] [--diff-out <dir>]` compares captured pairs: pixels that
differ, maximum and mean difference, bounding box, and the horizontal shift that best matches the eyes
(negative: eye L's content lies right of eye R's, as it should for parallel eyes).

## Desktop window

Route S presents twice per tick to the game's window. A present can be made to wait by the desktop: with
the game's two swapchain images, a compositor that keeps the shown image until the next refresh lets only
one present per refresh through, and a desktop capture can do the same; the tick rate is then half the
refresh. The owner's first session (Quest 3 over Virtual Desktop, the window on the 120 Hz TV) ran at
exactly 59.5 ticks per second, 119 renders per second, the title screen too, although the swapchain was
created with the immediate present mode (`present mode 0 instead of the game's 2`). On the rig (the TV and
the virtual display, no Virtual Desktop) the same build was not capped, so the mechanism is Virtual
Desktop's (its desktop capture of the primary display is the likely one); the layer now makes the window
irrelevant to the game's pace in three steps (`src/vkcore/stereo_present.cpp`, `presenter_window.cpp`,
`presenter_mirror.cpp`, `src/stereo_seq/desktop_window.*`):

1. **More images.** The game's swapchain gets 4 images instead of 2 (`ETERNALVR_STEREO_SWAP_IMAGES`),
   still with the immediate present mode, so no acquire waits for a refresh to free an image.
2. **Only the presents the window shows reach it.** With `VK_KHR_swapchain_maintenance1` (the layer asks
   for `VK_KHR_surface_maintenance1` on the game's instance and the extension and its feature on the
   device; both optional, each retried without it on failure) a present the window does not need is not
   presented: its ring copy runs as usual, without signalling the present semaphore, and the image is
   handed back with `vkReleaseSwapchainImagesKHR` once that copy's timeline value has completed (the
   game's rendering and the copy are then done). Of a stereo pair only the mirrored eye is presented, and
   only when the last present to the window is at least two refreshes of its display old
   (`stereo_seq::WindowPresentGate`; the display from `MonitorFromWindow`, its rate from
   `EnumDisplaySettingsW`, which the XR worker reads every 3 s so that the query never runs on the game's
   present path or under the presenter's lock; 60 Hz when unknown). At most
   two images are held; beyond that, and whenever the multiplayer guard is not armed, every present goes
   out as before. This runs only on NVIDIA GPUs by default: on an AMD Radeon 890M the GPU stopped answering
   seconds into VR with it (Windows reset the GPU and the game froze) and ran without it, so on any other
   vendor every present reaches the window. `ETERNALVR_WINDOW_PRESENTS=all` turns it off everywhere,
   `ETERNALVR_WINDOW_PRESENTS=gated` turns it on for any GPU. A present that returns
   `VK_ERROR_OUT_OF_DATE_KHR` or `VK_ERROR_SURFACE_LOST_KHR` hands every held image back at once (after its
   copy, waiting up to 250 ms) instead of at the next present, since a game that recreates its swapchain or
   waits in `vkAcquireNextImageKHR` may not present again first, and the image gets a new present semaphore
   (`presenter_result.cpp`; logged as `present: the game's window present returned out of date ...`). An
   AMD Radeon 890M's driver returned out of date when the window changed size, where NVIDIA's returns
   `VK_SUBOPTIMAL_KHR`; the game then stopped presenting. `ETERNALVR_TEST_PRESENT_OUT_OF_DATE=<seconds>`
   makes one present return out of date on any GPU, to run that path.
3. **The window shows one eye.** `ETERNALVR_MIRROR=left` (default), `right` or `off`. With step 2 the window
   simply receives only that eye (black for `off`, cleared in the ring copy); without the extension the
   kept eye is copied over the other eye's image in the ring copy (two image copies per tick, about 0.05
   ms each at 2064x2100 on the RTX 4080).

**Menus in the window.** While a menu or popup is up over a head-tracked frame (the pause menu, the
Dossier, a tutorial popup; `docs/VR_MENUS.md`) the headset shows the GUI on the menu panel and the UI
layer's composite skip leaves the GUI out of the eye images (`presenter_frame.cpp`,
`ui_engine::setSkipComposite` while the UI quad shows). The eye image the window received was therefore the
world alone, and behind the single-player pause menu the engine clears the world to black
(`HACK_clearViewPreViewGuis`, `docs/rig-findings/ui-layer.md` section 4): the window was solid black for
the whole pause (owner's headset session and the rig's `mf3` run, 2026-09-27). Now, while the game's
cursor, the menu panel or its hold is up and the composite is skipped, the window shows the panel's image,
the game's GUI target: the present that carries it (mono or eye L) copies it into the mirror's private
image in its ring copy, while the GUI copy has it as a transfer source (a copy for an RGBA8 swapchain, a
blit that reorders the channels for BGRA8), and the present that reaches the window loads that image over
its own, cut to the `ETERNALVR_MIRROR_CROP` band like an eye (the menus are laid out in the centred 16:9
band, so a 16:9 window shows exactly the panel's content). Only presents whose image the window needs do
this: with step 2 and `left`, eye L when it reaches the window (one copy and one blit); with `right`, eye L
keeps and eye R loads; without the extension eye L keeps and both presents load. `off` stays black; the
title screen, main menu and loading screens (the cinema path, composite not skipped) are unchanged, as is
gameplay. The GUI is premultiplied: the window shows it over black, which is exactly what the pause menu
looks like (its backdrop is opaque); a popup over a live world shows its GUI without the world behind it
(a copy cannot blend). `stereo_seq::panelMirror` decides the steps (tested in
`tests/stereo_seq/desktop_window_tests.cpp`); `DesktopMirror::plan` and `keepPanel` record them. Log, once
per menu: `mirror: a menu is up: the window shows the panel's image (WxH band, ETERNALVR_MIRROR_CROP)` (or
`whole image`) and `mirror: the menu is down: the window shows the eye again`; the 10 s `mirror:` line
counts `N menu panel image(s) kept` (the loads are in `replaced`).

Rig measurements (OpenXR-Simulator at 90 Hz, e1m2 spawn, `-WindowSize`, 10 s windows):

| Run | Per eye | Before (main) | After |
|---|---|---|---|
| Virtual display (144 Hz) | 2064x2100 | 74 to 78 ticks/s (76), pose age 28.4 ms | 96 to 97 ticks/s, pose age 22.0 ms |
| TV (120 Hz, window at 0,0) | 1415x1440 | 116 ticks/s, pose age 19.4 ms | 129 ticks/s, pose age 17.5 ms (window 44 presents/s) |

The "before" rows ran with the owner's settings, i.e. with TAA on (Cvars, above); "after" holds TAA off,
which accounts for part of the gain. The window presents take 0.3 to 0.6 ms of CPU and the acquires none
(`window:` lines). The owner's own headset session is the check that the Virtual Desktop cap is gone:
`rates:` should show the tick rate above 90 with `display 120 Hz`.

## Ghost check

`tools/stereo/ghost_coeff.py <capture folder> [--pairs 12]` measures how much of one eye's image shows up in
the other at the same pixels: both images band-passed (difference of Gaussians, sigma 2 and 10 px), eye L
regressed on eye R and the reverse, and a control regressed on eye R shifted by 200 px. Without a ghost the
coefficients stay near the control. The eyes see different directions at the same pixel, but repeating
architecture (a balustrade, wall grooves) can line up at some head angles, so compare runs at the **same
static view**:

```
& tools\rig\launch-ht.ps1 -Layer <staged build> -Label ghost -DisplayWidth 3840 -DisplayHeight 2160 -Stereo `
    -WindowSize 1415x1440 -Map game/sp/e1m2_battle/e1m2_battle -XrRuntimeJson <simulator json> `
    -ExtraEnv 'ETERNALVR_HEAD_POSITION=0','ETERNALVR_TEST_HEAD_SWAY=0,0,1','ETERNALVR_CAPTURE_EYES=<dir>,45'
python tools\stereo\ghost_coeff.py <dir> --pairs 12
```

(e1m2's spawn, the head still, every 45th pair, the newest 12 pairs, about 40 s after the launch.) Result,
2026-09-26: the game's TAA on (`ETERNALVR_STEREO_RUNTIME_CVARS=0`) 0.205 / 0.176 with the control at 0.058;
TAA held off 0.050 / 0.042, control 0.054. With a 25-degree head sway (`ETERNALVR_TEST_HEAD_SWAY=25,6,3`)
TAA on measured 0.09 to 0.30 in every pair, TAA off near the control. Turning SSR, depth of field,
ray-traced reflections, light scattering and SSDO off (all through `ETERNALVR_DEBUG_CVARS`) left the ghost
unchanged; only TAA removed it. The stereo-taa branch's per-eye TAA build measured 0.07 to 0.08 with the
sway. Evidence: `<workspace>\tmp-hg\ghost-evidence.png` (eye L with TAA on, with TAA off,
their difference: the displaced copy, and eye R), captures in `tmp-hg\stOn-eyes`, `stOff-eyes`,
`dbread-eyes`, `fix1-eyes`.

## GPU timing

GPU timing measures the GPU with Vulkan timestamps, in any mode (flat, head-tracked, Route S), so
performance work has numbers on the rig, where PresentMon records nothing without elevation. Off by
default: the layer then hands out no extra hook and the present path returns at once.
`ETERNALVR_GPU_TIMING=sample` samples: only the submits of 3 frames in every 45 (about 4 times a second
under Route S) are bracketed, and the 10 s line is short. `ETERNALVR_GPU_TIMING=1` times every frame, with
the full summary and the CSV below.

How it measures (`src/vkcore/gpu_timing*.cpp`, pure logic in `src/gpu_timing/`):

- Every `vkQueueSubmit` batch of the game's device, on any queue whose family has timestamps
  (`timestampValidBits` > 0) and can reset queries (graphics or compute), gets two command buffers added
  around its own: the first resets its query and writes a timestamp at the top of the pipe, the last
  writes one at the bottom. Each batch takes a pair from a ring of 512 query pairs (one
  `VK_QUERY_TYPE_TIMESTAMP` pool of 1024 queries); the command buffers are recorded once per pair and
  reused. The first timed batch also resets the whole pool. Batches with a device group or protected
  submit in their chain, semaphore-only batches and batches past 160 in one frame are not timed (counted).
- A frame is every batch submitted on the device since the previous present, on every queue (async
  compute included); each present of the game closes one. Route S tags each closed frame with its eye
  from the eye tag the present hook takes (`presenter_copy.cpp`), so each eye's render is its own frame.
- A few presents later the frame's results are read with `vkGetQueryPoolResults` (64-bit, with
  availability, no wait). A frame whose results are not all available yet stays, and so do the ones after
  it, until the next present; one still not ready 60 presents later is counted lost. A pair comes back to
  the ring only once read, so the ring never overwrites a query the GPU may still write; when the next
  pair is still out the batch goes untimed (`ring full`). A reused pair whose new command buffers have
  not run yet still shows its last result, so a value equal to the one read at the pair's last use counts
  as not ready.
- Timestamps are compared at the narrowest valid bits of the frame's batches, relative to the frame's
  first batch, so a counter wrap within the frame is harmless; ticks become milliseconds by
  `timestampPeriod`. Queues of different families are assumed to share one time base (true on desktop
  GPUs; `compute and graphics` in the start-up line is the device's `timestampComputeAndGraphics`).

Per frame: **busy** (the union of the batches' intervals: the frame's GPU cost), **span** (first batch
start to last batch end, idle gaps included; the part of span not busy is the GPU waiting on the CPU or a
semaphore), busy per queue family (0 to 3), and the CPU present interval (present to present). The
presenter's own copy into the ring is not in any batch (it is submitted past the hook), nor is the
present itself.

Logs, every 10 s from the present hook (all lines start with `gpu:`):

- At start-up: `GPU timing requested (mode): timestamp period P ns, valid bits per queue family [...]`, then
  `GPU timing on` (or `off` and why), and `timing submits on queue family N` per family on first use.
- Sampled (`ETERNALVR_GPU_TIMING=sample`): `last 10.0 s, 3 of every 45 frames timed: eye L GPU busy A ms
  average, M ms longest (n N); eye R GPU busy ...; stereo tick GPU busy ...; L frame(s) lost` (`mono` for
  frames outside Route S; a stereo tick is an eye L frame plus the eye R frame after it). Three frames in a
  row are sampled so each run holds such a pair. A submit racing the present that ends a sampled frame goes
  untimed, so a frame can read a little low. The lines below are those of `ETERNALVR_GPU_TIMING=1`.
- `last 10.0 s: F frame(s) measured, T stereo tick(s); B batch(es) timed, untimed ... (ring full) / ...
  (device group or protected) / ... (no timestamps) / ... (frame cap); L frame(s) lost; U of 512 query
  pairs in use`.
- `mono frames` / `eye L frames` / `eye R frames`: `GPU busy`, `GPU span` and `CPU present interval`, each
  as `mean/p50/p95/p99/max ... ms (n N)` (nearest-rank percentiles).
- `stereo tick (eye L + eye R)`: GPU busy of an eye L frame plus the eye R frame right after it, and the
  CPU time of the two (present to present of eye L frames): the whole stereo frame.
- `pose age ...` (the presenter's pose age per shown XR frame, as in the frames CSV) and the mean GPU busy
  per frame by queue family (async compute shows as its own family; family 2 on the rig).

With `ETERNALVR_GPU_TIMING=1` and `ETERNALVR_LOG_DIR` set, `eternalvr-gpu-<pid>.csv` next to
`eternalvr-frames-<pid>.csv` has a row per measured frame: `frame,eye,cpu_ms,gpu_span_ms,gpu_busy_ms,
busy_f0_ms,busy_f1_ms,busy_f2_ms,busy_f3_ms,batches,untimed_batches` (`eye` is `mono`, `L` or `R`; `frame` counts presents, so eye L and eye R of a
tick are consecutive).

Cost of a timed frame: two extra command buffers and a copy of the submit info per batch, one mutex per
device, and one non-blocking query read per batch a few frames later. Sampled, a submit of any other
frame costs a shared lock and an atomic read before it goes on. Lost frames, a busy ring or a large share
of untimed batches in the 10 s line mean the numbers are incomplete.

## CPU timing

`ETERNALVR_CPU_TIMING=1` splits the CPU side of a tick: where the wall time of one stereo tick goes and
how busy the process's threads are. Off by default: no extra hook is handed out and every timer returns at
once. It combines with GPU timing (`ETERNALVR_GPU_TIMING=1`); the two hooks of `vkQueueSubmit` chain.

How it measures (`src/vkcore/cpu_timing.cpp`, pure logic in `src/gpu_timing/cpu_split.cpp`):

- A tick runs from the start of one frame-end job that is not eye R's own (`seq_hooks.cpp`'s wrapper) to
  the next: one game frame, which under Route S is eye L, then eye R.
- Stage timers read the performance counter and the calling thread's CPU time (`QueryThreadCycleTime`) at
  both ends: eye L's frame-end job, eye R's render (its views and its own frame-end job, run inside eye L's
  job), the drain for a new tag base, and every `vkQueueSubmit`, blocking `vkWaitForFences`,
  `vkWaitSemaphores[KHR]` and `vkQueuePresentKHR` call on the game's device (any thread). The time left in
  the tick outside the wrapped jobs is the game frame and eye L's views.
- The engine's job system runs the frame-end job and the presents on any of its workers, so the frontend
  thread's own CPU time per tick is only known when both of a tick's jobs ran on one thread (counted).
  The whole process's CPU time per tick (`QueryProcessCycleTime`) is always known.
- Every 10 s the frontend hands the finished window to a reporting thread (`EternalVR CPU timing`), which
  snapshots every thread's cycle count (Toolhelp) and logs; cycles become milliseconds at the time-stamp
  counter's rate, measured against the performance counter since start-up.

Logs (all lines start with `cpu:`):

- At start-up: `CPU timing on: one stage timer costs about N us`, and which Vulkan calls are timed.
- `last 10.0 s: T tick(s), K with both frame-end jobs on one thread; tick period ...; timer overhead about
  X ms per tick; the last summary took Y ms on its own thread`.
- `frontend per tick`: the frontend CPU (single-thread ticks only), the time outside the frame-end jobs,
  then eye L frame end, eye R render and drain, each as wall mean/p50 (p95), CPU mean and calls per tick.
- `Vulkan calls per tick (all threads)`: the same for the four Vulkan calls, then the whole process's CPU
  time per tick.
- `CPU per tick by thread`: the process's CPU per tick, how many cores that keeps busy, and the eight
  busiest threads with their share of one core (the game's workers have no thread descriptions).

Cost when on: about 0.01 ms per tick of timers (measured at start-up and logged), one map lookup per timed
Vulkan call, and a Toolhelp snapshot every 10 s off the game's threads.

## Stalls

Always on, any mode (`src/vkcore/stall_watch.*`, `vram_watch.*`; which gaps count in
`src/gpu_timing/present_stall.*`). The present hook stamps every present of the game's device. A present
more than 50 ms after the previous one is a stall. It counts as in play when the camera hook ran a game
frame at most 250 ms before the gap began and another during it; menus and loading screens run no game
frame, so their stalls (a map streaming in, the first frame after a load) are only counted. Without the
camera hook (cinema mode) every stall counts. Each of the first 30 stalls in play gets one line:

`stall: 1043.2 ms between game presents (in play, line 3 of 30); in the gap: our present hook 2.1 ms
(lock wait 0.0 ms, driver present 1.4 ms), Route S drain 0.0 ms; the game created 12 pipeline(s) in 4
call(s), 1021.7 ms (longest call 998.3 ms), and made 2 allocation(s) of 64.0 MB in 0.9 ms; VRAM 7012 of
7420 MB`

- **our present hook**: the layer's whole `vkQueuePresentKHR` path during the gap (the ring copy, the
  pairing, the logging); **lock wait** is the part spent waiting for the presenter's lock (the XR worker
  holds it while it rebuilds the ring), **driver present** the part in the driver's own present (the
  desktop display can make it wait, Desktop window above).
- **Route S drain**: the frontend held for a new tag base (Eye tags and pairing, above).
- **pipeline(s)** and **allocation(s)**: the game's `vkCreateGraphicsPipelines`/`vkCreateComputePipelines`
  and `vkAllocateMemory` calls on its device, with their wall time; a shader compile stutter shows as a
  long pipeline call.
- **VRAM**: the last reading of the process's local video memory against its budget (below).
- **the game saved a checkpoint in the gap** (only then, at the end of the line): the game began one of its
  own checkpoint saves during the gap. A save holds the game's thread for a few hundred milliseconds (it
  serialises the map), flat as well, after a fight or before a cutscene; the stall is the game's, not the
  layer's. The layer sees it through a mid hook in the game's `SaveCheckPointAndGetFiles` (on the lea of its
  `SaveCheckPointAndGetFiles` log string, found by that string; `src/vkcore/save_hook.cpp`), which only
  counts. At start-up: `stall: checkpoint save hook at RVA 0x...`; when the string or its one reference is
  missing, a line says so and stall lines just do not name saves.

When none of these accounts for the gap, the time went elsewhere: the game's own frame, a GPU that fell
behind, paging (VRAM near or over the budget), the driver or another process. After the 30th line: `stall: 30 stall lines logged; later stalls
are only counted in the 10 s stall summary`. Every 10 s with any stall or checkpoint save: `stall: last 10 s: N stall(s) in
play (longest X ms), M on loading screens or in menus; T in play in total, K logged`, ending with `; the
game saved a checkpoint` (or `S checkpoints`) when it saved any. At start-up: `stall:
game presents more than 50 ms apart get a line (the first 30)` and whether loading screens and menus are
told apart. Stalls before that line (the game starting up and loading its first map, before the XR worker
runs) have no game tick, so they count as loading screens.

Every 10 s the XR worker also logs `vram: the process uses U MB of local video memory, budget B MB (P%);
last 10 s: peak K MB, O of R reading(s) over the budget` (`IDXGIAdapter3::QueryVideoMemoryInfo` on the
runtime's adapter, which is the game's, read once a second by the worker; Windows counts the whole
process). Past the budget Windows pages memory out and back in, which stutters.

Cost: four performance-counter reads and a dozen atomic exchanges per present; a map lookup and two counter
reads per pipeline creation or allocation. The display rate query (Desktop window) and the VRAM reading run
on the XR worker between its frames.

## Live experiments (S1 to S5, stereo-routes.md section 2.10)

Staging (the game locks the DLL it loads):

```
robocopy build\windows-msvc\src\vkcore <workspace>\tmp-vr\seq1 EternalVR.dll EternalVR.pdb VK_LAYER_ETERNALVR.json openxr_loader.dll
$sim = '<workspace>\tools\bin\openxr-simulator\openxr_simulator_rig.json'
$common = @{ Layer = '<workspace>\tmp-vr\seq1'; DisplayWidth = 3840; DisplayHeight = 2160;
             XrRuntimeJson = $sim; ExtraEnv = @('ETERNALVR_HEAD_POSITION=0') }
```

Each run ends with `& tools\rig\stop.ps1 -Run latest`; logs are in `<workspace>\tmp-vr\seq1-logs`.

| # | Command | Pass |
|---|---|---|
| S1 | `& tools\rig\launch-ht.ps1 @common -Label s1 -StereoSameView -CaptureEyes '<workspace>\tmp-vr\s1-eyes,120' -WatchSeconds 120`, then two minutes of e1m1 including a fight; `<workspace>env\Scripts\python.exe tools\stereo\eye_diff.py <workspace>\tmp-vr\s1-eyes --diff-out <workspace>\tmp-vr\s1-diff` | no crash; `X per game frame` 2.00, B = R, tags in sync, pairs complete = stereo ticks; game speed normal; pair diffs zero or small local boxes (list which effects: particles, water, animated materials) |
| S2 | as S1 without `-StereoSameView`, `-Label s2`, captures to `s2-eyes`; `eye_diff.py ... --disparity` | every latch equals the matrix written; HUD and weapon visible in both PNGs; negative disparity of a few pixels; fusion in the simulator's preview (and later the headset) |
| S3 | S2 with `-ExtraArgs '+r_TAASafeMode 0'` (all temporal effects back), strafing; if it ghosts, again with safe mode 0 and one of `+r_SSDOTemporalAA 0`, `+r_lightScatteringTAA 0`, `+r_SSR 0`, `+r_waterReflectionsTAA 0` at a time | which effects ghost between the eyes; each one that does stays off (record in stereo-routes.md) |
| S4 | S2 twice, `-WindowSize 2064x2100` and `-WindowSize 1440x1470`, 60 s on the same route | the 10 s render and game frame rates per size against section 2.8 and T-075; decides the default size. Add `ETERNALVR_GPU_TIMING=1` for GPU ms per eye (section GPU timing) |
| S5 | S2 at the edge of an arena with dense occluders, looking along walls; then again after `$common.ExtraEnv += 'ETERNALVR_STEREO_DISCONTINUOUS=1'` | no pop-in at the outer eye edges, or the flag removes it (note the frame-rate cost) |

The shader census for Route M runs in the same session as a separate launch (stereo-routes.md section 4).

## Live results (2026-09-26, rig, OpenXR-Simulator, build 25216728)

Runs under `<workspace>\runs\20260926-02*` and `-03*`, layer logs in
`<workspace>\tmp-vr\seq<N>-logs`, eye captures in `tmp-vr\<label>` folders. e1m1 now spawns
in the Fortress with no weapon and no combat, so S2 to S5 used `-Map game/sp/e1m2_battle/e1m2_battle`.

| # | Result | Evidence |
|---|---|---|
| S1 | **Pass (stability, counters), with one finding.** Several minutes of e1m1 per run, no crash: 2.00 render frames per game frame in every 10 s window, backend frames = render frames, stereo ticks = eye R frame ends = game frames, pairs complete = started, 0 dropped halves, tags in sync with one drain at start, previous matrices rewritten every frame. Game speed normal (the game tick runs at the stereo rate: 72 Hz with FIFO, about 180 Hz with the immediate present mode at 2560x2100 per eye). Same-view pair diff: the **world is identical** in both eyes; what differs is every GUI element (HUD, crosshair, subtitles, the FPS counter, the intro's full-screen logo), which is drawn in eye L only, plus faint weapon-animation differences. The screen view's GUI list and its flags are the same in both chains (logged at the post-latch hook), so the GUI geometry is used up by the first render of the tick. Two bugs found and fixed: eye R's ring copy re-recorded the command buffer eye L's copy was pending on (eye L's half was lost), and the game re-applied FIFO (tick rate capped at 72). | runs `20260926-023425-s1`, `-024045-s1b`, `-024508-gui1`, `-024637-gui2`; `tmp-vr\s1b-eyes`: 2.0 to 2.5% of pixels differ, all at GUI elements (`s1b-diff`) |
| S2 | **Pass after two fixes.** Each eye latches exactly the projection written (0 of about 15000 latches differ). The captured pair shifts by -616 to -624 px at infinity, the offset the two asymmetric frusta predict (2 x 0.2425 x 1280 = 621 px), so near geometry adds only a few pixels of crossed disparity. The weapon was missing with any explicit projection, even one equal to the engine's own (centred matrix, fixed); it now shows in both eyes in each eye's frustum. HUD in eye L only (S1). One early S2 run exited silently about 20 s into gameplay (no dump, no event log entry, layer log clean); it did not recur in about 20 later runs, including one under procdump. | runs `20260926-024914-s2` (the exit), `-025122-s2b`, `-031014-gun-fix`, `-031239-gun-fix2`; `tmp-vr\s2b-eyes`, `gun-fix2`; `eye_diff.py --disparity --search 900` |
| S3 | **No cross-eye ghosting seen.** `+r_TAASafeMode 0` (temporal accumulation back, AA off) and then the game's own temporal settings (no v1 cvars except `rs_enable 0`) with a 25-degree, 2 s head sway: no double images in the captures. The game's AA mode in that run was not read back from memory, so whether TAA itself was on is not confirmed. | runs `-031651-s3a`, `-031826-s3b`, `-032010-s3c`; `tmp-vr\s3c-eyes` |
| S4 | **Each stereo tick costs exactly two mono renders** (table below). | runs `-032245-s4-st-2048`, `-032919-s4-mono-2048b`, `-033233-s4-st-1440`, `-033544-s4-mono-1440` |
| S5 | **No pop-in seen; the flag costs nothing.** With a 40-degree, 3 s head sway, the share of 32-pixel blocks that are dark in one eye and lit in the other (after the infinity shift) is 0.61% without and 0.62% with `discontinuousViewPosition`; tick rate 109 vs 110 Hz. Not tried at an arena edge with dense occluders. | runs `-034225-s5-base`, `-033903-s5-disc` |

Re-test after the review fixes, with motion controllers merged (integration branch, 2026-09-26, runs
`20260926-042607-int-s2`, `-043831-int-sc`, `-044356-int-guard`; logs in `tmp-vr\int-*-logs`):

| Check | Result |
|---|---|
| Stack | `eye L's frame end runs with 8189 KiB of a 8192 KiB stack left`; every 10 s: eye R chain at most 51 KiB deep, least stack left 8188 to 8189 KiB, `eye R skipped 0 / 0 / 0 (stack)` in every window |
| Drain | one drain at start (`render thread idle at backend frame 1897 after 110 ms (quiet)`), none afterwards; 0 out of sync |
| Previous matrices | 2 rewritten per stereo tick, 0 kept in steady play (7 kept in the first window, at the map start) |
| S2 with capture, 4 min 10 s of play (walking, strafing, turning, firing through scripted controller input) | no exit, no dump (procdump attached): 26 windows, 2.00 renders per tick in every steady one, pairs complete = started (28862), 0 mono, 0 dropped halves; 128 captured pairs, infinity shift -502 to -548 px (predicted 2 x 0.2425 x 1032 = 500 px) |
| Rate at 2064x2100 per eye | 99 to 133 stereo ticks per second, mean 115 (218 to 250 renders per second) |
| Stereo with controllers (hand aim, snap turn) | fire, jump, walk, strafe, snap turn (4 x 45 degrees each way), hand aim (view yaw and pitch follow `right.aim`), shots from the hand along the hand ray (error 0.00 degrees), weapon drawn in both eyes with the same pose and a crossed disparity of about 120 px beyond the infinity shift (about 0.4 m away); counters as above (mean 111 ticks per second) |
| Guard trip (`ETERNALVR_GUARD_TEST_TRIP_MS=40000`) | at the trip: key injection off (0 held keys), the tags drop out of step (`mono until the next base`), the headset shows the cinema screen; the game keeps running flat (213 fps), a held trigger fires nothing, no crash |

The silent exit did not recur. The job thread that runs eye L's frame end has an 8 MiB stack on this
build, so the stack check never skips; it stays as the guard for a thread with a smaller stack.

S4 (RTX 4080, e1m2 spawn, immediate present mode, per-eye image = window, 10 s windows):

| Per-eye size | Mono (fps) | Stereo (ticks/s, renders/s) | Pose age mono / stereo |
|---|---|---|---|
| 2048x2100 (4.3 MP) | 222 | 109, 218 | 13.2 / 18.6 ms |
| 1440x1470 (2.1 MP) | 299 | 147, 295 | 9.7 / 13.8 ms |

The layer has no GPU timestamps yet; the numbers are whole-frame rates. At the Quest 3's 2064x2208 per
eye, 90 Hz is within reach and 120 Hz is not.

Rig note: `run.ps1` sets the game's environment variables in the calling PowerShell process, so two
launches from one PowerShell call share them (a "mono" run inherited `ETERNALVR_MODE=stereo`); launch
each run from its own call.

## Known gaps (v1)

- **TAA is held off** (Cvars): the ghost is gone, but surfaces the game dithers for TAA to resolve look
  stippled (the owner saw it). Per-eye TAA (the stereo-taa branch) brings TAA back; it owns the TAA cvars
  through `runtime_cvars::setStereoTemporal(PerEye)` once it is active.
- **Render size follows the window** (T-031): the launcher sized the window to fit the 1440-pixel TV, so the
  owner's eyes rendered at 1415x1415 while the headset recommends 2496x2688 (Virtual Desktop showed "render
  resolution 57%"). The launcher also puts the window on the primary display; the rig's virtual display
  would keep it off the owner's screen.

- **HUD and 2D overlays** (S1: eye L only). Solved by the UI layer, on by default in Route S
  (`docs/rig-findings/ui-layer.md`): the game's GUI target is captured each tick, shown on a head-locked
  quad (`ETERNALVR_UI_DISTANCE`, `_WIDTH`, `_OFFSET_Y`) and kept out of both eyes; under hand aim the game's
  crosshair is left out and a dot is drawn on the weapon hand's ray. `ETERNALVR_UI_LAYER=0` goes back to
  the HUD in eye L only.
- Temporal effects other than TAA, DLSS, auto exposure and the light scattering (SSDO, depth of field,
  water, refraction, the anti-ghosting mask) still share their history between the eyes and stay off; the
  headset check of per-eye TAA is open (`docs/rig-findings/stereo-temporal.md` section 8).
- The eye tags rely on one backend frame per tagged frame. A frame the engine kicks but never renders,
  with no present in its place, would shift the pairing by one frame without a counter disagreement. Not
  seen on the rig (0 desyncs in every run), and stereo resuming after 30 mono frames takes a new base
  (such a frame also makes the next drain time out, which drops the frame count as a reference).
- If the tags fall out of step between eye L's per-eye hook and its frame end (a present disagreeing in
  that window) and the drain then fails, that one frame, drawn with eye L's projection, is shown mono.
- The camera-cut counter (idRenderView + 0x29944, decremented by the previous-matrix store) counts down
  twice per tick.
- Per-eye render size follows the window (T-031). Latency grows by one eye render (pose age +5 ms at
  2048x2100).
- One unexplained silent exit (S2, first run, about 20 s into gameplay, capture on). It did not recur in
  the re-test (4 min of play with capture). The job thread measured there has an 8 MiB stack and eye R's
  chain goes 51 KiB deep, so a stack overflow on that thread is unlikely; the cause stays unknown and the
  stack check stays in place.

## EngineNativeStereo (earlier work, not available on build 25216728)

The engine's two-view path renders one view per frame in this build; a second view crashes the renderer.
Findings, addresses and run evidence: `docs/rig-findings/stereo-reentry.md` section 12. What it proved and
Route S reuses: the latch honours an explicit asymmetric projection (E2: 0 of 2870 latches differed), the
per-eye hook point and its registers, and the per-eye math (`src/xr_math/stereo_view.*`, tested in
`tests/xr_math/stereo_view_tests.cpp`, including a check that a point lands at the same place through the
engine's view and through the OpenXR eye pose and FOV).

Why it stops: with two views the renderer needs per-view state for view 1, and this build has one of each
(`r_maxRenderViews` is an INIT cvar the command line cannot raise; the static per-view block at 0x66EF4F0
holds one 0xAF8 entry; the device context keeps one inline view slot; the render thread's job setup
0x1CDCD90 copies render-list entries into a one-element local array).

The experiment harness stays behind `ETERNALVR_STEREO_EXPERIMENT` (with `ETERNALVR_MODE=stereo`):
`left-eye` (E2/E3: the one view rendered as the left eye with its asymmetric projection) and `two-views`
(E4: side-by-side screen views sharing view slot 0; crashes this build). `ETERNALVR_STEREO_EYE_POSES=0`
renders both views from the head centre (E4 control), `ETERNALVR_STEREO_JITTER_COPY` gives the second
view the first's TAA jitter, `ETERNALVR_TEST_WEAPON_FOV` forces `weaponFOVX/Y` (E3). Launch with
`launch-ht.ps1 ... -StereoExperiment left-eye` or `two-views` (which widens the automatic window to 3840
and installs its hooks from `vkCreateDevice`, before the first map loads).

Verified on 2026-09-26 with OpenXR-Simulator: E2 as above; eye poses left (-0.032, 0, 0) and right
(0.032, 0, 0) in head space, FOVs 54/40/44/54 degrees mirrored; without an experiment the mode then ran
head-tracked mono cleanly (run `20260926-013809-final-mono`).
