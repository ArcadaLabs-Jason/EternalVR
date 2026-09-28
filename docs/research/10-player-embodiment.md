# 10: Player embodiment: scale, height, room-scale, hands and weapons, audio

Status: research, 2026-09-25. Tags: **[C]** = confirmed from a source we read (a mod's source tree, a cached
source tree, the cvar dump, a fetched page); **[U]** = unverified, inferred, or needs a check on the
rig before we rely on it.

Reference extracts are in `reference/embodiment/` (see `reference/embodiment/MANIFEST.part.md`).
Background: `06-vr-gameplay-comfort.md` (rotation ownership, control map, comfort presets). This
report does not repeat it; it covers how the player's body, head and hands map onto the Slayer.

---

## 1. Findings

### 1.1 World units: id Tech 7 is (almost certainly) metric

This is the most important finding here, because everything else multiplies by it.

- id Tech 4 and id Tech 6 used **1 game unit = 1 inch**. Doom 3 BFG VR describes its height cvar "in
  real world inches" [C]; a DOOM 2016 VR mod uses 39.3701 units per metre and it plays
  correctly [C].
- id Tech 7 points to **1 game unit = 1 metre** [U until measured, but the evidence is strong]:
  - `vr_metersToGameUnits` "How many game units one meter represents" = **1.0**, part of the dormant
    VR subsystem id left in the executable [C, cvar dump].
  - Raw descriptions written in metres (`pm_minPlayerVel` "in meters/sec" 0.01;
    `ai_physics_stepUpHeight` "in meters" 0.4) and untagged `pm_normalheight 1.79`, which is
    nonsense in inches [C].
  - The cvar parser accepts a `_gu` suffix (`"60.0_gu"`), so game units are not the canonical unit [C].
  - Physics is Havok, which is SI-based [C].
  - Shipped `spawnPosition` values: `e5m1_spear` checkpoints span 187 x 1990 x 1368 units. As metres
    that is Immora's 2 km descent; as inches it would be a 50 m room [C data, our reading].
  - Many metre defaults are exact inch conversions (0.9144 = 36 in, 1.65735 = 65.25 in, 9.525 m/s =
    375 in/s), i.e. id Tech 6 tuning converted at 0.0254 [C].

Consequence: **the DOOM 2016 factor of 39.37 must not be carried over.** Our starting scale is 1.0
game unit per metre, kept as a config value and settled by the rig measurement in section 8. Topic 03
reached the same tentative conclusion from the cvar alone; the map extents are new supporting evidence.

### 1.2 Slayer dimensions (from cvars, metres)

| Quantity | cvar | Value | Note |
|---|---|---|---|
| Standing eye height | `pm_normalViewHeight` | 1.657 m | a ~1.77 m tall person; ordinary human scale [C value] |
| Standing box height | `pm_normalheight` | 1.79 m | [C value] |
| Box width (x/y) | `pm_bboxwidth` | 0.914 m | half-width 0.457 m: the eye stops 0.46 m from a wall [C value] |
| Crouch eye / box | `pm_crouchviewheight` / `pm_crouchheight` | 0.876 / 0.89 m | crouch code remains; no crouch binding in Eternal (topic 06) [C]; whether it can be triggered [U] |
| Step height | `pm_stepsize` | 0.305 m | [C value] |
| Jump / double jump | `pm_jumpheight` / `pm_doubleJumpHeight` | 1.37 / 0.99 m | [C value] |
| Walk / run speed | `pm_walkspeed` / `pm_runspeed` | 3.62 / 9.53 m/s | run is 34 km/h [C value] |
| Neck model | `g_viewNodalX/Z` | 0 / 0 | no neck model in the flat game [C value] |
| Near plane | `r_znear` | 0.06 m | too far for a gun sight held to the eye [U, see 3.7] |

The "Slayer is huge" impression from marketing does not show in the numbers: the engine's Slayer is a
normal-height adult with a wide collision box. Eternal's architecture and demons are authored large
(heroic scale), so at a true 1:1 scale the world will read as big rather than the player as big [U,
playtest].

### 1.3 How scale, height and room-scale have been handled in DOOM 2016 VR

Summary of one working DOOM 2016 VR approach [C]:

- **Scale**: one factor (39.37) applied to head translation, controller positions and native-stereo
  IPD. No user-facing world-scale option.
- **Height**: OpenXR LOCAL space; the head baseline is re-taken (yaw only) every time gameplay starts;
  the vertical head offset from that baseline is added to the game's own eye height. The user's real
  height never matters, physical crouching lowers the view. No seated mode.
- **Camera composition**: camera = physics origin + a stable "body view offset" (measured once while
  the player has weapon control) + HMD offset. This removes view bob and landing dips; a small stance
  filter lets real crouch/blocked-stand changes through after two confirmations.
- **Room-scale (body follow)**: each frame the horizontal head offset from the body is turned into a
  synthesized left-stick command (speed = min(offset x 40, 5.5 m/s)) so the game's own player physics
  moves the body; the next frame the physics-origin displacement along that direction (clamped to
  0.12 m/frame) is accepted into the follow state. The camera is always drawn at the full head offset.
- **What goes wrong**: nothing stops the head entering walls. When the body is blocked, the remainder
  stays as a camera-only offset and there is no fade or clamp. Room-scale is suppressed while the
  stick is held. Synthesizing stick input also means game animations/footsteps react to room-scale
  steps, and the loop depends on the game's acceleration and friction.
- **About "1.03 movement fix"**: the release notes show it was a stick fix, not a room-scale fix:
  atomic X/Y publication and full magnitude at 90% stick travel; the root cause of the reported
  slowdown was never confirmed, and synthesized room-scale input was explicitly left out of it [C].

### 1.4 Other prior art for room-scale

- **Doom 3 BFG VR** [C, source]: body follows the head through `MotionMove` (a collision sweep). If
  less than 75% of the move is achieved it switches to "leaning": the remainder becomes a head/weapon
  offset capped at 36 in (0.91 m). Each frame it tests whether the body now fits under the head and
  snaps it there when it does. While leaning, an eye trace detects a head inside geometry, fades to
  black and freezes the lean offset at the last clear value.
- **UEVR** [C, source]: moves the pawn by the head offset with a sweep and then resets the standing
  origin to the HMD, so whatever the sweep blocked is thrown away: the head cannot enter walls, but
  the world slides relative to the room when blocked. Done on one eye pass only to avoid eye desync.
- **Meta guidance** [C]: never block camera motion at a wall; fade to black by penetration depth.
  Push-back is the uncomfortable option. A 2020 study compared fade, delayed push-back, instant
  push-back and teleport (full results not retrieved) [C citation].

### 1.5 First-person viewmodel in Eternal

What the engine exposes [C, cvar dump unless noted]:

- `hands_show` 0/1/**2 = "show weapon only"**: a native switch that hides the arms and keeps the
  weapon. In DOOM 2016 this took clearing surface visibility bits by hand.
- `hands_fovScale` 1.15, "scale the fov for gun models": the gun has its own projection. Weapon decls
  also carry `zoomedHandsFOV` (topic 03). The equivalent 2016 routine has to be forced to a constant 1.0
  because otherwise a controller-placed weapon "slides when the HMD view rotates" [C].
- `hands_offsetX/Y/Z` and `Pitch/Yaw/Roll` (default yaw 4, roll 1, z -0.02): static offsets only,
  useful for calibration experiments, not for per-frame tracking.
- Sway/bob/lag/kick: `pm_noBob`, `g_setting_hands_bob`, `pm_doom4BobCycle`, `hands_weaponLagEnable`,
  `hands_bobOffsetZMax`, `g_weaponkick`, `hands_hitReactionsEnable`.
- `g_weaponDoomClassicPose`: centred weapon pose; shows the viewmodel pose is data-driven.
- Aim: `hands_adjustFirePosDistCheck`, "adjust firePos to viewPos from muzzlePos" when the muzzle
  trace and aim trace disagree by more than 1.9 cm. With a tracked gun they will always disagree, so
  this path can silently move shots back to the head [U, must test]. In DOOM 2016 the
  `useMuzzleAsFireAxis` branch can be forced [C].
- Sync kills: `hands_hideDuringSyncInteraction` 1; glory-kill FOV forced by `sync_autoFOV` (90) and
  `p_ForceFov`.
- Depth hack: DOOM 2016 passed a `depthHack` float into the weapon render update [C]. No `depthHack`
  cvar exists in the Eternal dump; whether Eternal draws the viewmodel with a separate depth range is
  unknown [U, RenderDoc].
- A near-wall "front pushback" hands animation existed in 2016 and can be patched out [C];
  an Eternal equivalent is likely [U].

### 1.6 Prior art for tracked weapons and hands

- **DOOM 2016 VR** [C]: replace the final idHands origin/axis; pivot at the animated `righthandattach`
  joint so reloads still play around the hand; per-weapon yaw/pitch/roll offsets; own static GLB hands
  calibrated per weapon; muzzle fire axis; laser from the rendered muzzle; two-hand profiles
  (`barrel` or `side-grip` per weapon) and a virtual stock 0.18 m below the HMD; Left Hand mode puts
  the (unmirrored) weapon in the left hand.
- **DOOM VFR** (id, 2017) [C, press]: weapons "a tad bit smaller" than flat DOOM so they sit in the
  hand; all weapons one-handed; grenades in the off hand.
- **Doom 3 BFG VR** [C]: body modes full body / hands only / weapons only; `vr_disableWeaponAnimation`
  on by default; damage view kick off; per-weapon pivot and forearm length; five sight styles.
- **UEVR** [C, docs]: attach the weapon mesh component to a controller, align it by hand ("Adjust"),
  make the attachment permanent so projectiles spawn from the right place, set Aim Method to that
  controller, hide the first-person arm mesh.
- **REFramework RE8** [C]: keeps the game's arms and drives them with hand IK plus body IK: the
  best-looking option, and the most rig work.

### 1.7 Body

Eternal still carries a full third-person Slayer (`pm_thirdPerson`, `pm_bodyAnim_EnableForLocalViewPlayers`
1, leg IK `pm_walkIKBlendInMS`, `g_showPlayerShadow` 0) [C]. Glory kills show animated first-person
arms authored for the flat camera. There is no evidence the first-person view renders legs.

### 1.8 Audio

Eternal uses Wwise (`s_metaDataWwiseProjectName "ghost.wproj"`) and the listener update is a
separate step (`s_lockListener`, `s_showPaths` "paths from listener") [C]. The listener is almost certainly fed from the player's view (body
yaw) and not from our stereo camera: whether it picks up our head translation depends on where we write the pose
relative to the listener update [U]. REFramework handles the same problem by hooking the Wwise listener
update and temporarily swapping the camera transform to the HMD pose ("Head Oriented Audio", default
on) [C].

---

## 2. Recommended world-scale and height calibration design

### 2.1 Scale

- `world_scale` (game units per metre) default **1.0**, config only, verified at startup against
  `vr_metersToGameUnits` and logged. One factor multiplies all tracked positions: per-eye view
  positions from `xrLocateViews` (IPD comes with them), head translation, controller positions, hand
  and HUD placement. IPD is never set on its own; we do not use `stereoRender_separation`.
- A user **"World size" option** of 0.85–1.20 (default 1.00) multiplies the same factor. Above 1 the
  world looks smaller (the user feels bigger: the "Slayer scale" some players will want); below 1
  the world looks bigger. It changes only on the options screen, never during play, because changing
  scale while playing causes eye strain [C, 2013 write-up] and because visible hands make users judge
  scale by the world, not their body [C, Mine et al. 2020].
- **No automatic scaling to the user's height by default.** Doom 3 BFG VR's "scale world to your
  height" mode exists as prior art [C]; we offer the same thing as a height mode (below), not as a
  silent scale change.

### 2.2 Height modes

Eye height is set with a separate offset, not with scale:

| Mode | What the user sees | Use |
|---|---|---|
| **Slayer height** (default, standing) | Standing eye = `pm_normalViewHeight` (1.657 m) whatever the user's height; ducking lowers the view 1:1 | Matches game sightlines: enemy aim, ledges, cover all behave as designed |
| **Real height** | Eye = the user's measured standing eye height (STAGE/floor reference) | Users who want a true floor; short users see below Slayer sightlines |
| **Seated** | Seated head is lifted to 1.657 m; vertical motion below a small threshold is filtered; room-scale body follow off by default | Seated play; turning required |

Calibration flow ("stand up straight and press both grips", repeatable from the overlay): record the
standing HMD height H in the tracking space. Vertical head offset = `hmd_y - H` in Slayer mode;
`hmd_y - pm_normalViewHeight` in Real-height mode. Capturing the reference implicitly at every
gameplay entry works in DOOM 2016 [C]; we add an explicit calibration because Eternal loads often
mid-crouch or mid-lean (a bad implicit capture would persist for the whole level). Implicit capture remains as a fallback
before the first calibration.

Guards: clamp the downward offset so the eye never goes below ~0.3 m above the physics origin (fade
if the head is pushed into the floor); clamp upward to ~0.25 m above the standing reference (a tall
user reaching up should not see over walls the Slayer cannot).

Physical crouching does not change the Slayer's collision box, so ducking a projectile visually does
not dodge it [U]. If the crouch state can still be triggered through the usercmd, a "physical crouch
= game crouch" option (Doom 3 BFG VR `vr_crouchMode`, trigger distance 7 in / 18 cm) would fix that;
this is an open question, not a default.

---

## 3. Recommended room-scale translation design

### 3.1 Composition of the stereo camera

Use this composition, which is known to work in DOOM 2016 [C]:

`eye = physicsOrigin + stableBodyViewOffset(yaw only) + R_body * (headOffset_m * world_scale) + perEyeOffset`

- `stableBodyViewOffset` is measured while the player has weapon control and no animated sequence;
  it removes bob and landing dips. Step-up smoothing (`p_stepUpViewSpringK`) is worth keeping for
  stairs [U, playtest]; landing dips and `pm_doubleJumpViewPitchChange` (10°) are dropped.
- All authored rotation is stripped (topic 06 rule). Also neutralise the view-snapping cvars that
  rotate the view on the player's behalf: `meleeLunge_snapViewToTarget`, meathook auto-orientation
  (`meatHook_playerViewOverrideMode`), `monkeybar_attachViewToTag` [C cvars; effect in VR U].

### 3.2 Head vs body: hybrid body-follow with a lean zone and fade

1. **Free lean zone.** Horizontal head offset within **r_lean = 0.15 m** of the body moves the camera
   only. This absorbs head sway and leaning without moving the body every frame (moving the body
   at a 2.5 mm threshold keeps the player physics constantly walking).
2. **Body follow.** Beyond r_lean the body is moved toward the head, preferably by a direct
   collision-respecting move of the player physics (the Doom 3 BFG VR `MotionMove` pattern) if we find
   one on `idPhysics_Player`/the Havok character proxy [U]. Fallback: the DOOM 2016 closed loop with
   synthesized movement input and "accept only what the physics actually moved". With Eternal's
   acceleration (`pm_walkaccelerate` 52) the fallback will lag and overshoot more than 2016 did; tune
   gain and cap on the rig.
3. **Blocked body.** When the body achieves less than 75% of the requested move, the remainder stays
   as a camera offset (lean), capped at **0.6 m** total from the body centre. The capsule is 0.46 m in
   half-width, so the eye can reach the wall surface before any penetration, so walking up to a wall
   feels natural.
4. **Head in geometry → fade.** Every frame sweep a sphere (radius ~0.10 m) from the body eye point to
   the head position. If it hits, fade to black by penetration depth: 0% at the surface, 100% at
   0.10 m inside; a thin grey outline or the HUD stays visible so the user can orient and step back.
   Do not push the camera back (that is the uncomfortable option) [C, Meta]. While fully black, freeze
   the lean offset at the last clear value, as Doom 3 BFG VR does [C].
5. **Anti-exploit.** The fire origin and any trace the game starts from the view (usable focus, melee,
   glory-kill target selection) use the clamped, collision-valid head position, never a head inside a
   wall. The weapon muzzle, if it ends up inside geometry, fires from the last valid point on the
   grip-to-muzzle line (the engine already does something similar for obstructed muzzles in the demon
   player code: `dp_aimViewMuzzlePercentDiff`) [U for the player].
6. **With stick input** body follow keeps running (suppressing it there is a known weakness [C]); stick velocity and
   room-scale correction are summed before they reach the physics, so leaning while strafing still
   works.

### 3.3 Recentering

- Yaw-only recenter on every gameplay entry [C] and on the OpenXR
  `XrEventDataReferenceSpaceChangePending` event, plus a long-press on the left Menu button.
- After cinematics and glory kills the accepted body pose is kept, not recentered
  (scripted yaw must never counter-rotate the tracking space [C]).
- Height is not reset by recentering; it only changes through calibration.

---

## 4. Viewmodel and hands design

Default presentation: **tracked weapon + our own hand models; the game's arms hidden** (`hands_show
2`), switching to the game's arms only during sync animations (glory kills, chainsaw, Crucible kill),
where our hands and weapon are hidden. This layout is proven in DOOM 2016 [C]; in Eternal native
switches do what had to be patched there.

| Weapon-handling problem | Solution | Engine need | Confidence |
|---|---|---|---|
| Weapon must follow the controller | Replace the final idHands root transform after animation, pivot at the weapon's grip joint (`righthandattach` equivalent); per-weapon offsets from a table | Hook on idHands transform update; joint lookup | High (DOOM 2016 precedent) |
| Gun drawn with its own FOV (`hands_fovScale` 1.15, `zoomedHandsFOV`) | Force the hands FOV scale to 1.0 while tracked; neutralise zoom FOVs | Hook the FOV-scale getter or set the cvar and patch zoom | High |
| Separate depth range / depth hack | Keep the weapon in the world depth buffer (it clips into walls like any VR game); if Eternal uses a depth hack, disable it while tracked | RenderDoc capture of the viewmodel pass | Medium [U] |
| Arms cut off at the forearm | `hands_show 2` (weapon only) + our GLB hands; no arm rendering | cvar write | High that the cvar exists; [U] that it keeps equipment/Crucible visible |
| Bob, sway, weapon lag | `pm_noBob 1`, `g_setting_hands_bob 0`, `hands_weaponLagEnable 0` | cvar writes | High |
| Recoil kick moves the camera | `g_weaponkick 0`, `view_skipKicks 1`; keep the weapon's own fire animation around the grip; add haptics | cvar writes | High |
| Hit reactions shove the gun | `hands_hitReactionsEnable 0` | cvar write | High |
| Near-wall pushback animation | Patch it out, as in DOOM 2016; collision and knockback untouched | Find the Eternal branch | Medium [U] |
| Reload / mod-switch animations authored for a flat camera | Keep them, relative to the grip pivot (they read as the hands working the gun) | Joint-relative transform | Medium |
| Weapon switch bring-up/put-down flies from below the screen | Remove root translation of bring-up/put-down (weapon appears in hand), short scale-in, haptic tick | Detect switch state; clamp root delta | Medium [U] |
| Shots must leave the rendered barrel | Force muzzle-as-fire-axis; disable or retarget the `hands_adjustFirePosDistCheck` view fallback; aim assist off | Hook idHands fire path; muzzle transform | Medium-High |
| Crosshair meaningless with a tracked gun | Hide it; optional laser/red dot from the muzzle (Doom 3 BFG VR offered five sight styles [C]) | HUD element filter; muzzle transform | High |
| Two-handed weapons | Support grip near a per-weapon support point latches two-hand aim: `barrel` or `side-grip` profiles, orientation from grip to support hand | Profile table per weapon | High |
| Long-range stability | Virtual stock: aim origin 0.18 m below the HMD once two-hand latched | none | High |
| Left-handed players | Weapon to the left controller, **not mirrored**; hands are separate left/right models | none | Medium: mirroring flips winding, normals and decal text, not worth it |
| Weapon feels too big at the hand | Per-weapon visual scale (VFR shrank its guns), default 1.0, tuned per weapon in playtest | Scale on the root transform | Medium [U] |
| Gun sight close to the eye clips | `r_znearOverride` ~0.02–0.03 m while in VR (reverse-Z should hide the precision cost) | cvar | Medium [U] |
| Glory kills / sync animations | Hide our hands and weapon, show the game's sync arms, HMD owns rotation (topic 06) | Sync-state classifier; `hands_show` toggling | High |
| Melee / Blood Punch with arms hidden | Physical punch drives the input; our hand model is the visual; impact haptics | Punch detector | High |
| Crucible blade, Berserk fists | Crucible: keep blade visible on the off hand [U]; Berserk: switch our hands to fist models | Weapon identity; blade model visibility | Low-Medium [U] |
| Hand/weapon registration | Calibration mode per weapon and per hand, saved to config; Meta's "back of the other hand" check for hand-model alignment | Overlay UI | High |

Why not IK on the game's arms (the RE8 route): Eternal's arms are authored cut off at the forearm, so
IK would still show a floating forearm, and the rig work is large. Revisit after release.

### 4.1 Body recommendation

**Hands only.** No full body, no player shadow. The third-person Slayer exists [C], but a body driven
by a 9.5 m/s double-dashing player with a free head and free hands will almost always be wrong, and
Mine et al. show visible hands alone are enough to anchor body scale [C]. Keep `g_showPlayerShadow`
off; revisit a legs-only shadow as an experiment later.

---

## 5. Audio listener

- Requirement: the Wwise listener position = the head position we render (clamped, collision-valid);
  orientation = HMD yaw/pitch/roll. Otherwise turning your real head does not move sounds, which is
  the most noticeable audio flaw in injected VR.
- Approach: find the listener update (idSoundWorld/idSoundSystem listener setter, or the Wwise
  `SetPosition` call on the listener game object) and do what REFramework does: set the head pose
  immediately before the original update and restore after, so gameplay code never sees it [C
  pattern]. Test with `s_showPaths 1`.
- Set `s_panningRule 1` (headphones) when VR is active [C cvar].
- If our camera write happens before the listener update, head translation may already reach the
  listener; orientation will still follow body yaw. Confirm on the rig [U].

## 6. Haptics tie-in (brief)

Recoil is felt, not seen: per-weapon haptic envelopes on the weapon hand (short click for Heavy
Cannon, sustained for Chaingun spin-up, heavy double pulse for Super Shotgun); both hands when the
two-hand grip is latched; meathook tension; punch impact; a
light pulse when the head enters the fade zone. Controller haptics replace the camera kick we remove.

---

## 7. Implications for our design

1. **World scale = 1.0 until measured.** Put the factor in one place and assert it at startup; the
   first milestone with head tracking must include the unit measurement.
2. **Camera composition is shared infrastructure.** physics origin + stable view offset + clamped
   head offset + eye offset; head pose clamping and fade live in the same module as rotation stripping
   (topic 06), with pure, testable math in small policy headers.
3. **We need a collision query.** Sphere/box sweep against world + monsters from the body eye to the
   head, every frame, from our thread or the game thread. Finding a safe callable trace
   (idClip-style) is a new engine research item.
4. **Direct body move is worth finding.** A collision-respecting move on the player physics avoids
   the stick-synthesis loop entirely; otherwise use the synthesized-input loop with Eternal-specific tuning.
5. **Viewmodel control is mostly cvars plus one hook.** `hands_show 2`, bob/lag/kick/hit-reaction
   cvars, FOV scale 1.0, and one per-frame idHands transform hook; the fire-axis and fire-position
   fallback need a second hook.
6. **Per-weapon data tables** (grip offsets, hand pose, support point and mode, visual scale, haptic
   envelope) in one versioned config, with an in-headset calibration mode.
7. **Sync-state classifier** (topic 06) also drives hand visibility and the game-arms switch.
8. **Audio listener hook** is a small, separate item with a large payoff; schedule it early.

## 8. Open questions to measure on the rig

1. Units: with noclip, compare `getviewpos`/`mh_spawninfo` deltas with a known move; read player
   origin to eye height (1.657 = metres, 65.25 = inches).
2. Does `hands_show 2` hide both arms (weapon hand and off hand) and keep weapon, Crucible blade,
   equipment and chainsaw visible? Does it survive level loads and glory kills?
3. Does Eternal render the viewmodel with its own projection only (`hands_fovScale`), or also with a
   depth range/depth hack? RenderDoc of the weapon draws.
4. Which function writes the final idHands transform, and is the grip joint still called
   `righthandattach`?
5. Does `hands_adjustFirePosDistCheck` redirect a tracked gun's shots to the view position? Test with
   `hands_drawMuzzlePos 1` and `g_weaponTraceDebug`.
6. Is there a callable collision-respecting move on the player physics (Havok character proxy), and a
   safe trace function for the head sphere?
7. Actual collision shape: box or cylinder, and is the width really 0.914 m?
8. Can the crouch state still be set from a usercmd bit?
9. Where is the Wwise listener set, and does it read the render view or the player view? Check with
   `s_showPaths 1` while physically turning the head.
10. Is 0.06 m near plane visible when sighting down a barrel, and does `r_znearOverride` work in the
    retail build?
11. Weapon switch animation path relative to the grip joint: how far does the root travel?
12. Subjective: at 1:1, do Eternal's arenas feel too big? Which "World size" value do playtesters
    pick?

---

## Sources

- DOOM Eternal cvar list, https://github.com/Official-KEX/doom-eternal-full-cvarlist
- DOOM Eternal Archipelago mod map data, https://github.com/snowzzrra/DoomEternal-AP-Mod
- Doom 3 BFG VR, https://github.com/CarlKenner/DOOM-3-BFG-VR (local, commit 4451656)
- UEVR, https://github.com/praydog/UEVR (local, commit 4ee5c6b); http://docs.uevr.io/usage/adding_6dof.html
- REFramework, https://github.com/praydog/REFramework (local, commit d146137)
- Mine et al. 2020, PLoS ONE, https://journals.plos.org/plosone/article?id=10.1371%2Fjournal.pone.0232290
- "Dwarf or Giant", https://diglib.eg.org/items/40f2fe61-f43b-4a7f-9872-b8589eda8109
- "A Sense of Scale in VR", https://kholdstare.github.io/technical/2013/10/06/sense-of-scale-vr.html
- Meta, "Lessons from the Frontlines: Modern VR Design Patterns",
  https://developers.meta.com/horizon/blog/lessons-from-the-frontlines-modern-vr-design-patterns/
- "How to Handle Head Collisions in VR", https://link.springer.com/chapter/10.1007/978-3-030-55789-8_54
- DOOM VFR preview, https://www.uploadvr.com/preview-pulse-pounding-vr-shooter-doom-vfr-quakecon/
- Helifax DOOM Eternal VR preview, https://x.com/Flat2VR/status/1704495949978984506
- Project docs: `docs/research/03-doom-eternal-internals.md`, `06-vr-gameplay-comfort.md`
