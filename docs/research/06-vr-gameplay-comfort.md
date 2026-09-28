# 06: VR gameplay, controls and comfort for DOOM Eternal (plus naming)

Status: research, 2026-09-25. Tags: **[C]** = confirmed from a source we fetched; **[U]** = unverified (from memory, inference, or needs an in-game check before we rely on it).
The file name keeps the original topic slug; the legal part of the topic was dropped from scope, so this
report covers gameplay mapping, controls, comfort and a short naming check only.

Reference extracts saved under `reference/design/` (see `reference/design/MANIFEST.part.md`).

---

## 1. What Eternal adds over 2016, and why it matters for VR

DOOM (2016) was already fast, but Eternal turns the player into a projectile. The things that change the
VR problem:

- **More verbs, more buttons.** Eternal has dedicated inputs for Dash, Flame Belch, Equipment
  Launcher, Switch Equipment, Chainsaw, Crucible/Hammer, Switch Mod, Melee/Glory Kill/Blood Punch,
  weapon wheel, plus eight weapon slots [C, `eternal-pc-keybinds.md`]. There is **no crouch binding**
  in the default list [C], which frees a face button compared with DOOM 2016.
- **Air mobility.** Double jump, 2-charge omnidirectional Dash usable in the air, monkey/swing bars,
  climbable walls, jump pads, Meathook nodes and the Meathook itself [C]. Each is a large
  artificial acceleration, often vertical: the worst case for vestibular mismatch [C, Meta].
- **Resource loop that forces close range.** Glory Kill = health, Chainsaw = ammo, Flame Belch = armor,
  Blood Punch charged by Glory Kills [C]. Glory kills happen every few seconds, so however we present
  them, that presentation will be seen hundreds of times per level.
- **Precision targets.** Destructible weak points (Arachnotron turret, Mancubus cannons, Revenant
  launchers, Doom Hunter sled, Maykr Drone head, Blood Maykr head only while it charges, Cyber-Mancubus
  armour, Marauder eye-flash window) [C, `eternal-mechanics-extract.md`]. This is where tracked
  controllers are genuinely better than a mouse-less gamepad, and a selling point.
- **Forced camera sequences** beyond glory kills: Sentinel Hammer slam launches the player up and
  down [C], Meathook pull, swing-bar launch, "Seek and Destroy" rune long-range glory-kill leap [U],
  extra-life activation, cutscenes (both first-person in-engine and third-person).
- **Weapon wheel slows time** while held [C, community observation]: a native "bullet-time" radial menu,
  which maps beautifully to a 3D wrist/hand radial menu.

---

## 2. Master mapping table

Default = what we ship on first launch (Recommended preset). Comfort risk: L / M / H / VH.
"Hook/data" is what the engine side must give us (see research topics 02 to 04 for the how).

| Eternal action / mechanic | Proposed VR interaction (default) | Alternatives / options | Comfort risk | Implementation needs (hook / data) |
|---|---|---|---|---|
| Walk / strafe | Left stick, direction from **head** yaw | Off-hand controller direction; speed scale 60–100% | M | Inject move axes rotated into chosen frame; HMD yaw vs player yaw separation |
| Look / turn | HMD owns view; right stick smooth turn 230°/s | Snap 30/45/90°; off; vignette on turn | M (smooth) / L (snap) | Add stick yaw to player yaw; never let game roll/pitch camera |
| Room-scale | Physical translation moves player collision | Off (seated) | L | Feed HMD delta into player move each frame |
| Jump / double jump | **A** (right) | Hands-jump gesture (off by default); jump-arc preview | M–H | Button injection; vertical-motion vignette trigger on jump events |
| Dash (2 charges) | **B** (right), direction = left stick in locomotion frame, forward if centred | Dash toward off-hand pointing; "blink dash" (brief fade / stepped translation) | H | Detect dash start/end (player state or velocity spike) to drive vignette/blink |
| Monkey bars / swing poles | Automatic (as flat); **strip authored camera rotation**, keep translation | Blink through swing; optional grab-to-latch gesture (later) | VH | Identify swing mechanic state; filter camera-delta rotation; vignette |
| Wall climbing | Jump into climbable wall (as flat); detection faces **head** direction | Off-hand direction; physical hand-pull climbing (long term) | M | Redirect wall/ledge trace axis to HMD yaw (in 2016 the trace axis lives in `idPlayerMechanicLedgeGrab`); Eternal equivalent [U] |
| Ledge grab / mantle | Automatic, immersive, rotation from HMD | Cine-style blink | M | Ledge-state capture to classify it as traversal, not cinematic |
| Jump pads | Automatic; vertical vignette pulse | Blink to apex | H | Detect launch impulse |
| Meathook (SSG mod) | SSG in hand, **grip** fires hook aimed by the SSG controller | Aim by head gaze; hook pull as blink; stronger vignette during pull | VH | Aim ray must be controller; know hook-attached state for comfort effects |
| Glory Kill | Right stick click **or physical punch** at a staggered demon; animation shown **immersive, HMD owns rotation**, authored translation kept | Cine window; "blink kill" (fade, skip to end pose) [U feasibility]; freelook | H | Sync-state classifier; hide VR hands, show game arms; target selection via weapon/head ray |
| Blood Punch | Same punch gesture / stick click when charged (game auto-selects) | Two-hand "slam" variant for AoE feel [U] | L | Velocity threshold on either hand (1.0 to 4.0 m/s, default 2.8) |
| Melee (no stagger) | Same punch | None | L | Same |
| Chainsaw (fuel pips) | **Right stick up** | Draw from left hip with off-hand grip + push forward (gesture) | H (it is a forced kill animation) | Same presentation rules as glory kill |
| Flame Belch | **Left grip** (away from weapon) | Head-aimed vs off-hand-aimed | L | Per-action aim source: shoulder launcher → head ray default |
| Equipment Launcher (frag/ice) | **Left trigger**, fires from shoulder along **head gaze** | Off-hand aimed; physical throw gesture (release = fire along throw vector) | L | Per-action aim override for the fire frame [U]; frag arc preview option |
| Switch equipment | **X** tap | Left grip double-tap | L | Button injection |
| Weapon mod (alt fire / ADS) | **Right grip** hold | Toggle | L; **H if the mod zooms FOV** | Block/neutralise FOV zoom (Heavy Cannon Precision Bolt, etc.); projection must stay HMD-owned |
| Switch weapon mod | **Y** tap | None | L | Button injection |
| Quick switch (last weapon) | **Right stick down, tap** | Shoulder holster | L | Q tap |
| Weapon wheel (slow-mo) | **Right stick down, hold**: 3D radial at the weapon hand; select by stick *or* by pointing | Left-stick select; wheel attached to wrist | L | Reproject native wheel HUD to a hand-anchored quad, or draw our own and send slot keys 1–8 |
| Direct weapon slots | Holsters: right shoulder = Super Shotgun, left shoulder = Ballista/Rocket (configurable), right hip = last weapon | Off | L | Send slot key 1–8; hand-position zones |
| Two-handed aiming | Left grip near fore-grip → support; virtual stock optional | Per-weapon profiles | L | Per-weapon support-point profile for Eternal's weapon models |
| Weak-point aiming | Plain controller aim; optional laser sight | Two-hand stabilisation filter | L | Weapon ray = game aim; muzzle transform for laser |
| Marauder shield / eye flash | Plain aim; haptic tick when eyes flash [U data] | None | L | Optional: Marauder state for haptic cue |
| Crucible (3 charges) | **Left stick click** readies/strikes; blade drawn into off hand | Physical swing gesture with left hand after arming | H (kill animation) | Button injection; same presentation as glory kill |
| Sentinel Hammer (TAG2) | **Left stick click** → slam | Two-hands overhead-and-down gesture | VH (up-and-down launch) | Vertical vignette; strip camera pitch |
| BFG-9000 / Unmaykr | Weapon slot (wheel/holster) like any gun | Left-grip hold shortcut when not two-handing | L | Slot 8; Unmaykr slot sharing [U] |
| Power-ups (Berserk, Onslaught, Overdrive) | Automatic | Berserk: fist-only, punches via gesture | M (Berserk punches pull you) | Detect Berserk state to switch hand models to fists |
| Extra life | Automatic (brief slow-mo) | None | L | None |
| Dossier / codex / automap / suit / runes | **X hold** opens Dossier as a curved panel in front | Wrist menu | L | Menu/HUD-surface detection; laser pointer or stick navigation |
| Mission info / objective | **Y hold** | None | L | Left Alt injection |
| Pause | Quest **left Menu (≡) button** | Left stick click | L | None |
| HUD (health/armor/ammo/equipment) | Split: health/armor on off-hand wrist, ammo on weapon, rest on a head-locked-lag panel | Flat HUD quad at distance | L | HUD layer capture and element re-placement |
| Cutscenes (third person) | Cine window, follows head yaw slowly | Immersive | VH if immersive | Cinematic classifier |
| First-person scripted sequences | Immersive, HMD rotation | Cine window | M–H | Same classifier with "first-person" class |
| Fortress of Doom hub, Sentinel Battery doors, Ripatorium, Doom 1/2 PC | Normal gameplay controls; interact = melee/use | None | L | Confirm interact binding [U] |
| Slayer Gates, Master Levels, Horde Mode | Same as campaign | None | as campaign | None extra |
| Cheat codes (mission select) | Menu | None | None | None |
| BATTLEMODE | **Out of scope** (online; other characters) | None | n/a | Block VR hooks when in MP [U] |

---

## 3. The hard cases in detail

### 3.1 Who owns rotation

The single rule that removes most sickness risk: **the headset always owns view rotation (yaw, pitch,
roll); the game may own translation only.** Every Eternal system that rotates the camera (glory-kill
camera pans, swing-bar arcs, Sentinel Hammer slam pitch, landing dips, damage kicks, screen shake, dash
FOV kick [U]) gets filtered to translation before the stereo view is built. Where translation alone would be
unreadable (true third-person cutscenes), we fall back to the cine window.

A related rule: **projection is ours.** Because the stereo projection comes from the OpenXR runtime,
any FOV change the game makes (dash FOV punch [U], ADS zoom, Precision Bolt scope) must be ignored.
Scoped mods then become "no zoom". That is acceptable at first; a picture-in-picture scope is a later
feature [U].

### 3.2 Dash, double jump and the air game

Eternal's accelerations are short and near-instant. Meta's guidance lists *instant velocity changes*
and *quantized speeds* as mitigations because the vestibular mismatch comes from sustained
acceleration, not from speed itself [C]. That favours keeping Eternal's snappy physics instead of
smoothing them. What we add:

- **Event-driven dynamic vignette** (Fernandes & Feiner style restriction, scaled by speed and angular
  velocity [C]) with extra pulses on dash, jump-pad launch, meathook pull and hammer slam.
- **Blink options** for the three worst events (dash, meathook pull, swing launch): a 60 to 120 ms fade or
  a stepped translation. VFR shipped exactly this for its dash [C]; Sairento has a "reduced movement"
  dash-only mode [C].
- **Vertical vignette bias**: stronger restriction on vertical velocity than horizontal (jump pads,
  double jump, slam). [U: design proposal; no study found specific to vertical.]
- **Predictability**: Hellsweeper shows the landing arc and slows time when you aim a jump [C]. We
  cannot slow Eternal's time per jump without changing balance, but an optional **landing-arc marker**
  (drawn by us from player velocity + gravity) gives the same "I know where I'm going" cue. [U]
- **Dash direction**: Eternal dashes along the movement input, forward if none [C, wiki]. We keep that,
  in the same frame as locomotion (head by default). A "dash toward off-hand pointing" option suits
  players who want Sairento-style aimed movement.

### 3.3 Glory kills, chainsaw and Crucible

These are **the** comfort-versus-feel decision, because they are constant. Options:

1. **Immersive, HMD rotation** (default): authored camera translation kept, rotation from the headset,
   VR hands hidden and the game's arm animation shown. Kills are short (~1–1.5 s [U]).
2. **Cine window**: the full frame on a floating screen. Safest, but a window
   popping up every few seconds breaks flow in Eternal far more than in 2016.
3. **Blink kill**: fade out on kill start, fade in at the end pose, play audio + haptics. Needs the
   kill duration and end pose, and must not desync the game [U feasibility].

Separate toggles for Glory Kills, Chainsaw, Crucible/Hammer, traversal and regular cutscenes, which
keeps cinematics apart from interactive VR moments. The physical punch trigger (default
2.8 m/s) covers melee, glory kill and Blood Punch because Eternal routes all three through
one input [C].

### 3.4 Equipment launcher and Flame Belch

In lore and in game the Equipment Launcher is a **shoulder cannon** that fires along the view [C].
The natural VR mapping is **fire along head gaze**, which also means the off hand stays free for the
weapon's fore-grip. Offer off-hand aiming and a throw gesture (fire when the off-hand trigger is
released during a throwing motion, along the release velocity; DOOM VFR's tracked grenade throws were
well liked, with the caveat that overhand throws fell short [C]). Implementation question: if the game
aims everything along one view vector and we point that vector along the weapon controller, we need
to swap the vector to the head ray for the frame the launcher fires [U; see topic 04].

### 3.5 Meathook

The best VR moment in the game and the most intense. Aim with the SSG controller (grip), render the
chain from our muzzle transform, keep the game's pull. Default: strong vignette during pull, rotation
from HMD. Options: blink pull; hook aim from head. Hook targeting in the flat game likely has a
forgiveness cone [U]; the cone follows the controller ray.

### 3.6 Weapon handling and precision

- Motion aim is an advantage for weak points; no aim assist needed. Laser sight optional
  (cosmetic).
- Two-hand support with per-weapon support points; Heavy Cannon, Plasma Rifle, Rocket Launcher, SSG,
  Ballista, Chaingun, BFG and Unmaykr are all two-hand shaped. Each weapon profile
  distinguishes a barrel grip from a side grip.
- Shoulder holsters matter more in Eternal than 2016 because high-level play is SSG ↔ Ballista (or
  Rocket) quick-swapping. Default holsters: right shoulder SSG, left shoulder Ballista; configurable.

### 3.7 Weapon wheel

Hold right stick down → the game opens its wheel and slows time [C]. We place the wheel as a quad
around the weapon hand (or wrist) and select by stick direction (native) or by pointing (we convert
hand direction to the stick vector). Releasing selects. Low risk, high payoff.

---

## 4. Default control map: Meta Quest Touch

### 4.1 Right-handed (default)

**Left controller (off hand)**

| Input | Action |
|---|---|
| Stick | Move (head-relative) |
| Stick click | Crucible / Sentinel Hammer |
| Trigger | Equipment Launcher (frag / ice), head-aimed |
| Grip, near weapon fore-grip | Two-hand support |
| Grip, away from weapon | Flame Belch |
| X tap / hold | Switch equipment / Dossier |
| Y tap / hold | Switch weapon mod / Mission info |
| Menu (≡) | Pause menu |

**Right controller (weapon hand)**

| Input | Action |
|---|---|
| Trigger | Fire |
| Grip, in front | Weapon mod / alt fire (Meathook on SSG) |
| Grip, behind right shoulder | Holster draw (default Super Shotgun) |
| A | Jump / double jump |
| B | Dash |
| Stick left / right | Turn (smooth or snap) |
| Stick up | Chainsaw |
| Stick down tap / hold | Quick switch / weapon wheel |
| Stick click | Melee / Glory Kill / Blood Punch |
| Physical punch (either hand, threshold) | Melee / Glory Kill / Blood Punch |

Deliberate choices: Jump and Dash on A/B mirror the console layout (A jump, B dash [C]) so Eternal
players' thumb habits transfer. Crouch is gone in Eternal, so A is free for jump. Chainsaw on stick up and
weapon selection on stick down keep the most frequent actions on the weapon hand.

### 4.2 Left-handed

Two modes:

- **Button swap**: triggers, grips and stick clicks swap sides (weapon in left hand); sticks keep
  move-left / turn-right; face buttons stay physical.
- **Button and stick swap**: move on the right stick, turn + chainsaw + weapon select on the left
  stick; Jump/Dash on X/Y; Switch equipment/mod on A/B; the pause stays on the left Menu button
  (only one Menu button on Touch is app-accessible [U]).

### 4.3 Other controllers (OpenXR interaction profiles)

| Controller | Differences / notes |
|---|---|
| Valve Index | No X/Y: left A/B take X/Y roles. Grip force → Blood Punch "clench" option; finger curl drives hand pose; trackpad can host the weapon wheel [U]. |
| HP Reverb G2 | Touch-like layout plus menu buttons; same map. |
| PSVR2 Sense (SteamVR) | Touch-like; Create/Options buttons for Dossier/Pause [U]. Adaptive triggers via PSVR2 Toolkit, per-weapon resistance on the weapon hand: chaingun spin-up, Ballista charge, Precision Bolt, Plasma heat. |
| Pico 4 / 4 Ultra | Touch-like; via Virtual Desktop/VDXR or SteamVR [U]. |
| Gamepad | Not a target; allow only for menus [U]. |

Build the input layer on OpenXR actions with suggested bindings per profile, so SteamVR / Meta
rebinding tools also work.

### 4.4 Haptics

Controller: per-weapon recoil (short for pistol-class, long rumble for chaingun), hook fire + tension
pulse while pulling, chainsaw rev loop, punch impact, Blood Punch charged tick, dash/jump-pad kick,
low-health heartbeat. bHaptics vest (optional): directional damage (needs damage
direction from the game [U]), dash/jump/landing, glory-kill impacts, BFG discharge.

---

## 5. Comfort options and defaults

Three presets (Meta's "Recommended / Comfortable / Advanced" tiers [C]), chosen on first launch,
changeable live from our overlay without restart, persisted.

| Option | Values | Recommended (default) | Comfortable | Advanced |
|---|---|---|---|---|
| Turn mode | Smooth 150–400°/s / Snap 30/45/90° / Off | Smooth 230°/s | Snap 45° | Smooth 300°/s |
| Snap-turn fade | Off / 60 ms / 120 ms | n/a | 60 ms | n/a |
| Movement direction | Head / Off-hand | Head | Head | Head |
| Movement speed scale | 60–100% | 100% | 80% | 100% |
| Dynamic vignette (speed + turn) | Off / Low / Medium / High | Low | High | Off |
| Event vignette pulses (dash, jump pad, hook, slam) | Off / On | On | On | Off |
| Vertical vignette bias | Off / On | On | On | Off |
| Dash presentation | Normal / Blink | Normal | Blink | Normal |
| Meathook pull | Normal / Blink | Normal | Blink | Normal |
| Swing bars | Normal / Blink | Normal | Blink | Normal |
| Glory Kills | Immersive / Cine window / Blink [U] | Immersive (HMD rotation) | Cine window | Immersive |
| Chainsaw / Crucible / Hammer | Same choices | Immersive | Cine window | Immersive |
| Regular cutscenes | Cine window / Immersive | Cine window | Cine window | Immersive |
| Cine window follows head | On / Off | On | Off | On |
| Camera shake (translation) | 0–100% | 0% | 0% | 25% |
| Authored camera roll/pitch | Always stripped | stripped | stripped | stripped |
| Landing dip / head bob | Off / On | Off | Off | Off |
| Landing-arc marker | Off / On | Off | On | Off |
| Motion blur, chromatic aberration, DoF | forced off | off | off | off |
| Physical punch threshold | 1.0–4.0 m/s | 2.8 m/s | 2.8 | 2.4 |
| Hands jump | Off / On | Off | Off | Off |
| Holsters | Off / On | On | On | On |
| Laser sight | Off / On | Off | On | Off |
| Virtual stock | Off / On | On | On | Off |
| Seated mode | Off / On (height offset, turning required) | Off | n/a | n/a |

Why Hands-jump defaults off: Eternal jumping is constant and
two-hand aiming raises both hands often; false jumps would be common [U: test].

---

## 6. Naming (short)

Collision check (GitHub search + web, 2026-09-25):

- **"Praetor VR"**: used by a small 2024 student VR-headset project (`S001-133792-NS/Praetor-VR-Website`)
  [C]. Nexus DOOM Eternal search results are dominated by "Praetor Suit" skin mods, so "Praetor" will
  be hard to find on Nexus [C]. No existing VR mod uses it.
- **Helifax's DOOM Eternal VR mod** (previewed by Flat2VR in Sept 2023: 6DoF, single-pass stereo,
  initially no motion controls) has no distinctive name we could find [C]; it is a project overlap
  worth knowing about, not a name clash.
- "Eternal VR": a GitHub user name exists (empty profile repo) [C]; too generic anyway.

Alternatives (none had GitHub hits unless noted):

1. **ARGENT VR**: Argent energy powers the Praetor Suit; short and lore-true, but **ruled out**: an
   unreleased DOOM VR project already uses the name [C].
2. **Praetor XR**: keeps the working name but separates from "Praetor VR".
3. **Slayersight**: invented, zero hits [C], says what it is (the Slayer's eyes).
4. **Night Sentinel VR**: lore name, no VR project hits [C]; longer.
5. **Hellwalker VR**: a known Slayer epithet; no VR hits, but "Hellwalker" is used elsewhere [U].
6. **UNMAYKR VR**: distinctive in-lore spelling; one inactive GitHub account named unmaykr [C].
7. **Ripatorium VR**: memorable (Fortress training room); a Half-Life: Alyx "Ripatorium" arena repo
   exists [C].

Decision: **EternalVR** (tagline "a DOOM Eternal VR mod"): easy to spell, matches what people search for, and
says what it is. The earlier working name, built on "Praetor", proved easy to misspell. ARGENT is out because of the name collision.

---

## 7. Implications for our design

1. **Rotation ownership is the core comfort feature.** The camera pipeline must separate authored
   camera translation from rotation for every classified state (gameplay, traversal, glory kill,
   chainsaw, crucible/hammer, first-person script, cutscene) and let the options choose per class.
   This needs a state classifier covering cinematics, glory kills and ledges, plus swing bars, meathook pull, hammer slam and jump pads.
2. **Comfort effects are event-driven**, so we need cheap signals: dash start, jump-pad launch, hook
   attach/detach, swing state, slam state, landing, player velocity. Velocity alone gets most of the
   way; explicit states make blink modes possible.
3. **Per-action aim source.** Weapon fire, weapon mod and Meathook aim from the weapon controller;
   Equipment Launcher and Flame Belch from the head (or off hand); glory-kill target selection from
   head or weapon. If the game has a single view vector, we need a per-frame override when those
   actions fire [U: engine research].
4. **Projection and FOV are ours.** Neutralise game FOV changes (zoom mods, dash kicks) and force-off
   motion blur, chromatic aberration and DoF.
5. **Input layer = OpenXR actions → synthetic game inputs** covering the full binding list in
   `docs/notes/eternal-pc-keybinds.md`, including slot keys 1–8 for holsters and a pointing-to-
   stick converter for the weapon wheel. Unknowns to confirm in game: interact binding, Unmaykr slot,
   Dossier/automap gamepad bindings, hold/toggle options.
6. **HUD must be decomposable**: wrist (health/armor), weapon (ammo), wheel (hand), Dossier/menus
   (curved panel), with a calibration step for the off-hand HUD position.
7. **Traversal detection direction**: redirect ledge/wall-climb traces to HMD yaw (Eternal's equivalent
   of the 2016 ledge-grab function must be found).
8. **Scope boundaries**: campaign, TAG1/TAG2, Master Levels, Horde Mode, Fortress hub in scope;
   BATTLEMODE out of scope and should disable VR hooks.
9. **Playtest plan**: the first milestone that has stereo + head tracking should immediately test
   glory-kill immersive vs cine window and dash/jump-pad vignette levels, because those two decisions
   shape the camera pipeline more than anything else.

---

## 8. Open questions [U]

- Exact glory-kill duration and whether a "blink kill" can skip visuals without desync.
- Whether swing bars and hammer slam author camera pitch/roll or only translation.
- Default hold/toggle and weapon-wheel options in Eternal's control menu; interact binding.
- Meathook aim forgiveness cone and whether it reads the view vector.
- Whether Eternal's Precision Bolt / other zoom mods change projection or only a render FOV cvar.
- Real Slayer ground speed (m/s) to calibrate vignette curves.

---

## Sources

- Bindings: https://frondtech.com/doom-eternal-pc-keyboard-controls-and-key-bindings/ ;
  https://www.shacknews.com/article/117015/doom-eternal-controls-and-keybindings
- Mechanics: https://doom.fandom.com/wiki/Meat_Hook ; https://doom.fandom.com/wiki/Equipment_Launcher ;
  https://doom.fandom.com/wiki/Destructible_Demons ; https://doom.fandom.com/wiki/Cheat_Codes_(Doom_Eternal) ;
  https://doom.fandom.com/wiki/Crucible ; https://doom.fandom.com/wiki/Sentinel_Hammer ;
  https://en.wikipedia.org/wiki/Doom_Eternal ;
  https://www.shacknews.com/article/127261/doom-eternal-update-666-brings-horde-mode-next-week ;
  https://steamcommunity.com/app/782330/discussions/0/6086066536281439701
- Comfort: https://developers.meta.com/horizon/design/locomotion-best-practices/ ;
  https://developers.meta.com/horizon/design/locomotion-comfort-usability/ ;
  https://developers.meta.com/horizon/design/locomotion-user-preferences/ ;
  https://www.researchgate.net/publication/301723818 ; https://dl.acm.org/doi/10.1145/3562939.3565611 ;
  https://dl.acm.org/doi/10.1007/s10055-020-00466-2
- Precedents: https://blog.playstation.com/2017/12/01/three-ways-to-slay-in-doom-vfr-out-today-for-playstation-vr/ ;
  https://www.useapotion.com/2017/12/doom-vfr-review/ ; https://www.uploadvr.com/hellsweeper-vr-review/ ;
  https://www.meta.com/blog/hellsweeper-vr-launch-meta-quest-2/ ; https://sairento.fandom.com/wiki/Controls_and_Locomotion ;
  https://compoundvr.com/games/doom-3-bfg/ ; https://www.teambeefvr.com/
- Other Eternal VR work: https://x.com/Flat2VR/status/1704495949978984506 ; https://compoundvr.com/games/doom-eternal/
- Naming: GitHub repository search (gh), https://github.com/S001-133792-NS/Praetor-VR-Website ;
  https://www.nexusmods.com/games/doometernal
