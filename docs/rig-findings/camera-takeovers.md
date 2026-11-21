# Camera takeovers: the R12 inventory against the code (ROADMAP M7)

Every camera-takeover and special state of R12 (`docs/research/12-eternal-content-audit.md` section 5, 27 states),
and the ones the code handles that R12 lacks, walked against the layer on main (2026-11-12). Per state: how the
layer recognises it, the feature that handles it (T-132: per feature, no central classifier), the log lines at its
entry and exit, the QA scenario that covers it (`tools/rig/qa`, by name) and the known issue when it is not handled.
Retail `DOOMEternalx64vk.exe`, Steam build 25216728, for the RVAs. Evidence: [code] read in the code, [test] a unit
test, [doc] a rig run recorded in another doc, [rig: run] a run of the `takeover-qa` branch (OpenXR-Simulator, the
QA suite; the runs and what each showed are under "Rig runs" below), [inferred] reasoned from the code or R12 and not
seen. The walk itself ran no game; the rows the rig has not reached keep the code's lines unconfirmed.

## The general rule

The layer writes every head-tracked game view itself: the view axis is the body's yaw composed with the head
(`presenter_head.cpp:218`, `xr_math::composeHeadAxis`), so the pitch and roll of any camera the game drives never
reach the headset, whatever the camera [code]. The body's yaw is head aim's own, or the game's while it drives the
view: under hand aim the forced-view gate (`input::ForcedAngleGate::update`, `forced_angles.cpp:42`: a SetViewAngles
call from anything but the per-tick update, the view inhibit bits 0x108, a cutscene, a hands camera animation played,
a climbable wall, the weapon wheel) makes hand aim yield and the body take the game's yaw (`xr_math::drivenBodyYaw`);
in every view, and so under head aim and in gamepad mode too, a camera more than 15 degrees from the player's view
angles drives it (`xr_math::cameraDrivesView`, `presenter_aim.cpp:187`); under view aim the body is the game's yaw
throughout [code]. A camera within those 15 degrees leaves the body on the player's own yaw, under hand aim too: a
glory kill's camera crossing the line switched the view between the two, up to 25 degrees in one game frame [rig: tq2,
`tq-gk-e1m1`]. So a pickup's camera with Still view and, since 2026-11-12 (`615656b6`), a glory kill's with follow the
camera drive every view of theirs (`PickupView::followsCamera`, `comfort::killCameraDrivesView`) [code, test, rig:
tq3]. The body's yaw is the camera's heading with its pitch and roll removed (`xr_math::yawOnly`): from 75 degrees off
the horizon that heading moves over to the view's up's, all of it from 85 (the two headings' unit vectors weighted, so
a roll through upside down turns no jump), since a rolled camera looking almost straight down swings its forward's
heading round while its image hardly turns. The death camera did: before 2026-11-12 (`b06b9dc6`) the body turned 100
degrees in one game frame as it looked 86 to 89 degrees down, rolling (`aim: the body turned -100.2 deg in one game
frame`), and swung round for 0.7 s; after, 13 at most with the two headings' angles blended (`b06b9dc6`) and 26 with
their vectors (`b7ea3c06`: the angles took the shorter way round, which jumped by up to 108 degrees as a roll crossed
180), as the camera rises through 80 degrees and its heading turns from the up's to the forward's [rig: tq2, tq3, tq5
and tq7 `death-camera`; test]. The gate runs in gamepad mode as with the controllers (its lines below, the glory kills
and room-scale read it). The view's eye is the game's (the head's room offset eases out while the game drives the
view). `yawOnly` also gives the arms' shoulder frame from the headset's own axis (`viewmodel_view.cpp:197`): with the
headset 75 degrees or more down and the head rolled that frame now takes part of its heading from the head's up too
[code; not tried in a headset]. Only two opt-in settings add a camera's own turn on top of the head:
`ETERNALVR_CAMERA_ANIMATIONS=1` (the hands animation's camera joint) and Pickup animations Camera moves
(`ETERNALVR_PICKUP_CAMERA=play`).

So a camera the inventory does not name (a scripted first-person shot, a boss intro that is not a cutscene, a
community map's camera) gets the general rule. Its lines are the gate's: `controllers: forced view starts (<reason>;
inhibit 0x<bits>): ...` and `controllers: forced view ends (none; ...): ... resume` (`aim_hooks.cpp:440`, the first
50 episodes; the reasons are `forced view angles`, `view inhibited`, `cutscene`, `camera animation`, `climbable
wall`, `weapon wheel`, `settling after a forced view`), and at its end `camera: forced view N: F game frame(s); the
rendered view left the player's view angles by up to X deg (largest pitch, yaw, roll)` (`camera_anim_hook.cpp:250`,
the first 50). A camera only head aim's 15 degrees sees has no line of its own (the 10 s `aim: ... on another camera`
count only).

## The inventory

| # | State (R12) | How the layer recognises it | Feature | Entry log | Exit log | QA scenario | Status |
|---|---|---|---|---|---|---|---|
| 1 | Glory kill | The player's sync entity (`syncEntity`, `aim_hooks.cpp:471`) whose entityDef is not under `interact/` (`comfort::isKillSync`, `glory_kill.cpp:37`; `syncKind`, `aim_hooks.cpp:485`); the episode is `comfort::GloryEpisode` (the sync flag, then the forced view settling 0.5 s, 10 s at most) in `GloryKills::frame` (`glory_view.cpp:38`) | Glory kills (Play tab, `ETERNALVR_GLORY_KILLS`): follow the camera (default), steady, fade, flat screen (`glory_view.cpp`, `presenter_cinema3d.cpp:151`); the FOV is the headset's (`presenter_head.cpp:224`), depth of field off (`r_dof 0`) | `controllers: sync '<entityDef>' (a kill)` (`aim_hooks.cpp:501`, the first 40 syncs); `glory: kill N starts (<view>)` (`glory_view.cpp:60`, `:63`, the first 30) | `glory: kill N ends; the view's body turned up to X deg during it` (`glory_view.cpp:79`), or `glory: kill N still running after 10 s; taken as ended ...` (the first 5) | `glory-follow`, `glory-steady`, `glory-fade`, `glory-screen` under hand aim (the launcher's default), `glory-follow-head`, `glory-steady-head`, `glory-fade-head`, `glory-screen-head` (by name; `token-pickup` checks that a pickup logs no `glory: kill`) | handled [code, test: `glory_kill_tests.cpp`]; steady and flat screen tried in a headset (ROADMAP M7) [doc]; 5 real kills in each view, each `syncmelee/zombie_tier1` sync with its start and end lines, the camera's pitch and roll never in the view; steady: the view less the head's angles kept within 0.00 degrees in every kill (the render rotation changed by the head's alone); fade: full 103 to 112 ms after each start; screen: each kill its own screen run [rig: tq3 to tq7]; follow and steady also under head aim [rig: tq7]; fade and screen under head aim too, the same checks passing (fade full 103 to 112 ms after each kill's start; each kill its own screen run, no head-tracked view inside one; no body turn over 90 degrees in a frame) [rig: tl-r2]; follow: the camera now drives every view of a kill (it switched with the player's view angles within 15 degrees before, up to 25 degrees in a game frame) [rig: tq2, tq3] |
| 2 | Seek and Destroy leap into a glory kill | As 1 if the leap is part of the sync; otherwise the gate (the leap's view set) or the player's own motion [inferred] | As 1; the general rule for the leap | as 1, or the gate's lines | as 1, or the gate's lines | none | handled by 1 or the general rule [inferred]; not measured |
| 3 | Chainsaw kill | A sync whose entityDef is not `interact/` counts as a kill (`isKillSync`; the chainsaw's entity name is not confirmed, `glory_kill.hpp`) | As 1: the same four views (no toggle of its own) | as 1 | as 1 | none | handled as a glory kill [code]; its sync name not seen on the rig |
| 4 | Crucible kill / Sentinel Hammer slam | The Crucible kill as 1 if it is a sync [inferred]; the Hammer's slam: the hands' held item is the hammer (`weapon/player/hammer`; it is in the hands only for a slam) and its `idHammerWeapon::slamStartTime` takes a new value, a game frame after the hammer comes up (`comfort::SlamWatch`, `src/vkcore/game_motion.cpp`) | Crucible: as 1. Hammer: the comfort vignette's launch pulse (`features/comfort/launch_pulse.hpp`), held until the feet are down (2 s at most) | Crucible as 1; Hammer `vignette: launch N: the Sentinel Hammer's slam, from the ground` (or `in the air`: the feet the frame before; the first 40) | Crucible as 1; Hammer `vignette: launch N (the Sentinel Hammer's slam) held X s: on the ground again ...` | `launch-hammer` (by name) | handled [code, test: `launch_pulse_tests.cpp`]: the Crucible as 1 [inferred]; the slam on the rig (e5m2_earth's start inventory, its meter filled from the map's hammer charge pickups; "Rig runs (2026-11-20, save-rides)" below): each of four slams, two standing and two after a double jump, a pulse 0.03 s after the press, held 0.77 to 0.83 s to the landing, none from a press with the meter empty or from a double jump [rig: y5, y6]. The readings tried first did not tell the slam: the player physics' ground pound (`groundPounding`) stayed 0, `idPlayer::lastSlamTime` and the hammer's `slamInProgress` changed only as it landed (0.8 s after the press), and the hammer's `slamming` byte read 0x4F in one run and 0 in another [rig] |
| 5 | Blood punch / melee lunge | The lunge's `MeleeLunge_SnapViewToEntity` is a foreign SetViewAngles (`docs/rig-findings/input-aim.md` section 2); the punch's hands camera joint (`camera_anim_hook.cpp:105`, `:197`) adds 2 to 4 degrees, under the 5-degree line | The gate (hand aim yields); the joint's rotation left to the head unless `ETERNALVR_CAMERA_ANIMATIONS=1` | `controllers: forced view starts (forced view angles; ...)`; `camera: camera animation N starts (X deg; ...)` at 5 degrees or more (`camera_anim_hook.cpp:128`, the first 50) | the gate's end lines; `camera: camera animation N ends after F game frame(s) ...` (`:137`) | none (`action-aim` holds a melee press back, not checked as a forced view) | handled by the general rule [code, test: `forced_angles_tests.cpp`]; punches measured (camera-animations.md section 5, cs20, cs21) [doc] |
| 6 | Meathook pull | The pull's SetViewAngles (0x166F5E0, from `idDoubleBarrelShotgun::Service_PulledForward`): the gate's `forced view angles` | The gate; `meatHook_playerViewOverrideMode 1` held (oriented once, `src/stereo_seq/seq_settings.cpp:150`, `launcher/data/forced-cvars.txt`); the vignette (option) while the view is forced (`presenter_frame.cpp:341`) | the gate's lines | the gate's lines | none | handled [code]; KNOWN-ISSUES: "The meathook and glory kills with hand aim have had little testing" |
| 7 | Monkey bars / swing poles | Nothing of its own: the view follows a bone (`monkeybar_attachViewToTag`); whether the gate sees it (an inhibit bit, a SetViewAngles) is not known | The general rule (the bone's pitch and roll never reach the headset) | the gate's lines if it sees it; otherwise none | as entry | none | handled by the general rule [code]; how the gate sees it [inferred]; not measured |
| 8 | Wall climb / wall grab | The wall-climb dead-zone step (RVA 0x13B88F0), hooked (`onDeadZoneStep`, `climb_hook.cpp:118`) and counted per frame by `input::ClimbFrames` in `climbFrame` (`climb_hook.cpp:286`); the gate's `climbable wall` | Climbing that follows the head (the wall-climb cvars held at 0, `input::kClimbLookCvars`; hand aim aims with the head on the wall) | `climb: climb N starts: <state>` (`climb_hook.cpp:323`, the first 30; added in this walk), once `climb: on a climbable wall (...)` (`:318`); `controllers: forced view starts (climbable wall; ...)` | `climb: climb N ends after F frame(s) on the wall` (`climb_hook.cpp:293`; added); the 10 s `climb: N frame(s) on a climbable wall in M climb(s) ...` | `climb-hand`, `climb-head`, `climb-dash`, `climb-dash-head` (by name; Exultia's cliff by `cp_09_first_signal`). What starts a climb: the melee button with the view's ray on a climbable face within 7.5 m, or a dash along the view with that ray on it (`dash_autoAttachWallClimb`; "Rig runs (2026-11-18, climb-reach)" below); the earlier runs pressed only jump and dash, and the checkpoint loads without the dash ability (a press of B there moved no one on the rig; `climb-dash` gives it first), so none grabbed (`tq2`, takeover-left) | handled [code, test: `forced_angles_tests.cpp`]; tried on a Quest 3 (KNOWN-ISSUES) [doc]; the start and end lines, the gate's line and the 10 s line seen, under hand and head aim: the rendered view kept the head's angles through each climb (0.01 degrees a game frame at most less the head's, the body still) and each jump off went where the head looked (within 1 degree), also after the stick turned the view on the wall [rig: cr-r3, cr-r4]; a dash grabs too, standing and from the air, under hand and head aim, with the same lines and view checks ("Rig runs (2026-11-20, save-rides)" below) [rig: y4]; a bounce pad's grab window not reached |
| 9 | Ledge grab / mantle / springboard | The root-motion SetViewAngles (return 0x138F33B) off the wall: forced views of about 0.45 s after a jump, likely mantles (input-aim.md section 2) | The general rule | the gate's lines | the gate's lines | none | handled by the general rule [code]; that these are mantles [inferred] |
| 10 | Jump pads / boosters | The launch hooks (`bhaptics_launch.cpp`: the `touchedBouncePad` handler, `idTrigger_SonicBoom`'s trigger), installed once for bHaptics or the comfort vignette (with both off, none); not a camera takeover (an impulse) | The comfort vignette's launch pulse (`features/comfort/launch_pulse.hpp`, `src/vkcore/game_motion.hpp`): in within 0.08 s, held until the feet are on the ground again (at most the pad's flight, or a throw's time at its launch speed, 0.5 to 4 s), out over 0.6 s | `launch: jump pad '<name>' (...)`, `launch: booster '<name>' (...)` (`bhaptics_launch.cpp`, the first 100) | `vignette: launch N (...) held X s: on the ground again ...`, `its limit`, or `carried on by the next` (the first 40) | `launch-pad`, `launch-booster` (by name) | handled [code, test: `launch_pulse_tests.cpp`, rig: gm-r1, gm-p2, rc-x1, rc-x4b]: e1m2's `trenches_trigger_bounce_pad_2` gave a launch line per throw (none for the double jumps and dashes before it), pulses held 1.04 s until the ground and 2.07 s (the pad's 21 m/s throw under the game's gravity) when the fall landed back on the pad, and launch frames in the vignette's 10 s lines; the game's only booster (`capitol_trigger_boost_1`, e1m2, the only `idTrigger_SonicBoom` in the 20 single-player maps' entities; `idInteractable_SonicBoost` in none) [static], switched on by its relay and entered in the air, gave a `launch: booster` line per visit, each pulse held 0.30 to 0.31 s (its minimum is 0.30) until the player was on the ground at the booster's destination 17 m on, launch frames, no ride; `capitol_trigger_bounce_pad_4` under it (19 m/s) threw the player back onto itself every 1.85 s, each pulse carried on by the next [rig] |
| 11 | Dash / double jump | The Dash action held (`presenter_frame.cpp:341`); not a camera takeover | The comfort vignette (option); the dash's radial blur held off (`r_blurRadialScale 0`, stereo) | none of its own (a press of the player's) | none | none | handled [code] |
| 12 | Teleporters and portals | The player physics' origin jumps between two head-tracked game views by more than 2 m (10 m while the game drives the view: a glory kill's sync snaps the player 3 to 4 m) that its own velocity does not explain, faster than 100 m/s (`comfort::TeleportWatch`, `src/features/comfort/teleport.hpp`; read in every view whatever the settings, `body_jump_watch.cpp`; added 2026-11-18); room-scale: the origin jumps over 1 m in a game frame (`src/features/roomscale/body_follow.cpp:186`, standing with body follow on); bHaptics' portal hooks (`bhaptics_portal.cpp`, bHaptics on) | Body follow skips the jump; the yaw the game sets on arrival becomes the body's in one frame (an instant turn, as a snap turn), the head keeps its own; no fade of the mod's own (the `_Fade` teleporters fade by themselves) | `game: teleport N: the player moved X m in one game frame (<from> -> <to>, T ms apart; forced view yes/no, cutscene yes/no); the view turned Y deg over it and 5 game view(s) after (largest step Z)` (`body_jump_watch.cpp`, the first 40, logged five game views after the jump; added); `room: body follow: the origin jumped X m in one frame (teleport N); not followed` (`room_follow.cpp:173`, the first 20); `bhaptics: teleport '<name>' (...)` (`bhaptics_portal.cpp:113`); `aim: the body turned X deg in one game frame ...` (`body_jump_watch.cpp`, over 90 degrees, the first 20) | none (instant; the teleport line comes five game views after the jump) | `teleport-fall` (by name: e1m1's pit, debug teleports and the game's way back after a fall, bHaptics and body follow off) | handled [code, test: `teleport_tests.cpp`]; the Slayer Gate's teleport turned the view 138 degrees (Still view) and 45 (Camera moves) in one frame (camera-animations.md section 7) [doc]; the teleport line on the rig for every debug teleport and the game's way back after falls into e1m1's and e1m2's pits (the view turned up to 155 degrees in one game frame as the game faced the player back over the pit), none for the runs, jumps and falls of those runs or the booster's throw [rig: tl runs, "Rig runs (2026-11-18)" below]; the line's first build, which took the move alone, also logged a pull into one of e1m2's pits (2.7 m a game frame, the physics' own velocity 160 m/s) as teleports, which the velocity now explains [rig: tl-x10; test]; a portal teleporter, the Slayer Gate's and fast travel not run with the line |
| 13 | Swimming | The swim fists held (`swimFistsHeld`, `viewmodel_hook.cpp:362`), read in `aimAngles` (`aim_hooks.cpp:538`) | Under hand aim the view (and the swim) follows the head under water; the underwater warp off (`r_waterPostProcess 0`, stereo) | `controllers: swimming: the view follows the head` (`aim_hooks.cpp:541`, the first 20 changes) | `controllers: out of the water: the weapon hand aims again` (same line) | `swim` (by name) | handled [code]; KNOWN-ISSUES (The DLC): swimming has had a short check; both lines, and under water the game's view yaw on the head's sway with the hand held 30 degrees off, the written pitch the head's [rig: tq3] |
| 14 | Death | The player's dead flag (`playerDead`, `bhaptics_game.cpp:432`), watched every head-tracked game view (`watchDeath`, `game_view_poses.cpp:57`; added) | The general rule: the death camera's pitch and roll never reach the headset; the death screen is a menu (row 24) | `game: the player died (death N): the death camera is the game's, the view keeps the head's pitch and roll` (`game_view_poses.cpp:71`, the first 20; added) | `game: a living player is back X s after death N` (`game_view_poses.cpp:76`; added) | `death-camera` (in every plain run: both lines, the general rule, the body's steps with the camera 80 degrees down or more); `crosshair-hold` (the schedule's `kill` at 50 s, shots of the death fade) | handled by the general rule [code, rig: tq3, tq5]: the death camera goes 89 degrees down and rolls round while the written view keeps the head's pitch and roll; it turned the view up to 100 degrees a game frame for 0.7 s until `xr_math::yawOnly` took its heading from the view's up near the vertical (26 at most since, as it rises through 80 degrees) [rig: tq2, tq3, tq7]; the death screen waits for its Load Checkpoint (a popup's cursor, A), then `a living player is back` after the reload's loading screen |
| 15 | Extra life / Saving Throw | Nothing of its own: the slow motion (row 16) and the teleport to a safe spot (row 12) | Rows 12 and 16 | as 12 | as 12 | none | handled by 12 and 16 [inferred] |
| 16 | Slow motion (the weapon wheel, Chrono Strike, Saving Throw) | The pose path runs on the headset's clock (the head predicted from `xrWaitFrame`, `presenter_frame.cpp`), not game time; the weapon wheel by its button and inhibit bits 0x18 (`input::wheelSignal`, `aim_hooks.cpp:428`; `forced_angles.cpp:54`) and its cursor (`wheelCursor().update`, `presenter_menu_update.cpp:153`) | Head tracking at the headset's rate in any slow motion; the weapon wheel straight ahead (`presenter_wrist.cpp`), its slowdown (`ETERNALVR_THUMBREST_SLOWDOWN`) | the wheel: `controllers: forced view starts (weapon wheel; ...)`, `menu: the weapon wheel is up: ...` (`presenter_menu_update.cpp:155`), `ui: weapon wheel straight ahead (...)` (`presenter_wrist.cpp:336`); other slow motion: none (no takeover) | `menu: the weapon wheel's cursor is gone` (`:159`), `ui: weapon wheel back on the HUD after X s` (`presenter_wrist.cpp:318`), the gate's end | `wheel-centre`, `thumbrest-pick`, `thumbrest-slowdown` | handled [code, test: `forced_angles_tests.cpp`] |
| 17 | Cinematic (third person) | `renderView_t::inCutscene` (`presenter_cinema3d.cpp:146` into `trackCutscene`, `presenter_cutscene.cpp:21`) | Cutscenes: on a flat 16:9 screen (default; stereoscopic, `presenter_cinema3d.cpp`), around the player (Cutscene view; each cut re-based, `cutsceneBodyYaw`), or skipped (`ETERNALVR_SKIP_CINEMATICS`, the skip key held) | `game: cutscene starts (on the flat screen \| around the player ...)` (`presenter_cutscene.cpp:30`, the first 20 changes); `cinema3D: pair run started: fov ...` (`presenter_cinema3d.cpp:263`); `game: holding the skip key (hold N)` (`:65`, the first 10); on the screen `xr: screen run N starts ...` (row 24) | `game: cutscene ends; the player's view is back`; `cinema3D: pair run ended (...)` (`presenter_cinema3d.cpp:111`); `cutscene: end, yaw re-based ...` (around the player) | `cutscene-screen`, `cutscene-around` (by name, `ETERNALVR_SKIP_CINEMATICS=0`; the other launches skip cutscenes) | handled [code]; the flat screen and around the player tried in a headset (ROADMAP M7, KNOWN-ISSUES) [doc]; e1m1's intro (155 s) played in both views: on the screen as one screen run from 0.02 s after the cutscene's start to 0.02 s after its end, the stereo pair run started; around the player no screen run, the re-base at the start and end, the camera's pitch and roll never in the view (3974 trace lines) [rig: tq4] |
| 18 | Scripted first-person sequence | Nothing of its own: the gate (an inhibit bit, a SetViewAngles) or head aim's 15 degrees (`presenter_head.cpp:487`) | The general rule | the gate's lines; under head aim within the gate, none | as entry | none | handled by the general rule [code]; which scenes [inferred] |
| 19 | Interaction animation (pickups, sockets, doors, keys) | A sync entity under `interact/` (`syncPickupActive`, `aim_hooks.cpp:514`) in `PickupView::frame` (`pickup_view.cpp:25`), its episode as 1's; a hands animation's camera joint (`camera_anim_hook.cpp:197`) | Pickup animations (Play tab, `ETERNALVR_PICKUP_CAMERA`): Still view (default; the camera's heading followed, pitch and roll left out) or Camera moves (`pickup_view.cpp`, `features/comfort/pickup_camera.hpp`) | `controllers: sync 'interact/...' (not a kill)`; Still: `pickup: pickup N starts (Still view: ...)` (`pickup_view.cpp:125`, the first 30; added); Camera moves: `pickup: camera turn N starts ...` (`:59`) | Still: `pickup: pickup N ends after F game frame(s)` (`pickup_view.cpp:134`; added); Camera moves: `pickup: camera turn N ends after ...` (`:71`) | `token-pickup`, `pickup-camera`, `pickup-camera-play`, `pickup-nest`, `pickup-nest-play`, `menu-crystal`, `menu-crystal-play`, `menu-crystal-behind`, `menu-crystal-behind-play`, `menu-rune`, `menu-rune-play`, `menu-modbot`, `menu-modbot-play`, their `-head` and `-pad` variants | handled [code, test: `pickup_camera_tests.cpp`, `glory_kill_tests.cpp`]; Still view's added lines seen at the token (`pickup: pickup 1 starts (Still view: ...)`, `ends after 130 game frame(s)`) [rig: tq4 `pickup-camera`]; on the rig for the token, gore nests, the golden nest, the automap station, the crystal, the rune, the mod bot (camera-animations.md sections 6 to 8) [doc]; open: the mod bot's menu with a pad, the chainsaw pickup not reached (KNOWN-ISSUES, "Pickup animations keep your view still") |
| 20 | Scripted transport (trams, the Titan's platform, the Argent stream, the Ion Catapult, the escape pod, the creature ride, long elevators) | One general rule, no per-map list: on the ground, the body carried at 1 m/s or more beyond the player physics' own velocity for 0.5 s (`comfort::CarriedSpeed` over a 0.2 to 0.4 s window: walking, the level speed by which the body outruns its own, never below 0, so a wall, a corner, the wheel's slow motion or a stall is never carried and a lift under a walking player is missed; standing, the body's velocity less its own without the part that falls short of it; `features/comfort/ride.hpp`, `src/vkcore/game_motion.cpp`), until it slows under 0.5 m/s for 0.4 s, the player is off the ground for over a second, the frames stop, or a cutscene, climb, swim, piloted demon or death comes. A ride the game plays as a cutscene goes to row 17 | The comfort vignette (option): the carried speed on the move stick's scale (0 at 1 m/s, full from 6.5 m/s) | `game: ride N starts: the player carried at S m/s or more for T s, more than the player's own motion (...)` (the first 40; read only with the vignette on, or the trace) | `game: ride N ends after X s (<why>); fastest V m/s, at ...` | `ride-lift`, `ride-cult-tram`; no ride: `ride-hunter-tram`, `ride-argent-stream`, `ride-spear-creature`, `ride-swamp-titan` (by name) | handled for rides that carry the body [code, test: `ride_tests.cpp`, rig: gm-p2]: e2m3's Phobos elevator one ride of 25.7 s, fastest 6.7 m/s, the vignette at its full level throughout; e1m3's end tram one ride of 8.6 s, fastest 91.8 m/s; no ride line in a glory-kill run (5 kills), through double jumps, dashes, a walk and jump pad throws (walking at 9.5 m/s carried 0.01 m/s), or running into the trench's walls and corners and with the weapon wheel open (`motion-negative`, gm-x1). The rides R12 names, on the rig 2026-11-17 (rc runs, "Rig runs (2026-11-17, ride coverage)" below): the Doom Hunter Base tram carries no one: the player's car is no mover, the cars behind the coupling are one (`train_func_mover_train_disconnect_1`, 900 m in 40 s, its collision shape not in the shipped data), and when the coupling's relay sent them off the player's origin stayed put, on the front car or where the rear cars had been, with no ride line [rig]: the ride is what goes past a car that stands (fire streaks beside the car in the rig's shots; the scenery's own motion [inferred]), which no reading of the player shows; the jump into the Argent stream (`ride-argent-stream`), the World Spear's creature ride (the torch scene and the flight above the clouds, `ride-spear-creature`; crossing the Nether Lake too) and the Titan carrying the platform (the Blood Swamps' end, `ride-swamp-titan`) are cutscenes on the flat screen, started by their trigger (within 0.3 s) or the Use, with no ride or launch line [rig]; the escape pod (Mars Core) is a scene-director cinematic that teleports the player into the Hell core, started by the Use event of its use proxy (`slayer_gate_volume_use_proxy_1`, in the slayer bunker's layer, past the bunker's back wall that the bunker's kiosk opens) [static], not started on the rig: Use at the proxy with its layer woken by the schedule and `trigger` on it started nothing (rc runs), and through the level's own progress the walk from `cp_19_slayer_gate` stopped at the shut back wall: at the kiosk the game offered its Use prompt but the presses did not open the wall, and the master checkpoint by the pod (`cp_master_pod_slime`) crashes the game at load ("Rig runs (2026-11-20, save-rides)" below); the Ion Catapult is a cinematic (`cinematic_target_relay_ball_launcher`) [static]. A teleport of 11.6 to 17.9 m (under the 20 m that starts the window over) read as carried at up to 46 m/s for 0.2 to 0.3 s, shorter than the 0.5 s a ride needs, and a cinematic's pull to its start position 1 m in 0.25 s: no ride [rig]. The player physics' `groundVelocity` read 0 0 -1 everywhere and the ground fields stayed as on the world on the bound tram, so neither tells a ride [rig: gm-r1, gm-p1] |
| 21 | Boss introductions / finishers | As 17 when a cutscene, as 18 otherwise [inferred] | Rows 17, 18 | as 17 or 18 | as 17 or 18 | none | handled by 17 or the general rule [inferred] |
| 22 | Level exit / mission end | The map load (`mp_guard.cpp:131`); the end of level screen's cursor (row 24); bHaptics' `level exit '<name>': portal` | Row 24 (the menu panel, the screen) | `mp guard: map load '<map>' (single-player)`; `menu: the game shows its cursor (...)`; `xr: screen run N starts ...` (row 24) | as 24 | none by name | handled by 24 [code] |
| 23 | Slayer Gate entry / secret encounter start | The gate's golden nest is a pickup's sync (`interact/golden_gore_nest/...`, row 19), then a teleport into its arena (row 12); a secret encounter starts from an interactable (its timer on the HUD) | Rows 19 and 12 | as 19 and 12 | as 19 and 12 | none by name (camera-animations.md section 7 runs d5, f1 by hand) | handled [code, doc] |
| 24 | Menus / loading / video | Menus: the game's idCursor `active` flag (`menu_cursor.cpp`, read in `presenter_menu_update.cpp:134`), the weapon wheel's cursor kept apart (`menu::WheelCursor`), and in gamepad mode the GUI's backdrop or a menu's 3D model without a cursor and the Dossier's own open and close (`menu::PadMenuWatch`, `menu_kind.cpp:78`; `menu::dossierOpen`, `dossier_screen_hook.cpp`). Loading screens and the title and main menus: images with no head-tracked view within 0.25 s (`kViewStaleSeconds`, `presenter_latch.cpp:74`) go on the cinema screen; a video the same [inferred] | The menu panel and pointer (docs/VR_MENUS.md); head aim holds the body under a menu (`presenter_head.cpp:460`); the cinema screen | `menu: the game shows its cursor (<kind>): panel ...` (`presenter_menu_update.cpp:228`); `menu: a menu over the game without a cursor ...` (`:317`); `menu: the Dossier opens ...` (`:282`); `aim: a menu is up: head aim holds the body yaw at X ...` (`presenter_head.cpp:463`); `mp guard: map load '<map>' (...)`; `xr: screen run N starts: the game's frames go on the screen, no head-tracked view (...)` (`presenter_frame.cpp:56`, the first 40; added) | `menu: the cursor is gone; ...` (`:246`), `menu: the panel is down (held X s)` (`:179`), `menu: the menu without a cursor is gone` (`:323`), `menu: the Dossier is closed` (`:286`); `xr: screen run N ends after X s: head-tracked frames again` (`presenter_frame.cpp:61`; added) | `menu-mission`, `thumbrest-menu`, `crosshair-hold` (the pause menu, the death screen), `foveation-eyes-reload` (Load Checkpoint: the map load twice), the menu-started pickup scenarios (a popup), `-pad` variants, `pad-model-screens` (the Dossier's pages with a pad) | handled [code]; menus on the rig and in a headset (docs/VR_MENUS.md) [doc]; the added screen-run lines seen at every start (the title's load, 5.6 to 6.1 s), at a reload's loading screen (2.0 s) and around each cutscene and glory kill on the flat screen [rig: tq3, tq4]; videos [inferred] (KNOWN-ISSUES, The DLC: "the DLC's in-mission videos" to watch) |
| 25 | Camera shake, view kicks, damage kicks | Not a state: held off for the session | The comfort cvars (`stereo_seq::stereoComfortCvars`, `src/stereo_seq/seq_settings.cpp:138`: `view_skipShakes 1`, `view_skipKicks 1`, `g_skipViewEffects 1`, `pm_noBob 1` and the rest; mono: `launcher/data/forced-cvars.txt`) | `cvars: held at run time: ...` at the start (`runtime_cvars.cpp:363`) | none (held throughout) | every game scenario's profile check (`profile.bin: no held value saved`) | handled [code] |
| 26 | Photo mode / free cam | The game starts photo mode only with `g_setting_photomode` and `g_allowPhotoMode` on (RVA 0x143DFB0) [static] | Held off: `g_setting_photomode 0` in the comfort set (`stereo_seq::stereoComfortCvars`, Route S and Parallel Eye Rendering; not in mono), the player's own value kept in profile.bin by the profile hooks (`mp_policy::profileStoredCvars`); held only while they keep it (`profileKeeps`): a build without the cvar leaves it to the player and the hooks keep the other three (`mp_policy::profileOptionalCvar`) | `cvars: held at run time: ... g_setting_photomode 0`; `cvars: g_setting_photomode 1 -> 0 (reads 0); held in VR` when it is turned on | none (held throughout) | `photo-mode-held` (by name) | handled [code, test: `seq_settings_tests.cpp`, `profile_cvars_tests.cpp`, rig: gm-r1: turned on in play from the schedule, written back to 0]; mono not held; the Game Pass build not checked (no copy on the test PC); the profile's value over a whole session not checked on the rig |
| 27 | Automap / collectible viewer cameras | The menu's cursor (row 24); the Dossier's page (`dossier_screen_hook.cpp`) and its map page (the router); a camera the game drives under a menu (`xr_math::menuBodyYaw`, `presenter_head.cpp:470`) | Menus: the map's sticks (`features/menu/map_drag.hpp`, `map_pan_keys.hpp`), a menu's 3D model on the panel (`menu_model_hook.cpp`), the collectible viewer facing its camera | `menu: Dossier page A -> B (...)` (`presenter_menu_update.cpp:450`), `menu: the Dossier's map page: the sticks pan, zoom and rotate the map` (`:465`), `aim: under the menu the view faces the camera the game drives (...)` (`presenter_head.cpp:473`, the first 40 changes) | `menu: not the Dossier's map page ...`; `aim: under the menu the view is the player's own again (...)`; the menu's exit lines | none by name | handled [code] |

Found in the code or other docs, not in R12 section 5:

| # | State | How the layer recognises it | Feature | Entry log | Exit log | QA scenario | Status |
|---|---|---|---|---|---|---|---|
| E1 | Piloting the Cultist Base Revenant (R12 section 2.1, open question 3) | The idPlayer's controlled entity is a demon (`isPilotedDemon`, `demon_view.cpp:163`), held 0.5 s (`notePilotedDemon`, `:194`) | Piloting a demon (`demon_view.cpp`, `demon_aim.cpp`, `demon_models.cpp`): the stick turns, head or hand aim, the cannons on the view | `controllers: piloting a demon: the move stick follows its facing, body follow off` (`demon_view.cpp:202`, every change) | `controllers: back in the Slayer's body` (`:203`) | `crosshair-hold-demon` (both lines), `revenant-view`, `revenant-look`, `revenant-side`, `revenant-rockets` | handled [code]; the whole-game audit found it the only body swap (ROADMAP M7) [doc] |
| E2 | A hands animation's camera (the `camera` joint; part of 5 and 19) | The joint read at RVA 0x14526C5 (`onCameraJoint`, `camera_anim_hook.cpp:105`), 5 degrees or more (`camera_anim::frame`, `:197`) | Its rotation left to the head; `ETERNALVR_CAMERA_ANIMATIONS=1` plays it on top of the head (camera-animations.md section 3) | `camera: camera animation N starts (X deg; forced view yes/no); ...` (`:128`, the first 50) | `camera: camera animation N ends after F game frame(s), largest angle X deg; ...` (`:137`) | none | handled [code, doc] |
| E3 | Scoped zoom (the Precision Bolt; R12 section 8 item 13) | The player's zoom flag (`playerZoomed`, `zoom_look.cpp:208`) | Zoom look (T-136): the view stays at the headset's FOV, the gun and HUD kept, the reticle held under head aim | `zoom look: scoped zoom starts: the reticle style ...` (`zoom_look.cpp:214`, the first 20) | `zoom look: scoped zoom ends: ...` (same line) | `crosshair-hold`, `crosshair-hold-head` | handled [code] |
| E4 | Tutorial and lore popups, pickup menus (part of 24) | A cursor nobody asked for in a play map (`menu::MenuChain`, `menu_kind.hpp`) | Menus: A / X send Space, the trigger clicks | `menu: the game shows its cursor (a popup over the game, not asked for: ...)` | `menu: the cursor is gone; ...` | the menu-started pickup scenarios (the upgrade menu) | handled [code] |
| E5 | The multiplayer guard tripped (R12 section 6: BATTLEMODE, online) | `mp_guard::allowsGameTouch()` false (`presenter_head.cpp:45`, `presenter_frame.cpp`) | The layer stops writing the view for the rest of the process; the game goes on the flat cinema screen | `xr: multiplayer guard <state>: the game is shown on the flat cinema screen from now on; ...` (`presenter_frame.cpp:304`), `head: the multiplayer guard is <state>; ...` | none (for the process) | `mp-guard-trip` | handled [code] |

## Not handled: candidate jobs

- Rides the general rule cannot see (row 20): a ride that moves the scenery past a standing car (the Doom Hunter
  Base tram, rig 2026-11-17): a named list (the map and the coupling's relay, `train_target_relay_7`, or the train
  fight's encounter) if the tram's motion turns out to bother players in a headset. The other rides R12 names are
  cutscenes (row 17) or not reached (the escape pod, a cinematic by its logic, behind the slayer bunker's kiosk on the rig).
- A horizon cue during a ride (R12), if the vignette alone is not enough in a headset.

## Rig runs (2026-11-12)

The `takeover-qa` branch on the test rig (OpenXR-Simulator, the QA suite with the scenarios of
`tools/rig/qa/qa-scenarios-takeover.ps1`, under hand aim with the simulator's head swaying 8 degrees of yaw and 4 of
pitch every 3 s). Evidence [rig], not a headset.

- tq1 (exploration): e1m3's `cp_03_shoot_gate`, where the pickup scenarios start, holds no live demon (the debug
  target's only AI is `cinematic_ai_heavy_revenant_shakes_too`), so no glory kill there. A `-checkpoint` the map does
  not have crashes the game during the load (a crash dump), after `qconsole.log` lists the real ones (e1m3: 30, among
  them `cp_10_jump_hallway1` and `cp_11_jump_hallway2`).
- tq2 (exploration, the build before the two fixes of 2026-11-12, `615656b6` and `b06b9dc6`): e1m1's barge encounter
  (the rig's perf scene: the checkpoint's layer, the view at the barge's door, the encounter started) gives three
  zombies; with `ai_alwaysStagger 1` and `ai_sync_onlyDieFromGK 1` a shot staggers each one brought in front of the
  player (`nextActiveAI;ai_teleportToPlayer trace`) and a melee glory-kills it: three real kills
  (`syncmelee/zombie_tier1`), the first glory kills a rig run made on purpose. With follow the camera the view
  switched between the kill's camera and the player's own heading whenever the camera crossed 15 degrees from the
  player's view angles: 45 of 221 trace lines off the camera's heading, up to 25 degrees in one game frame (row 1).
  The death (`kill` on the schedule): `game: the player died (death 1)`; the death camera looked 86 to 89 degrees
  down, rolling round, and the view spun up to 100 degrees a game frame for 0.7 s (the general rule above); the death
  screen waited for its Load Checkpoint, so no living player came back. Climbs: the starts of `cp_10_jump_hallway1`
  and `cp_11_jump_hallway2`, four headings each with a walk and three jumps: corridors behind closed gates, no
  climbable wall, no `climb:` line.
- tq3, tq4, tq5 (with both fixes): every scenario of the file passed: `glory-follow` (tq5; its tq3 run's own kill count
  failed on a log-parse fault of the scenario, fixed after, and the same log passes), `glory-steady` (tq5; tq3 as
  `glory-follow`), `glory-fade`, `glory-screen` (tq4), `death-camera` (tq3, tq5), `swim` (tq3), `cutscene-screen`,
  `cutscene-around` (tq4) and `pickup-camera` (tq4, Still view's added lines). The glory kills: the barge's three, then
  the map loaded again from the schedule for two more, 5 in each view.
- tq6, tq7 (merged with pickup-viewaim and beta-checks; `yawOnly` blending the headings' vectors): `glory-steady`,
  `pickup-camera` (tq6), `glory-follow`, `glory-follow-head`, `glory-steady-head`, `death-camera` (tq7) passed. In tq6
  `glory-follow` and `glory-steady-head` made 4 kills of 5 (teleports without a kill: two of the second wave's three
  in `glory-follow`, one in each wave in `glory-steady-head`; three spare teleports since), `glory-follow-head`'s last
  kill had no vignette line before the stop (the timeline waits 11 s after the last kill since), and `death-camera`'s
  largest body step was 25.5 degrees over its first bound of 20 (the vector blend; 40 since).

## Rig runs (2026-11-17, game motion)

The `game-motion` branch on the test PC (OpenXR-Simulator, the QA suite: `tools/rig/qa/qa-scenarios-motion.ps1`
and `glory-follow`, the vignette Light). Evidence [rig], not a headset.

- gm-r1, gm-p1 (the first rule, on the type info's `groundVelocity`, and a probe of the other ground fields): the
  launch hooks went in for the vignette with bHaptics off and logged each throw of the e1m2 pad; `groundVelocity`
  read 0 0 -1 standing, walking, falling and on the elevator, so every frame on the ground counted as a ride and
  the elevator's never ended. On the Phobos elevator `worldOrigin` went down 6.4 m/s with `velocity` 0; on the
  Cultist Base tram (the player bound to it) `previous.worldOrigin`, `groundPhysics` and the ground's spawn ids
  stayed as on the world while `worldOrigin` went 500 m. The rule became the carried speed. `photo-mode-held`
  passed.
- gm-p2 (the carried speed): `ride-lift` and `ride-cult-tram` passed (row 20); `glory-follow` passed with no ride or
  launch line in its 5 kills; `launch-pad` logged no ride through its double jumps, dashes, walk and throws.
- gm-x1 (the carried speed that never counts a body slower than its own velocity, nothing read with the vignette
  off): `motion-negative` ran 8 headings and 8 diagonals from e1m2's trench spawn and then with the weapon wheel
  open: no ride line; 15 blocked steps (the body under 0.5 m in a trace step with 0.5 m/s or more of own
  velocity), carried 0.26 m/s at most. Against walls and corners the game's own velocity drops to 1 to 2 m/s;
  under the wheel's slow motion it stayed 4.7 m/s while the body moved 0.06 to 0.16 m a step, carried 0.00 (the
  first rule would have read about 4.5). `ride-lift`, `ride-cult-tram`, `launch-pad` passed again; `baseline`
  (the vignette off) logged no ride, launch or trace line.

## Rig runs (2026-11-17, ride coverage)

The rides and launches game-motion had not tried, on the `ride-coverage` branch (the 0.1.99 layer, no code
change) on the test PC: OpenXR-Simulator, the QA suite, the vignette Light, `ETERNALVR_GAME_MOTION_TRACE=1`, the
cutscene cases with `ETERNALVR_SKIP_CINEMATICS=0`. Places and names come from the maps' `.entities` (read from the
game's `.resources`) and the logic decls of the cinematics. Exploration runs rc-x1 to rc-x3 and rc-x4b (one
scenario each, `rx-*`, not kept), then the kept scenarios in rc-x4b and all five passing in rc-x5. Evidence
[rig], not a headset.

- Booster (rc-x1, rc-x4b, rc-x5 `launch-booster`): `capitol_trigger_boost_1` has `triggerFirst` and is switched
  on by `capitol_target_relay_750` (with `capitol_trigger_bounce_pad_4` under it). Put into it in the air, the
  sonic blast mechanic took it every time: `launch: booster 'capitol_trigger_boost_1' (speed 75.0)`, the player on
  the ground at the destination (223.7 316.1) 0.30 s later, each pulse held 0.30 to 0.31 s (its minimum is 0.30)
  and faded. No other map has a booster: Urdak's "sonic rings" are particle effects of its jump pads.
- Doom Hunter Base tram (rc-x1, rc-x2, rc-x4b, rc-x5 `ride-hunter-tram`): the campaign's coupling relay is
  `train_target_relay_7` (the `master_level_` relays belong to the Master Level layer and moved nothing). At
  `cp_01`, where the train fight starts on the rear cars, it sent them 900 m down the tunnel and left the player
  standing over the rails at the same origin (-42 -0.6) for 60 s: `train_func_mover_train_disconnect_1`'s collision
  shape is not in the shipped data (`can't generate in production`, `invalid bounds`), so the cars never carried
  the body. From `cp_02_post_train_fight` (the front car, the player's car after the coupling breaks) the rear cars
  left and the player stayed at 59 -0.55: no ride, no launch, no trace step carried.
- Argent stream (rc-x2, rc-x3, rc-x4b, rc-x5 `ride-argent-stream`): `cp_09_towertop_combatend`, the pressure plate
  (`teleport game_trigger_plate_587207834`), `trigger` on `titan_chains_target_melee_left` and `_right` (a 3 s and a
  6 s cutscene), then `teleport titan_chains_portal_trigger`: a 2.4 s cutscene 0.25 s later, then the mission's
  summary. Put into the portal before the chains, the player fell about 20 m at up to 31.7 m/s into the fade teleport
  (`game_trigger_teleport_fade_13`) and back to the tower: no ride, no launch.
- World Spear (rc-x2, rc-x3, rc-x4b, rc-x5 `ride-spear-creature`): the torch's logic starts on
  `village_trigger_trigger_8`; put into it, a cutscene 0.06 s later (the torch video, then the Slayer on the flying
  creature above the clouds on the flat screen; still running 100 s on). Crossing the Nether Lake
  (`trigger crossing_nether_lake_trigger_trigger_1` from `cp_18c_mountain_ravine_monkey_bars`) is a cutscene too,
  still running 60 s on.
- Blood Swamps (rc-x4b, rc-x5 `ride-swamp-titan`): at `CP_10_BOSS_post_combat`, after the boss's relay
  (`titan_level_raise_target_relay_activate_interact_1`), Use 2.5 m from `titan_level_raise_volume_use_proxy` (not
  from inside it) started the Titan's lift as a cutscene; the player's origin went to the cinematic's start (-32 287
  7) and stayed there. The level's transition sits at the lift's end, 1.3 km up [static].
- Escape pod (rc-x2, rc-x3, rc-x4b): `cp_19_slayer_gate`; its use proxy is in the slayer bunker's layer, woken by
  `dormancy_trigger_visibility_controller_trigger_1` (`trigger dormancy_target_change_layer_slayer_bunker_on` from
  the schedule); Use on the proxy and 3.8 m from it, and `trigger` on it, started nothing. Not reached: its logic
  (`cinematic_info_logic_escape_pod`: scene-director shots `cinematic_escape_pod_s010` to `s040`, then a teleport into
  the Hell core) also waits for level events the debug commands cannot send [static].
- Sentinel Hammer (rc-x2, rc-x3, rc-x4b): not reached (row 4).
- Teleports by the schedule 11.6 to 17.9 m away read as carried at 30 to 46 m/s for 0.2 to 0.3 s (the window
  holds the jump for at most 0.4 s), and the Titan cinematic's pull to its start position 1.1 m in 0.25 s just
  before it began read 3.3 m/s once: under the 0.5 s a ride needs, no ride.

## Rig runs (2026-11-18, takeover-left)

The `takeover-left` branch on the test PC (OpenXR-Simulator, the QA suite; tl-x1 to tl-x23 exploration runs, `rx-*`
scenarios not kept; the kept scenarios in tl-r1 and tl-r2). Evidence [rig], not a headset.

- Climbs (row 8), none grabbed. e1m1 `cp_06_post_yellow_key`: the wall-climb tutorial's trigger
  (`tutorial_wallclimb_trigger_1`, 221.8 -537.3 -201.75) sits over a pit with a rock pillar in it; the map's climbable
  models include `pitclimb_pillar_a` and `_c` [static]. Put above the trigger the player falls in and the game puts
  them back on the ledge before it (224.7 -550.9 -212.2); from that ledge, runs with a jump or a double jump at 55 to
  120 degrees of yaw cross the pit or land on the pillar's top, and from the trigger's spot jumps toward 135 to 225
  degrees reach nothing (tl-x2, x3, x12, x13). e1m2 `cp_09_first_signal`: the climb pieces are `func/dynamic`
  entities whose model is a half cylinder of the invisible `wallclimb_nodraw_rock` material [static: its collision
  data]; at `hell_func_dynamic_4057` (a half cylinder of 4.5 m round -52.8 -615.6 -8.75, facing 52.5 degrees) runs and
  jumps from its floor stop against its inner face at 4.1 m from its axis and slide down, and placements in the air
  outside it fall into the pit beyond or pass round it (tl-x6 to x9, x11, x14, x17); at `sentinel_func_dynamic_4188`
  (`cp_06_throne_room`) the placements fell into the pit before it (tl-x10); a run, a double jump and one or two
  dashes from the last solid ground before 4057's pit (-38 -594.5) fall short of it, about 14 m on (tl-x20). From the
  test PC's Exultia save (Game Slot 2's Continue Game, the map loaded with its own layers and inventory, not the
  `-checkpoint` load's developer loadout) the same runs at 4057, and placements by the two pieces near the map's
  start (`capitol_func_dynamic_4138` and `_4139`, in the `dorm_sentinel_power` layer), grabbed nothing either: the
  placements there fell about 20 m and the game put the player back by the map's start (-115 415.1 24.3)
  (tl-x22, x23). No `climb:` line in any run, also with `ETERNALVR_CLIMB_LOOK=0` (the game's own wall-climb cvars,
  tl-x15, x16), without god mode (x18) and under head aim (x19), so the layer's cvar hold is not what keeps the grab
  away [rig]; why the game never grabbed there is not known (the face that takes a grab, or a condition of the
  mechanic's jump to the wall) [inferred]. Found after (climb-reach, below): the game grabs on the melee button,
  which none of these runs pressed.
- Glory kills under head aim (row 1): `glory-fade-head` and `glory-screen-head` passed (tl-r2), with `glory-follow`
  and `glory-follow-head` on the same build (5 kills each).
- Teleports (row 12): the new line in every run: the debug teleports (`setviewpos`, 4.1 to 41.3 m, the view turned
  by the yaw set), the game's way back after a fall into e1m1's pit (20.2 to 23.8 m, `forced view yes`, the view
  turned 45 to 135 degrees in one game frame to face the pit) and into e1m2's (25.6 to 50.2 m, up to 155 degrees);
  `teleport-fall` passed (tl-r2), with bHaptics and body follow off and so no other teleport line. In
  `launch-booster` (tl-r2) the booster's throw gave none, but 0.35 to 0.4 s after each throw, with the player down at
  the booster's destination, the game put the player 824 m away at -0.3 -476.0 11.2, by `cp_08_hell_entrance`'s start
  [rig]: likely its return of a player outside the spawned layers, the destination being outside what the
  `-checkpoint` load spawns [inferred] (the ride-coverage runs saw the landing but not this).
- The pad's two-step turn at a crystal or a rune (camera-animations.md, "Cause and fix" after the pv5 notes):
  `menu-crystal-pad` (tl-x4, tl-r1) and `menu-rune-pad` (tl-r1): the menu's hand-over 0.0 degrees, then the one cut;
  `menu-crystal-play-pad`, `menu-crystal`, `menu-crystal-play`, `menu-crystal-play-head`, `pickup-camera`,
  `pickup-nest` passed (tl-r1), and `menu-crystal-head` (tl-r2; in tl-r1 the QA's pointer held the trigger off the
  upgrade and the menu stayed up, the known flake).

- After review (tl-f1, tl-f2): the first teleport rule took each glory kill's sync snap onto the demon (3.0 to 4.1 m
  in a game frame, forced view) for a teleport in `glory-follow` and `glory-follow-head` (tl-r2); with 10 m while the
  game drives the view, the same scenarios logged only the schedule's debug teleport (14 m) and `meathook-hand` no
  line for its pulls [rig] (a pull is a forced view and moves the player far less than 10 m a game frame
  [inferred]).
  `teleport-fall`, `menu-crystal-play` and `menu-crystal-pad` passed; one `menu-crystal-pad` run showed a 14.5
  degree gap between the view and the crystal's camera under head aim's sweep (the head yaw in the camera's starting
  angles is the put-back write's, from before the menu), over the check's old 10 degree cap, now the sweep's whole
  range (18); the rerun passed with 2.8.

## Rig runs (2026-11-18, climb-reach)

What starts a climb, read in the retail executable (Steam build 25216728) before the runs [static]. The wall-climb
mechanic (`idPlayerMechanicWallClimb`, the player's +0x35130, vtable RVA 0x2DAF710) is a state machine: Init, then
Searching (service RVA 0x13BCF20), JumpToWall (0x13BA7F0), then the wall states, where its per-tick update
(0x13B7190, run for every mechanic by `idPlayerMechanicManager::HandleAll`, 0x13901C0) calls the dead-zone step the
layer hooks (0x13B88F0). Each tick in Searching the mechanic clears `canActivateClimb` and sets it again when all of
these hold: no ledge grab in the last 16 frames, not swimming, not within `wallclimb_slamDelay` (1000 ms) of a
Hammer slam, `reconnectToWallDelaySec` since the last let-go and `disableWallClimbUntilTime` passed (a trigger's
`disableWallClimbTimeMS`), and its `playerViewForwardTrace` ("straight out of the camera, through the crosshair":
from the player's eye along the view axis it copies from the player each tick, 10 m long, the player decl's
`globalTraceDistance`) hits a face whose surface flags hold `SURF_WALL_CLIMB` (0x40000), whose normal is within 0.1 of
horizontal (a vertical face) and which is no farther than `distanceToWallToActivate` (7.5 m in
`generated/decls/entitydef/player.decl`). The attach (`StartJumpToWall`, 0x13BDC20) then runs when the user
command's melee button (`_attack2`, bit 0x2) goes down that tick (it checks the same trace again); when the hands'
flags (the player's +0x16068) show bit 20, by the type info's order `isMeleeAnimPlaying` [inferred], with
`canActivateClimb` set; inside a bounce pad's `autoWallClimbTimeMS` window; or, with `dash_autoAttachWallClimb` 1 (its
default), on a dash along the view with `canActivateClimb` set. Moving toward the wall, being in the air and a
minimum speed are not conditions: a climb starts from the ground too (`startedOnGround`, the `mount_ground`
animation). The wall-climbing tutorial's text names the melee bind and the four move binds
(`prompt_binds_tests.cpp`), and the game showed "[Right Stick Click] Climb Wall" (the layer's name for melee) with the
wall in reach [rig: a shot], the mechanic's `wallClimbText` ("text displayed when you can wall climb") [inferred]. In the layer, under hand aim the game's view angles are the weapon hand's
ray (`aim_hooks.cpp`) and a stick-click melee keeps it (`action_aim.hpp`; a physical punch and
`ETERNALVR_MELEE_AIM=head` aim it with the head), so the gun's ray picks the wall; under head aim the head does.

The `climb-reach` branch on the test PC (OpenXR-Simulator, the QA suite; `cr-x1` to `cr-x8` exploration runs, not
kept, and the kept scenarios in `cr-r1` to `cr-r4`), e1m2 `cp_09_first_signal`, god, by `hell_func_dynamic_4057`: a
half shell of climbable rock about 4 m round (-52.8, -615.6) facing 52.5 degrees. Two places: standing at the shell's
axis facing its inner face about 4 m away ("inner"), and in the air 6 m out facing its outer face ("outer"), where the
player starts to fall into the pit at once. Evidence [rig], not a headset.

- The condition that mattered: the melee button. Inner, with the weapon hand level at the wall, standing still and
  pressing nothing gave no climb; the stick click gave one standing, after a jump, after a run and a jump, and with the
  hand 30 or 50 degrees down at the same wall (cr-x1, cr-x4): the first `climb: climb N starts` lines a rig run
  logged. Under hand aim the hand turned 70 degrees off the wall gave none and 25 degrees off (still on the rock)
  one (cr-x4); with `ETERNALVR_MELEE_AIM=head` the 70 degree hand climbed (the press held back 13 ms until the view
  took the head, cr-x5); under head aim the click climbed with the hand 70 or 90 degrees off (cr-x3, cr-r3). Outer,
  stick clicks every 0.2 s from the placement grabbed within 0.1 to 0.45 s of it, at two of three heights (cr-x8,
  cr-r3).
- Every climb logged its start, the gate's `controllers: forced view starts (climbable wall; inhibit 0x7): shots and
  viewmodel leave the game alone; hand aim aims with the head` in the same game frame, its end `after 257 to 719
  frame(s) on the wall` and the 10 s line (in the scenarios `the game set the view N time(s) on the wall, 0 of them
  turned it`, the let-go's write from RVA 0x13B4677 among them).
- The view (camera trace, the simulator's head swaying 8 degrees of yaw and 4 of pitch every 3 s; `climb-hand`,
  `climb-head`, cr-r3 and cr-r4): held on the wall the written pitch and roll were the head's (0.00 degrees off), the
  game's own view yaw followed the sway (16 degrees of range) and the body yaw did not move; within 1 s of each start
  and end the written view less the head's angles stepped 0.01 degrees a game frame at most: no flick. The game's own
  view angles (the aim) stepped by the weapon hand's offset from the head as a climb began and ended under hand aim
  (7.8 degrees in cr-r3; 96 degrees with the hand 90 degrees off in cr-x2), the aim handed to the head and back as
  designed; the rendered view does not show it. The right stick turned the view on the wall (130 to 144 degrees in
  0.6 s), and each jump off went where the view looked: the first velocity of 5 m/s or more within 1 degree of the
  view's yaw at the climb's end. Looking at the inner face the game jumps through the shell into the pit beyond;
  looking at the outer face, the jump lets go (straight up, as in the flat game).
- With `ETERNALVR_CLIMB_LOOK=0` (the game's own wall-climb view, the flat game's) the mechanic turned the view 178
  degrees in the first game frame of an inner climb (`the game set the view ... largest 177.95 deg, from RVA
  0x13B87CE`, the body following: it faces the shell's axis, its cylinder rule [inferred]) and 0.08 degrees at most on
  the outer face (cr-pc1, cr-x7). With the layer's default (both cvars held at 0) neither place turned the view.
- The gun and hands: on the wall the game's own climbing arm (the left gauntlet reaching to the rock from the lower
  left, the gun out of sight; the held item `weapon/player/pointing_arm` as the jump to the wall begins), alike under
  hand and head aim (the shots of cr-x1 to cr-x3 and cr-r1); the weapon comes back with the let-go
  (`controllers: held item 'weapon/player/shotgun'` 46 ms after the end line, cr-x2).
- Not reached: e1m1's tutorial pit and other walls, a dash or bounce pad attach (the dash: the save-rides runs
  below), a climb's own moves along the wall (the scenarios hold still, turn and jump off), a physical punch as the
  grab (`ETERNALVR_MELEE_AIM=head` stood in for its aim), and a headset.

## Rig runs (2026-11-20, save-rides)

The `save-rides` branch on the test PC (OpenXR-Simulator, the QA suite; exploration runs x1 to x5 and y1 to y3, not
kept, and the kept scenarios in y4 and y5), the Vignette Light and `ETERNALVR_GAME_MOTION_TRACE=1`. Evidence [rig],
not a headset.

- Sentinel Hammer (`launch-hammer`): e5m2_earth from its start (no checkpoint), whose start inventory holds the
  hammer with its meter empty; the hammer's button is the Crucible's (the left stick click, the HUD's
  `_crucible`). With the meter empty a press brings nothing up (no hammer in the hands, no line). The meter filled
  from the map's own hammer charge pickups (the player put on four of them), each press brought the hammer up
  (`controllers: held item 'weapon/player/hammer'` 0.02 s after it) and the slam threw the player up at 24 m/s,
  then down at 50 m/s, landing 0.8 s after the press. The readings: the player physics' `groundPounding` stayed 0;
  `idPlayer::lastSlamTime` and the hammer's `slamInProgress` changed only as the slam landed (a pulse from them
  came 0.8 s late and held 0.3 s past the landing, y1); the hammer's `slamming` byte read 0 through every slam in
  one run (no pulse, y2) and 0x4F the whole run in another (y3: a byte dump of the hammer's slam fields each
  frame); its `slamStartTime` took a new value one game frame after the hammer came up and kept it to the landing.
  The layer now reads that (`comfort::SlamWatch`): in y4 to y6 each of four slams, standing and after a double
  jump, gave `vignette: launch N: the Sentinel Hammer's slam, from the ground` (or `in the air`) 0.03 to 0.05 s
  after the press and a pulse held 0.77 to 0.83 s, ended by the landing; none from the presses with the meter
  empty or the double jumps; launch frames in the 10 s vignette lines; no ride.
- Escape pod (Mars Core): not reached through the level's own progress. From `cp_19_slayer_gate` (the last
  checkpoint before it) the walk east stopped at the slayer bunker's back wall (`slayer_gate_func_mover_bunker_back_wall`,
  x -2131), which the bunker's kiosk (`slayer_gate_interact_use_panel_kiosk_console_1`, its timeline
  `slayer_gate_target_timeline_bunker_slider`) slides open. Walking to the kiosk stopped at a canister; put 1.6 m
  from it on each of its four sides, Use (the stick click: `_attack2 _use`) with the weapon hand at it at 0 to -35
  degrees of pitch, and held 1.2 s, opened nothing, although on its front side the game showed its `_use` prompt
  (`prompts: tooltip text asks for _use`) [rig: x4, y1 to y4]. Why the press does not take is not known; the
  bunker's own fight (its encounter, not fought with `notarget`) may have to end first [inferred]. The master
  checkpoint by the pod, `cp_master_pod_slime` (36 m from it), crashes the game at load: "Trying to use checkpoint
  cp_master_pod_slime but there is no layer state change ... with that checkpoint name in the map" (its layer is
  not in the shipped map) [rig: y1]. The pod's logic (`cinematic_info_logic_escape_pod`) starts the launch scene on
  its use proxy's Use event alone, then teleports the player [static].
- Climbs by a dash (`climb-dash`, `climb-dash-head`): `cp_09_first_signal` loads without the dash (B moved no one
  until `give ability_dash`). With it, B with the view's ray on the shell grabbed it (`dash_autoAttachWallClimb` at
  its default): standing under hand aim with the weapon hand at the wall and 25 degrees off with the stick forward,
  under head aim with the hand 70 degrees off, and in the air at the outer face under both, the climb starting 0.09
  to 0.16 s after the dash; every check of `climb-hand` passed (the view on the wall the head's, no flick, 0.01
  degrees at most, the stick turn 140 to 142 degrees, each jump off within 1.2 degrees of the view) [rig: y4].
  Under hand aim the dash goes along the weapon hand's ray: with the hand 70 degrees off the inner face it grabbed
  in three runs of four (x4, x5, y2; not y3), the ray at the shell's edge. From the air, a dash that reached the
  game 0.08 to 0.28 s after the placement grabbed under either aim, and under hand aim ones 0.36 and 0.64 s after
  it (1.5 m and more lower, the scenario then timed by the log's 0.5 s poll) did not (x5, y1, y2).
- Bounce pads with a grab window (`autoWallClimbTimeMS`, in three maps' data): e4m3's hoop toward the city
  (`game_trigger_bounce_pad_1_tocity`, 5000 ms), switched on by its relay (what the forest puzzle's console does),
  threw the player twice: `launch: jump pad '...' (flight time 3.00 s)`, a pulse held 3.01 s to the landing, on a
  round platform with no climbable face, so no grab (y5). Not switched on: e3m3's four hoops (two of them, at
  `cp_01b_enter_temple`, in a dormant layer: the player fell through, x4) and e4m1's fan pad
  (`starting_platform_trigger_bounce_pad_3`: shots at its shootable did not switch it on, y5). A pad's grab is not
  reached.

## What the rig still has to confirm

A portal teleporter's and the Slayer Gate's teleport with the teleport line (row 12), and every row marked
[inferred]; the M7 box in `docs/ROADMAP.md` says which.
