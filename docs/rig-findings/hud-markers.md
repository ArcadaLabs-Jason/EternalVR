# The HUD's world-attached markers on the head-locked quad (T-058, T-145)

Steam build 25216728 (RVAs below); the Game Pass build (1.0.56.0) has the same code, checked offline on an image
dump. Labels: `[static]` read from the exe, `[rig]` measured on the test PC (OpenXR-Simulator, Route S, the
simulator's eyes at -54/40/44/-54.3 degrees like a Quest 3), `[test]` unit tests, `[inferred]` a reading of these.

## 1. How the HUD places a marker

- Each game frame the player's GUI build (0x6CF640, `idMapInstanceLocal` vtable slot 0x130, "Player gui %d")
  makes one world-to-screen matrix for the HUD: the projection of the world's render view (its latched view,
  `idRenderView` + 0x289D0, through the builder 0x1CE1290 into `[rbp + 0x20]`; the builder copies
  `explicitProjectionMatrix` when `useExplicitProjectionMatrix` is set, else builds from `fov_x` / `fov_y`)
  times the view matrix of the player's view (the builder 0x1CE1350, from `vieworg` / `viewaxis`, into
  `[rbp - 0x20]`), multiplied (0x39BA30, through the thunk 0xF29890 called at 0x6CF769) into a global matrix
  (0x4643250) [static]. It runs on the game's job threads, a different one from frame to frame [rig].
- The HUD's projector (0xF2ABC0) maps a world point through that matrix onto the SWF's screen, `x = (1 + x_ndc) /
  2 * width`, `y = (1 - y_ndc) / 2 * height`, the SWF's width and height from 0x1801E70 / 0x1801E50 (3840 x 2160
  for the HUD), and returns whether the point is in front [static, rig].
- The POI update (0xEEE970, called for each marker of `idHUD_POI`; r14 the marker's `idHUD_POIInfo`, r15 its
  `idDeclPOI`) projects `worldPosition + decl.worldOffset`, tests the result against the SWF's frame (0, 0,
  width, height) into `dil` (1: off the frame), then stretches the position about the frame's middle for the
  render's aspect (the game's ints at `+0xAE7F8` / `+0xAE7FC` of the object at 0x45F7370): by
  `max(1, (width / height) / aspect)` in y and `max(1, aspect / (width / height))` in x. Off the frame (and with
  the decl's `drawOffScreen`) it pulls the position onto the frame's edge (0xF26650) and shows the off-screen
  arrow [static]. On the near-square GUI target of a headset the stretch maps the projection's full height onto
  the target's and the SWF's frame onto the 16:9 band the quad shows: a marker's logged position and the 1280 x
  1400 GUI target's band agree to the pixel [rig].
- The projector's other callers are `idHUD_DamageNumber`, `idHUD_CharacterStatusText` and the two
  `idHUD_Diagnostics` panels; a variant (0xF2AD50) is used only by the 3D threat ring (`view_damageRing3DEnabled`,
  default 0). Nothing else reads the matrix, so these are the world-attached HUD pieces that share it [static].
  The interact prompt (`[Right Stick Click] USE`) and the compass do not use it: the prompt stays at the bottom
  of the band whatever the view does [static, rig].
- Nothing in the layer hides the markers: the T-058 plan to hold `g_setting_objectiveMarkers 0` was never built
  (that cvar is a profile setting, and hiding the markers takes a guide away from the player) [code].

## 2. What the markers did before

The UI layer shows the GUI target on a head-locked quad, 2.0 m wide at 1.5 m by default: its 16:9 band covers
67.4 x 41.1 degrees. Under Route S the matrix's projection is eye L's latched one (`[0] 0.9027 [2] 0.2425 [5]
0.8492 [6] -0.1805`, the explicit asymmetric matrix of the simulator's eye L) with the head-centred view of the
player: so the HUD placed a marker where eye L's frustum puts its target, mapped over the whole GUI target,
which the quad shows at another angle [rig, the HUD's projection logged once a second]. (Head-tracked mono, the
enclosing symmetric FOV of about 108 degrees, would give the other pattern of the research: right at the middle,
about half the angle away from it.)

Measured at the e1m3 gate (`cp_03_shoot_gate`) with the map's POI targets triggered from the console (9 markers)
and the body turned a full circle twice in 15-degree steps (the head-locked quad turns with it, as with a head
turn; hand aim, the hands straight ahead, render size `auto`: a 1280 x 1400 GUI target), markers inside the
band, by the direction of their point from the HUD's view against the direction the quad shows their drawn
position in [rig, the build before the fix with the test log, 67 samples]:

| Target's yaw (degrees, right +) | Error | Shown minus true, yaw | Shown minus true, pitch |
| --- | --- | --- | --- |
| -35 to -25 | 6.4 to 11.7 | +1.2 to +3.5 | +6.5 to +12.9 |
| -25 to -15 | 6.8 to 8.1 | -2.7 to +0.8 | +6.7 to +8.7 |
| -15 to -5 | 7.2 to 13.0 | -6.1 to -3.4 | +6.6 to +12.5 |
| -5 to +5 | 10.9 to 16.2 | -11.1 to -8.6 | +6.6 to +12.4 |
| +5 to +15 | 14.4 to 16.3 | -14.9 to -11.6 | +6.8 to +8.6 |
| +15 to +25 | 16.8 to 20.0 | -17.5 to -15.5 | +6.6 to +12.7 |
| +25 to +35 | 20.2 to 21.0 | -19.9 to -19.1 | +6.5 to +6.8 |

So a marker sat about 9 degrees left of and 7 to 13 degrees above its target straight ahead, more to the
right: eye L's frustum, wider to the left and lower than up, drawn on a quad half its width. The GUI captures
agree: the POI diamond found in 48 captures of the same moments (yellow-green, alpha 120 or more, within 30 px
of the logged position) sat 0.5 px (mean, sd 2.0 and 2.8 px) from the logged position, 6.5 to 22.7 degrees
(mean 14.5) from its target's direction [rig].

Above and below the band the game's own off-frame test, made before its aspect stretch, let a marker count as on
its frame while it was drawn in the GUI target's rows above or below the band, which the quad does not show
[static].

## 3. The fix (`src/vkcore/marker_hooks.*`, `src/xr_math/marker_projection.*`)

While the XR worker shows the HUD on the head-locked quad (not the menu panel, the cinema screen or the flat
game; renewed every XR frame, a 250 ms lease):

- A mid hook after the view build (0x6CF761) gives the projection in `[rbp + 0x20]` rows 0 and 1 for the GUI
  target as the quad shows it, seen from the head centre (`xr_math::markerProjection`: the quad's width,
  distance and height offset, the part of the GUI target it shows); rows 2 and 3 stay the game's. The thunk then
  multiplies the HUD's matrix from it, so every reader of that matrix places its pieces where the quad shows
  their targets. The HUD's view is the head's: turning the weapon hand 20 degrees left and 10 down changed
  nothing in it [rig], and its origin is the head-centred view's, not an eye's.
- A mid hook on the POI update's `test dil, dil` (0xEEEC05) marks a marker off the frame when the position it
  draws (after the stretch) is outside it, so the game's own off-frame path clamps it to the band's edge with
  its off-screen look, as on a 16:9 screen (`xr_math::markerOffFrame`). Only while the quad shows the band alone
  (`ETERNALVR_UI_CROP`, on by default).

Not chosen: (a) a quad as wide as the eye's frustum (108 degrees), which halves the size of every HUD text; (b)
each marker reprojected per eye at its target's depth (stereo markers), which needs the markers drawn by the
layer, not by the game's SWF (v1.x, SCOPE.md); (c) the markers hidden (`g_setting_objectiveMarkers 0`), which
takes away the guide players use.

## 4. Results after the fix

Same run as section 2 on the fix build [rig, two runs, the second on the final code; QA scenario
`hud-markers` passed in both]:

- 65 marker samples inside the band (out to 32 degrees across and 19.5 up and down): error 0.00 degrees (mean
  and largest, to the log's precision) between the target's direction and the direction the quad shows the
  drawn position in.
- The POI diamond in 35 GUI captures of the same moments (the second run): its centre 0.6 px (mean; sd 3.0 and 4.0
  px) from the logged position, 0.16 degrees (mean) from the target's direction; 0.01 to 0.12 degrees for a
  diamond on its own, up to 0.73 where two diamonds or a diamond and the crosshair's ability icons overlap and
  the measured centre is the pair's (the first run: 38 captures, 0.18 mean, 0.74 at most).
- 5 samples of a marker 22.6 to 26.3 degrees below the view (below the band, inside the GUI target): all off
  the frame (`moved off`: 47 and 398 marker updates in the two 10 s windows it was in view); in the captures its diamond sits on the band's bottom edge,
  pulled toward the middle by the game's off-screen pull (`hud_poiOffscreenPull` 0.15), not in the unseen rows.
- The quad's projection was set in every game frame with the quad up (2 at the start without it); no frame with
  a projection that is not one of the engine's builder.
- The hooks' own work: see the cost line in section 5.

The QA scenario `hud-markers` (tools/rig/qa/qa-scenarios-markers.ps1) checks both from the test log alone
(largest error under 1 degree over 20 samples or more inside the band; every sample 22 to 36 degrees up or down
off the frame). Its positive control: the same check on the log of the build before the fix gives
67 samples inside the band with errors of 6.4 to 20.7 degrees and 5 of 5 samples below the band drawn on the
frame, so both checks fail.

## 5. Cost

Measured by the hooks themselves (QueryPerformanceCounter around their work, the log lines left out) and
reported every 10 s (`hud-markers: ... hooks N us per game frame`) [rig, Release, RTX 3080 Ti]: 2.1 to 2.3 us per
game frame in e1m2 (about 85 marker updates a second), 2.3 to 3.0 us at the e1m3 gate with nine markers (about
500 a second), at 80 to 100 game frames a second: under 0.03 % of a frame. The longest single call in a 10 s
window was 8.5 to 123 us (the job thread descheduled inside it; the lease is read without a lock). The mid
hooks' own entry and exit (safetyhook saving and restoring the registers) come on top, two per game frame plus
one per marker update.

## 6. Only a headset can confirm

- The markers are drawn on the quad 1.5 m ahead; a target further away is seen with the eyes converged on it,
  so its marker may look doubled while the player looks at the target, and the other way round [inferred]. How
  that feels at the default 1.5 m is a headset question.
- Markers now spread over the whole quad (out to its edges) where they used to bunch toward the middle; on the
  wrist and weapon HUDs a marker drawn where a corner block is cut would go with that block [inferred, not
  seen on the rig].

## 7. Rig test log

`ETERNALVR_TEST_MARKER_LOG=1` (never set by the launcher) logs, about once a second, the HUD's projection
(the game's or the quad's) and view (origin and axes) and, for each marker the HUD updates, its world point
(`worldPosition + worldOffset`), that point in the view's space and where the HUD draws it in the SWF's 3840 x
2160 frame, on or off the frame (`moved off` when the hook moved it off). At most 4000 lines.
