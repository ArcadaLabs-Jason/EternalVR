# Room-scale, recenter, posture and eye height

Builds on `docs/VR_HEAD_TRACKED.md`, `docs/VR_STEREO.md` and `docs/VR_CONTROLLERS.md`. The player's real head
moves the view within a lean cap, a long press recenters the play space, seated and standing both work
without setup (and standing up or sitting down mid-game is noticed), and a head pushed into a wall fades
the view to black instead of seeing through it. The
design is T-029, T-045, T-062, T-063 and T-106 (`docs/DECISIONS.md`) and R10 sections 2 and 3
(`docs/research/10-player-embodiment.md`); the engine facts are `docs/rig-findings/collision-query.md`.

**Status.** Built, unit-tested and checked live on the rig with OpenXR-Simulator in stereo (results below);
the headset checks are the owner's. Everything that
writes to the game runs only while the multiplayer guard is armed and asks it again before each write.

## How it works

```
camera hook (every game frame)                                 XR worker (every XR frame)
head in LOCAL --> room anchor --> head in room space           runtime events: LOCAL moved -> re-anchor yaw
   |               (recenter,       |                          fade: penetration -> rate-limited alpha
   |                first stable    +--> game view orientation     -> black quad over the projection
   |                pose)           +--> head offset: lean cap, height clamp
   |                                     --> world (body frame, world scale)
   |                                     --> sweep eye -> head (engine query)
   |                                     --> penetration (fade), first contact (shots)
   +--> projection layer keeps the LOCAL pose it was rendered with
```

- **Room anchor** (`src/features/roomscale/room_anchor.hpp`). The runtime's `LOCAL` space is left as the
  runtime keeps it; our own transform on top of it defines room space: origin at the anchored head
  (horizontally, and vertically once the height is anchored), `-Z` along the anchored head's heading,
  level. The game's view, the controllers (`controllers::setRoomFromLocal`, applied by every locate in
  `input_xr.cpp`) and the mapper's head all use room space, so a recenter turns and moves the game world
  around the player in one step. The compositor still gets the `LOCAL` poses the frames were rendered with,
  so a recenter never disturbs reprojection.
- **Recenter** (T-029, T-063).
  - Recenter: hold both sticks pressed for 2 s, or use the headset's own recenter (hold the Meta / Oculus
    button). The `recenter` action goes down while **both thumbsticks are held pressed** (the mapper's
    stick chord, `features/input/stick_chord.hpp`, docs/VR_CONTROLLERS.md): it starts 0.25 s after the second
    stick went down, and the layer then counts the rest of `ETERNALVR_RECENTER_HOLD` (2 s by default) before
    it recenters. It was a held left Menu button until the owner's Quest 3 session of 2026-09-27: Virtual
    Desktop watches a held Menu button too and dropped the player to its desktop. The Menu button now only pauses
    (`left.menu.tap`); a player's own map can still bind `left.menu.hold` to `recenter`. A hold released
    early is logged (`room: recenter binding released after 0.62 s; hold it 2.00 s to recenter`), and a gap
    in the mapper's calls (over 0.25 s: menus, a stall) counts as a release, so a press can never carry over
    into the next one.
  - The runtime's recenter (`XrEventDataReferenceSpaceChangePending` for `LOCAL`, e.g. holding the Meta
    button): the anchor first follows the runtime's move (`poseInPreviousSpace`, when given; VDXR gives
    none), then re-anchors like ours.
  - **Every recenter is a full re-anchor** (owner's session 4, refining T-063, which kept the height on
    runtime events): heading, horizontal origin and height at the current head pose, and the posture is
    detected again from the head's height above the floor. So a recenter after standing up or sitting down
    puts the Slayer's eye right and unblocks (or blocks) body follow. The re-anchor happens 0.12 s after the
    request, behind a **blink**: the fade layer goes black (0.10 s), the anchor jumps, and the view clears
    over 0.25 s. The runtime's event is also taken this way because the head pose of the frame it arrives in
    may still be in the old space.
  - Every recenter is logged: `room: user recenter: posture standing (detected), head 1.62 m above the
    floor; anchored at (...) LOCAL (height was 0.254), heading ... (was ...); eye height ...`, or the same
    with `runtime recenter`.
- **First stable head pose** (T-045, T-106; `src/features/posture/anchor_detector.hpp`). With
  `ETERNALVR_AUTO_ANCHOR` on (default), the first window of 1 s in which the session is focused, the head is
  tracked, it moves less than 2 cm (stable) and more than tracking noise (1 mm of position or 0.2 degrees of
  rotation: worn) anchors yaw, origin and height once. A headset lying on a desk never shows the worn
  motion, so it never anchors; any untracked or unfocused sample restarts the window. Until the anchor,
  room space is `LOCAL` as the runtime set it.
- **Posture and eye height** (T-029, REQ-06; `src/features/posture/posture_detector.hpp`,
  `src/features/roomscale/head_offset.hpp`). Where the runtime has `LOCAL_FLOOR` (OpenXR 1.1, else
  `XR_EXT_local_floor`) or `STAGE`, the head's height above the floor at the anchor picks seated (below
  1.30 m) or standing (above 1.45 m); `ETERNALVR_POSTURE` overrides it. Without a floor the posture is
  unknown unless overridden. Seated feeds the input mapper's seated behaviour, the seated viewmodel
  offsets (T-074) and body follow's seated block. The height mode:
  - `slayer` (default): the anchored head is at the game's own eye (`pm_normalViewHeight`, 1.65735 units =
    metres) whatever the player's height; ducking lowers the view one to one.
  - `real`: the anchored head is at the player's own eye height above the floor, times the world scale
    (needs a floor space; otherwise Slayer height).
  - The eye never goes below 0.30 m above the feet and never rises more than 0.25 m above the anchored
    height (a seated player who stands up is held there until the posture change below re-anchors).
- **Posture re-detection** (`src/features/posture/posture_tracker.hpp`). With `ETERNALVR_POSTURE` auto and a
  floor space, the head's height above the floor is watched after the anchor. Seated, it must stay above
  1.35 m for 1.0 s to become standing; standing, below 1.20 m for 1.5 s to become seated. The head must also
  be at least 0.30 m from the height the posture was detected at, so a tall player detected seated near the
  top of the band does not flip by sitting up straight. A shorter dip or rise (ducking, reaching) never
  flips it. On a change the **height only** is re-anchored at the current head (heading and horizontal
  origin stay: the step forward out of the chair is left to body follow), behind the same blink, and the
  new posture is in force at once (body follow unblocks when standing). Logged: `room: posture change:
  seated -> standing (head 1.66 m above the floor for 1.0 s); re-anchoring the height`, then `room: posture
  change: posture standing (detected), head 1.66 m above the floor; anchored at ...`. A forced posture
  (`ETERNALVR_POSTURE=seated` or `standing`) turns re-detection off; without a floor space the posture is
  unknown and nothing is re-detected (the recenter binding still re-anchors the height).
  The thresholds bracket the anchor's own (seated below 1.30 m, standing above 1.45 m): seated heads sit
  around 1.1 to 1.25 m on a chair (the owner's 0.96 m), standing ones from about 1.5 m.
- **Lean cap** (T-062). The horizontal offset from the anchor is capped at `ETERNALVR_LEAN_CAP` (0.60 m) in
  the same direction; the first frame of each clamp is logged: `room: lean 0.800 m clamped to 0.600 m`. The
  lean is measured across the floor only: rising is the separate height clamp above (0.25 m allowance) and
  never counts as lean. Past the cap by more than 0.05 m the camera stops following the head and the view
  fades (`room: fade full N ms after the head went past the lean cap`), except while the head is on its way
  between postures (more than 0.15 m from the posture's reference height toward the other one, or a
  re-detection dwell running) and for 1 s after a posture re-anchor, while body follow walks the body to the
  head: standing up out of a chair steps the head forward, and that is not a lean. The cap still holds the
  camera then; geometry still fades. (Session 4 went black this way: seated follow was blocked, so standing
  up and moving around was all lean.) While something other than the player's walking moves the body (the
  stick, a jump or dash, a fall, the Meathook, a teleport, a cutscene or glory kill, a menu, and body
  follow's resume delay after them), follow cannot close the gap, so the lean past the cap is taken into the
  room instead: the view rides with the body and nothing fades (`followBlockRidesWithBody`, `leanPastCap`;
  logged once as `room: body follow: the head went past the lean cap while <cause> moved the body`, and as
  `m ridden past the lean cap` in the follow stats). A player walking a large space through a glory kill, or
  walking while using the stick, went black here before (a tester's report, 2026-09-30).
- **Never stuck in the dark.** The head's fade (geometry or the lean cap, not a blink or a glory kill shown
  as a fade) held fully black for 1.5 s moves the room onto the body, heading kept, behind the blink: `room:
  the view was black 1.5 s; the room moved N m onto the body` (with the fade off, which leaves the view clear:
  `the head was in geometry or past the lean cap (fade off) 1.5 s`). The same tester stood in a train door with a
  black view and could not tell which way was out.
- **Head in geometry** (T-062, R10 section 3.2 step 4; `src/vkcore/head_sweep.cpp`,
  `src/features/roomscale/head_fade.hpp`). Each game frame with an offset over 1 cm, the engine's own
  collision query (`idHavokCollision::Translation`, synchronous, as the game calls it for its cameras)
  sweeps the collision world's 0.16 m sphere from the game's eye to the rendered head, against the world
  and player clip (not monsters), ignoring the player. From the first contact on, the rest of the way is
  the penetration depth. The camera is never pushed back:
  - the view fades to black by depth, 0 at the first touch and fully black 0.10 m further in (so it is
    black before the 0.06 m near plane reaches the wall), rising at most 0.10 s from clear to black and
    clearing over 0.25 s. The fade is a head-locked black quad layer (16 x 16 swapchain cleared to
    `(0, 0, 0, alpha)`, source alpha) over the projection layer: core OpenXR, the same on VDXR, SteamVR and
    the simulator (`src/vkcore/presenter_fade.cpp`). `room: fade full N ms after the head entered geometry`
    logs the time. The layer is made with `ETERNALVR_HEAD_FADE=0` too (`room: fade layer ready`): that
    setting turns off the head's own fade and the blink over a re-anchor, while a glory kill shown as a fade
    still goes black (`shownFadeDepth`). Before 2026-10-02 the layer was not made with the setting off,
    so `ETERNALVR_GLORY_KILLS=fade` logged `room: fade full ... (shown as a fade)` and showed nothing.
  - shots and the hand's aim ray start from the head where the sweep first touched (the last clear point
    between the eye and the head) while the head is in geometry (`controllers::endGameView` gets it instead
    of the rendered offset), so a head in a wall cannot shoot from inside it. The previous frame's clear
    offset is not used: when the body walks up to a wall with the head leaned ahead, it reaches into the
    wall (found live). Head aim's hitscan already starts from the game's own eye.
  - no sweep in cutscenes, or when the query was not found (an unknown build, a signature that does not
    match): the lean cap still applies and nothing fades (the conservative fallback). A fault in the
    query is caught and turns it off for the session; a sweep that starts in contact is ignored.
- **World scale and IPD.** `ETERNALVR_WORLD_SCALE` (units per metre, the launcher clamps it to 0.85–1.20)
  scales the head offset, the eye separation and the Real height. `ETERNALVR_IPD` (millimetres, 50–80)
  moves the game's two eyes apart or together about their midpoint (`src/features/roomscale/eye_separation.hpp`);
  the compositor still gets the runtime's eye poses. Unset, the runtime's IPD is used.

## Body follow

**Status.** Built, unit-tested and checked on the rig with OpenXR-Simulator (results below); **on by
default**, `ETERNALVR_BODY_FOLLOW=0` turns it off. The headset checks (real head motion, how it feels) are the
owner's.

When the player walks in the room, the Slayer's body walks after the head through the game's own movement,
so the game's collision stops it at walls; the part it cannot follow stays as head offset under the lean
cap and the fade. `src/features/roomscale/body_follow.hpp` is the logic, `src/vkcore/room_follow.cpp` and
`src/vkcore/body_follow.cpp` the glue.

```
camera hook, frame N                                          user command, frame N+1
origin(N) - origin(N-1)  --body frame (N-1)--> room displacement
   blocks? (below) -- yes: nothing taken, no request
   a follow move went out within 0.3 s?
      --> anchor shifted by it (capped)   head offset shrinks by what the body really moved
   gap = head in room space (+ test offsets), body speed toward the head (smoothed)
      --> walk 85 / creep 60 / coast  --publish-->  stick, keys, jump or dash in the command?
                                                    no: rotate into the view frame, add to
                                                    forwardmove / rightmove
```

- **The game's response to a move command is tiered, not linear** (measured, `ETERNALVR_TEST_MOVE`, runs
  `20260927-144422-bf3` and `-144620-bf4`, e1m2, 1.5 s per value, speed over the second half):

  | Move value (of 127) | 5–20 | 25 | 30 | 35–65 | 70 | 75 | 80–95 | 110 | 127 |
  |---|---|---|---|---|---|---|---|---|---|
  | Speed, m/s | 0 | 0.005 | 0.01–0.02 | 0.08–0.21 (creep) | 0.25 or 1.2 | 1.5–2.1 | 2.1–2.7 (walk) | 7.1–8.2 | 8.1–9.3 (run) |

  First motion takes 10–30 ms from 35 up (0.2–0.8 s at 20–30). After the command stops, the body coasts
  about its speed x 0.1 s (0.15 m from 2.2 m/s, 0.88 m from 9.3 m/s) and stops dead from the creep tier. So
  no command gives 0.3–1.5 m/s, and the first build's proportional request (7 to 13 of 127 for 10 to 20 cm)
  never moved the body (runs `20260927-143615-bf1`, `-143820-bf2`).
- **Request (pulses between the tiers).** With the horizontal gap between the head and the body (the
  quantity the lean cap clamps, in room axes) and the body's smoothed speed toward the head `v`, the body
  would stop `v x ETERNALVR_BODY_FOLLOW_COAST` (0.1 s) further on if the command ended now. Follow sends the
  walk value (`ETERNALVR_BODY_FOLLOW_WALK`, 85) along the gap while that stop would still be short of the
  stop radius, the creep value (`_CREEP`, 60) while it would still be short of the head, and nothing (coast)
  otherwise. It starts beyond the deadzone (`_DEADZONE`, 0.04 m) and stops within 40 % of it (0.016 m), so a
  head standing still never makes the body creep. The move goes into the command in the game's view frame,
  turned by the same view yaw the stick uses (`viewYawTracking`), so it works under head, hand and view aim.
- **Feedback on the real body.** Each frame the player's origin (`idHavokPhysics_Player` futureOrigin or
  bodyOrigin, as its GetOrigin picks, collision-query.md section 2; checked to be 0.3 to 2.5 m below the
  eye and within 0.5 m of it, else the eye stands in, logged; the physics read held in every rig run after a level's first frames) is
  differenced and turned into room axes with the body frame the view was built with. While a follow move
  went out within the last 0.3 s, the body's whole horizontal move (toward the head, sliding along a wall,
  coasting) shifts the room anchor (`shiftedBy`), so the head's room offset shrinks by exactly what the body
  did; it is capped per frame at 1.5 x `_SPEED` (3 m/s) plus 5 mm. The first build took only moves toward
  the head, which turned origin jitter into anchor drift while the body stood still (bf1: `anchor took
  0.003-0.020 m` with `body moved 0.000`); taking both signs lets jitter cancel. Moves while nothing was
  sent are the game's own: the view rides with the body, as without body follow. The anchor is shifted
  before the head is placed, so the view and the hands never lag the body by a frame.
- **Blocks.** Follow asks nothing and takes nothing, this frame and for 0.3 s after, when: body follow is
  off or the user-command hook is missing (it needs `ETERNALVR_CONTROLLERS=1`); the room is not anchored;
  the posture is seated; the origin could not be read or game frames stopped for more than 0.25 s
  (menus, loading); the origin jumped more than 1 m in one frame (a teleport, a checkpoint, a respawn:
  never fed into the anchor); a VR menu or the pause menu drives the controllers, or the game suppresses
  buttons (the command side); a cutscene, or the forced-view gate (glory and sync kills, the Meathook
  pull's view snap, melee lunges, scripted cameras, respawns); the stick or the keyboard moves the player
  in the command (**the stick wins**); jump or dash is pressed; the origin moves vertically faster than
  1.5 m/s (airborne: a jump, a fall, a lift, a step-up on stairs); or horizontally faster than 6 m/s (a
  dash, the Meathook pull, a knockback). The multiplayer guard gates both hooks as before. The grounded
  flag itself is not read: the vertical-speed test and the jump button stand in for it.
- **Why the stick wins rather than summing (R10 section 3.2 item 6 suggested summing).** The feedback
  cannot tell the stick's share of a displacement from the follow share, so with both in one command the
  stick's walk would be taken into the room and the stick would feel slow while leaning. With the stick in
  charge, stick locomotion behaves exactly as without body follow (live: an 8.5 m stick walk during the
  steps, `anchor took` only the follow share, 0.17 m), and follow resumes 0.3 s after the stick is released.
- **Turning.** Snap and smooth turns still pivot about the body, not the head (the known P1): the room
  offset turns with the body. Body follow keeps the gap within 1.6 cm while standing still, so a turn
  swings the view by at most that much; while walking the gap is the follow lag and the swing is that size.
- **Seated.** Seated posture blocks follow (a seated lean stays a lean). The block follows the posture in
  force, so it lifts when the player stands up (posture re-detection or a recenter) and returns when they sit
  down. Without a floor space the posture is unknown and follow runs.
- **Test offsets.** `ETERNALVR_TEST_HEAD_OFFSET` is followed too (the body walks to the fake head), so the
  lean-cap and wall checks of the live test plan need `ETERNALVR_BODY_FOLLOW=0`.

**Logs** (`room:` lines): the settings line `room: body follow on: deadzone 0.040 m, walk 85 and creep 60 of
127, coast 0.10 s, up to 3.00 m/s` (or `room: body follow off (ETERNALVR_BODY_FOLLOW=0)`); `room: body
follow: the player's origin from its physics ...` (or `from the game's eye ...` after `the physics origin
(...) is ... not used`); `room: body follow: first request: gap ... m, walk (...)`; `room: body follow:
command N moves (f r) of 127 ...` for the first five commands; `room: body follow: first move taken into the
room: ... m`; `room: body follow: first frame blocked by <reason>` once per reason; `room: body follow: the
origin jumped ... m in one frame (teleport N); not followed`; and every 10 s `room: body follow: N frame(s)
following (walk N, creep N, coast N), N command(s) sent, ... m taken into the room, largest gap ... m;
blocked frames: stick N, ...`. Without the command hook: `room: body follow is on but the user-command hook
is not in place ...`. While test steps run, every change of tier is traced: `room: body follow: walk at gap
0.180 m, body 0.000 m/s toward the head`.

**Live results (2026-09-27, rig, OpenXR-Simulator, stereo, e1m2, hand aim; logs `tmp-vr\rs\bf<N>-logs`).**
Each leg starts from what the previous one left (up to 1.6 cm), so a 10 cm step is an 8 to 10 cm gap.

| Run | Steps | Gap closed to within 2 cm | Left | Overshoot |
|---|---|---|---|---|
| bf7 (`20260927-150234`) forward | 10 cm (8.4–10 cm gaps), 20 cm, 40 cm | 214–306 ms; 376–438 ms; 558–598 ms | 1.4–1.6 cm | none (peak = moved) |
| bf8 (`-150533`) right | 10, 20, 40 cm | 201–241 ms; 352–372 ms; 534–566 ms | 1.3–1.7 cm | none |
| bf9 (`-150850`) forward 20 cm, with a 1 s stick walk and a jump every 11 s | 20 cm | 357–472 ms (legs a stick walk or jump fell into: 0.6–1.5 s) | 1.4–1.7 cm | none |

In every leg `anchor took` equals the body's own follow move to the millimetre. bf9 logged `first frame
blocked by jump or dash`, `airborne`, `stick`, `fast motion` and, after falling off the start ledge,
`cutscene or forced view` and two teleports, none of them taken into the anchor. 5 cm steps (3 cm gaps)
stay inside the deadzone, as designed.

**Unverified:** real head motion (continuous walking, sway, a headset's tracking noise) and how the pulsed
walk feels (footsteps and the body's walk animation start and stop with the pulses); other levels and
surfaces (slopes, stairs trip the airborne block for a moment); the Meathook pull and dash in combat (the
fast-motion block backs up the forced-view gate); whether the tiers are the same on other builds or with
other movement settings (the values are tunables).

### Driven views

While the game drives the view (the forced-view gate: glory and sync kills, the Meathook pull, melee
lunges, scripted cameras, a view-inhibit; or a cutscene) its eye is an animated one on the Slayer's own
body and body follow is off. Whatever offset the head had from the body when the game took over (a lean,
a step not yet followed, a crouch) would put the camera that far from the animated eye: standing in session
5 (2026-09-27) the owner saw the Slayer's own shoulders and arm during a glory kill. So the offset the head
had when the game took over eases out (`driven_offset.hpp`, 0.06 s time constant, about 0.2 s): the camera
sits on the game's eye and moves only by what the head does from there, and the offset eases back in when
the game lets go, after which body follow closes it as before. The log says `room: the game drives the view
(...)` with the offset held back, for the first 20 episodes.

The view's heading during and after a driven view is head aim's (`VR_HEAD_TRACKED.md`): the game's forced
angles or camera heading re-aim the view the player had, so it faces where the game points it whatever the
player's heading in the room.

### Precision test (usercmd movement precision, rig)

Two scripted modes for a runtime whose head never moves (OpenXR-Simulator); both start one hold after the
first anchor (so add `ETERNALVR_TEST_RECENTER=3`), hold each leg `ETERNALVR_TEST_STEP_SECONDS` (3 s) and
repeat their list.

- `ETERNALVR_TEST_MOVE=35,-35,80,-80,...` (whole move values, negative backward or left) sends each value
  as a constant command along the axis for one leg, then nothing for one leg; body follow's own request is
  off meanwhile. Each leg logs the command-to-speed point: `room: test move leg 24: command 40 of 127
  forward: moved 0.116 m in 1.49 s, steady 0.098 m/s, first motion 21 ms, across 0.000` (on the stop legs,
  `moved` is the coast).
- `ETERNALVR_TEST_STEPS=0.10,0.20,0.40` steps the fake head out by each size along the axis and back. Body
  follow walks the body after it, and each leg logs `room: test step leg 2: asked 0.200 m, gap 0.184 ->
  0.014 m, within 2 cm at 376 ms; body moved 0.171 m (peak 0.171, across 0.000) in 1.99 s, 90% at never;
  anchor took 0.171 m; largest command axis 85`. `gap` is where the head was from the body at the leg's
  start and end, `within 2 cm at` the criterion, `peak` above `body moved` is overshoot, `anchor took`
  must equal `body moved` when nothing blocked.

`ETERNALVR_TEST_STEP_AXIS=right` strafes instead. On the rig (`tmp-vr\rs\rsrun.ps1` stages the layer and
launches the simulator in stereo; stop every run with `tools\rig\stop.ps1 -Run <run folder>`):

```
& <workspace>\tmp-vr\rs\rsrun.ps1 -Name bf7 -Tree <worktree> -WindowSize 1280x1400 -WatchSeconds 20 `
    -ExtraEnv @('ETERNALVR_AIM=hand','ETERNALVR_RENDER_SIZE=1280x1400','ETERNALVR_TEST_RECENTER=3',
                'ETERNALVR_TEST_STEPS=0.10,0.20,0.10,0.20,0.05,0.40','ETERNALVR_TEST_STEP_SECONDS=2')
```

If the body stops short or overshoots, `_COAST` is the knob (larger stops earlier); if a tier moves at a
different speed on another build, `_WALK` and `_CREEP` move the values (a `TEST_MOVE` run shows the tiers).

## Settings

| Variable | Default | Meaning |
|---|---|---|
| `ETERNALVR_POSTURE` | auto | `seated` / `standing` override the detected posture (and turn re-detection off) |
| `ETERNALVR_HEIGHT` | slayer | `real`: the player's own eye height (needs a floor space) |
| `ETERNALVR_AUTO_ANCHOR` | 1 | 0: no automatic anchor; only recenters anchor |
| `ETERNALVR_RECENTER_HOLD` | 2.0 | seconds both sticks are held to recenter (0.3–5); 0 turns the chord off |
| `ETERNALVR_LEAN_CAP` | 0.60 | metres (0.05–2) |
| `ETERNALVR_HEAD_COLLISION` | 1 | 0: no head sweep (cap only, no fade) |
| `ETERNALVR_HEAD_FADE` | 1 | 0: no fade for the head in geometry or past the lean cap (the launcher's "Fade in walls", Play tab) and no blink over a re-anchor; a glory kill's fade still shows |
| `ETERNALVR_IPD` | unset | millimetres (50–80) the game renders with; unset or 0: the runtime's |
| `ETERNALVR_BODY_FOLLOW` | 1 | 0: no body follow (Body follow above) |
| `ETERNALVR_BODY_FOLLOW_DEADZONE` | 0.04 | metres (0.01–0.5) of gap before the body follows; it stops within 40 % of it |
| `ETERNALVR_BODY_FOLLOW_WALK` | 85 | the move value (30–127) of the game's walk tier |
| `ETERNALVR_BODY_FOLLOW_CREEP` | 60 | the move value (20–127) of its creep tier |
| `ETERNALVR_BODY_FOLLOW_COAST` | 0.1 | seconds (0–0.5): the body coasts its speed times this after a command |
| `ETERNALVR_BODY_FOLLOW_SPEED` | 3 | m/s (0.5–5): the fastest follow moves the body (caps what the anchor takes per frame) |
| `ETERNALVR_TEST_MOVE` | unset | test: move values `a,b,...` (whole, ±1–127) sent as constant commands, each followed by a stop leg |
| `ETERNALVR_TEST_STEPS` | unset | test: step sizes in metres, `a,b,...` (0.005–1 each): the fake head steps out and back (precision test) |
| `ETERNALVR_TEST_STEP_SECONDS` | 3 | test: seconds each leg of a step or move is held (0.5–60) |
| `ETERNALVR_TEST_STEP_AXIS` | forward | test: `forward` or `right` |
| `ETERNALVR_TEST_HEAD_OFFSET` | unset | test: `x,y,z[,period]` metres added to the head in room space (+x right, +y up, -z forward); with a period it eases 0 -> offset -> 0 over that many seconds. Its height also counts as the head's height for anchoring and posture re-detection (a scripted stand-up or sit-down) |
| `ETERNALVR_TEST_RECENTER` | unset | test: one user recenter this many seconds after the first head pose |

The launcher (`launcher/`) has Posture (Auto / Seated / Standing), Eye height (Slayer / Real), IPD (0 = the
headset's) and the recenter hold (on / off) next to World scale, and passes them as the variables above.
Problems in a value are logged (`room: ETERNALVR_...='...': ...`) and the default kept.

## Tests

- `evr_posture_tests` (`posture_tracker_tests.cpp`): sit to stand at the owner's heights (0.96 -> 1.66 m,
  after 1.0 s), stand to sit (after 1.5 s) and back, a brief crouch or rise never flips, sitting up straight
  near the top of the seated band is not standing, unknown posture or no floor reports nothing, missing
  samples, time going backwards, a reset re-references the height, bad settings fall back.
- `evr_posture_tests` (`tests/features/posture/anchor_detector_tests.cpp`): a desk headset held perfectly
  still, and one with 0.1 mm of tracking noise, for 60 s never anchor; worn-head jitter (five seeds)
  anchors between 1 and 2 s; a head still moving into place anchors only once settled; unfocused, absent
  or interrupted streams do not; time going backwards restarts; reset re-arms.
- `evr_roomscale_tests`: recenter math (heading of level, pitched and vertical heads; full and
  yaw-and-origin recenters; moves in the anchored frame; the runtime's move leaves room poses unchanged; a
  height re-anchor keeps heading and origin and puts a stood-up head back at the eye; the runtime's
  recenter after standing up re-anchors the height, with or without a known move), the lean cap (0.8 m to
  0.60 m in the same direction; a rise, within the allowance or beyond it, is never lean), the height
  clamps and Real height with world
  scale, the fade (black within 150 ms for a head put into a wall and for one walked in at 1 m/s; partial
  fade; clears gradually; with the head fade off a glory kill's fade still shows), the first contact as the shot start while blocked, the long press, the settings parser and the
  eye separation. Body follow (`body_follow_tests.cpp`, `follow_test_steps_tests.cpp`): the controller
  (deadzone and hysteresis, walk, creep and coast by the stop distance), the displacement in room axes
  through a turned body, the anchor shift, 10, 20 and 40 cm steps against a model of the game's tiers
  (within 2 cm in 0.5 s, 0.7 s for 40 cm, no overshoot, the anchor taking what the body moved, no creep
  after), a steady 1.2 m/s walk tracked within 0.35 m, a wall leaving the gap as head offset, the absorb
  rules (both signs while commanded, nothing otherwise, capped), every block with its resume delay, the
  teleport rejection, and the precision-test phases, the command test and the probe. Driven views
  (`driven_offset_tests.cpp`): a standing glory kill puts the camera on the game's eye without a jump,
  the head moves it from there, the offset comes back smoothly after, a new episode during the ease back,
  a long stall, bad input.
- `evr_game_eternal_tests`: the default maps bind `left.menu.tap` to pause (and no longer bind its hold),
  and every bound action reaches the game or the layer. `evr_input_tests`: the stick chord (a single click
  on its first frame, both sticks together, a second stick inside the window, a quick second click sent
  late, the hold, a release) and through the mapper (no melee or Crucible from the chord, Recenter after
  the hold time).
- Launcher: the new settings round-trip, default in old files, clamp, and reach the layer's variables.

## Live test plan (rig)

With OpenXR-Simulator (`tools\rig\launch-ht.ps1 -XrRuntimeJson ...`): its head stands still 1.7 m above
`LOCAL`'s origin, so it never looks worn and never anchors by itself (as designed). A test recenter makes
that head the anchor, after which head position can stay on (earlier simulator runs needed
`ETERNALVR_HEAD_POSITION=0`). The simulator's own WASD / Q / E moves the head when its preview window has
focus; the scripted offset needs no focus.

1. Anchor and height: `-ExtraEnv ETERNALVR_TEST_RECENTER=3`. Expect `room: floor space ...`, then `room: user
   recenter: anchored at (0.000 1.700 0.000) LOCAL ... posture ...` about 3 s after the first head pose; the
   view at the game's normal eye height (compare with a flat screenshot). Without the variable, expect no
   anchor line in 60 s (the still head counts as not worn).
2. Lean cap (steps 2 to 4 with `ETERNALVR_BODY_FOLLOW=0`, or the body walks to the fake head): add
   `ETERNALVR_TEST_HEAD_OFFSET=0.8,0,0,6`. Expect `room: lean 0.800 m clamped to 0.600 m` (the
   criterion: 0.60 ± 0.01 m), and the view swinging 0.6 m to the right and back every 6 s.
3. Wall fade: walk the player up to a wall (scripted input `left.stick = 0, 1` until blocked, then 0), then
   `ETERNALVR_TEST_HEAD_OFFSET=0,0,-0.6,4` (a forward lean into it). Expect `room: first head sweep: fraction
   1.000, player spawn id 0x...` in the open, `room: head in geometry (... m deep); shots start from the
   first contact (...)`, `room: first fade frame`, and `room: fade full N ms after the head entered
   geometry` with N ≤ 150 once the lean is fast enough (the ease-in makes the first contacts slow; a
   constant offset, `0,0,-0.6`, set while standing at the wall, is the step case). The preview shows the
   view going black and clearing as the lean eases back.
4. Shots: during step 3, fire (scripted `right.trigger = 1`) with hand aim; with `ETERNALVR_CONTROLLERS_TRACE=1`
   the shot origin stays at the first contact plus the hand, not inside the wall.
5. Recenter chord: scripted `left.click = 1` and `right.click = 1` for 2.5 s, then 0. Expect `room: recenter
   binding held for 2.00 s`, `room: user recenter: ...`, and no `action melee` or `action crucible`; a short
   `right.click = 1` then 0 still melees, and `left.menu = 1` then 0 still pauses.
6. Runtime recenter: needs a headset (the simulator is not expected to send the event): the runtime's recenter shows
   `xr: LOCAL change pending` and `room: runtime recenter: posture ..., head ... above the floor; anchored at
   ...`, with the height re-anchored.
6b. Posture re-detection (the simulator's head stands 1.7 m above the floor, so detection starts standing):
   `ETERNALVR_TEST_RECENTER=3`, `ETERNALVR_TEST_HEAD_OFFSET=0,-0.7,0,24` (the head sinks to 1.0 m and rises
   again over 24 s). Expect `posture change: standing -> seated`, then `seated -> standing`, each with the
   height re-anchor line, body follow's `first frame blocked by seated` between them, and no `past the lean
   cap` fade.
7. Settings: `ETERNALVR_POSTURE=seated` shows `posture seated (override)`; `ETERNALVR_IPD=70` in stereo moves
   the logged eye world offsets to ±35 mm; `ETERNALVR_HEAD_COLLISION=0` shows no sweep lines and no fade.
8. Body follow: the precision test (Body follow above) in the open, along both axes; then, with steps
   running, scripted `left.stick = 0, 1` for 1 s and `right.primary = 1` (jump) now and then (expect
   `first frame blocked by stick`, `jump or dash`, `airborne`, and `anchor took` only the follow share
   while the stick walks); a fall or respawn shows `the origin jumped ... (teleport N); not followed`. Done
   in bf7 to bf9 (results in Body follow); into a wall is still to run.

In the headset (owner): the ROADMAP M4 posture criteria (10 of 10 seated and standing fresh starts, eye
height within 5 cm, a headset left on a desk for 60 s never anchors, a long press after standing up
corrects height) and the M5 room-scale criterion (0.8 m lean, a wall, shots).

## Live results (2026-09-26, rig, OpenXR-Simulator, stereo, e1m2, hand aim)

Runs `<workspace>\runs\20260926-0623*-rs1` to `-0630*-rs4`, layer logs in
`<workspace>\tmp-vr\rs<N>-logs`. Every run: `room: head sweep through the collision query at RVA
0x4FC0B0 (map instance global RVA 0x45F7370)`, `room: floor space STAGE`, `room: fade layer ready`, `room:
first head sweep: fraction 1.000, player spawn id 0x1002A27`, thousands of sweeps (8110 in 100 s) and no
fault.

| # | Check | Result |
|---|---|---|
| 1 | Anchor, posture, eye height (`ETERNALVR_TEST_RECENTER=3`) | Pass: `user recenter: anchored at (0.000 1.700 0.000) LOCAL ... head 1.700000 m above the floor; posture standing (detected); eye height 1.657 unit(s) = 1.657 m at world scale 1.00` |
| 2 | No anchor from a still head (no test recenter, 90 s) | Pass: no `anchored at` line; `not anchored, posture unknown` in every 10 s summary |
| 3 | Lean cap (`0.8,0,0,6`; `0.5,0,-0.6,4`) | Pass: `lean 0.601 m clamped to 0.600 m`, 16 clamps in 100 s, `lean clamp 1 ended: peak lean 0.781 m held at 0.600 m`, 10 s summaries `lean max 0.800 m` |
| 4 | Wall fade (walk to a wall with the head 0.6 m ahead; eased leans into it) | Pass: `head in geometry`, `first fade frame`, `fade full 113 ms after the head entered geometry` (walking in); with a 1.5 s eased lean 31 entries, full after 100 to 122 ms from the first touch and 34 to 60 ms after 0.10 m deep. A slow 4 s ease took 178 to 189 ms from the first touch: the time from touch is set by how fast the head moves (0.10 m of depth), the fade's own delay stays under 60 ms |
| 5 | Shots while the head is in the wall | Fixed, then pass: the first build kept the previous frame's clear offset, which put the shot start 0.6 m ahead of a body that had walked up to the wall (inside it). Now the first contact: `shots start at the first contact (-0.31 -0.03 0.00)`, hand start 0.3 m from the eye instead of 0.6 m |
| 6 | Recenter binding (scripted `left.menu = 1` for 1.8 s, then two 0.12 s presses) | Pass: `action recenter`, `recenter binding held for 1.00 s`, `user recenter: ...`, no pause; each short press `action pause` |
| 7 | Overrides | Pass: `ETERNALVR_POSTURE=seated` gives `posture seated (override)`; `ETERNALVR_IPD=70` moves the eye world offsets to ±0.0350 (runtime eyes ±0.0320) |

The fade layer is drawn by the compositor, so the game-window captures show the view without it (the view
inside the pillar, half through it). The simulator's own window shows the composited result: after the merge
(main, run `20260926-064404-rs5`, UI layer on) walking up to a wall with the head 0.6 m ahead gave `fade full
102 ms after the head entered geometry` and a fully black view, HUD quad and reticle included, since the fade
is the top layer (`tmp-vr\launcher-layer-logs\rs5-wall.png`).

**Through the launcher** (main after the merge, Release launcher, layer staged in `tmp-vr\launcher-layer`,
OpenXR-Simulator, `launcher.ini` with `posture = seated`, `ipd_mm = 66.0`, `recenter_hold = 1`): the launcher
passed `ETERNALVR_POSTURE=seated`, `ETERNALVR_HEIGHT=slayer`, `ETERNALVR_RECENTER_HOLD=1.0` and
`ETERNALVR_IPD=66.0`; the layer logged `posture seated ... IPD 66.000000 mm`, eye world offsets ±0.0330, the
recenter and `lean clamp 1 ended: peak lean 0.800 m held at 0.600 m` (the test variables set in the launcher's
environment). Evidence `<workspace>\tmp-launcher\rs-live\L-rs`.

## Known gaps

- Checked with the simulator only: its head never moves by itself, so real head motion, the runtime's
  recenter event and a real floor height are headset items.
- `XR_EXT_user_presence` is not used: without a presence signal the worn test is the only guard, as T-045
  allows.
- The eye height used for Real height and the vertical clamps is the game's standing value (1.65735); the
  game's crouch and the animated camera are not read yet. `docs/rig-findings/collision-query.md` section 2
  has how to read the effective eye height at the hook (`vieworg.z - origin.z`) for a later logging pass.
- Body follow is checked on the simulator only (scripted head steps); real head motion, how the pulsed walk
  feels and combat (dash, Meathook, stairs) are headset items.
- The fade covers the projection only; the HUD, when it moves to its own layer (M6), must be drawn under
  the fade quad or kept visible on purpose (R10: "a thin outline or the HUD stays visible").
