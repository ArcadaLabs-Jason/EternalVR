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
blur, depth of field, chromatic aberration, vignette, view bob, the view kicks and shakes, the damage tint and
blur, the view effects' screen overlays, the weapon's FOV scale and the Meathook's single view turn. The game's
own settings override the command line here as well (a rig run with them on the command line still wrote
`pm_noBob 0 -> 1`, `view_damageBlur 1 -> 0` and six more), so in stereo the launcher no longer puts them on the
command line: the layer's hold sets them, from Route S's first present (`presenter_copy.cpp`, before the
runtime's session and the first map), and a multiplayer guard trip gives the player's own values back
(`cvar_book.hpp`). Only `r_hdrDisplay 0` stays on the command line, since the game picks the swapchain's format at
start-up (a trip leaves it, as it leaves the window set). In mono the layer holds none of these, so mono launches
keep them on the command line (`launcher/data/forced-cvars.txt`, `| mono`). The launcher's session restore puts
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
fails closed)`, since per-eye history writes it itself while it runs).

## Per-eye temporal history (default; `ETERNALVR_STEREO_TAA=0` turns it off)

TAA and DLSS per eye, written up in `docs/rig-findings/stereo-temporal.md`: eye R gets its own TAA
accumulation images (built by the engine's own slot builder when the renderer starts, from a hook
installed at `vkCreateInstance`), the two accumulation selectors pick each frame's eye's pair by its eye
tag, both eyes of a tick share one jitter phase, both are reset together when eye R missed a tick (TAA
by the view's reset flag, DLSS by `Reset` on both eyes' evaluations), eye R evaluates a twin DLSS feature
(each evaluation's eye found by its output image), and the temporal effects whose history is still shared
(anti-ghosting mask, SSDO, depth of field, water, refraction, ray-traced reflection upscale, dynamic
resolution) are switched off (on the command line; the layer writes any that
differ through the engine's cvar setter), and eye R's slot is rebuilt by the slot builder when the engine
resizes its own (resizing eye R's targets one by one hung a 12 GB card, `stereo-temporal.md` 5.1). A missing
piece fails closed: the first stereo tick writes `r_antialiasing 0` and `r_TAASafeMode 1`. The launcher and
`launch-ht.ps1 -Stereo` put the cvars on the command line (`-StereoV1`: the v1 set and
`ETERNALVR_STEREO_TAA=0`; `-StereoDlss`: `ETERNALVR_STEREO_DLSS=1`). Live on the rig (2026-09-26): ghost
coefficient 0.029 / 0.029 against a control of 0.014, the same as v1 and against 0.21 for the game's shared
TAA; DLSS per eye 0.030 / 0.030; TAA costs about 0.09 ms per eye at 1280x740
(`docs/rig-findings/stereo-temporal.md` section 7).

Auto exposure and the light scattering are kept per eye by hooks of their own, which need the eye tags
only. They run whenever Route S runs: with per-eye history on, off (`ETERNALVR_STEREO_TAA=0`, the
launcher's Anti-aliasing Off, `-StereoV1`) or failed closed. The start-up line `seq-exposure: per-eye TAA
...; auto-exposure index per eye hooked, eye R takes eye L's exposure; scattering history per eye hooked`
names the mode.

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
`ETERNALVR_STEREO_DLSS=1` (DLSS), when `ETERNALVR_STEREO_EXPERIMENT` is set (the experiment then runs) or the
UI layer is off (`ETERNALVR_UI_LAYER=0`: its image hooks give each eye its view's picture). Unset or any value
other than `1`, none of it runs and nothing is logged (`src/vkcore/parallel_eyes_settings.*`, unit-tested).

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
- **Async compute off**: the device setup's one read of `r_enableAsyncCompute` is made 0 (`view_async.cpp`); the
  cvar itself and the player's config are not changed. With async compute on, both views record into the
  engine's one set of async compute contexts: a driver crash in the shell world, a hang a few seconds into a
  map (rig runs pa1, rsa1, rsa2; the headset's black screen). Should the game's device still get a compute-only
  queue, view 1 is not rendered (one line) and both eyes show view 0's image.
- View 1's clones of the screen-sized targets (`view_clones.*`, `view_clone_binds.cpp`) and its own passes where
  the engine has one for the frame: screen pass, environment, light scattering's wait for its volumes, its
  shadow atlas work left out (it shades with view 0's atlas), its light and decal tile list pool
  (`view_one_passes.cpp`).
- The eye copy: eye 0 is the presented image (view 0's screen pass), eye 1 view 1's screen pass output, in a
  two-eye ring (`presenter_eyes.*`). Eye 1 takes it only when the frame rendered view 1 (the last three frames
  sent did, and since view 1's clones were last made, `view_frames.hpp`); on frames with view 0 alone (loading
  screens, the async compute safety net) and for three frames after new clones (a resize without a loading
  screen) eye 1 takes the presented image too, never an older frame's or an unwritten one (`eye-copy: eye 1
  takes the presented image: view 1 was not rendered for this frame`).
- Its cvars, held as Route S holds its own, from the first present on every present (menus and loading
  screens included): `r_useNewDepthDownscale 0` (with the new downsample view 1 drew tile-shaped black holes
  in e1m3), the launcher's anti-aliasing (`r_antialiasing 1`, or with Off `r_TAASafeMode 1` and
  `r_antialiasing 0`; `ETERNALVR_STEREO_RUNTIME_CVARS=0` leaves it as the game has it), Route S's window and
  present set (`r_fullscreen 0`, `r_swapInterval 0`, the launch size, left to the game once the render size is
  off) and its comfort set; the launcher's sharpening, CPU Saver and button prompts as under Route S
  (`runtime_cvars.hpp`).
- Route S's own modules stay off: per-eye TAA, the scattering and exposure hooks, alternate eyes, its present
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
| `ETERNALVR_TEST_EYE_COPY=0`, `1` | `0`: both eyes the presented image; `1`: each view's image before the screen pass (gamma-darker, from the engine's post-process final target, RVA 0x66E3208) |
| `ETERNALVR_TEST_VIEW_OFF=<parts>` | leaves fixes out, comma-separated: `edges` (binning edges), `dc` (view 1's device context copy), `binds` (view 1 binds its clones), `pool` (tile list pool), `shadows` (view 1 renders its own shadow atlas work), `env`, `volumes`, `screen` |
| `ETERNALVR_TEST_VIEW_ONLY=0`, `1` | renders that view alone |
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
frame(s); view 0 alone: ...` with the redirect and binning counts, every minute the clones' counts. After a
multiplayer guard trip: `parallel eyes: the multiplayer guard has tripped: view 0 alone from now on`, then
`parallel eyes: after the multiplayer guard trip no frame holds view 1 any more; ...`. Off: no
line at all without the variable, else `parallel eyes: off: <why>; the standard renderer` or `parallel eyes:
not available for this game version`. A hook that failed after the engine was changed: `parallel eyes: FAILED
after the engine was changed (above): not on; both eyes show the same image (view 0's) for this session, ...`.

**Known limits.**

- Steam build 25216728 only. Anti-aliasing TAA or Off: the layer holds `r_antialiasing 1`, or with
  `ETERNALVR_STEREO_TAA=0` (the launcher's Off) `r_antialiasing 0` and `r_TAASafeMode 1`. Not with DLSS: with
  `ETERNALVR_STEREO_DLSS=1` it stays off (one line) and Route S runs DLSS; the launcher greys the option out
  with DLSS and does not set the variable.
- Rig-checked on the simulator (deaths, a level change, both eyes matching Route S by numbers in e1m2 and e1m3);
  not yet the main menu to campaign flow, pause and Dossier menus, cutscenes or long sessions; the headset only
  in early tries (the black screen there was async compute).
- The 8th change of the engine's targets (resolution or render scale changes; the first set of clones counts as
  one of 8 builds) turns the clones off for the process: view 0 alone from then on, both eyes its image
  (`view-clones: ... clones off, view 0 alone from now on`); a restart brings view 1 back. Up to 64 remakes
  after map loads are fine.
- The in-headset bug capture records the presented image only (eye 0).
- The two views' pictures can still differ in small ways (faint motion-vector blocks low in view 1 were seen).
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
  still ends it in time), at most 50 ms, and counts as a timeout. Once the headset has begun no frame for three
  periods (not shown, the dashboard, a lost session, shutdown) nothing waits until it does again: the game
  never hangs on the headset.
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
  measured lead under `ETERNALVR_POSE_LEAD` (`xr_math/display_lead.hpp`). Paced, every game frame is shown the
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
| `ETERNALVR_CAPTURE_EYES` | unset | `<dir>[,<N>]`: every Nth pair (default 60) written as `eyes-<pid>-p<pair>-t<tick>-L.png` / `-R.png` |
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
| `ETERNALVR_STEREO_SSDO` | unset (= 1) | 1 or unset: `r_SSDO 1` held at run time under Route S (the launcher passes the player's own value from their config, 1 unless it sets 0). The game turns SSDO off itself after `r_TAASafeMode 1` (0x1C6FCC0), which Route S holds at start-up, so before this every Route S session ran without it and each eye's outer strip read 15 to 20% too bright against a mono reference in shadowed corners. SSDO's own temporal filter stays off (`r_SSDOTemporalAA 0`), so neither eye reads the other's history (sway ghost check: no cross-eye ghost; about +0.5 ms GPU per stereo pair on an RTX 3080 Ti at 2048x2208). 0: not held, the game's knock-on stays (one `cvars: r_SSDO is left as the game has it` line) |
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
| `ETERNALVR_STEREO_TAA` | 1 | per-eye temporal history (TAA and DLSS; section "Per-eye temporal history"); 0: v1, no temporal accumulation (auto exposure and the light scattering stay per eye) |
| `ETERNALVR_STEREO_DLSS` | 0 | 1: DLSS per eye instead of TAA (`r_antialiasing 2`) |
| `ETERNALVR_STEREO_DLSS_QUALITY` | unset | with DLSS per eye: `quality`, `balanced`, `performance`, `ultra_performance` (or `3` to `0`), the `r_dlssQuality` held while DLSS runs; `dlaa`: DLSS at the full render size (render size = output size). The game maps `r_dlssQuality` 0 to 3 only (RVA 0x1CC5D40, 0x1CC5760; any other value is Balanced), so DLAA holds `r_dlssQuality` 3 and the layer sets NGX's `PerfQualityValue` to DLAA (5) on every write of it (`src/vkcore/dlss_dll.cpp`): the game's optimal render size, its feature and eye R's twin all become DLAA. Only with `ETERNALVR_DLSS_DLL` of DLSS 3.1 or later; otherwise DLSS runs at Quality and `dlss:` says why. Unset: the player's own quality |
| `ETERNALVR_DLSS_DLL`, `_PRESET`, `_ROUTE` | unset | a newer `nvngx_dlss.dll` of the player's own (DLSS 310: the transformer model) and its render preset, for both eyes' features; loaded from its own folder, never copied into the game's (`docs/rig-findings/dlss-dll.md`) |
| `ETERNALVR_STEREO_OBJECT_PREV` | 1 | eye R's moving objects take their previous frame from eye R's own render of the tick before (`docs/rig-findings/stereo-object-motion.md`); 0: from eye L's render of the same frame (no motion, smeared by TAA) |
| `ETERNALVR_STEREO_VIS_GATE` | 1 | models one eye sees are drawn: the first-visible gate takes either eye's last render for models; particles, flares, beams and ribbons keep the engine's test (`docs/rig-findings/stereo-visibility-counter.md`); 0: the engine's own test, so a pickup in one eye's outer strip only is not drawn |
| `ETERNALVR_ALTERNATE_EYES` | 0 | 1: one eye per game tick, eye L then eye R; auto: only while the ticks fall behind the headset (section "Alternate eyes"; `docs/rig-findings/alternate-eye.md`) |
| `ETERNALVR_FOVEATION` | unset | `subtle`, `balanced`, `aggressive` or `maximum` (the launcher's Foveated rendering, experimental): fixed foveated rendering through `VK_NV_shading_rate_image`, full rate within the region of 30, 24, 18 or 12 degrees around head-forward in each eye, half rate within the region of 16 degrees more (12 for `maximum`, so quarter rate from the region of 24 degrees), quarter rate outside (`src/vkcore/vrs_nv.cpp`). A region has the area of the cone of its angle on the eye's tangent plane and reaches the same fraction of the way from head-forward to every edge of the eye's image, so the reduced-rate band takes the same share of the way to the edge on the nasal side and the top as on the temporal side and the bottom (`src/features/foveation/foveation_region.cpp`); in a symmetric square FOV it is the cone's circle. Each render pass gets the pattern of the eye of the backend frame it is recorded for, and only where that frame is sure: the backend counter at the pass equals the counter the render-view job read for its render. Every other pass stays full rate. What its command buffer's recording since its `vkBeginCommandBuffer` or its parity (once two recordings in a row agreed two frames apart) would guess is counted, with the recordings a later agreement contradicts and the parity breaks, but not used: those guesses can name the other eye's frame (`src/stereo_seq/pass_frames.hpp`, `src/vkcore/vrs_command_buffers.cpp`; `vrs: render pass frames: ...` every 200,000 passes; on the rig every pass in the map agreed). A pass's eye so comes only from a frame its two counters agree on; with `ETERNALVR_TEST_VRS_PARITY=1` (a rig test) the guesses are used too. NVIDIA RTX only: other cards log it as unsupported and render normally. Off with Parallel Eye Rendering (one line; "Parallel Eye Rendering", Known limits). Only render targets in the eye's space are foveated: the eye image and any copy of it scaled by one factor down to 1/8 (DLSS's smaller render size, the half size buffers; `src/features/foveation/eye_targets.cpp`); shadow maps and other targets stay full rate. Mono frames (the cinema screen, menus) stay full rate. The game's menus and HUD (render passes into its GUI target, found by the UI layer) stay at full rate too (`src/vkcore/vrs_gui.cpp`); without the UI layer they are foveated like the eye image |
| `ETERNALVR_TEST_DLSS_TWIN_FAIL` | unset | test knob: a count from 1 to 100; eye R's first tries at its own DLSS feature fail without a create (result 0xBAD00000), to exercise the fallback to TAA, the tries after it and the menu's try (`docs/rig-findings/stereo-temporal.md`, Fail closed) |
| `ETERNALVR_TEST_CPU_LOAD_MS` | unset | test knob: `<ms>[,<s on>,<s off>]` busy-waits at every render's frame end, as a slower processor (`alternate-eye.md` 10.4) |
| `ETERNALVR_TEST_PRESENT_OUT_OF_DATE` | unset | test knob: `<seconds>`: the first game present that reaches the driver that long after the first one returns `VK_ERROR_OUT_OF_DATE_KHR` to the game (the driver took it), once, so the game's swapchain recreate and the layer's hand-back of held images (Desktop window) run on any GPU; VR on only |
| `ETERNALVR_STEREO_SCATTER_TAA` | 1 | the light scattering's temporal filter per eye (`docs/rig-findings/stereo-scatter.md`); 0: the filter held off in stereo |
| `ETERNALVR_STEREO_EXPERIMENT` | unset | `left-eye` or `two-views`: the EngineNativeStereo experiments instead of Route S (Parallel Eye Rendering stays off with either) |
| `ETERNALVR_PARALLEL_EYES` | unset | `1` in stereo: Parallel Eye Rendering, both eyes as two views of one frame (section "Parallel Eye Rendering"), on Steam build 25216728 only; any other build, DLSS or the UI layer off keep Route S, and with a stereo experiment the experiment runs (logged). Async compute off, view 1's clones and the eye copy come with it. The launcher's "Parallel Eye Rendering (experimental)", off by default |
| `ETERNALVR_STEREO_EYE_POSES`, `_JITTER_COPY`, `ETERNALVR_TEST_WEAPON_FOV` | | experiments only (below) |
| `ETERNALVR_GPU_TIMING` | 0 | 1: GPU timestamps around every submit batch, summarized per eye (below); any mode |

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

`ETERNALVR_GPU_TIMING=1` measures the GPU with Vulkan timestamps, in any mode (flat, head-tracked, Route
S), so performance work has numbers on the rig, where PresentMon records nothing without elevation. Off
by default: the layer then hands out no extra hook and the present path returns at once.

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

- At start-up: `GPU timing requested: timestamp period P ns, valid bits per queue family [...]`, then
  `GPU timing on` (or `off` and why), and `timing submits on queue family N` per family on first use.
- `last 10.0 s: F frame(s) measured, T stereo tick(s); B batch(es) timed, untimed ... (ring full) / ...
  (device group or protected) / ... (no timestamps) / ... (frame cap); L frame(s) lost; U of 512 query
  pairs in use`.
- `mono frames` / `eye L frames` / `eye R frames`: `GPU busy`, `GPU span` and `CPU present interval`, each
  as `mean/p50/p95/p99/max ... ms (n N)` (nearest-rank percentiles).
- `stereo tick (eye L + eye R)`: GPU busy of an eye L frame plus the eye R frame right after it, and the
  CPU time of the two (present to present of eye L frames): the whole stereo frame.
- `pose age ...` (the presenter's pose age per shown XR frame, as in the frames CSV) and the mean GPU busy
  per frame by queue family (async compute shows as its own family; family 2 on the rig).

With `ETERNALVR_LOG_DIR` set, `eternalvr-gpu-<pid>.csv` next to `eternalvr-frames-<pid>.csv` has a row per
measured frame: `frame,eye,cpu_ms,gpu_span_ms,gpu_busy_ms,busy_f0_ms,busy_f1_ms,busy_f2_ms,busy_f3_ms,
batches,untimed_batches` (`eye` is `mono`, `L` or `R`; `frame` counts presents, so eye L and eye R of a
tick are consecutive).

Cost when on: two extra command buffers and a copy of the submit info per batch, one mutex per device,
and one non-blocking query read per batch a few frames later. Lost frames, a busy ring or a large share
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
