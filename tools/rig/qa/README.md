# Rig QA suite

Simulator runs of a layer build with scripted input, screenshots and layer-log assertions, plus the
launcher's end-to-end suite. One report per run.

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\rig\qa\qa-suite.ps1 `
    [-LayerSrc <folder with EternalVR.dll, VK_LAYER_ETERNALVR.json, openxr_loader.dll>] `
    [-Out <dir>] [-Only baseline,token-pickup] [-LauncherBin <launcher>\src\EternalVR.Launcher\bin\Release] `
    [-DlssDll <newer nvngx_dlss.dll>]
```

`-DlssDll` runs every game scenario on the launcher's newer-DLSS route (`ETERNALVR_DLSS_DLL`, preset K), which
installs two more inline hooks; run the suite once without and once with it before a release or a headset
package. Every game scenario also fails on a `no free hook slot` or `no free inline hook slot` line.

Defaults: `-LayerSrc` is `build\windows-msvc\src\vkcore` of this tree, `-Out` is
`E:\Code\resources\Testing\QA-<yyyyMMdd-HHmm>`. Exit code 0 all passed, 1 a scenario failed, 2 refused or
stopped.

Needs: Steam running, the virtual display driver, OpenXR-Simulator at
`<workspace>\tools\bin\openxr-simulator\openxr_simulator_rig.json`, the 100% save at
`<workspace>\tmp-release\save100\resigned-jason` (or `EVR_QA_SAVE_SOURCE`: the save is signed for one Steam
account, so another PC's account needs its own re-signed copy). procdump (`tools\bin\procdump`) is optional.

## Safety

- `<workspace>\tmp-vr\STOP-RIG` ends the suite before the next scenario and during any wait
  (the running game is stopped; the Steam-Cloud files are left for the next cleanup, see below). It is not
  seen while `launch-ht.ps1` runs (up to about two minutes).
- A running `DOOMEternalx64vk.exe` refuses a game scenario.
- After every game run, also on failure: `stop.ps1 -Run <run>`: the local settings restored, the Steam-Cloud
  files (`PROFILE\profile.bin`, the save slot the map start autosaves into) kept as the game wrote them, so
  Steam's record matches them. Before the first game run the suite holds a cloud restore of the settings
  as they are then (`cleanup.ps1 -HoldCloudRestore`, `tools\rig\README.md`, Steam Cloud; one an earlier
  suite left pending is finished first, so its runs are never taken as the start), and `qa.log` names the
  folder and the command that finishes it. After the last game scenario the suite finishes it by name: the
  cloud files back, the one resync launch for the suite; the report's Suite section says so, and that the
  100% save is in `GAME-AUTOSAVE0` then. A slot that holds something else before the first game run gets
  the 100% save copied in first (touched, `game_duration.dat` 958137 bytes). An error or Ctrl+C finishes it
  too; a killed suite leaves it pending and the next cleanup or game start of anyone finishes it. On
  STOP-RIG it is left pending (no resync launch while the owner uses the PC) and the Suite section names the
  command (`tools\rig\cleanup.ps1 -Run <folder>`), as it does when the restore fails or is refused. A
  failed or left restore makes the exit code 2. The hold belongs to the suite's PowerShell process, so run the
  suite as its own process (`powershell -File`, as `tools/unelevated.sh` does), not dot-sourced into a shell
  that stays open: until that process exits, only it can finish the folder. A second suite refuses to start
  while another suite's hold is live.
- What a run leaves for the next one is checked from its run folder's `config-before\` and `config-after\`
  copies of `profile.bin` (`qa-cloud.ps1`: AES-GCM with the account's key, then field by field). The fields
  the game rewrites in every run carry over: its lifetime statistics, the telemetry queue, the online
  progression and item store, `r_antialiasing` (the per-eye TAA's held 0, which every stereo run saves) and
  `r_raytracedReflectionsTemporalUpscaleQuality` (the game's own 1). With either entry carried the game
  runs with the same values as from the rig profile's own (rig, 2026-10-17: `r_antialiasing` 1 until the
  layer's hold writes `1 -> 0`, the reflections 1, in every scenario of both suites). A text field outside
  them that changed is a held value saved into the player's profile: `profile.bin: no held value saved`
  fails for that scenario (`docs/rig-findings/profile-leak.md`), and so does a profile that cannot be read
  or that the run added or removed (nothing shows it holds no leak). The telemetry's `count`, `start`,
  `end` and `type` carry only with the values the telemetry holds there (a number, a UTC time stamp, an
  event name): the parse finds fields by their bytes, not by the profile's nesting, so their section is not
  known. Any other field (a tutorial marked as seen: `token-pickup` and `action-aim` mark one), any of the
  above or a stale Steam record sends the cloud files back before the next scenario (the held restore
  finished, then held again), so it starts as the first one did. The save slot
  carries the previous run's autosave: a `+map` run starts from the developer loadout at the map's own
  difficulty whatever the slot holds, and only the main menu's save list reads it. A view_skipShakes leak
  is only seen with a profile that does not already hold the held 1 (the rig's profile holds 1 from the
  sessions before the profile hooks).

## What each run does

A game scenario stages the build into `tmp-vr\rs\qa-<name>` (layer log in `qa-<name>-logs`), writes the
scripted input file `qa-<name>-input.txt` (`ETERNALVR_TEST_INPUT`, docs/VR_CONTROLLERS.md), starts Route S
stereo through `launch-ht.ps1` (simulator, 1280x720 window and mirror on the virtual display), waits up to
200 s for `the schedule's clock starts` or `aim: head aim on` in the layer log, runs its timeline, stops,
restores the save, then checks the layer log (the stage's logs, else the run folder's). A scenario's `Prepare`
step writes its own files into its folder before the launch (a player's controller data) and returns the
`NAME=VALUE` pairs that point the run at them.

## Menu-started scenarios

A scenario with `MenuMission` starts the way a player does instead of with `+map`: the game starts at the title
screen (`launch-ht.ps1 -Map ''`), and `qa-menu.ps1` takes it through the game's own menus with the scripted right
hand (held at the head, `right.position = 0, 0, 0`) pointing at the menu panel and the trigger pulled
(docs/VR_MENUS.md). The path (rig, build 25216728, the 1280 x 720 frame): the title screen (main menu screen 2; a
click connects to Bethesda.net, 6 to 10 s, then the main menu, screen 3), Campaign (the save slots, screen 13), Game
Slot `MenuSlot` (1 by default; the slot's own menu on the same screen), then either Mission Select (screen 14) and the
mission's row (`MenuMission` = a map folder of `$script:QaMenuMissions`, `e1m1_intro` to `e3m4_boss`, all 13 on one
page), or Continue Game (`MenuMission = 'continue'`, with the map it must load in `MenuMap` and what the slot must
hold in `MenuSaveNote`). There is no checkpoint choice in these menus: Continue Game loads the slot's own save. With
the 100% save the whole path to play took 17 to 20 s on the rig, the map load about 4 s, the player in the map 4 s
later; `menu-mission` takes about 100 s in all, and its run's profile changes put the cloud files back before the
next scenario (about 30 s more).

**The network.** The title's click needs Bethesda.net: the connection and the account's sign-in. When the title took
the click (`menu: button down`) and the main menu never came, the scenario is skipped as the environment's, not
failed: `scenario <name>: SKIP (environment: the title screen took the click and the main menu never came ...)`, the
note with what the log showed (the screens, the cursor's kinds, a popup), `SKIP (environment)` in the report's row,
and the summary `QA: N of N scenarios passed, 1 skipped (environment)`: the count leaves the skip out and the exit
code stays 0. A skip whose run failed a check of its own (the stop, the cloud files, the guard) fails. Any other
failure of the path (no title cursor, the click not sent, a later step) fails the scenario. `qa-suite.ps1
-TestMenuTitleTimeout <s>` gives the title step that many seconds and one try, short of the connection, to see the
skip work.

**The steps.** Each step takes one mark in the layer log before its first click; its answer is the first line after
that mark that matches its own pattern: the main menu's next screen (`mp guard: main menu screen N (next M)`, which
the multiplayer guard's poll logs on every change), the map load (`mp guard: map load '<map>'`, which must name the
map asked for: `menu: <target> loads <map>`), or, for a slot, the click itself (`menu: button down`: the slot menu has
no screen of its own and the line does not say which button the game took; the next step's screen 14 or map load
shows it). A click is made only while the main menu shows the screen the step starts from (the title 2, Campaign 3, a
slot and Mission Select and Continue Game 13, a mission 14) and its answer has not come yet, both checked again right
before the trigger, so a late answer to the click before is never taken for a screen that waits for another click.
The aim for an item's pixel comes from the newest panel line (`menu: the game shows its cursor (...): panel W x H m at
(x, y, z)`: its width over the 1280 x 720 frame, its distance and its side), read for each step and before each retry.
The click goes once the layer read the input that points at the item (`test input '...' read`) and the router's closed
loop had 0.5 s to bring the game's cursor there; the log is read every half second. A step without its answer in its
time (the title 15 s, Campaign and Mission Select 8 s, a slot 3 s, the map load 15 s) clicks again: the title 4 tries
in all (its first click went unanswered for 30 s in 2 of 15 starts on the rig, and a second one was taken at once),
Campaign 3, a mission row 2, a slot, Mission Select and Continue Game 1 (the save slots share the slot menu's rows:
a second click there would open another slot's save). Each failed try leaves a shot of the game window
(`<scenario>-menu-<step>-try<N>-game.png`, PrintWindow: the virtual display's shot can come out black), and a failed
step's line in the report says what the log showed since the step began. Then the `in the map` wait is skipped. Each
step is a line in the report (`menu: <step>` with the seconds it took and the line it saw). After the load the input
file is the scenario's `Input` again; the in-map wait, the timeline and the checks are as for any game scenario.

**The saves.** Mission Select needs the 100% save in the first slot (every mission open). A save copied into a slot
that the profile (`PROFILE\profile.bin`) lists with another save, or one signed for another Steam account, is
`Corrupt save data detected` on the save slots screen, and the path fails at its next step. So the menu scenarios
come first in the suite, while the first slot still holds the 100% save the suite started from (with the profile that
lists it), and before one that follows an earlier game run's autosave there (a rerun after a late start included) the
suite puts the cloud files back as it started (`Invoke-QaCloudBetween`). The suite's own copy before its first game run
must be the save signed for the rig's Steam account: `EVR_QA_SAVE_SOURCE`, else a one-line file beside the rig's other
local settings naming the save folder (`qa-save-source.txt`, as `qa-save-target.txt` names the slot), else the
default copy. The variable does not reach a suite started through `tools/unelevated.sh` (a scheduled
task's environment); the file does, or the variable set inside that command (`env EVR_QA_SAVE_SOURCE=... powershell
...`).

A scenario with `OnlyByName = $true` runs only when `-Only` names it: a measurement, not a check for every merge.

## Scenarios

| Name | Proves |
|---|---|
| menu-mission | `qa-scenarios-menu.ps1`, first in the suite: Cultist Base (`e1m3_cult`) from Mission Select in Game Slot 1, started at the title screen: every menu step within its time, the map loaded by the menus (its `map load` line after Mission Select's screen 14, no `+map` in `launch.txt`), `aim: head aim on` after that load, eye views in stereo; 15 s into play shots (`-in-map-xr-both.png`, `-in-map-game.png`), then 11 s more: the last 10 s `xr:` line's head-tracked count grew and its on-the-screen count by no more than a twentieth of that (no cinema-screen fallback), the frames table's last 3 s (100 rows or more) all head-tracked (`view` not 0) with the HUD quad shown (`ui_shown` 1), no `ERROR`, `exception` or `crash`. About 100 s on the rig, and the run's profile changes put the cloud files back before the next scenario (about 30 s more); needs Bethesda.net (an environment skip without it, see above) |
| menu-crystal, menu-crystal-play, menu-crystal-behind, menu-crystal-behind-play | `qa-scenarios-menu.ps1`, `-Only` only (OnlyByName): Game Slot 2's Continue Game, which must hold a save of Exultia (`e1m2_battle`) from before its Sentinel Crystal it depends on the test PC's own slot 2 (the scenario takes the slot as it is, see above; another map's load fails it at once, `Game Slot 2 does not hold the pre-crystal save ...`, and no upgrade menu fails it too), under hand aim with `ETERNALVR_TEST_CAMERA_TRACE=1`: teleported onto the crystal (`pickups_progress_argent_cell_1_1318987582_e1m2`), which is then behind the player. menu-crystal (Pickup animations Still view) and menu-crystal-play (`ETERNALVR_PICKUP_CAMERA=play`) turn the player with snap turns of 90 degrees (`ETERNALVR_TURN=snap`, `ETERNALVR_SNAP_DEGREES=90`) until Use with the hand ahead (30 or 45 down) opens the upgrade menu, so the head looks at the crystal as a player's does; the -behind pair points only the hand at it (six hand aims; the simulator's head stays still, so it looks 180 degrees away). The upgrade menu (`menu: the game shows its cursor (a popup over the game`), the trigger held 3.5 s on Quickdraw Belch's right end (twice at most) until the menu closes, `controllers: sync 'interact/argent_cell/use_sync'`, and the last 10 s xr window after the sync head-tracked. From the trace lines (`camera: trace` and `camera: sync trace`, which carry the body yaw, the head, the yaw the game's aim holds and the menu and sync flags): no turn of the written view over 5 degrees in one game frame from the menu to 2 s after the sync, the heading after the sync within 1 degree of the menu's; menu-crystal and menu-crystal-play also the view facing the game camera's heading through the sync (median within 3 degrees, all within 15: the crush is drawn in front of that camera); the -play pair `pickup: camera turn N starts ...; the view keeps the body yaw`. Notes: the aim that opened the menu, the sync's length to the game's `SetViewAngles` (RVA 0xB4567C) and the forced views in it, the trace's numbers. Shots of the upgrade menu, the sync (two) and after (`-xr-both.png`, `-game.png`). About 120 s each |
| menu-crystal-turns, menu-crystal-sway | `qa-scenarios-menu.ps1`, `-Only` only: measurements for Still view's hold, as menu-crystal (Still): during the sync the hand's yaw swept 30 degrees each way every 0.25 s (`right.sweep`) and then a snap turn each way (-turns), or the head swaying 10 degrees each way every 0.2 s throughout (`ETERNALVR_TEST_HEAD_SWAY=10,0,0.2`, -sway). Notes: in how many game views of the sync the player's view angles changed and by how much at most, and how many holds of the camera's heading started after the sync began; checks only that the sync was traced |
| baseline | e1m2 stereo for 40 s: `stereo: eye views` and `rates: game` logged, no `ERROR`, `exception` or `crash` (case-sensitive; `view-to-hand error` is normal text) |
| foveation | `ETERNALVR_FOVEATION=balanced`: the eyes get a VRS pattern, the 8192x8192 target is logged as not in the eye's space and never gets an eye pattern |
| foveation-eyes | `ETERNALVR_VRS_TEST=eyetest` (eye L's left half and eye R's right half at 4x4) with eye captures every 81 pairs for at least 40 s and then until 32 pairs can be measured (past the 3 the measure leaves out) and a `vrs: render pass frames:` line is logged, at most 75 s (a slower hour on the rig no longer comes up short), the suite's TAA: in the captures (`qa-blockiness.ps1`) eye L's left half and eye R's right half have a block-edge ratio of 1.20 or more, the other halves 1.10 or less (a script error is reported as `measuring failed: ...`, apart from no captures); the last `vrs: render pass frames:` line has no parity break and no recording contradicted later, and the passes kept at full rate for want of a frame are 5% or less between the last two such lines (in the map; 0% on the rig, those all before it); with one line only, the share is a note |
| foveation-eyes-dlss | The same with DLSS Quality per eye (`ETERNALVR_STEREO_DLSS=1`, `_QUALITY=quality`, `+r_antialiasing 2`; the newer DLL with `-DlssDll`): eye R's DLSS feature made, the render size logged, the block measured at the render size (6 captured pixels at Quality) |
| foveation-eyes-reload | `qa-scenarios-foveation.ps1`: the eye test with DLSS Quality per eye as foveation-eyes-dlss, at e1m3 `cp_03_shoot_gate` (e1m2's start opens a menu after a load), with the debug schedule loading the same map and checkpoint again at 112 s on the schedule's clock, 2 s after the pause menu opens (the game's Load Checkpoint is chosen from that menu and loads the map again): `mp guard: map load` of the map exactly twice, and the halves measured separately over the captures written before the menu and over those written from 8 s after the second load's first frames (`eye tags start at`) for at least 45 s and then until that stretch has its 32 pairs (at most 75 s), each with the foveation-eyes thresholds and 32 pairs or more; then the render pass frames as foveation-eyes. Positive control: the same captures with eye L's and eye R's files swapped fail every half |
| seated-jump | `ETERNALVR_HANDS_JUMP=1`, `ETERNALVR_POSTURE=seated`: a throw to 0.10 m over the head does not jump, one to 0.25 m does: exactly one `gesture: hands-up jump (seated height)` |
| arm-pose | The right hand at (0.35, -0.15, -0.45), aim yaw 45: screenshot `arm-pose-arm-pose.png` to compare with the references by eye (no image check yet) |
| token-pickup | e1m3 `cp_03_shoot_gate`, debug schedule into the token's use trigger, Use under hand aim at pitch -15, -30, 0 until a sync starts: `controllers: sync 'interact/preator_suit_token...' (not a kill)`, no `glory: kill`; screenshot 4 s after |
| action-aim | e1m3 `cp_03_shoot_gate` as token-pickup, hand aim with `ETERNALVR_MELEE_AIM=offhand` and `ETERNALVR_EQUIPMENT_AIM=head`, the weapon hand 90 degrees to the left: Use with the off hand pointing away, then at the token (pitch -15); the token is picked up (`controllers: sync 'interact/preator_suit_token`; its Use is a trigger box, so this does not test the aim); the melee press is held back until the view took the off hand's target and reaches the game with the view within 3 degrees of the off hand (`action aim: melee press, game frame N ...`), the view goes back to the weapon hand, no press sent without its target; the equipment launch hook installed and a frag turned onto the head (`equipment launch (slot N): ... along the head, from the launcher at the head`) with the view left on the weapon hand; a Flame Belch shot recognised as the Belch's (`action aim: a Flame Belch shot while flame_belch held`); the shoulder launcher hook installed and the launcher drawn at the head (`shoulder launcher (slot 5, ...): ... at the head, along the head`) |
| thumbrest-pick | `qa-scenarios-wheel.ps1`, window 1 s: the left thumb rest, then the turn stick up within the window: `thumb-rest wheel: armed: the right stick picks (left thumb rest, edge`, the game's wheel held pointing up (`action weapon_wheel`, the wheel cursor moving), then right, let go (`the game's wheel let go pointing right`, `weapon wheel released after`); no quick switch or chainsaw; the held item changes after the pick (which weapon depends on the game's wheel layout) |
| thumbrest-resting-turns | The default window, the left thumb resting from the start: the turn stick turns (`trace: mapper turn` with `ETERNALVR_CONTROLLERS_TRACE=1`), nothing arms, no `weapon_wheel` |
| thumbrest-flick | A push to 0.4 (out of the centre, short of a direction) is armed then cancelled: no `weapon_wheel`, `quick_switch` or `chainsaw` |
| thumbrest-window-expiry | The default window: the left thumb rests, 0.9 s later a turn-stick flick turns (`trace: mapper turn`); nothing arms |
| thumbrest-cancel-then-flick | Window 1 s: a push to 0.4 is cancelled, then a flick with the thumb still resting turns; exactly one `armed` line, no `weapon_wheel` |
| thumbrest-quick-pick | The turn stick right for about 0.2 s, then let go: the wheel held pointing right, let go after the hold (`the game opens its wheel ... held at least`), no quick switch, the held item changes |
| thumbrest-extreme | `ETERNALVR_THUMBREST_WHEEL=extreme`: the turn stick opens the wheel with no thumb resting (`armed: the turn stick picks weapons; the left thumb rest gives turning back`), and turns with the left thumb on its rest |
| thumbrest-slots | `ETERNALVR_THUMBREST_PICK=slots`: the turn stick held right picks `weapon_slot_2` on letting go (the game's wheel layout); no `weapon_wheel` |
| thumbrest-menu | The pause menu up (`action pause`, then `gameplay input back on`): a resting thumb and a pushed stick arm nothing |
| thumbrest-slowdown | `ETERNALVR_THUMBREST_SLOWDOWN=0`: `weaponWheel_slowTimeScale <value> -> 1 while the wheel is open`, then `back to <value>, the game's own value (the wheel closed)` with the same value |
| wheel-centre | The weapon wheel held open with the right stick down for 2 s (panel HUD), GUI captures every 10 (`<stage>-ui`): `forced view starts (weapon wheel`, `ui: weapon wheel straight ahead (panel HUD): GUI rect (x, y) w x h ...` and `ui: weapon wheel back on the HUD after`; the last GUI capture before the wheel and the first with it (more than a quarter of the logged rectangle covered) differ only inside that rectangle and over at least 70% of its width and height, so a game that draws the wheel anywhere else fails; no `where the game drew it was not measured`; an in-headset capture (left Menu, then the left trigger) with the wheel open names `weapon wheel quad straight ahead: GUI rect` in its text and, when it saved the GUI target (the periodic GUI capture's writer is often busy), writes `-HUD.png`; shots of the composed frame with the wheel up and after |
| hud-front-wrist | `qa-scenarios-hud.ps1`: the wrist HUD with the HUD in front kept (`ETERNALVR_HUD=wrist`, `ETERNALVR_HUD_FRONT=1`) at e1m3 `cp_03_shoot_gate`, GUI captures every 10: `ui: HUD wrist (corner blocks on the off hand's wrist), in front on;`, the cuts line `HUD cuts the corner blocks where the game drew them`, no `equipment are hidden`; an in-headset capture's text shows the head-locked HUD in `1 piece(s)` and no `cut for the hand HUD`; XR shot `both` |
| hud-hidden | The HUD in front off with no hand HUD (`ETERNALVR_HUD=panel`, `ETERNALVR_HUD_FRONT=0`) at the same checkpoint: `ui: HUD panel, in front off (health, armor, ammo and equipment are hidden);`, the cuts line, no `ui: HUD marker swapchain` (the markers stay in the GUI image on the cut panel); an in-headset capture's text shows the head-locked HUD in 2 or more pieces (cut round the blocks); XR shot `hidden` |
| hud-stall | `qa-scenarios-hud.ps1`: e1m3 `cp_03_shoot_gate` with `ETERNALVR_TEST_HANG=700ms@20` (one present in play held 700 ms, 20 s into the map, as a checkpoint save holds the game's thread): `holding this present 700 ms` and a `stall:` line; in the frames table (`eternalvr-frames-<pid>.csv`) the last gap of 400 ms or more in the game's images is 600 ms or more, repeats the image before it (same `view`, same `head_q*`), has `ui_shown` 1 in every frame and ends on a head-tracked image (`view` not 0); the `ui: a <N> ms gap in the game's images` line counts no frame without the GUI quad (`left out of 0`). A simulator burst of 200 composed frames from half a second before the hold goes to the scenario's `burst` folder (a note, not a check). Positive control: the build before the HUD's stall hold fails the `ui_shown` and gap-line checks (the quad gone 250 ms into the gap) |
| hand-hud-large | The wrist HUD (`ETERNALVR_HUD=wrist`) at e1m3 `cp_03_shoot_gate` (the extra lives row and the equipment row on the HUD), GUI captures every 10 (`<stage>-ui`): `held at run time: ... swf_anchorProfile 2` (the game's HUD Scale at Large in VR; the profile check shows it stays out of profile.bin), `profile cvar serializer hooked at RVA`, the readback's `ui: corner blocks:` line and `ui: wrist HUD cuts the corner blocks where the game drew them: ...`; in the newest 5 GUI captures with the HUD drawn in both cuts, no drawn shape (alpha 16 or more, 8-connected) has pixels on both sides of a cut's edge, and the health block's cut reaches past 0.215 of the band's width (the Large HUD; the fixed cut made for Medium ended there); an in-headset capture names `cut for the hand HUD: (x, y) w x h (x, y) w x h` in its text |
| restbutton-tap | `qa-scenarios-binds.ps1`: the scenario writes the built-in Touch file with `"left.thumbrest.tap" = "weapon_slot_2"` into its folder (`ETERNALVR_CONTROLLER_DATA`), hold time 1 s, the wheel off; a 0.4 s touch of the left rest: `controller data '...' replaces the built-in data of its profile`, `the left thumb rest is bound (tap = weapon_slot_2)`, `thumb rest button: the left controller reports a resting thumb`, the only action line `action weapon_slot_2`, the rest button's 10 s line(s) 1 tap and 0 holds, and the heavy cannon the first held item after it |
| restbutton-unbound | The control: built-in data (no rest bound), the wheel on (edge, window 1 s), the same touch: the wheel sees it (`thumb-rest wheel: the left controller reports a resting thumb`, its 10 s line), no `thumb rest is bound` or `thumb rest button` line, nothing armed, no action line, one held item all run |
| restbutton-wheel | The bound file with the wheel on (edge, window 1 s): `the left thumb rest is bound (tap = weapon_slot_2); the thumb-rest wheel uses the right rest only`, `rest sensors on the right hand`; the 0.4 s touch presses `weapon_slot_2` once; the left rest then the turn stick within the window turns (`trace: mapper turn`) and does not arm (that 2 s touch is a hold, so still one press); the right rest then the left stick arms (`armed: the left stick picks (right thumb rest, edge`, `action weapon_wheel`); 1 tap and 1 hold counted |
| touchbtn-tap | `qa-scenarios-binds.ps1`: the scenario writes the built-in Touch file with `"right.primary_touch.tap" = "weapon_slot_2"` into its folder (`ETERNALVR_CONTROLLER_DATA`, hold time 1 s); `right.primary_touch = 1` for 0.4 s (A touched, not pressed): `1 touch binding(s) for touches bound as buttons added`, `touches bound as buttons ...: right.primary_touch.tap = weapon_slot_2`, `trace: touch sensors: right primary_touch`, `touch button: the right controller reports primary_touch`, `action weapon_slot_2` the only action line, the 10 s line counts 1 tap, the heavy cannon comes up |
| touchbtn-unbound | The control: built-in data (no touch bound), the same touch of A reaches the layer (`trace: touch sensors: right primary_touch`): no touch binding added, no touch button line, no action, the held item unchanged |
| touchbtn-press | The bound file: A touched 0.2 s, pressed 0.3 s (a jump), let go with the thumb still on it, lifted; then a plain 0.4 s touch: actions `jump` then `weapon_slot_2` only, the 10 s lines count 1 tap, 0 holds, 1 ended by its own control |
| touchbtn-face | The file binds `"right.secondary_touch.tap" = "weapon_slot_2"` (B touched): the thumb on A and B 0.2 s, A pressed 0.3 s (a jump), let go with the thumb still on both, lifted; then a plain 0.4 s touch of B: actions `jump` then `weapon_slot_2` only, the B button's 10 s lines count 1 tap, 0 holds, 1 ended by its own control (A is under the same thumb) |
| touchbtn-stick | The file binds `"left.primary_touch.tap" = "weapon_slot_2"` (X touched): the left stick pushed 0.5 s and let go, X touched 0.15 s later for 0.2 s: `action weapon_slot_2` the only action line, the X button's 10 s lines count 1 tap, 0 holds, 0 landings voided (only the control under a touch voids its landing) |
| fwdmaps-skip | `qa-scenarios-controls.ps1` (T-127): the built-in Touch file with a made-up `"left.futurebutton.tap" = "weapon_slot_3"` and `"left.thumbrest.tap" = "weapon_slot_2"`, hold time 1 s, the wheel off; a 0.4 s touch of the left rest: `controllers: <file>: [map.right] 'left.futurebutton.tap' (= 'weapon_slot_3') is not a binding this version knows; this entry is skipped` once, `controller data '...' replaces the built-in data of its profile`, no `has issues; the built-in data is used`, `the left thumb rest is bound (tap = weapon_slot_2)`, the only action line `action weapon_slot_2`, and the heavy cannon the first held item after it |
| crosshair-hold | e1m3 `cp_03_shoot_gate` under hand aim, `ETERNALVR_UI_CROSSHAIR=hold`, `ETERNALVR_RENDER_SIZE=auto` (the GUI target near-square as in a headset): `the game's crosshair under hand aim: hold`, `held at run time: ... hud_reticle_styleOverride 0 while hand aim is on`, a write `hud_reticle_styleOverride -1 -> 0 (reads 0); hand aim ...` and, before the schedule's `kill` (the death screen and the reload may stop the GUI copies), exactly one `ui: crosshair hold confirmed` with no `not confirmed` after it (every 10 s line from the first confirmed one says `confirmed now`); shots (`-xr-both.png`: the simulator's composed frame, the HUD quad as the headset gets it) at yaw 225 (map markers next to the middle of the view) and 45 (the Praetor Suit token's name), of the ability indicators after a Flame Belch and a frag, the chainsaw's NO TARGET, the Heavy Cannon scoped and firing, the pause menu, back in play, and the death fade after the schedule's `kill`; and the Precision Bolt's zoom (T-136): its `zoom look:` decl line (`hands hidden on zoom 0 -> 0, after 74 -> 0 ms, scope GUI 2 -> 0, zoomed reticle '..._zoomed' -> '...'`: the gun stays, no scope GUI, and the weapon's own reticle, which keeps the HUD under the hold) and `zoom look: scoped zoom starts` |
| crosshair-mask | The same with `ETERNALVR_UI_CROSSHAIR=mask`, the earlier behaviour and the positive control: the centre of the GUI copy cleared (whatever the game draws there cut in the shots), `... under hand aim: mask`, no `hud_reticle_styleOverride` line |
| crosshair-hold-head | The same with `hold` under head aim: `hud_reticle_styleOverride` written to 0 only between `zoom look: scoped zoom starts` and `... ends` (the Precision Bolt zoomed: no scope ring, T-136), the last start followed by an end and then by the write back (`0 -> ... and no scoped zoom`), neither log at its cap (under 20 zoom lines; no `cvars: later writes are counted, not logged`), and no `crosshair hold confirmed` (the game's crosshair stays otherwise); the Bolt's `zoom look:` decl line as in `crosshair-hold` |
| crosshair-hold-demon | e1m3 `cp_12_possession` under hand aim with `ETERNALVR_DEMON_AIM=head`: the hold's write `-1 -> 0`, then into the Revenant's pod (the weapon hand on its use proxy, Use): `controllers: piloting a demon` and the earlier value written back for the aim switch, `hud_reticle_styleOverride 0 -> -1 (reads -1); the hold's request ended (no hand aim) ...` and no `-> 0` write while piloting, with every write of the run logged (the last logged count under 13: later writes are only counted, so the piloting check would not see them); shots before and while piloting |
| hud-markers | `qa-scenarios-markers.ps1` (T-145): e1m3 `cp_03_shoot_gate` under hand aim, render size `auto` (a near-square GUI target), the map's POI targets triggered from the console and the body turned a full circle twice in 15-degree steps, `ETERNALVR_TEST_MARKER_LOG=1`, GUI captures every 15: `hud-markers: the HUD matrix build hooked at RVA`, the quad the markers follow logged, and from the test log alone (each marker's point in the HUD's view against the direction the quad shows its drawn position in): 20 samples or more inside the band (32 degrees across, 19.5 up and down), the largest error under 1 degree, and every sample 22 to 36 degrees up or down off the frame. Positive control: the same check on the build before the fix (errors 6.4 to 20.7 degrees, 5 of 5 below the band on the frame) fails both |
| head-sweep | `qa-scenarios-room.ps1`: e1m3 `cp_03_shoot_gate` under hand aim, `ETERNALVR_BODY_FOLLOW=0`, a test recenter at 3 s, the head leaned 0.6 m ahead and back (`ETERNALVR_TEST_HEAD_OFFSET=0,0,-0.6,3`) while the body walks 2.5 s to each side in turn until the lean meets a wall, four shots, then the pause menu for 21 s: `room: head in geometry`, `only with the world at rest`, `first head sweep`, `ui: the aim dot at ... (where the ray meets the world)`, the physics object read the one whose post-step job sweeps, and from the `room: collision queries` lines: the camera hook asked with the step in flight and the asks were swept after the sync, no post-step found its pending task unfinished, the fire hook swept at once and held none back (and no `a shot's sweep ... was not made`); in the pause the camera hook swept at once and the room summary's times in geometry rose; no `collision query faulted` |
| mp-guard-trip | `qa-scenarios-guard.ps1`, the multiplayer guard's positive control in the game (offline): e1m3 `cp_03_shoot_gate` with the Heavy Cannon drawn and its Precision Bolt mod (T-136), `ETERNALVR_GUARD_TEST_TRIP_MS=55000` (`mp guard: test trip in 55000 ms`), the trip after `aim: head aim on` and the only one (`TRIPPED by test`), the game still running 25 s later, `xr: multiplayer guard tripped: the game is shown on the flat cinema screen`, the last two 10 s `xr: N frame(s)` summaries after the trip with the same head-tracked count and a larger on-the-screen count (the headset gets only the cinema screen), `head: the multiplayer guard is tripped; the game's view and aim are no longer changed`, the `cvar restore: the multiplayer guard tripped:` summary, the Bolt's `zoom look:` decl line before the trip and `zoom look: the game's values back in N of N weapon decl(s) (the multiplayer guard tripped)`, no `REFUSED`; shots after the trip |
| mp-guard-standin | No game: a stand-in Vulkan program (compiled by `Add-Type`, one `vkCreateInstance`) named `DOOMEternalx64vk.exe` loads the layer build. `+connect_lobby 1`, then `+map game/pvp/pvp_laser/pvp_laser`: `TRIPPED by command line (argument "+connect" ...` / `"game/pvp/" ...`, `the layer does not load`, no `EternalVR layer loaded`; posing as the game's instance (app `DOOMEternal`, engine `idTech`) in an exe that is no known build: every detection point `MISSING`, `REFUSED` (fail closed), no `armed`; not the game's instance: `(not the game's; pass-through)`, no guard line; under another file name: no layer log at all. The program's own `vkCreateInstance` succeeds every time. Runs unelevated only (the loader ignores the layer path in an elevated process), needs no simulator, and starts only on an idle rig like a game scenario (its process is named like the game; run it under the rig lock) |
| fit-touch | `qa-scenarios-fit.ps1` (T-225), the reference for `fit-index`: both hands scripted ahead at their rest positions under hand aim, `ETERNALVR_HUD=wrist`, `ETERNALVR_TEST_RUNTIME_NAME=SteamVR`, an arm dump (`ETERNALVR_TEST_ARM_DUMP`) in the scenario's folder: `the right hand's oculus_touch controller (the weapon hand): no fit of its own; controller angle 0 deg (the launcher's)` and the left hand's, once each, no `xrCreateActionSpace failed`, the aim dot, the dump's last line with both grips and the shot ray |
| fit-index | The same with `ETERNALVR_TEST_PROFILES=valve_index` (only the Index bindings suggested, so OpenXR-Simulator reports the Index): the runtime reports `valve/index_controller` for both hands, each hand's fit line once (`its fit moves it 8.5 cm (grip frame 0.0 4.2 -7.4 cm, aim frame 0.0 8.5 -0.7 cm) and turns it -20 deg` for the weapon hand, -40 for the off hand), no refusal and no `xrCreateActionSpace failed`, the aim dot, no wrist HUD without its space; against `fit-touch`'s dump (run it first, in the same suite folder): both grips 8.5 cm (within 5 mm) from the Touch run's and the shot ray 20 degrees (within 0.5) lower |
| fingers-index | `qa-scenarios-fit.ps1` (T-226), on its own: the hands as in `fit-index`, the Index reported, an arm dump; `left.grip_curl = 0` for 12 s, then `1` for 25 s, then `left.grip = 1` for 2 s with the curl kept: `2 grip curl binding(s) for the fingers added to the /interaction_profiles/valve/index_controller data`, no refused bindings; the dump's off-hand curls: middle, ring and little at most 0.05 open and at least 0.95 with the curl alone, the index finger under 0.05 (no trigger); no `flame_belch` in a `controllers: input` line while wrapped, and one after the grip's own press (the control) |
| unseen-hold | `qa-scenarios-unseen.ps1` (T-228): headset pacing with `ETERNALVR_TEST_UNSEEN_VISIBLE=1` (the simulator's VISIBLE taken as nobody looking once the game ticks); the scenario posts `WM_ACTIVATE` to the simulator's window (active: session state 5; inactive for 25 s: state 4, the low hold; active again), as an unelevated QA process cannot move the desktop's foreground: the low hold's first start line within 1 s of state 4 and its end line within 1 s of the next state 5, 20 s or more held at 8.5 images a second or fewer (summed over the end lines, so a stray focus change between them is only a note), a `pace: held low for` 10 s line and a whole 10 s rates window inside a hold at 8.5 pairs and ticks a second or fewer, 30 images or more in the first 1.5 s after it (`pace: in the ... s after the low hold`) and a whole rates window after it at 30 pairs a second or more, no stall line in a hold and none of the hold switch's three change lines from the first start to 2 s after the end; the runtime's user presence line is a note. Positive control: a build before T-228 logs no low hold line |
| fx-sync | `qa-scenarios-fxsync.ps1` (T-230): e1m3 `cp_03_shoot_gate`, teleports to the outdoor starts of the 0.1.34 tiles (`game_player_start_5`, `_3`, `_8`), nothing set (the particle sync on by default): the start-up lines `seq-fx: ring advance (RVA ...) ... generation hooked: eye R draws eye L's particles and effects`, `seq-fx: GPU particle stages bound by eye R` and `seq-fx: ray-traced particle mask: eye R keeps eye L's`, no `seq-fx:` line with `did not check out` or `failed`; from the 10 s `seq-fx:` lines with eye R reuses (4 or more): eye R never advanced the ring, kept eye L's light pool (within 2 of the reuses), handled every GPU-staged particle system it met (U equal to W, W above 0: the tiles' case on screen), had at least half of eye L's GPU particle emitter records in every window, and kept the ray-traced particle mask; no `Bump MAX_TRANSPARENCY_QUADS` in the game's `qconsole.log` when the run wrote it. Positive controls (the asserts on the rig's logs of 2026-11-08): `ETERNALVR_STEREO_FX_SYNC_GPU=0` fails U = W and the emitter records, `=count` fails the ring and the mask, `ETERNALVR_STEREO_FX_SYNC_RT=0` fails the mask |
| depth-layer | `qa-scenarios-depth.ps1`: e1m2's start with `ETERNALVR_DEPTH_LAYER=1` and `ETERNALVR_TEST_DEPTH_READBACK=1` (the depth swapchain's image of one shown image, as the runtime gets it, read back every 10 s); the timeline waits for 3 depth lines and 2 read-backs (40 s at least, 75 s at most): `depth: depth layer on: D32_FLOAT...`, the first copy of `_upscaledOpaqueDepth`, the depth swapchain and `first frame submitted with depth`; every `depth: last 10 s` line after the first has 90% of its projection frames or more with depth and eyes copied, and one has timed copies; no `off for this game`, no failed `xrEndFrame`, no `ERROR`; over the read-backs (two or more): no invalid texel or distance nearer than 0.24 m, each eye with some sky in one or more, a median of the medians of 0.5 to 300 m, and the median equal share outside the sky under 50%. Positive control (the copy's eye halves, Vulkan stage): `-GameEnv ETERNALVR_TEST_DEPTH_FAULT=1` (eye R's copy also into eye L's half) fails the last check |
| depth-layer-dlss | `qa-scenarios-depth.ps1`: the same checks with DLSS Quality per eye (`ETERNALVR_STEREO_DLSS=1`, `ETERNALVR_STEREO_DLSS_QUALITY=quality`, `+r_antialiasing 2`): `_upscaledOpaqueDepth` is the output-sized depth also when the eyes render smaller |
| launcher-e2e | `launcher\tests\e2e.ps1 -Root <Out>\e2e -Runtime <simulator json>` prints `E2E OK` (skipped without a Release build) |

Every game scenario also checks: in the map, stopped and cleaned up, Steam's record matching the cloud files,
no held value saved into `profile.bin`, no crash dump, and the multiplayer
guard (REQ-16): `mp guard: armed`, a `mp guard: map load '...' (single-player)` line, and (but in `mp-guard-trip`) no
`mp guard: TRIPPED` or `REFUSED` line.

A scenario's timeline starts once `launch-ht.ps1` returns, while the layer's own timers count from the game's start or
the map (`ETERNALVR_DEBUG_COMMANDS`' schedule, `ETERNALVR_GUARD_TEST_TRIP_MS`). When the timeline starts more than 12 s
after the layer log's in-map line (`qa.log` has the distance of every start as `timeline start: N s after the game
was in the map`; on time 2.5 to 7.2 s, and crosshair-hold-head's schedule breaks at about 15 s), the run is a harness
timing failure, `TIMING: the driver started N s after the game was in the map`: its timeline and the scenario's own
checks are skipped and, if nothing else failed, the scenario runs once more and the rerun's result is its result (the
first run's files move to `<Out>\<scenario>-try1`). A late rerun fails. `-TestLateStartSec N` holds every first run's
timeline back N s, to see the check and the rerun work.

## Output

`<Out>\report.md` (one row per scenario, every check, screenshots, run folder, duration, and
`QA: N of M scenarios passed`, then `QA: N rerun(s) after a late start` on its own line), `<Out>\qa.log`, `<Out>\<scenario>-<checkpoint>.png` (virtual display) and
`-sim.png` (simulator window), `<Out>\<scenario>\` with `eternalvr.log`, `launch.txt`, `stop.txt`.

## The block measure

`qa-blockiness.ps1 -Dir <captures> [-Period 4] [-Skip 3] [-Since <time>] [-Until <time>]` reads
`ETERNALVR_CAPTURE_EYES` pairs (`*-L.png`, `*-R.png`; with `-Since`/`-Until` only those whose eye L file was
written in that stretch) and prints, per eye and image half, the block-edge step ratio: the mean step between
neighbouring pixels at the boundaries of the strongest position modulo `-Period` over the mean at the other
positions, horizontal and vertical averaged, over the captures after the first `-Skip`. About 1 without
blocks (1.02 to 1.05 on the rig), above it with 4x4 shading. `-Period` is the 4x4 block in captured pixels:
4 at the eye image's own size, 6 with DLSS Quality (render size two thirds of it). Rig runs of the eye test on
2026-10-02 (merge-int build): TAA eye L 1.38 / 1.05, eye R 1.03 / 1.37 (left / right half, period 4); DLSS
Quality eye L 1.91 / 1.04, eye R 1.05 / 1.53 (period 6). On a fixed 4-pixel grid from each half's first
column (the earlier script) the same DLSS captures gave eye L 1.11 / 0.98 and eye R 0.96 / 0.96, although eye
R's right half is visibly blocky: that grid meets the 6-pixel blocks' edges at one boundary in three or none,
by where the half starts against the blocks.

## Adding a scenario

Add a hashtable to `$script:QaScenarios` in `qa-scenarios.ps1` (the thumb-rest wheel's are in
`qa-scenarios-wheel.ps1`, a rest or a touch bound like a button in `qa-scenarios-binds.ps1`, the control map's
in `qa-scenarios-controls.ps1`, the multiplayer guard's in `qa-scenarios-guard.ps1`, foveation across a map
loaded again in `qa-scenarios-foveation.ps1`, the HUD's markers in `qa-scenarios-markers.ps1`, the head sweep's in
`qa-scenarios-room.ps1`, the controller fit's in `qa-scenarios-fit.ps1`, the low hold's in `qa-scenarios-unseen.ps1`, the particle sync's in `qa-scenarios-fxsync.ps1`, the pickup camera's in `qa-scenarios-pickup.ps1`, the depth layer's in `qa-scenarios-depth.ps1` and the menu-started ones in `qa-scenarios-menu.ps1`, dot-sourced
after it in that order): `Name`, `Kind = 'game'`, `Proves`, `GuardTrips` (only for
a scenario that trips the guard on purpose), `OnlyByName` (run only when `-Only` names it),
`Env`, optional `Map` or `MenuMission` (with `MenuSlot` and, for `'continue'`, `MenuMap`: Menu-started scenarios above), `Args` (game command-line arguments), `EyeCaptures` (every how many pairs eye captures go to
`<stage>-eyes`) and `Input` (initial input lines), `Timeline = { param($c) ... }` and
`Asserts = { param($c) ... }`. Helpers (`qa-common.ps1`): `Wait-QaSeconds $c <s>`, `Wait-QaLog $c <regex>
<timeout>`, `Write-QaInput $c @(lines)`, `Save-QaShot $c <checkpoint>`, `Get-QaLogText $c`,
`Add-QaNote`, `Test-QaPresent`/`Test-QaAbsent $c <text> [-Regex [-CaseSensitive]]`, `Test-QaCount $c <text> <n>`;
(`qa-menu.ps1`) `Get-QaLogMark $c` and `Wait-QaLogAfter $c <mark> <regex> <timeout>` (a line after an action),
`Get-QaMenuAim $c <x> <y>` (the input lines that point at a pixel of the menu panel), `Save-QaGameWindowShot $c
<checkpoint>`.
Check the syntax without running anything:
`[System.Management.Automation.Language.Parser]::ParseFile(<file>, [ref]$null, [ref]$errors)`.
