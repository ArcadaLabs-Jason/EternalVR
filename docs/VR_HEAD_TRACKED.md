# Head-tracked view (mono)

Builds on `docs/VR_FIRST_LIGHT.md`. The game's camera follows the headset: mouse and stick yaw still
turn the body, and the headset's yaw, pitch and roll are applied on top. By default the head also aims
(head aim, below), so the crosshair and weapon follow the view. One image serves both eyes (no stereo
yet).

## How it works

- `src/vkcore/view_hook.cpp`: two mid-function hooks (their callbacks are in `presenter_head.cpp`) (safetyhook, `THIRD_PARTY_NOTICES.md`) installed by
  the XR worker, located by signature in the loaded `DOOMEternalx64vk.exe` (engine-facts.md section 5).
  Each signature must match exactly once, and the structure displacements inside the matched
  instructions must agree with each other; otherwise the hook stays off and the log says why.
  - Game view: at the join after the render-view build point (RVA 0x6A31B7 in build 25216728), where
    `players[0].view` holds the frame's final origin, axis and FOV. `renderView_t = r14 + d - 0x94`,
    with `d` read from the matched `vieworg` store. Runs on the game-frame thread.
  - Render latch: after `r = g` in `idRenderWorldLocal::Render` (RVA 0x1CE1464); read-only. It tells
    which game view the renderer latched, and exposes the previous projection matrix as a check.
- Each game frame the hook locates `VIEW` in `LOCAL` at the XR worker's latest `predictedDisplayTime`
  plus one display period, and rewrites the view:
  - `viewaxis = body * head`, where body is the game's axis with pitch and roll removed (yaw only), and
    head is the headset orientation converted from OpenXR (+X right, +Y up, -Z forward) to id Tech
    (+X forward, +Y left, +Z up): `id = (-z, -x, y)` for vectors and the quaternion's vector part.
  - `vieworg += body * head position`, in game units at `ETERNALVR_WORLD_SCALE` units per metre
    (default 1.0; id Tech 7 units are metres, docs/notes/eternal-unit-scale-evidence.md).
  - `fov_x / fov_y` are set to the symmetric FOV that encloses both eyes (`xrLocateViews`, eye
    orientations only, `enclosingFov(..., Symmetric)`). On the Quest 3 through VDXR that is 108 x 110
    degrees (eyes: 54/40 horizontal, 44 up, 55 down).
  - The frame's record (pose, pose time, FOV, the axis written) goes into a 32-entry history.
- Present: the ring slot carries the record of the view the render latch matched most recently (the
  newest view if the latch did not match), unless it is older than 250 ms.
- XR worker: an image that carries a record is submitted as an `XrCompositionLayerProjection` whose two
  views both have the record's pose and FOV (mono: the image is rendered from the head centre), so the
  runtime reprojects it from the pose it was rendered with. Images without a record (menus, loading,
  before tracking starts) are shown on the first-light cinema quad.
- Head aim (`ETERNALVR_AIM=head`, the default): with the render-only view the weapon and crosshair stay
  on the game's aim, so they appear to move against the head. Head aim moves the game's own view
  angles instead. The build point's r15 is the player; when its vtable is `idPlayer`'s (RVA 0x2DB5698)
  and the exe is build 25216728 (timestamp 0x6A7B9B8C), the hook reads `idHavokPhysics_Player`
  (`idPlayer + 0x8A50`): the user command angles (+0x3DE0 + 0x1C, shorts), `viewAngles` (+0x3F10),
  `deltaViewAngles` (+0x3F1C) and `current.deltaViewAngles` (+0x3F28 + 0x80). For its first 60 counted
  frames it only checks that `viewAngles = command + delta` holds for one of the two deltas (54 of 60;
  `xr_math/aim_check.hpp`). Frames in a cutscene, a forced view or a menu are not counted, and a failed
  try runs again after 120, 240, ... 600 frames of the player's own view, six tries in all: a check
  that overlapped a level's opening cutscene once left head aim off for the whole session (issue 7,
  2026-10-01: a save loaded straight into a level, 51/60 through the state delta). When both deltas
  pass, the frames that match only one of them decide, and a tie goes to the state delta: a level that
  starts at yaw 0 with both deltas 0 matches both in every frame, and the physics delta taken there left
  the view ignoring every value head aim wrote (issue 22). After the check, 30 frames in a row of the
  player's own view that hold `command + the other delta` and not `command + the written one` move head
  aim to the other delta (`aim: the view follows the ... deltaViewAngles`, at most 4 times). Once it passes, it
  adds to that delta each frame: yaw by the change in head yaw since the last frame (the mouse
  keeps turning the body), pitch to reach the head's pitch (the head owns pitch). The rendered axis is
  then `body yaw * head` with `body = command yaw + delta yaw - the head yaw the delta holds`. The
  game's own angles are read as command + delta, not from the frame's view angles: around cutscenes
  the game builds the view angles before it rewrites the delta, or from a scripted source. When the
  game rewrites the delta itself, the value decides how much head yaw it holds: the last 32 values
  head aim wrote are remembered with the head yaw each carried, so a restore of one of them (after
  the e1m1 intro the game holds the delta at its end-of-cutscene value for about 3 s) keeps the body
  where it was. Any other value (glory kills, teleports, scripted views) is the game re-aiming the view
  the player had, so it holds the head yaw injected last (kept while the game holds that value): the
  view faces where the game put it and the body keeps its heading in the room. Until 2026-09-27 such a
  value held no head yaw, which turned the view by the head's whole yaw in the room: a player standing
  turned round in the room ended every glory kill facing backwards (session 5: body yaw 135, head yaw
  170 after the kill at 297 s). Frames whose view is not the player's (the rendered forward more than 15
  degrees from the view angles: glory-kill and cutscene cameras) and forced-view frames are not aimed;
  their body is the camera's heading, or the game's yaw, without the head yaw it holds (`drivenBodyYaw`),
  so the view faces where the game points it and turns with the head from there, instead of the camera
  plus the head's yaw in the room. If the layout is not this build's, or the last try of the check fails,
  head aim stays off for the session and the log says so every 10 s. Roll is render-only.
  `ETERNALVR_AIM=view` keeps the game's aim (the first build's behaviour).
- Hands camera animations (docs/rig-findings/camera-animations.md): a hands animation's `camera` joint turns
  the first-person view (`p_applyAnimatedCamera`, in `idPlayer::CalculateViewWithoutUpdates`); the rotation
  the head replaces is lost. A read-only hook at RVA 0x14526C5 reads the added angles; animations of 5
  degrees or more are logged (`camera: camera animation N starts / ends`), and with
  `ETERNALVR_CAMERA_ANIMATIONS=1` their rotation goes on top of the head-tracked view (and the stereo eyes)
  the way the game adds it, fading in from 5 to 10 degrees, while aim and the body see the game's view
  without it and the controllers treat it as a forced view. Every forced view also logs how far the rendered
  view left the player's view angles (`camera: forced view N: ...`).
- Cutscenes: `renderView_t.inCutscene` (+0x15) changes are logged (`game: cutscene starts / ends`). With
  `ETERNALVR_SKIP_CINEMATICS=1` the layer holds the skip key (R) while a cutscene plays, 2.5 s at a time,
  without depending on desktop focus: the exe's `GetRawInputData` import is replaced, and a key event is
  a `WM_INPUT` posted to the window raw keyboard input is registered to (else the game window) with a
  handle only the replacement answers. The exe's `GetAsyncKeyState`, `GetKeyState` and
  `GetKeyboardState` imports are replaced too, so a held injected key also polls as down
  (`platform/key_injection`). The game ignores keyboard input while it believes its window is
  inactive, so the skip needs the session's keep-active (below). The e1m1 intro accepts the skip only
  about 24 s in (in every run, with the layer's key or with R held from outside); it then ends at once.
- Cutscenes on the flat screen (`ETERNALVR_CUTSCENES=cinema`, the default) have a flat display's shape
  (`ETERNALVR_CINEMA_ASPECT`, `vkcore/cinema_view.hpp`). The game keeps its vertical FOV across aspects and
  stops narrowing the horizontal one at 1:1 (95 x 63.09 at 3840x2160, 63.09 x 63.09 at 2048x2100 and
  1415x1440 in the rig logs), so a cutscene drawn into the near-square or tall eye image showed a narrow
  slice of the flat view on a tall screen. The camera hook now gives each cutscene frame the flat display's
  horizontal FOV (`2 atan(tan(fov_y / 2) * 16/9)`, 95 degrees for the default 63.09) across the image's
  width, extended above and below with square pixels (99.3 degrees at 2056x2216), so the image's centred
  16:9 band is exactly what a flat player sees, and the screen (2.4 m wide, as before) shows only that band.
  Nothing is resized or re-viewported: the rows outside the band are drawn and not shown (the game's GUI,
  subtitles included, sits in that band already, section 6 of docs/rig-findings/render-size.md). A game FOV
  that is already taller than wide on a tall image (the game kept the width) keeps its horizontal FOV. The
  log's `cinema: cutscene fov ...` line gives the game's FOV, the one drawn and the rows shown.
- Cutscenes around the player (`ETERNALVR_CUTSCENES=immersive`, the launcher's "Around you"): the head-tracked
  view on the cutscene's camera, its yaw only (`xr_math/cutscene_cuts.hpp`). A reverse shot turns that camera
  about 170 degrees in one frame, which swung the world by as much and left the action behind a player who had
  turned to follow it (GitHub issue #24). With `ETERNALVR_CUTSCENE_CUT_REBASE` on (the default) the body yaw is
  re-based on the cutscene's first frame, on every cut (the camera's yaw changing more than 90 degrees in
  one game frame, except while the camera looks within 10 degrees of straight up or down, where its yaw
  flips) and on the first frame after a gap of more than 0.5 s (a map load between two cutscenes, a hitch),
  so the new shot's forward is where the head looks (body = camera yaw - head yaw); within a shot the view
  follows the camera's turns as before, a long pan included. While a menu is over the cutscene nothing is
  detected and the offset stays (once head aim has written, the menu's own held body is used, as in play).
  On the first frame after the cutscene head aim's yaw is re-based the same way (`rebaseHeadYaw`: the game's
  yaw is taken to hold the head's yaw now, the values head aim wrote before are forgotten), so the player's
  view faces the game's heading where the head looks; under head aim nothing is added to the game's aim
  then, under hand aim the hand's yaw from there is. Before head aim is on the view keeps the game's heading.
  A camera within 15 degrees of the player's angles stays a scripted camera for the whole cutscene.
  `renderView_t.cameraCut` (+0x16) is logged with each re-base but not used yet. Logs: `cutscene: start / cut
  / gap, yaw re-based by X deg (camera turned Y deg in one frame, Z s since the last frame, cameraCut N;
  ...)`, `cutscene: end, the cuts' re-base of X deg dropped`, then `cutscene: end, yaw re-based by X deg:
  ...` (or `cutscene: end; head aim is not on, ...`). The arms: docs/VR_HANDS_HUD.md, "Arms in cutscenes".
  Depth in these cutscenes (issue #24): a cutscene camera renders relative to `viewOriginOffset`
  (`renderView_t.usesViewOriginOffset` +0xC4 set), and the engine then draws meshes from `viewOriginOffset +
  localViewOrigin` (+0xD4, +0xC8; its `computeMVP` shader), not from `vieworg`, so an eye or head offset written
  to `vieworg` alone left both eyes at one point. `render_view::moveViewOrigin` moves `localViewOrigin` with
  `vieworg` while the flag is set (`viewOriginOffset` stays the same for both eyes, and the shaders need
  `vieworg == viewOriginOffset + localViewOrigin`). Rig, e1m1 opening cutscene: the eyes' disparity in the ship
  went from 0 to up to 80 px on near objects. The logs:
  `cutscene-eyes: cutscene start / in the cutscene / play: vieworg ..., usesViewOriginOffset ...,
  localViewOrigin ..., viewOriginOffset ..., viewBypass.allowBypass ..., forceIdentityViewMatrix ..., cameraCut
  ...` (at each start, every 10 s inside, once in play) and `cutscene-eyes: eye L-R distance of game frame N
  (both eyes of it) ... m (r.vieworg), ... (inverse view matrix, row-major), ... (its column-major reading),
  ... (view matrix); cutscene yes / no` from an eye R latch whose eye L was of the same game frame (so none
  with alternate eyes), every 10 s for each, 200 lines each (about 0.065 m in play).
- Glory kills (`ETERNALVR_GLORY_KILLS`, `features/comfort/glory_kill.hpp`, `vkcore/glory_view.hpp`; M7, REQ-15).
  A glory kill is the game's sync kill: `idPlayer::savedSyncEntity` holds a `syncmelee/<demon>` entity while
  it runs (the camera hook reads it for the view's object once it is the idPlayer, with the controllers on;
  `idPlayer::syncMaster` only as a fallback: it never changed in headset sessions, docs/BHAPTICS.md, and up
  to 0.1.14 the episode read only it, so the options never engaged in real kills). A pickup's animation
  (`interact/...`: a Sentinel Crystal, a Praetor token) is a sync too but not a kill. The chainsaw's kills are
  sync kills too [inferred], so they are shown the same way. An episode starts with the
  flag and ends when the flag has cleared and the game no longer forces the view, at most 0.5 s later
  (`glory: kill N starts (...)` / `ends`, for the first 30). What the headset shows:
  - `follow` (default): as above, the view faces where the kill's camera points and turns with the head
    from there; the camera's own pitch and roll never reach the view.
  - `steady`: the view stays on the kill's animated eye (the head's room offset eases out as for any driven
    view, docs/VR_ROOMSCALE.md) but keeps the body yaw it had the frame before the kill, so only the head
    turns it. When the kill ends, head aim turns the game's aim back to that heading (logged once per kill,
    `glory: the aim turned back ... deg`) for 0.5 s in case the game rewrites the aim as it lets go, so the
    view never turns on its own at either end. With `ETERNALVR_AIM=view` there is no head aim to turn the
    game's aim back, and the view takes the game's heading when the kill ends.
  - `fade`: the view fades to black while the kill runs and back in when it ends, with the room-scale
    blink's timing (black within 0.10 s, clear 0.25 s after; `RoomScale::holdBlack`), with the head fade
    (`ETERNALVR_HEAD_FADE`, the launcher's "Fade in walls") on or off: the fade layer is made either way.
    `glory: kill N starts (fade)`, then `room: fade full N ms after a glory kill started (shown as a fade)`;
    the session's `room: fade layer ready` line shows the layer was made.
  - `screen`: the kill plays on the flat screen in front of the head, as a cutscene does (the same screen
    shape and placement). The camera hook leaves the game's view alone for those frames, and the worker
    stops showing head-tracked views at once (`GloryKills::flat`), not after the 0.25 s a cutscene takes.
- Window size: `ETERNALVR_WINDOW=x,y,width,height` moves the game window and sizes its client area in
  `vkCreateWin32SurfaceKHR`, before the first swapchain. The game clamps `r_windowWidth/Height` to the
  primary display's work area, so this is how a larger render (on the rig's virtual display) is set.
- The ring and the XR swapchain are rebuilt when the game's swapchain changes size or format: presents
  pass through meanwhile, and the old ring is freed once the shared fence and the D3D12 copy fence
  show every copy done. A change that arrives while a ring is being built is picked up when the build
  ends.
- While the session runs, the game's window procedure does not see deactivation (`WM_ACTIVATEAPP`
  false, `WM_ACTIVATE` inactive, `WM_KILLFOCUS`), so desktop focus changes do not pause the game. If the
  window lost focus before the session started (the rig hands focus back right after launch), the game
  already saw the deactivation and ignores input; the layer then posts `WM_ACTIVATEAPP` true,
  `WM_ACTIVATE` active and `WM_SETFOCUS` to it once. The game does not capture or recentre the cursor
  while it is not really in the foreground. The window is recorded from the game's
  `vkCreateWin32SurfaceKHR`.

## Settings (environment)

| Variable | Default | Effect |
|---|---|---|
| `ETERNALVR_MODE` | head-tracked | `cinema` restores first light: quad only, camera untouched, no hooks; `stereo` is an experiment harness (docs/VR_STEREO.md) and otherwise runs head-tracked |
| `ETERNALVR_WORLD_SCALE` | 1.0 | game units per metre for the head position |
| `ETERNALVR_HEAD_POSITION` | 1 | 0 keeps the game's eye position (rotation only) |
| `ETERNALVR_SET_FOV` | 1 | 0 keeps the game's FOV (the projection views then use it) |
| `ETERNALVR_KEEP_ACTIVE` | 1 | 0 lets focus changes reach the game (it pauses) |
| `ETERNALVR_AIM` | head | `view` keeps the game's own aim (render-only head tracking) |
| `ETERNALVR_SKIP_CINEMATICS` | 0 | 1 holds the skip key while a cutscene plays |
| `ETERNALVR_CUTSCENE_CUT_REBASE` | 1 | in cutscenes around the player (`ETERNALVR_CUTSCENES=immersive`) each big camera cut, and the end, turn the view so the new shot faces where the head looks (Cutscenes around the player, above); 0 leaves the cuts as the camera makes them |
| `ETERNALVR_CAMERA_ANIMATIONS` | 0 | 1 plays a hands animation's camera rotation (5 degrees or more) on top of the head-tracked view |
| `ETERNALVR_CAMERA_ANIM_MIN` | 5 | degrees (0.5 to 45): where the camera animation ramp starts (full at twice), for rig tests |
| `ETERNALVR_GLORY_KILLS` | follow | how glory kills are shown: `follow`, `steady`, `fade` or `screen` (Glory kills, above) |
| `ETERNALVR_TEST_GLORY` | unset | `start,duration` (seconds on the `ETERNALVR_DEBUG_COMMANDS` clock): a glory kill is taken to run then, whatever the game does, to check each option on the rig without a staggered demon |
| `ETERNALVR_CINEMA_ASPECT` | 16:9 | the flat screen's shape during a cutscene: `16:9`, `16:10` (or any `W:H` from 1:1 to 4:1), drawn as a flat display of that shape shows it; `full` shows the eye image as the game draws it (tall) |
| `ETERNALVR_WINDOW` | unset | `x,y,width,height` of the game window's client area before its first swapchain |
| `ETERNALVR_TEST_XR_LOSS` | unset | seconds: once the session has run this long, it is taken as lost (as if the headset had gone away) and the worker reconnects (ARCHITECTURE section 6, session state) |
| `ETERNALVR_TEST_XR_LOSS_REMOVE` | unset | `1`: that test loss also removes the presenter's D3D12 device, as a graphics card reset would; the worker then stops reconnecting and the status says the graphics card was reset |
| `ETERNALVR_TEST_HEAD_SWAY` | unset | `yaw,pitch,period[,base]` (degrees, seconds): a sinusoidal head turn added to the tracked pose, for checking head tracking and head aim without a moving headset; `base` turns the head by that much yaw first, and a zero amplitude holds that view (`0,0,30,180`), so runs can be compared at one view (with a non-zero amplitude the view was seen to ignore the base: use the held form) |

## Render size

The game renders the 108 x 110 degree FOV into its window, so pixels are stretched horizontally in the
image and the runtime maps them back. On the owner's 2560x1440 display the window is 2542x1333, and
vertical density is the limit (about 470 pixels per unit tangent against about 920 horizontally;
VDXR's recommended 2496x2688 per eye is about 1120). The game clamps `r_windowWidth/Height` to the
primary display's work area, and `r_windowPosX` does not change that. The rig's virtual display offers
at most 3840x2160, so the larger render uses it with `ETERNALVR_WINDOW=2560,0,2560,2100` (client area,
about 735 pixels per unit tangent vertically). Rendering independent of the window is PLAN 3.6 / T-031:
`ETERNALVR_RENDER_SIZE=auto|WxH` (docs/rig-findings/render-size.md) renders at the headset's size whatever the
window, in this mode too.

## Verified on the rig (2026-09-25, build 25216728, Quest 3 via VDXR)

- Hooks at RVA 0x6A31B7 and 0x1CE1464; `renderView_t = r14 - 0x9C8`.
- The game's own axis rows are forward, left and up (first frame: fwd (0, 1, 0), left (-1, 0, 0),
  up (0, 0, 1)).
- The renderer uses `fov_x / fov_y` as given: the latched projection has [0][0] 0.7265 and [1][1] 0.7002,
  exactly 1 / tan(54 deg) and 1 / tan(55 deg), with no off-axis terms.
- Two latches per game view, on render job threads; at present time the latched view is one game
  frame behind the newest.
- Pose age (from the head locate in the camera hook to the `xrEndFrame` that shows the frame) averages
  about 16 ms at 120 fps.
- A Vulkan validation run showed no messages from the layer's calls. The messages it did show came
  from the game: sampler min/max, ray tracing pipeline lookups, and the first swapchain's
  layout transitions and semaphore reuse, which all happened before the layer's first copy.

## Launch

`tools\rig\launch-ht.ps1` wraps `run.ps1` with the settings below, captures the game window while the
level starts (`<layer>-logs\shot-NNN.png`, through `PrintWindow`, so covered windows still show) and
writes `timeline.txt` into the run folder with the layer's milestones and the display layout:

```
$layer = '<workspace>\tmp-vr\<build>'   # a staged copy of build\windows-msvc\src\vkcore
& tools\rig\launch-ht.ps1 -Layer $layer -Label <label> -DisplayWidth 3840 -DisplayHeight 2160
& tools\rig\stop.ps1 -Run latest
```

It starts the game with `+logFile 1 +com_skipKeyPressOnLoadScreens 1 +com_skipIntroVideo 1
+com_skipSignInManager 1 +r_hdrDisplay 0 +r_motionblur 0 +r_dof 0 +r_chromaticAberration 0 +r_vignette 0
+map game/sp/e1m1_intro/e1m1_intro` and `VK_ADD_IMPLICIT_LAYER_PATH`, `ETERNALVR_ENABLE_LAYER=1`,
`ETERNALVR_LOG_DIR=<layer>-logs`, `ETERNALVR_SKIP_CINEMATICS=1`.

- With `-DisplayWidth/-DisplayHeight` the virtual display is added (largest mode 3840x2160) and
  `ETERNALVR_WINDOW` is derived from where it actually is on the desktop: its top-left corner, client
  2560x2100 at most. Where it sits depends on the other monitors: with the owner's TV on it is right
  of it (x = 2560); with the TV off it becomes the only, primary display at 0,0. `-Window x,y,w,h`
  overrides. Without them the run uses no virtual display and `ETERNALVR_WINDOW` stays unset. End
  the work block (`session.ps1 end`) afterwards so the virtual display goes away.
- `-XrRuntimeJson <manifest>` points only the game process at another OpenXR runtime
  (`XR_RUNTIME_JSON`); the system's active runtime is not changed. Without a headset:
  OpenXR-Simulator 1.5.0 (github.com/elliotttate/OpenXR-Simulator) unpacked in
  `<workspace>\tools\bin\openxr-simulator`, with a manifest whose `library_path` is
  `./openxr_simulator.dll` (the release's manifest names the DLL bare, which the loader looks up on the
  system path and fails with `XR_ERROR_RUNTIME_UNAVAILABLE`). The simulator's head stands 1.7 m above
  `LOCAL`'s origin, so its runs add `-ExtraEnv ETERNALVR_HEAD_POSITION=0`; it opens a preview window
  in the game process and runs at 90 Hz.
- `-ExtraEnv`, `-ExtraArgs` add environment and game arguments; `-KeepFocus` leaves the game in the
  foreground (not needed for the cinematic skip).
- `tools\rig\keys.ps1 -MoveX 20 -Steps 50` turns the player with relative mouse moves (it focuses the
  game for the moves and hands focus back).
- Motion blur, depth of field, chromatic aberration and vignetting are off: head motion drives the
  camera, and the game blurs camera motion.
- Without `ETERNALVR_SKIP_CINEMATICS`, `tools\rig\keys.ps1 -Hold R -Ms 3000 -KeepFocus` skips a
  cutscene when DOOM has keyboard focus (`launch-ht.ps1 -ExternalSkip` does that in a loop).
The log directory also receives `eternalvr-frames-<pid>.csv`: one line per XR frame with the view it
showed, its pose age and the prediction horizon (T-111), the pose lead, the view's head orientation and,
under hand aim, the weapon hand's aim orientation as used (smoothed) and as tracked (zeros without one).
`tools/frames/aim_jitter.py` summarises it (`docs/rig-findings/aim-jitter.md`). `ETERNALVR_POSE_LEAD=1`
predicts the head and hands for when frames are measured to be shown instead of one display period ahead
(`xr_math/display_lead.hpp`, off by default; on by default under `ETERNALVR_PACE=headset`, docs/VR_STEREO.md
"Frame pacing").

## Verified on the rig without a headset (2026-09-26, OpenXR-Simulator)

Runs under `<workspace>\runs\20260926-*-ht4h-*` (layer logs in `tmp-vr\ht4h-logs`).

- Head aim: `aim: head aim on through the state deltaViewAngles (60/60 frames matched)`. With
  `ETERNALVR_TEST_HEAD_SWAY=30,15,8` the game's view yaw is body + head and its pitch the head's
  (body 90.0, head yaw 29.9 pitch -14.9: game view yaw 119.9 pitch -15.0) and the body stays at 90.0
  through the post-cutscene hold (455 rewrites, 361 of them back to a value head aim wrote) and
  60 s of play. 1000 counts of mouse to the right turned the body from 90.0 to 24.0, where it stayed.
- Cinematic skip without focus: the game window had the foreground only for a moment at launch (the rig
  hands it back before the session starts); `game: cutscene starts`
  at 12.1 s, `cutscene ends` at 36.7 s, gameplay with the HUD after it.
- Render size: `window: placed at 0,0 with client 2560x2100`, swapchain 2560x2100; the game presents
  at 143-144 frames per second (the virtual display's 144 Hz, FIFO; the game's counter shows 145),
  copied into the ring with none dropped. Resizing the window to 1920 wide and back rebuilt the ring
  and the XR swapchain each time (`presenter: ring rebuilt for 1920x2119`, `... 2560x2119`) with no
  frame lost. PresentMon records nothing on the rig without elevation, so the rate is the layer's.
- Exit: `stop.ps1` closes the game in under a second (`xr: session exiting`), no error reports.
- A run with the Khronos validation layer showed the same messages as the first validation run (the
  game's sampler, ray tracing, query, semaphore reuse and first-swapchain transitions); none names a
  call the layer makes (copies, imports, presents).

## Known gaps

- Mono: both eyes see the same image (stereo is next).
- With `ETERNALVR_AIM=view` the weapon and hands follow the game's aim, not the head. With head aim the weapon
  lags the head by one game frame, and mouse pitch is overridden by the head.
- The HUD is drawn into the head-tracked image.
- Recenter, posture, eye height and room-scale: `docs/VR_ROOMSCALE.md`.
- Head aim was checked with the simulator's static head plus the test sway; it needs a headset run
  (turning quickly, looking straight up and down, a teleport, a glory kill and another cutscene).
- The multiplayer guard (`src/vkcore/mp_guard.cpp`, `docs/rig-findings/mp-guard.md`) refuses multiplayer
  command lines, and head aim, camera writes, key injection and keep-active act only while it is armed,
  turning off for the rest of the process on any online signal (BATTLEMODE screens, lobby and game
  sessions, Steam joins, accepted invites, non-campaign map loads). The offline experiments were run on the
  rig (`mp-guard.md` section 4a: start-up, campaign loads, the BATTLEMODE menu, the command line, a refused
  build and a test trip passed; the lobby, `game/pvp/` load and Steam callback experiments were not run).
