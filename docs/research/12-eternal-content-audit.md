# 12: DOOM Eternal content audit for VR (scope inventory)

Status: research notes, 2026-09-25. Tags: **[C]** = confirmed from a source we fetched, a local
reference file, or a name that exists in the executable's typeinfo/cvar dump; **[U]** = unverified
(memory, inference, or needs a playthrough / capture before we rely on it).

Reference extracts are in `reference/content/` (see `reference/content/MANIFEST.part.md`):

| File | Contents |
|---|---|
| `eternal-mission-list.md` | every mission with runtime map path, boss, gate, notes; Master Levels; Horde and Battlemode maps |
| `eternal-modes-hub-and-progression.md` | Fortress, Ripatorium, gates/encounters, upgrade screens, runes, cheats, kill systems, modes |
| `eternal-ui-typeinfo-inventory.md` | every `idHUD_*`, `idHUDMenu_*`, `idMainMenu_*`, settings data source and HUD event name |
| `eternal-camera-and-sequence-types.md` | camera classes, player mechanics, traversal triggers, sync types, slow-mo sources, related cvars, autosplitter cutscene ids |
| `eternal-video-files.md` | Bink 2 location, boot cvars, all `.bk2` names seen in update deltas |
| `eternal-settings-menu.md` | settings tabs and options with the cvar behind each |

Important caveat on method: the wikis describe story and pickups well but rarely say *how* a moment
is presented (first vs third person, player control vs scripted). Where this report calls a sequence
third-person, scripted or controllable without a source, it is tagged [U]. Section 9 lists a
capture pass that turns those into [C] in one playthrough.

---

## 1. Summary

- **Content volume.** 13 base missions + Fortress hub, 3 TAG1 missions + ARC Carrier hub, 4 TAG2
  missions, 6 Master Levels (remixes of existing maps), 3 Horde maps, the Ripatorium [C]. All share
  one engine path; the per-map differences that matter for VR are a small set of special sequences.
- **The camera classifier needs roughly 14 classes**, not the 3-4 that DOOM 2016 needed. The
  frequent ones (glory kill, chainsaw, crucible/hammer, meathook, jump pad, dash) happen every few
  seconds; the rare ones (cutscenes, scripted transports, death) set the ceiling on sickness risk.
- **Engine signals exist for almost every class** by name: `idSyncAttack*` (glory kill / chainsaw),
  `idPlayerMechanicGorillaBarJump` + `monkeybar_attachViewToTag` (swing bars author the view from a
  bone), `idCinematicCamera` / `idAnimCamera` / `idInteractionCamera` / `idDeathCamera`,
  `idTrigger_Teleporter(_Fade)`, `idTrigger_BouncePad`, `idStatusEffect_SlowMotion`,
  `idHUDEvent_StartCinematic` / `_Video` / `_SlowMotion`, the `cutsceneID` global and the
  `levelName` string [C, typeinfo / ASL].
- **The game forces its own FOV and DoF during glory kills** (`sync_autoFOV 1`, `sync_autoFOVValue
  90`, `g_autoDOFOnGK_*`) [C, cvars]. Both must be neutralised; the projection belongs to the
  headset.
- **Pre-rendered video is Bink 2 in `base\video\`** [C]. It is used for the boot sequence, DLC logo
  stings, a handful of story "visions" and the Dark Lord ending, tutorial popups, loading-screen tip
  clips and weak-point / boss tutorials [C]. All of it goes on a cinema/panel layer.
- **UI scope is large but enumerable:** ~30 single-player HUD element classes, ~20 in-game full-screen
  menus, ~20 main-menu screens, all SWF-based and named in typeinfo [C]. Every menu accepts mouse
  cursor input on PC [C, PCGamingWiki "mouse menu: true"], which makes a laser-pointer-as-mouse
  approach viable everywhere except the weapon wheel when a gamepad is active [C, PCGW Steam Input
  note].
- **BATTLEMODE is out of scope** (online, demon characters, separate `game/pvp/*` maps) [C paths].
  Photo mode (`g_setting_photomode`) should be disabled in VR [C cvar exists].

---

## 2. Levels

### 2.1 Campaign inventory with VR-relevant sequences

"Special" = anything that is not ordinary arena combat and traversal. Boss rows are in 2.3.

| # | Mission (map) | Special sequences worth a VR test | Why it matters |
|---|---|---|---|
| 1 | Hell on Earth (`e1m1_intro`) | Opening cinematic (4 cutscene ids at start) [C ASL]; Deag Nilox execution; Citadel meeting with Priests + Khan hologram (`e1m1_intro_cinematic_khan_maykr_priest_meeting`) [C]; subway; first weapon pickups and dense tutorial popups (chainsaw objective) [C] | First impression: intro cinematic class, pickup animations, tutorial video popups all appear in the first 20 minutes |
| -- | Fortress of Doom (`hub`) | Arrival cutscene [U]; bridge consoles (codex by clicking a monitor) [C]; equipment podiums; battery sockets; Ripatorium elevator; classic DOOM/DOOM II on an in-world PC [C]; jukebox, toy viewer [C]; Khan power cut + Crucible restore after Sentinel Prime [C] | Lots of first-person interaction animations; in-world screens; a hub played ~20 times |
| 2 | Exultia (`e1m2_battle`) | Novik throne scene (Slayer kneels) [C]: presentation [U]; Betrayer scene [C]; spearing Titan corpses [C]: interaction animation [U]; first Slayer Gate; Dash, Blood Punch acquired [C]; monkey bars [U first appearance] | Kneel scene is likely a third-person cinematic [U] |
| 3 | Cultist Base (`e1m3_cult`) | Ranak holograms [C]; SSG + Meathook acquired [C]; "reclaim the SSG via a Revenant drone" [C wiki text; U what the player controls]; pit-arena floor lift [C]; swinging poles, climb walls [C]; ends on a tram [C]; `e1m3_cult_cinematic` [C] | Possible possession / remote-view sequence would need its own class [U] |
| 4 | Doom Hunter Base (`e1m4_boss`) | Tram ride at start [C]: moving platform, player-controlled or scripted [U]; levitating annexes; swing pole; Ranak execution [C] | Tram = sustained vection |
| 5 | Super Gore Nest (`e2m1_nest`) | Boosters (jump pads) [C]; elevator-shaft drop [C]; heart chamber; **timed escape countdown** with HUD timer [C]; end cutscene id used by the splitter [C] | Timer HUD must stay readable while fleeing |
| 6 | ARC Complex (`e2m2_base`) | Night level; crashed jet crossing [C]; "turret elevator" [C]: ride [U]; Marauder at the end [C]; Hayden rescue scene [C] | No player-controlled turret found in sources; "Arc Turret" is a fast-travel label [C] |
| 7 | Mars Core (`e2m3_core`) | **BFG-10000 firing** (`e2m3_core_cinematic`) [C]; Phobos evacuation [C]; **"Use the Ion Catapult"** [C]: launch presentation [U]; **escape pod into the Mars core** [C]: cutscene or controllable [U] | Two large forced movements; candidates for fade/cinema |
| 8 | Sentinel Prime (`e2m4_boss`) | No combat except boss [C]; **several flashbacks** of the Slayer's arrival on Argent D'Nur [C]: presentation [U]; Khan hologram; Grav execution [C] | Flashbacks likely third-person cinematics [U]; walk-and-talk level is good for early testing |
| 9 | Taras Nabad (`e3m1_slayer`) | **Swimming introduced** (sewers) [C]; Crucible retrieval cinematic `e3m1_slayer_cinematic_crucible` [C]; last Slayer Gate [C] | Underwater locomotion class |
| 10 | Nekravol (`e3m2_hell`) | Soul factory; secret encounters [C] | Mostly standard |
| 11 | Nekravol II (`e3m2_hell_b`) | Ascend the spire; **"Jump in the Argent Stream to Urdak"** [C]: scripted transfer [U] | Level-ending forced motion |
| 12 | Urdak (`e3m3_maykr`) | Celestial ring alignment [C]; Maykr architecture with floating platforms [C] | Standard + boss |
| 13 | Final Sin (`e3m4_boss`) | Icon of Sin in two halves with a portal between [C]; **finale: Slayer jumps onto the Icon's head and plants the Crucible** [C]: third-person [U]; ending cutscene id [C]; credits | Largest-scale moment in the game; credits screen |

TAG1 [C unless tagged]:

| # | Mission (map) | Special sequences |
|---|---|---|
| 1 | UAC Atlantica Facility (`e4m1_rig`) | **Underwater sections with a shark** [C]; dive-suit tutorial video [C]; keycards; facility self-destructs at the end [C]: escape timer [U] |
| -- | ARC Carrier (`dlc/hub/hub`) | DLC hub between missions [C]; smaller than the Fortress [U] |
| 2 | The Blood Swamps (`e4m2_swamp`) | Trials of Maligog with ghost Night Sentinels [C]; end boss [C]; **a Titan lifts the platform the Slayer stands on and carries him** [C]: player control during it [U] |
| 3 | The Holt (`e4m3_mcity`) | Rising floating objects, electric pillars forming a way up [C]; Samur Maykr (two parts) [C]; Dark Lord resurrection scene [C] |

TAG2 [C unless tagged]:

| # | Mission (map) | Special sequences |
|---|---|---|
| 1 | The World Spear (`e5m1_spear`) | Two combat parts with a **cinematic in the middle** and a **non-combat third part** [C]; Torch of Kings (`lighting_the_torch.bk2`) [C]; **rides a creature to the Nether Lake** [C]: cutscene or controllable [U]; Sentinel Hammer acquired; sword under a tree spawns Marauders [C] |
| 2 | Reclaimed Earth (`e5m2_earth`) | Gate of Divum rings [C]; TAG2 traversal mechanics (`idPlayerMechanicSonicBlast`, `idInteractable_SonicBoost`, `idPlayerMechanicAntiGravityWallGrab`) [C names; U which map uses which] |
| 3 | Immora (`e5m3_hell`) | **Opening cinematic of the Night Sentinel army** [C]; breach the wall |
| 4 | The Dark Lord (`e5m4_boss`) | Dark Lord boss (two tutorial videos, so two phases) [C]; **ending is pre-rendered Bink** (`dark_lord_death_s130/s140/s150`, `death_of_dark_lord`) [C] |

Not in Eternal: **Titan's Realm** is a DOOM (2016) level [C, it is not in any Eternal list]. No
underwater sections are documented for Sentinel Prime or the Blood Swamps [C absence in sources; U].
No player-driven vehicle or turret is documented anywhere in the campaign [C absence; U, verify
ARC Complex "turret elevator" and World Spear creature ride].

### 2.2 Side content

| Content | Where | VR notes |
|---|---|---|
| Slayer Gates (6 base + 2 TAG1) | Exultia, Cultist Base, SGN, ARC, Mars Core, Taras Nabad; Atlantica, Holt [C] | Enter by key interaction then teleport into a separate arena [C]; ordinary combat |
| Secret Encounters (2 per level in most) | [C] | Timer HUD [C `idHUD_Timer`]; started from an interactable |
| Escalation Encounters (TAG2) | one per TAG2 level [C] | Two stages; standard combat |
| Rune devices, Sentinel Crystals, Praetor tokens, Modbots, batteries | all levels [C] | Each opens a full-screen choice menu or plays a pickup animation (Section 4.3) |
| Ripatorium | under the Fortress [C] | Wave arena, infinite lives; good repeatable test bed for glory-kill comfort tuning |
| Master Levels (6) | ARC, Cultist Base, SGN, Taras Nabad, Mars Core, World Spear [C] | Same maps and sequences as campaign; some cinematics may be cut in Master Levels [U]; the ASL can split on the SGN end cutscene inside the Master Level, so at least that one plays [C] |
| Horde Mode (3 maps) | `e6m1_cult_horde`, `e6m2_earth_horde`, `e6m3_mcity_horde` [C]; Arena / Blitz / Traversal rounds + bonus rounds + bounty demons [C] | Score / coin / timer HUD (`idHUD_Score`, `g_setting_hordeScoreHud`) [C]; traversal rounds are timed platforming, the highest motion density in the game [U] |

### 2.3 Bosses and mini-bosses

| Boss | Mission | VR-relevant notes |
|---|---|---|
| Doom Hunter | Doom Hunter Base (and later as a heavy) [C] | Two-phase: sled with shield, then flying body [C]; boss health bar [C]; fast lateral movement; sled is a weak point [C, doc 06] |
| Marauder | ARC Complex end (mini-boss, no boss bar) [C] | Shield / eye-flash timing; wolf; later a common enemy |
| The Gladiator | Sentinel Prime [C] | Two-round boss [C]; shield-and-blade design [C wiki trivia]; arena duel; weapon details per round [U] |
| Khan Maykr | Urdak [C] | Airborne boss, shield broken by hitting the Khan while demons help [U detail]; boss death cinematic [U] |
| Icon of Sin | Final Sin [C] | Enormous scale; 8 armour sections then 8 body parts [C objectives]; portal between halves [C]; finale cinematic [C] |
| Trials of Maligog / swamp boss | Blood Swamps [C] | Boss tutorial video exists [C]; details [U] |
| Samur Maykr | The Holt [C] | Two parts (two tutorial videos) [C] |
| Dark Lord | The Dark Lord [C] | Two phases [C videos]; pre-rendered ending [C] |

Boss fights share three VR issues: the **boss health bar** (`idHUD_BossVitals`) must be readable
without looking away from a huge target; **boss introductions** are cinematics [U per boss]; and
the **boss defeated screen** (`idHUDMenu_BossDefeated`) is a menu [C name].

---

## 3. Cutscenes and videos

### 3.1 Classes

| Class | Evidence | Examples | Frequency | VR presentation |
|---|---|---|---|---|
| **A. Bink 2 full-screen video** | `.bk2` in `base\video\`, `idLogicNodeBink`, `idHUDEvent_Video` [C] | boot sequence; `dlc1_logo_slam`, `dlc2_logo_slam`; `dlc1_vision`, `priestslayer_vision_short_lessflash`, `first_priest_death_vision`, `lighting_the_torch`, Dark Lord ending videos [C] | ~a dozen story videos across all content [U: full folder listing] | **Cinema screen** in a dark void, always. Flat 2D source, no stereo possible. Prefer the "lessflash" variants when present [C they exist] |
| **B. Bink clip inside a UI panel** | tutorial / weak-point / boss / horde tutorial clips, loading tips, settings previews [C] | `tutorial_*.bk2`, `loading_*.bk2`, `raytrace_ON/OFF.bk2` [C] | Every tutorial popup and loading screen | Part of the menu layer; no special handling beyond capturing the UI texture |
| **C. In-engine cinematic, third-person Slayer** | `idCinematicCamera`, `idGuiEntity_Cinematic`, `idHUDEvent_StartCinematic`, `hud_skipCinematic_*`, `cutsceneID` [C]; third-person Slayer shots are asserted in the project brief [C brief; U which ones] | Likely: intro, Novik kneel, Hayden, Sentinel Prime flashbacks, Crucible retrieval, Icon finale, TAG openings, Immora army [U each] | A few per mission [U]; total to be measured | **Cinema screen by default.** Immersive (HMD on top of the cinematic camera) is unsafe because the camera cuts and orbits a body that is "you". Optional immersive mode for enthusiasts |
| **D. In-engine cinematic, first-person** | `idAnimCamera`, `idInteractionCamera` [C]; which scenes [U] | Likely: Priest executions, some pickups, Novik/Betrayer dialogue if first-person [U] | A few per mission [U] | Immersive with rotation stripped (translation from the animation, rotation from the HMD), fallback to cinema per cutscene id |
| **E. First-person interaction animation** | `idInteractable_*` list, `idInteractionCamera` [C] | Weapon / ability first pickup, battery socket, door punch (`Obstacle_SnapDoor`), Slayer key, rune device, modbot, Titan-spear interaction [U mapping] | Many per mission | Immersive, rotation stripped; these are short and expected by the player |
| **F. Sync kill (glory kill / chainsaw / crucible / hammer)** | `idSyncAttack*`, `sync_*` cvars [C] | every stagger finish | Every few seconds | Doc 06 section 3.3: immersive with rotation stripped (default), cine window, or blink |
| **G. Loading screen** | `inLoadingScreen` state (Advanced Options), `isLoading` (ASL) [C] | mission load, Fortress transitions | Every transition | Cinema-sized panel with the loading art + tip clip; head-locked is wrong here |
| **H. Credits** | `idMainMenu_Screen_Credits`, `idSWFWidget_Credits_Screen` [C] | after Final Sin and from Extras [C] | Once | Menu layer |

Letterboxing: whether in-engine cinematics add black bars is [U]. `bink_cropAspect` ("crop mode for
all fullscreen binks") [C] only affects videos. `mdnt_cinematicFOV` ("override game FOV calculation
to not assume the resolution is in 16:9") [C] suggests cinematic FOV is authored for 16:9, which
supports rendering class C/D to a 16:9 cinema target at a fixed resolution rather than to the eye
buffers.

### 3.2 Cutscene counting

The ASL shows the base-game intro alone uses four consecutive cutscene ids [C]; ids are not
sequential per mission and shift between versions [C], so there is no count to read from them.
Proposed measurement: log every change of `cutsceneID`, `hideHudForCinematic`,
`idHUDEvent_StartCinematic`, active camera class and `levelName` during one complete playthrough of
base + TAG1 + TAG2 (a 100% save plus mission select makes this ~15 hours [U]). That produces the
per-cutscene override table the classifier needs (cutscene id or cinematic entity name -> class C
/ D / E, default presentation) and the true count.

---

## 4. UI screens

Placement vocabulary (matches doc 04 section 3): **HL** head-locked (small, near centre, only for
urgent short-lived info); **BL** body-locked lazy-follow sheet below the line of sight; **W** off-hand
wrist; **G** on the weapon; **R** reticle replacement (our laser/dot at hit distance); **WS** world
space at the source; **P** full-screen menu panel (cylinder, yaw latched when opened); **C** cinema
screen.

### 4.1 Combat and exploration HUD

| Element (class / cvar) | Shown | Candidate placement | Interactive |
|---|---|---|---|
| Health + armour (`idHUD_HealthInfo`, `g_setting_health_info`) [C] | always in combat | **W** (off-hand wrist); low-health also drives HL vignette | no |
| Rad-suit / oxygen fill (`hud_RadSuitFillSpeed_PerSecond`, `hud_OxygenFillSpeed_PerSecond` cvars) [C] | toxic areas, underwater | **W** next to health | no |
| Ammo for current weapon (`idHUD_WeaponInfo`) [C] | always | **G** (on the weapon) | no |
| Equipment (frag / ice) + cooldown pips (`EquipmentUpdate`) [C] | always | **W** or shoulder-mounted launcher readout [U design] | no |
| Flame belch cooldown (`FlameBelchUpdate`) [C] | always | **W** | no |
| Chainsaw fuel pips (`ChainsawRecharge`) [C] | always | **W**; or on the chainsaw when drawn | no |
| Dash pips (`idHUD_Dash`) [C] | on use | **W** or brief HL pip | no |
| Blood punch charge (`idHUD_BloodPunch`) [C] | always | **W**; glow on the off hand fist [U design] | no |
| Crucible charges / hammer charge (`CrucibleUpdateCharge`, `icon_ammo_hammer`) [C] | when owned | **W** | no |
| Ability indicators near reticle (grenade, ice, belch, chainsaw, blood punch, hammer; type 0-4) [C] | per setting | drop; the wrist replaces them | no |
| Extra lives counter (`idHUD_ExtraLives`, `g_setting_hud_extra_lives`) [C] | per setting | **W** | no |
| Powerup meter (Berserk / Onslaught / Overdrive, `idHUD_Powerup`) [C] | while active | **BL** or wrist | no |
| Rune meters (Chrono Strike) (`idHUD_RuneInfo`) [C] | per setting | **W** | no |
| Crosshair per weapon / mod (`idHUD_Reticle`, `g_reticleMode` Full/Dot/Hide) [C] | always | **R**: hide the game reticle (`g_reticleMode 2`) and draw our own on the weapon ray; keep mod-specific cues (sticky-bomb count, rocket lock-on, SSG hook ready, BFG kill count) as small **G** icons | no |
| Hit / damage-to-enemy feedback (`DamageToEnemy`, damage numbers dev-only) [C] | per hit | keep in-world if drawn in 3D; else **R** | no |
| Damage direction (`idHUD_DirectionalFeedback`, `view_enableHelmetFX`) [C] | on damage | **HL** ring at low opacity, or haptics only | no |
| Low health / armour / ammo warnings (`idHUD_LowWarning`) [C] | thresholds | **HL** small text or wrist pulse | no |
| Low-health screen vignette / fullscreen damage (`hud_damageFullscreenShow`, `view_skipDamageEffect`, `r_vignette`) [C] | on damage | re-implement as our own HMD-space effect; do not stereo-shift the game's | no |
| Glory-kill highlight (blue/orange flash on enemies, `g_setting_gk_highlight`) [C] | stagger | in-world already (it is a material effect) [U] | no |
| Weak-point highlights | on enemies | in-world [U] | no |
| Boss health bar (`idHUD_BossVitals`, `BossBarShow`) [C] | boss fights | **BL** top arc, or anchored above the boss [design] | no |
| Objective text (`idHUD_TutorialObjectives`) [C] | on update | **BL** | no |
| Compass + objective markers (`idHUD_Compass`, `idHUD_POI`, `g_setting_objectiveMarkers`) [C] | exploration | markers **WS** reprojected to world depth, or hidden (Vk3DVision advice, doc 04) | no |
| Keycards (`idHUD_Keycard`) [C] | when held | **W** | no |
| Mission / mastery challenge popups (`idHUD_MissionChallenge`) [C] | on progress | **BL** toast | no |
| Notifications: pickups, secrets, codex, level-up, "later in the mission" (`idHUD_Notification`, `g_setting_notification_*`) [C] | events | **BL** toasts; ammo/health pickup toasts can go to **W** | no |
| Speaker portrait (`idHUD_Speaker`) [C] | VO lines | **BL** with subtitles | no |
| Subtitles (`idHUD_Subtitles`, size 0-2, speaker names) [C] | per setting | **BL** during play; attached under the **C** screen during cinematics | no |
| Timers (Secret Encounter, SGN escape, challenges) (`idHUD_Timer`) [C] | when active | **HL** small, top of view | no |
| Horde score / coins / round info (`idHUD_Score`, `g_setting_hordeScoreHud`) [C] | Horde | **BL** | no |
| Interact prompt (`idHUD_FocusInfo`, `g_setting_interact_prompt`) [C] | near interactables | **WS** at the object | no |
| Button tooltips / hints (`g_setting_hud_tooltips`, `g_setting_hud_hint`) [C] | contextual | **BL**; glyphs must show VR controller buttons, not KB/pad [design] | no |
| Skip-cinematic prompt (`idHUD_SkipCinematic`) [C] | cinematics | under the **C** screen | hold-to-skip |
| Tutorial popups with video (`idHUDMenu_Tutorial`, `_TutorialCinematic`) [C] | first encounters | **P** small panel, pauses game [U] | dismiss |
| Weapon wheel (`idHUD_WeaponWheel`, slows time to 0.14, hold time 70-500 ms) [C] | hold | at the weapon hand (doc 06 section 3.7) | **yes**: stick or pointing |
| Combat scoring (`g_setting_combatScoring`) [C] | per setting | **BL** | no |
| Nameplates (empowered demons, `idHUD_Nameplate`) [C] | when present | **WS** | no |

### 4.2 Full-screen menus

| Screen (class) [C names] | Opened by | Placement | Interactive | Navigation on PC |
|---|---|---|---|---|
| Main menu root / start / campaign / difficulty (`idMainMenu_Screen_*`) | boot | **P**, with the shell map or a void behind | yes | mouse + keyboard or gamepad |
| Mission Select + cheat codes (`MissionSelect`, `CheatCodes`) | main menu / Fortress | **P** | yes | mouse / pad |
| Master Levels (`MasterLevels`) | main menu | **P** | yes | mouse / pad |
| Customize (skins), Sets Collection, Milestones, Triumphs, Seasons, Extras (`Customize`, `SetsCollection`, `Milestones`, `Triumphs`, `Seasons`, `Extras`) | main menu | **P**; 3D model previews render inside the SWF [U] | yes | mouse / pad |
| Settings (7 tabs) + popups (key bindings, controller layouts, HDR / screen calibration) | main / pause | **P** | yes | mouse / pad; key-binding popup expects KB/pad input [C name] |
| Credits | Extras / after Final Sin | **P** | skip | -- |
| Bethesda.net / account popups | first boot | **P** | yes (text entry) | mouse + keyboard |
| Pause (`idHUDMenu_Screen_Pause`) | Menu button | **P** latched in front, world dimmed | yes | mouse / pad |
| Dossier (`idHUDMenu_Dossier`): arsenal (3D weapon models, `dossier_weaponFov`), Praetor suit, runes, sentinel crystal upgrades, **automap** (3D rotatable map, `idAutomapCamera`), codex, challenges, collectibles | Dossier button | **P**; the automap is the hardest screen (rotate / zoom a 3D map by mouse drag) [C camera class; U controls] | yes | mouse drag/scroll or sticks |
| Modbot mod choice (`idHUDMenu_ModBot`) | Modbot pickup | **P** | yes | mouse / pad |
| Rune choice (`idHUDMenu_Rune`) | rune device | **P** | yes | mouse / pad |
| Sentinel crystal / Praetor token spend (`CurrencyExchange`, confirmations) | pickup, Dossier | **P** | yes | mouse / pad |
| Respec station (`idHUDMenu_Respec`, TAG) | interactable | **P** | yes | mouse / pad |
| Collectible (toy) viewer, album viewer, jukebox (`CollectibleViewer`, `CollectibleAlbumViewer`, `JukeBox`) | Fortress / pickup | **P**; toy viewer uses an orbit camera [C `idCollectibleCamera_Orbit`], a candidate for a real 3D model in the hand later | yes | mouse / pad |
| Classic DOOM / DOOM II on the Fortress PC (`idInteractable_Doom`, `idInteractable_Minigame`) | Fortress PC | **P** or keep on the in-world monitor [U how it renders] | yes (play) | KB / pad |
| Death screen (`idHUDMenu_Death`) | death | **P** latched, world dimmed | yes (checkpoint / restart / quit) | mouse / pad |
| End of level (`idHUDMenu_EndOfLevel`: stats, rewards, season) | mission end | **P** | yes | mouse / pad |
| Boss defeated (`idHUDMenu_BossDefeated`) | boss kill | **P** | continue | -- |
| UI walkthrough overlay (`idHUDMenu_UIWalkthrough`) | first Dossier use | **P** overlay | yes | mouse / pad |
| Loading screen (`inLoadingScreen`) | transitions | **C** sized panel | no | -- |
| Sentinel Armor offer (`idSWFWidget_SentinelArmorPopup`) | repeated deaths | **P** popup | yes | mouse / pad |

Interaction implication: a laser pointer mapped to the SWF mouse cursor covers every menu [C mouse
support]. The weapon wheel is the exception: with a gamepad active it takes gamepad input only
[C, PCGW], so we inject stick vectors for it (doc 06). Gamepad-nav emulation (thumbstick to
d-pad/stick) is the fallback for any screen that misbehaves.

### 4.3 In-world screens

Fortress bridge monitors, the DOOM PC, holographic ARC announcers, Ranak / Khan holograms and
`swf/guientity/generic_text` world text are world-space GUI entities [C class names], not HUD.
They are rendered in 3D and should come out in stereo correctly without UI capture [U, verify
they are not composited in the UI pass].

---

## 5. Camera-takeover and special states the classifier must recognise

Frequency: VH every few seconds, H every minute or so, M a few per mission, L rare.
Comfort risk if shown naively (game rotation passed to the headset): VH / H / M / L.

| # | State | Trigger / signal (source) | Frequency | Authored camera? | Risk | Proposed handling |
|---|---|---|---|---|---|---|
| 1 | Glory kill | stagger + melee; `idSyncAttack*`, sync start/end, `hands_hideDuringSyncInteraction` [C] | VH | yes (anim camera, **FOV forced to 90**, auto DoF) [C cvars] | H | Immersive: keep translation, strip rotation, ignore FOV and DoF; hide VR hands, show game arms. Option: cine window, blink. Variants: per enemy several directional kills plus "Death From Above" when airborne [C wiki]; total animation count [U, well over 100] |
| 2 | Seek and Destroy leap into GK | rune [C] | VH when equipped | yes + long translation | VH | Same as 1 plus vignette during the leap; blink option |
| 3 | Chainsaw kill | `idChainsaw_SliceAndSync` [C] | H | yes | H | Same as 1 (separate toggle) |
| 4 | Crucible / Sentinel Hammer | `idStatusEffect_Crucible`, hammer charge events [C] | M-H | Crucible: sync kill [U]; Hammer: ground slam with vertical motion [C wiki] | H / VH | Same as 1; hammer adds vertical vignette |
| 5 | Blood punch / melee lunge | `meleeLunge_canBecomeSyncMelee` [C] | VH | short lunge, no camera takeover [U] | M | Normal gameplay; vignette on lunge |
| 6 | Meathook pull | SSG mod [C] | VH | no: forced translation, player keeps aim [U] | VH | Doc 06 section 3.5: vignette, optional blink pull |
| 7 | Monkey bars / swing poles | `idPlayerMechanicGorillaBarJump`, `idTrigger_GorillaBar`, `idFuncSwing`; **`monkeybar_attachViewToTag 1` "view is aligned to 'camera' tag"** [C] | M-H (several per level) | **yes: view follows an animation bone**, third-person body offset exists (`monkeybar_swingAnimationOffset`) [C] | VH | Strip rotation (the bone's pitch arc is the sick-maker), keep translation; vignette; blink option. Setting `monkeybar_attachViewToTag 0` is a cheap experiment [U effect] |
| 8 | Wall climb / wall grab | `idPlayerMechanicWallClimb`, `WallGrab` [C] | H | player-driven, possible view assist [U] | M | Redirect trace to HMD yaw (doc 06) |
| 9 | Ledge grab / mantle / springboard | `idPlayerMechanicLedgeGrab`, `LedgeSpringBoard` [C] | H | short anim [U] | M | Classify as traversal, not cinematic |
| 10 | Jump pads / boosters | `idTrigger_BouncePad` [C] | H | no: impulse | H | Vertical vignette pulse; optional landing-arc marker |
| 11 | Dash / double jump | player [C] | VH | possible FOV kick [U] | H | Doc 06 |
| 12 | Teleporters and portals | `idTrigger_Teleporter`, `_Fade`, `idTeleporterPad`, `idInfo_TeleportDestination` [C] | M | **sets view yaw to the destination angle** [U] | M | Fade (the `_Fade` variant already fades [C name]); apply the new yaw to the body/turn offset instead of the headset; add our own short fade to plain teleports |
| 13 | Swimming | `idWaterEntity`, `idHavokPhysics_Water*`, dive suit [C] | M (Taras Nabad, Atlantica) | no | M | Movement follows head pitch underwater [U]; vignette; oxygen on wrist |
| 14 | Death | `idDeathCamera`, `idHUDMenu_Death`, `idHUDEvent_PVPDeath` is MP-only [C] | M | **yes: falling / tilting death cam** [U shape] | H (roll) | Replace with a fade to the death menu; never pass roll |
| 15 | Extra life / Saving Throw | `PlayerSetExtraLives`, `idExtraLifeTeleportLocation`, `idStatusEffect_SlowMotion` [C] | M | slow-mo; teleport to a safe spot after lethal falls [C class name; U behaviour] | M | Keep slow-mo; fade across the teleport |
| 16 | Slow motion (weapon wheel 0.14, Chrono Strike, Saving Throw, last-enemy slow-mo off by default) | `idHUDEvent_SlowMotion`, `weaponWheel_slowTimeScale`, `sync_lastEnemyEncounterSlowMo 0` [C] | H (wheel) | no | L | Head tracking must stay at full rate while game time is slowed; our pose path must not be tied to game tick [design] |
| 17 | Cinematic (third-person) | `idCinematicCamera`, `StartCinematic`, `hideHudForCinematic`, `cutsceneID` [C] | M | yes | VH | Cinema screen (default); per-id override |
| 18 | Scripted first-person sequence | `idAnimCamera` / `idInteractionCamera` active outside sync [C] | M | yes | M-H | Immersive, rotation stripped; per-id fallback to cinema |
| 19 | Interaction animation (pickups, sockets, doors, keys) | `idInteractable_*` + interaction camera [C] | H | yes, short | M | Immersive, rotation stripped |
| 20 | Scripted transport | tram (Cultist Base -> DHB), Titan-lifted platform (Blood Swamps), Argent Stream (Nekravol II), Ion Catapult and escape pod (Mars Core), creature ride (World Spear), long elevators [C that they exist; U presentation] | L (one or two per mission) | varies [U] | H-VH (sustained vection) | Per-map override list keyed by `levelName` + cutscene id: cinema or fade-through if scripted; vignette + horizon cue if player stands on a moving platform |
| 21 | Boss introductions / finishers | per boss [U] | L | yes [U] | H | Treat as class 17/18 |
| 22 | Level exit / mission end | exit portal, `OpenEndOfLevelScreen` [C] | per mission | fade [U] | L | Fade to menu |
| 23 | Slayer Gate entry / secret encounter start | `idInteractable_SlayerGate`, timers [C] | L | key insert anim + teleport [U] | M | As 19 + 12 |
| 24 | Menus / loading / video | `idHUDMenu_*`, `inLoadingScreen`, `idHUDEvent_Video` [C] | H | n/a | L | Panel / cinema; freeze or dim the world |
| 25 | Camera shake, view kicks, damage kicks | `view_skipShakes`, `view_skipKicks`, `g_kickAmplitude`, `idPlayerCameraShake` [C] | VH | yes | H | Force rotation shakes off; optional small translation shake (doc 06) |
| 26 | Photo mode / free cam | `idPhotoModeCamera`, `g_setting_photomode` [C] | -- | -- | -- | Disable |
| 27 | Automap / collectible viewer cameras | `idAutomapCamera`, `idCollectibleCamera_Orbit` [C] | M | menu-owned | L | Inside the menu panel only |

The two conditions that separate "immersive-safe" from "must go to a screen" are: (a) does the
authored camera stay near the player's head, and (b) does it cut. Classes 1-11, 18, 19 satisfy
(a) and not (b); class 17 fails both. A cheap runtime test for unlisted cinematics: if the authored
camera origin moves more than ~0.5 m from the player's eye position, or jumps discontinuously, treat
it as class 17 [U threshold].

---

## 6. Game modes and DLC scope

| Content | In scope | Notes |
|---|---|---|
| Base campaign (13 missions) + Fortress + Ripatorium | **yes (v1)** | Primary target |
| The Ancient Gods Part One (3 missions + ARC Carrier) | **yes (v1)** | Adds underwater, Samur, support runes, respec station |
| The Ancient Gods Part Two (4 missions) | **yes (v1)** | Adds Sentinel Hammer, TAG2 traversal mechanics, Bink ending |
| Master Levels (6) | **yes** | Same maps; free once campaign works |
| Horde Mode (3 maps) | **yes, after campaign** | Extra HUD (score/coins/rounds); traversal rounds need testing |
| Mission Select, cheat codes, Extra Life Mode, Ultra-Nightmare | yes | Menu-only differences |
| Classic DOOM / DOOM II on the Fortress PC | yes, as a panel | Low priority |
| Photo mode | **no** | Disable |
| BATTLEMODE (`game/pvp/*`, demon HUD, lobbies) | **no** | Online and asymmetric; detect `levelName` under `game/pvp/` or the Battle Arena screens and disable VR hooks, show a notice |
| Invasion | n/a | Cancelled; classes remain in the exe |
| idStudio / community mods (Aug 2025 official mod support) | unsupported but tolerated | Custom maps will hit unknown cinematics; the geometric fallback test in Section 5 covers them |

---

## 7. Settings: what to force or override in VR

| Setting (menu label) | cvar | VR policy |
|---|---|---|
| Window Mode / Resolution / Monitor | `r_fullscreen`, `r_mode` | Windowed or borderless at a small size; the eye targets are ours. Avoid exclusive fullscreen, which the game switches into when the window matches the screen [C, PCGW] |
| Vertical Sync | `r_swapInterval` | Off; the OpenXR runtime paces frames |
| Enable HDR | `r_hdrDisplay` | **Force off** for v1 (SDR swapchain path); revisit with HDR-capable runtimes |
| Field of View | `g_fov` | Ignored: projection comes from the runtime. Keep the menu value at a known default so SWF layout and culling assumptions are stable [U culling effect] |
| Glory-kill auto FOV | `sync_autoFOV`, `sync_autoFOVValue` | **Force `sync_autoFOV 0`** (or ignore the FOV it writes) |
| Motion Blur (+ quality) | `r_motionblur`, `r_motionBlurQuality`, `g_autoMotionBlurOnGK` | **Force off** |
| Chromatic Aberration | `r_chromaticAberration` | **Force off** |
| Film Grain | `r_filmGrainRatio` | **Force 0** (grain is uncorrelated between eyes and shimmers) |
| Depth of Field (+ AA) | `r_dof`, `g_autoDOFOnGK_*` | **Force off**, including glory-kill auto DoF |
| Vignette | `r_vignette` | **Force off**; our comfort vignette replaces it |
| Lens dirt / flares | `r_lensDirtRatio`, `r_lensFlaresRatio` | Default off in VR [design]; screen-space flares look wrong in stereo |
| Camera shake / kicks / hands bob | `view_skipShakes`, `view_skipKicks`, `g_setting_hands_bob`, `pm_noBob` | Skip shakes and kicks, bob off (doc 06) |
| Sharpening | `r_sharpening` | Lower than flat default; tune on device (doc 07) |
| Resolution Scaling Mode / Scale / Target FPS | `r_enableResolutionScale`, `rs_*` | Off; per-eye resolution and any dynamic scaling are ours |
| DLSS | `r_antialiasing 2`, `r_dlssQuality`, `r_dlssSharpness` | Per doc 07 |
| Ray-traced reflections | `r_raytracedReflections` | Allowed but default off (cost doubles with two views) [design] |
| Texture Pool Size | `is_poolSize` | User choice; warn that stereo targets add VRAM use |
| Performance Metrics | `com_showFPS` | Off in the headset; our overlay reports timing |
| Colorblind modes, Loot Drop Brightness, Gamma | `r_colorBlind*`, `r_gamma` | Keep user choice |
| HUD preset / element toggles | `g_setting_hud_preset`, `g_setting_*_info` | Keep user choice if we separate by typeinfo; **force a known preset** if we use region cutting (doc 04 Tier B) |
| Reticle mode | `g_reticleMode` | Hide (2) when motion aiming; our reticle replaces it |
| Ability indicators on reticle | `g_setting_hud_ability_indicator_*` | Off; wrist shows the same data |
| Subtitles, size, speaker | `g_setting_subtitles`, `hud_subtitles_*` | Keep; we place them |
| Safe frame / HUD alpha / colour profile | `swf_safeFrame`, `hud_globalAlpha`, `swf_colorProfile` | Keep; safe frame 0 helps region cutting |
| Kill cam | `g_setting_killcam` | Off |
| Aim assist / motion aim | `g_setting_aim_assist`, `g_setting_motion_aim` | **Force off**; aim assist would fight controller aim |
| Dash style, weapon-wheel hold time, hammer press/hold | `g_settings_dashStyle`, `ui_settings_controls_weaponWheel*`, `ui_settings_controls_hammerActivateTypeDefault` | Keep, but our input layer must know them (hold vs press) |
| Mouse / controller sensitivity, invert, smoothing | `m_*`, `joy_*`, `in_invert*` | Irrelevant to head look; right-stick turning is ours |
| Photo mode | `g_setting_photomode` | Force off |
| Tutorials / hints | `g_setting_tutorials`, `g_setting_hud_hint` | Keep; tutorial glyphs will show pad/KB prompts [U override] |
| Skip intro video | `com_skipIntroVideo` | Offer as an option; saves a flat video on every boot |

Forcing method: prefer writing cvars through the engine at runtime (the same path doc 03 uses)
and restoring them on exit; editing `DOOMEternalConfig.cfg` is the fallback. The settings UI will
still let players change forced values, so we must re-apply on `idSettingsDataSource_*` apply
events or every level load [design].

---

## 8. Prioritised hard content

Ordered by (likelihood of sickness or breakage) x (how often the player sees it).

1. **Glory kills, chainsaw and crucible kills (state 1-4).** Seen hundreds of times per mission. The
   game forces FOV 90 and auto DoF [C] and moves the camera with the animation. Handling: rotation
   stripped immersive default; FOV/DoF neutralised; blink and cine-window options; first playtest
   item (doc 06 section 7).
2. **Third-person in-engine cinematics (state 17).** A camera orbiting your own body is the classic
   VR break. Handling: cinema screen by default with the world hidden behind it; subtitles and skip
   prompt under the screen; per-cutscene override table from the capture pass.
3. **Swing bars / monkey bars (state 7).** View is bolted to an animation bone
   (`monkeybar_attachViewToTag`) [C]. Handling: strip rotation, keep the arc translation, vignette,
   blink option; test `monkeybar_attachViewToTag 0`.
4. **Meathook pull, dash, jump pads, Seek and Destroy leap (states 2, 6, 10, 11).** Large
   accelerations, constant in combat. Handling: event vignette, blink options (doc 06).
5. **Death camera (state 14).** Likely includes a fall and roll [U]. Handling: replace with a fade.
6. **Scripted transports (state 20):** trams, the Titan platform, Argent Stream, Ion Catapult,
   escape pod, creature ride, long elevators. Sustained vection without control. Handling: per-map
   list; fade-through or cinema for scripted ones; vignette plus a fixed horizon/cockpit reference
   for rides where the player stands on the platform.
7. **Teleport yaw snaps (state 12).** If the game sets view yaw on arrival, passing it to the
   headset is an instant disorientation. Handling: write it into the turn offset, add a short fade.
8. **Swimming (state 13).** Two missions. Handling: head-pitch-directed swim, vignette, oxygen on the
   wrist.
9. **Sentinel Hammer slam (state 4, TAG2).** Vertical launch and slam [C, doc 06]. Vertical
   vignette bias, pitch stripped.
10. **Photosensitive flashes in vision videos and bright fullscreen effects.** A flash filling the
    FOV in a headset is harsher than on a monitor. Handling: prefer the `*_lessflash.bk2` files [C
    exist], dim the cinema screen, consider `r_fullscreenLumDeltaThreshold` for in-engine flashes [C
    cvar].
11. **Boss arenas at extreme scale (Icon of Sin, Khan Maykr, Dark Lord).** Mostly a VR highlight;
    risk is in the boss bar placement and in the boss intro/outro cinematics (class 17).
12. **Dossier automap and other 3D-in-menu screens.** Rotating a 3D map with a laser pointer is
    awkward. Handling: stick mapping for rotate/zoom inside the panel; later a hand-held hologram.
13. **Zoom mods** (Precision Bolt, rocket lock-on, etc.) change FOV, covered by doc 06 (projection
    is ours; picture-in-picture scope later).
14. **Horde traversal rounds.** Timed platforming with jump pads and swing bars back-to-back
    [U density]; relies on items 3-4.
15. **Fortress DOOM PC and toy viewer.** Harmless but need a panel mode so they are usable.

---

## 9. Implications

### 9.1 State classifier

- Classes to support at launch: gameplay, traversal (swing bar, ledge, wall climb, swim), sync kill
  (glory / chainsaw / crucible / hammer, each separately switchable), interaction animation,
  first-person scripted, cinematic (third-person), scripted transport, teleport, death, slow-mo
  modifier, menu, loading, video. That is 14 base classes plus modifiers.
- Signals (all named in typeinfo or the ASL, doc 03 has the resolver plan): sync start/end;
  active camera entity class (`idCinematicCamera`, `idAnimCamera`, `idInteractionCamera`,
  `idDeathCamera`); `idPlayerMechanic*` active mechanic; `hideHudForCinematic` / `hideReticle`;
  `idHUDEvent_StartCinematic`, `_Video`, `_SlowMotion`, `OpenDossier`, `OpenEndOfLevelScreen`;
  `idHUDMenu_*` show/hide; `isLoading`, `isInGame`, `cutsceneID`, `levelName`; teleport trigger
  touch; timescale.
- **Per-content override table** keyed by `levelName` + `cutsceneID` (or cinematic entity name such
  as `e2m3_core_cinematic`) with the chosen presentation. Ids shift across game versions [C ASL], so
  names are the better key; fall back to the geometric test (camera distance from eye, cut detection)
  for anything not in the table, including community maps.
- The pose path must run at display rate independent of game time, because slow-mo (0.14 during the
  wheel) and pauses are frequent.

### 9.2 UI system

- HUD: ~30 element classes, but only ~12 need their own placement (wrist, weapon, reticle, boss bar,
  objective sheet, toasts, subtitles, timers, interact prompts, wheel, speaker, horde score). The
  rest can live on one body-locked sheet. Typeinfo names per element (`idHUD_HealthInfo`,
  `idHUD_WeaponInfo`, ...) make doc 04's Tier C (per-widget) feasible; Tier B (region cutting)
  needs a forced HUD preset and safe frame.
- Menus: ~20 in-game + ~22 main-menu screens, all SWF, all mouse-capable. One cylinder-panel
  presenter with a laser-to-cursor mapping covers them; the weapon wheel and automap need special
  cases.
- Video: one cinema presenter handles Bink and third-person cinematics; loading screens reuse it.
- Glyphs: tutorials and tooltips show KB/pad prompts. A glyph remap is a later nice-to-have [U
  feasibility], not a launch blocker.
- `hud_drawPerspective*` cvars ("draw the hud elements with a perspective") [C] are an engine-side
  option to explore for a curved HUD before building our own [U usefulness].

---

## 10. Capture pass (turns the [U] rows into facts)

One scripted playthrough on the dev rig with a logger that records, per frame change:
`levelName`, `cutsceneID`, active camera class, active player mechanic, sync state, HUD events,
open `idHUDMenu_*`, timescale, player origin and view angles, authored camera origin/angles. Mission
select plus a 100% save makes each mission replayable. Output: a CSV per mission and a generated
override table. Specific questions it answers are in Section 11.

---

## 11. Open questions [U]

1. Which in-engine cinematics are third-person vs first-person, per mission, and how many in total?
2. Is Eternal's cinematic presentation letterboxed? Does it change FOV?
3. Cultist Base "Revenant drone": does the player control a Revenant or a drone view?
4. ARC Complex "turret elevator" and World Spear creature ride: controllable or cutscene?
5. Mars Core Ion Catapult and escape pod; Nekravol II Argent Stream; Blood Swamps Titan platform:
   player-controlled, scripted with player camera, or cinematic?
6. Death camera shape (fall, roll, third-person?) and whether `g_setting_killcam` affects single
   player.
7. Do teleporters set view yaw on arrival? Do `idTrigger_Teleporter_Fade` fades cover it?
8. Swimming: does movement follow view pitch? Is there a swim-state flag separate from water volume?
9. Boss intros and finishers: cinematic per boss? Is the 2016-style "boss finisher against a black
   background" used in Eternal?
10. Tram rides (Cultist Base end, Doom Hunter Base start): can the player move during them?
11. How are the Fortress DOOM PC and the in-level holograms rendered: world GUI in 3D, or composited
    in the UI pass?
12. Does `monkeybar_attachViewToTag 0` keep swing bars functional with a player-owned view?
13. Exact Game / UI / Accessibility tab option labels on the current build (6.66 Rev 3.x)? Our
    labels come from cvars and a launch-build Video tab.
14. Master Levels: are story cinematics removed or kept?
15. Horde traversal rounds: motion density and time limits.
16. Full list of `base\video\*.bk2` on the current build (only update deltas are known).

---

## Sources

Local:
- `reference/content/*` (this topic; manifest in `reference/content/MANIFEST.part.md`)
- `reference/_cache/meathook/m34thook/alltypes.h` and `pregenerated/doom_eternal_cvars_generated.hpp`
  (https://github.com/brongo/m3337ho0o0ok)
- `reference/idtech7/typeinfo/kex-cvarlist-2024.tsv` (https://github.com/Official-KEX/doom-eternal-full-cvarlist)
- `reference/_cache/de_advancedoptions/` (https://github.com/SteamKaibz/DE_AdvancedOptionsModPublic)
- `reference/_cache/livesplit/doometernal.asl` (https://github.com/loitho/doom-eternal)
- `reference/_cache/ap-mod/data/map_sources.json`, `location_names.json`
  (https://github.com/snowzzrra/DoomEternal-AP-Mod)
- `reference/_cache/downpatcher/data/*.txt` (https://github.com/mcdalcin/DoomEternalDownpatcher)
- `reference/_cache/eternalbasher/EternalModInjectorShell/EternalModInjectorShell.sh` (resource list)
- Project docs 03, 04, 06, 07.

Web (fetched 2026-09-25):
- Doom Wiki (Fandom) via MediaWiki API: https://doom.fandom.com/wiki/Doom_Eternal and the level,
  boss, hub and system pages listed in the headers of `reference/content/eternal-mission-list.md`
  and `eternal-modes-hub-and-progression.md`
- PCGamingWiki: https://www.pcgamingwiki.com/wiki/Doom_Eternal
- Wikipedia: https://en.wikipedia.org/wiki/Doom_Eternal
- DSOGaming launch settings screenshots:
  https://www.dsogaming.com/pc-performance-analyses/doom-eternal-pc-performance-analysis/
- Xbox Wire, Horde Mode: https://news.xbox.com/en-us/2021/10/29/dive-into-horde-mode-doom-eternal/
- DOOM Eternal Modding Wiki, video mods (via search summary):
  https://wiki.eternalmods.com/books/7-miscellaneous/page/creating-video-mods

Not reachable: doomwiki.org (Cloudflare 403), StrategyWiki (no Eternal pages), GameFAQs and
speedrun.com guides were not fetched; YouTube chapter lists could not be searched this session.
