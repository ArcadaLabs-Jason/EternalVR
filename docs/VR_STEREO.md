# Stereo

`ETERNALVR_MODE=stereo` runs **Route S, synchronized sequential stereo** (D-032, D-037, T-066, T-069):
each game tick is rendered twice, once per eye, from the same tick, and the two images go to the headset
as one projection layer. The design and every address are in `docs/rig-findings/stereo-routes.md`
section 2 (Steam build 25216728). The engine's own two-view path (EngineNativeStereo) is not available on
this build; its findings and the experiment harness that remains are at the end of this document.

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
   (exposure adapts once per tick, in eye L's frame), `inhibitModelFovScale` (+0x13) and, for S5,
   `discontinuousViewPosition` (+0xC). After each eye's latch (post-latch hook, RVA 0x1C75772) two sets
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
   quiet period); at most one drain per second. A disagreement (a tagged
   frame that never presented, a present no tag was queued for, more than 8 queued) drops the tags out of
   step: presents are then shown mono until the next base. The pairing (`src/stereo_seq/eye_pairing.*`)
   copies eye L into the left half of a free ring slot, holds the slot, copies eye R of the same tick into
   the right half and hands the slot to the XR worker; a half whose partner is missing is dropped (never
   shown alone), mono frames go into both halves.
5. **Projection layer.** The ring and the XR swapchain are two eye images wide. A pair is submitted with
   each eye's half, pose and FOV; a mono frame (menus, loading, a mono tick) with the head pose and the
   game's FOV in both halves; the cinema quad shows the left half.

Both presents also reach the game's window, and the window is only a mirror: the runtime paces the game.
How the layer keeps the desktop display from pacing it is in Desktop window, below. The ring keeps one
command buffer per slot half: eye R's copy into a slot is recorded while eye L's may still be pending. A pair
whose game frame record is gone is not shown (the headset repeats the last pair).

## Stack

Eye R's whole chain runs nested inside eye L's frame-end job on the same job thread's stack (the engine
runs the jobs of a job list it waits on inline; the S2 log shows eye L's per-eye hook, the frame end and
eye R's per-eye hook on one thread), and each job of the chain keeps a job list of about 14 KiB on the
stack (`sub rsp, 0x38C0` in the frame-end job, 0x3870 in render one and the render-frame job). The
engine never nests two chains, and many of its threads are created with 256 KiB stacks. A stack overflow
ends the process through the game's own crash handler (`SetUnhandledExceptionFilter`,
`TerminateProcess`) without a dump or an event log entry, which is what the one silent exit of S2 looked
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
forwards. The engine two-view experiment hooks check the guard the same way.

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
else is forced (depth of field, motion blur, SSR and the rest stay as the player set them); the present
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
(while the layer answers the client area) or the first swapchain's.

`ETERNALVR_DEBUG_CVARS="name=value;name=value"` writes more cvars the same way for rig experiments;
`name=?` only logs the value the game runs with. Writes from it may be saved by the game (a written
`r_SSR 0` landed in `DOOMEternalConfig.local`), so rig runs restore the configs afterwards as usual.

Two static findings (`docs/rig-findings/stereo-temporal.md`): `+r_jitter 0` has no effect, since the engine
sets `r_jitter` every backend frame to 1 exactly when `r_TAASafeMode` is 0 and an AA mode is on (reading it
back tells whether TAA or DLSS runs); and with eye R skipping its exposure update, eye L's auto exposure
reads a "previous" image nothing writes in steady stereo, so exposure may stop adapting (live test T6).

## Per-eye temporal history (default; `ETERNALVR_STEREO_TAA=0` turns it off)

TAA and DLSS per eye, written up in `docs/rig-findings/stereo-temporal.md`: eye R gets its own TAA
accumulation images (built by the engine's own slot builder when the renderer starts, from a hook
installed at `vkCreateInstance`), the two accumulation selectors pick each frame's eye's pair by its eye
tag, both eyes of a tick share one jitter phase, both are reset together when eye R missed a tick, auto
exposure alternates per eye, eye R evaluates a twin DLSS feature, and the temporal effects whose history is
still shared (anti-ghosting mask, SSDO, depth of field, water, refraction, ray-traced
reflection upscale, dynamic resolution) are switched off (on the command line; the layer writes any that
differ through the engine's cvar setter), and eye R's slot is rebuilt by the slot builder when the engine
resizes its own (resizing eye R's targets one by one hung a 12 GB card, `stereo-temporal.md` 5.1). A missing
piece fails closed: the first stereo tick writes `r_antialiasing 0` and `r_TAASafeMode 1`. The launcher and
`launch-ht.ps1 -Stereo` put the cvars on the command line (`-StereoV1`: the v1 set and
`ETERNALVR_STEREO_TAA=0`; `-StereoDlss`: `ETERNALVR_STEREO_DLSS=1`). Live on the rig (2026-09-26): ghost
coefficient 0.029 / 0.029 against a control of 0.014, the same as v1 and against 0.21 for the game's shared
TAA; DLSS per eye 0.030 / 0.030; TAA costs about 0.09 ms per eye at 1280x740
(`docs/rig-findings/stereo-temporal.md` section 7).

The light scattering's history is per eye as well (default; `ETERNALVR_STEREO_SCATTER_TAA=0` turns it off), and its filter
(`r_lightScatteringTAA`) stays on. Eye R gets four volume images of its own, which follow the engine's
resizes, and each eye's pairs and filter state go into the device context before its render
(`docs/rig-findings/stereo-scatter.md`). Without the filter the volume's coarse cells crawl as the head
moves: the "walking texture" in fog and god rays.

Moving and animated objects get their previous frame per eye too (default; `ETERNALVR_STEREO_OBJECT_PREV=0`
turns it off). The engine keeps each render's joint offsets and model matrices in a ring of three buffers and
reads the previous frame's from the render before. For eye R that is eye L's render of the same frame, so
moving objects had no motion there and TAA smeared them. Eye R now reads its own render of the tick before
(`docs/rig-findings/stereo-object-motion.md`).

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
| `ETERNALVR_WINDOW_PRESENTS` | gated | `all`: every present reaches the game's window (no image is handed back) |
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
| `ETERNALVR_STEREO_RUNTIME_CVARS` | 1 | 0: do not hold `r_TAASafeMode 1` / `r_antialiasing 0` at run time |
| `ETERNALVR_DEBUG_CVARS` | unset | `name=value;...` written at run time; `name=?` only logs (rig experiments) |
| `ETERNALVR_PRESENT_IMMEDIATE` | 0 | 1: immediate present mode in any mode (mono frame-rate references) |
| `ETERNALVR_STEREO_FULL_RES` | 1 | `forceFullResolution` on both eyes |
| `ETERNALVR_STEREO_EXPOSURE_ONCE` | 1 | `skipAutoExposureUpdate` on eye R |
| `ETERNALVR_STEREO_DISCONTINUOUS` | 0 | 1: `discontinuousViewPosition` on both eyes (S5) |
| `ETERNALVR_STEREO_BIN_TILES` | 1 | the light and decal binning's tile grid set from each view's own projection (docs/rig-findings/stereo-bin-tiles.md); 0: the engine's symmetric grid, whose lit areas end in tile-shaped steps in the headset |
| `ETERNALVR_STEREO_INHIBIT_MODEL_FOV` | 1 | `inhibitModelFovScale` on each eye's view, and the hands-and-guns matrices in the eye's frustum |
| `ETERNALVR_STEREO_TAA` | 1 | per-eye temporal history (TAA, DLSS, exposure; section "Per-eye temporal history"); 0: v1, no temporal accumulation |
| `ETERNALVR_STEREO_DLSS` | 0 | 1: DLSS per eye instead of TAA (`r_antialiasing 2`) |
| `ETERNALVR_STEREO_OBJECT_PREV` | 1 | eye R's moving objects take their previous frame from eye R's own render of the tick before (`docs/rig-findings/stereo-object-motion.md`); 0: from eye L's render of the same frame (no motion, smeared by TAA) |
| `ETERNALVR_STEREO_SCATTER_TAA` | 1 | the light scattering's temporal filter per eye (`docs/rig-findings/stereo-scatter.md`); 0: the filter held off in stereo |
| `ETERNALVR_STEREO_EXPERIMENT` | unset | `left-eye` or `two-views`: the EngineNativeStereo experiments instead of Route S |
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
  / ... (overflow) / ... (rebase); ... drain(s), ... failed, ... on a quiet period only; frames without
  a backend frame ...; previous matrices ... rewritten / ... kept; r_swapInterval V`.
- `eye R skipped ... (render-frame guard busy) / ... (multiplayer guard) / ... (stack); eye R chain at
  most N KiB deep, least stack left at eye R M KiB`.

Beside them (the 10 s blocks): `rates: game P present(s)/s, T tick(s)/s, S stereo pair(s)/s shown; XR F
frame(s)/s, N new image(s)/s`, the game's own rates next to the runtime's (a headset's frame counter shows
the XR rate, not the game's); `window: last 10 s: A acquire(s), average/max ms; C present call(s),
average/max ms` (the time the driver's acquire and present take); `window: ... present(s) to the window,
... of the other eye and ... too soon handed back; display H Hz; ... handed back, ... failed, ... held`;
`mirror: the window shows left ...`; and `cvars:` for every run-time cvar write. The eye capture logs each
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
   (`stereo_seq::WindowPresentGate`; the display from `MonitorFromWindow`, 60 Hz when unknown). At most
   two images are held; beyond that, and whenever the multiplayer guard is not armed, every present goes
   out as before. `ETERNALVR_WINDOW_PRESENTS=all` turns this off.
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
- Temporal effects other than TAA and DLSS (SSDO, light scattering, depth of field, water, refraction,
  the anti-ghosting mask) still share their history between the eyes and stay off; the headset check of
  per-eye TAA is open (`docs/rig-findings/stereo-temporal.md` section 8).
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
