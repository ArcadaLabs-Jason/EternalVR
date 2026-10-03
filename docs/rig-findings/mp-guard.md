# Multiplayer guard: detection points (REQ-16, T-109, ARCHITECTURE section 4a)

Static analysis, then the live offline experiments of section 4 (results in section 4a). Input: `DOOMEternalx64vk.exe`, Steam build 25216728 (Rev 3.2),
PE timestamp 0x6A7B9B8C, SHA-256 `69dc13e88d1c19133ead7950dc64ebcbd4a5a3f6bd6f9c336ebffe56df6a1c11` (the same
exe as `engine-facts.md`). Tools: the analysis scripts of `engine-facts.md` section 8, Ghidra 12.1 headless
for decompilation, the type-info reader for enums and field offsets. All addresses are RVAs in this build.

`[static-verified]`: read from the code or data end to end, and every locator below was re-run offline
against the exe with the same checks the layer makes (each signature matches once in `.text`, every
cross-check holds). `[inferred]`: what the signal means in play is a reading of names, strings and call
structure; the live experiments in section 4 confirm it.

The code is `src/vkcore/mp_guard.{hpp,cpp}` (detection and latch wiring), `src/vkcore/game_text.{hpp,cpp}`
(in-memory image helpers shared with the camera hooks) and `src/platform/mp_policy/` (portable rules:
argument screening, map classes, menu screens, the latch; tested in `tests/platform/mp_policy/`).

## 1. How the guard behaves

- **One question.** Every game-touching feature calls `mp_guard::allowsGameTouch()` before acting: the
  camera hook's view writes (`presenter_head.cpp`, checked on entry and again just before the write),
  head aim (`addDelta`, checked just before the write), key injection (`injectKey`, the replaced
  `GetRawInputData`, `GetAsyncKeyState`, `GetKeyState`, `GetKeyboardState`), keep-active (the window
  subclass stops swallowing deactivation) and `ETERNALVR_WINDOW`. The camera hook and key injection are
  not even installed unless the guard is armed. Anything added later must do the same; the stereo
  hooks do (`docs/VR_STEREO.md`, Multiplayer guard).
- **States.** `Unarmed` until every detection point is installed (features off), then `Armed` (features
  may act). `Refused` if any point fails to locate or validate: fail closed, features stay off for the
  process and the log names the failed point. `Tripped` on the first signal: features stay off for the
  process. Nothing returns to `Armed`; the hooks stay installed and only watch (we never unhook live).
- **When.** The command line is screened in `vkNegotiateLoaderLayerInterfaceVersion`; a refused argument
  makes the negotiation fail, so the loader leaves the layer out and the game runs untouched. The
  detection points are installed when the game creates its `VkInstance`, before its first frame and so
  before any menu; `poll()` runs on every present.
- **All features, not some.** Every feature writes into the local player's game state, and every online
  context exposes all of them, so there is no feature a missing detection point leaves safe: any missing
  point refuses them all.
- **Trip = off, not quit** (T-114). A signal latches every feature off and leaves the game running: the
  player can carry on flat, and the presenter shows the game on the cinema quad (no head-tracked
  projection) with one log line saying so. What the session changed in the game is put back where the
  layer knows the game's own value (next bullets); the cvars the launcher sets on the command line are not,
  so multiplayer still needs a relaunch without VR, as the trip's log line says. In stereo the comfort set
  (view bob, kicks, shakes, the damage tint and blur and the rest of `stereoComfortCvars`) is no longer on the
  command line: the layer's hold sets it, so a trip gives it back. What stays on the command line keeps its VR
  value after a trip: `r_hdrDisplay 0`, the per-eye temporal effects switched off (`r_TAAAntiGhosting`,
  `r_SSDOTemporalAA` and the rest of the stereo set the layer never had to write), `rs_enable 0`, and the window
  and present cvars.
- **Trip listeners.** `mp_guard::addTripListener` keeps a fixed list of 8 (`mp_policy::TripListeners`,
  lock-free, tested in `tests/platform/mp_policy/trip_listeners_tests.cpp`). The call that closes the
  latch calls each listener once, on its own thread (a game thread or the present thread), right after
  the latch closes, in the order added; a listener added after a trip is called at once on the adding
  thread. Each slot has its own called flag, so a listener racing a trip runs exactly once. A listener's
  write is the one kind of write made after a trip: it undoes the layer's own change.
- **A held key at trip time** (T-114). The trip closes the latch first, so no key-down is posted or
  delivered from then on; key injection's trip listener then posts one key-up for each key the layer holds
  (`HeldKeys::releaseAll`, each key exactly once), and only key-ups are delivered after the trip
  (`key_injection::deliverable`). Polls read the real keyboard at once.
- **What a trip puts back.**
  - The cvars the layer wrote at run time (`runtime_cvars.cpp`: the stereo and comfort sets, CPU Saver,
    Sharpening, `swf_platformOverride 2`, `ETERNALVR_DEBUG_CVARS`; `taa_hooks.cpp`: the per-eye TAA set and
    the v1 fail-closed set). Every such write goes through `cvar_book::write`, which keeps the value the
    cvar read before the layer's first write (one book for both modules, so the earliest value wins where
    both write a cvar). The book's trip listener writes each one back once with the engine's setter, on
    the tripping thread, under the lock every write takes (the guard is asked under it, so no layer write
    lands after the restore). One line per cvar: `cvar restore: <name> <now> -> <value>, its value before
    the layer's first write (the multiplayer guard tripped; reads <value>)`, then a summary line.
  - Left as the layer set them, logged as `cvar restore: <name> left at <now>, not set back to <value>`:
    `r_windowWidth`, `r_windowHeight`, `r_fullscreen`, `r_swapInterval` and `r_hdrDisplay`
    (`mp_policy::cvarLeftOnTrip`). Writing them would resize the window, switch its mode or change the
    swapchain while the game keeps running, and none of them changes play.
  - The wall-climb cvars: back to the game's values at the camera hook's first frame after the trip
    (`climb_hook.cpp`, `presenter_head.cpp`).
  - The free off hand: the layer's forearm, elbow and shoulder modifiers are set back to no change once,
    on the off-hand hook's first tick of the hands that have them (that hook's game thread owns the
    modifier list, so not from a listener); one line `offhand: the multiplayer guard tripped: ...`.
  - The button prompts: the detours give the game's key names from the trip on, and a trip listener bumps
    the bind generation once so the game rebuilds the prompts it cached with the VR buttons' names; one
    line `prompts: the multiplayer guard tripped: ...`.
  - Not put back: the cvars the launcher sets on the command line (`launcher/data/forced-cvars.txt`)
    where the layer never had to write them. The command line sets them before the layer can read them,
    and their flat values are not known (section 5).
- **Test hook.** `ETERNALVR_GUARD_TEST_TRIP_MS=<n>` trips the armed guard with signal `test` `n` ms after
  arming (development use; it can only turn features off).

## 2. Signals

| # | Signal | What is hooked | RVA | Why it means online play | Status |
|---|---|---|---|---|---|
| S1 | Map load | `idMapInstanceLocal::LoadMap`, at the `lea rcx, "----------- LoadMap(%s) ------------\n"` with the map name in `rdx` | site 0x6D3FED, hook 0x6D4004 (function 0x6D28F0) | the map name; `game/pvp/*`, BATTLEMODE tutorials and anything unknown trip (section 3) | [static-verified] site and register; [inferred] that every map load passes here |
| S2 | Steam lobby join | `idSteamOnlineSessionInviteProvider` handler for `GameLobbyJoinRequested_t` (callback id 333), hooked at entry | registration 0x1BC3AB6, handler 0x1BC4110 | Steam raises it when the player accepts a lobby invite or joins a friend's lobby from the overlay | [static-verified] (RTTI of the callback object names the struct; the handler checks for a chat-type Steam ID) |
| S3 | Steam rich presence join | the same provider's `GameRichPresenceJoinRequested_t` handler (id 337), hooked at entry | handler 0x1BC4290 | Steam raises it on "Join game"; the game also calls it at start-up when `connect_lobby` (from `+connect_lobby`) is set | [static-verified] |
| S4 | Invite accepted | `idOnlineSessionInviteManager::ConsumeInvite`, hooked at entry | 0x1A2C7A0 | the platform-neutral path for accepting any invite (Steam, PlayFab party) | [static-verified] function (log string); [inferred] that consuming an invite means accepting it |
| S5 | BATTLEMODE lobby session | `idLobbyUISessionCasualBattleArena` constructor, hooked at entry | 0x101BC40 | built only by `idLobbyUIManager` for session types 2 (casual BATTLEMODE matchmaking) and 3 (its tutorial); callers 0x1154F80 and 0x1156640 | [static-verified] callers and vtable; [inferred] that nothing builds it outside BATTLEMODE |
| S6 | BATTLEMODE game session | `idBattleArenaGameSession` constructor, hooked at entry | factory 0x1514629 (in `idBattleModePlayState`), constructor 0x14A2860 | the network game session of a BATTLEMODE match; its only caller is the BATTLEMODE play-state factory. The game also builds one during its own initialisation, before any map loads (section 4a), so S6 counts only from the first map load on (`mp_policy::StartupPhase`) | [static-verified]; [live] the start-up construction |
| S7 | Main-menu screen change | `idMenu::Update`, just before `activeScreen = nextScreen`, filtered to the main menu's `idMenu` | site 0x11CBD2C, hook 0x11CBD3E; main-menu global 0x46AE140 (getter 0x174EF70) | the BATTLEMODE, match browser, private/public match, multiplayer, play-online, Invasion and series screens (section 3) | [static-verified] layout and filter; [inferred] screen meanings from their enum names |
| S8 | Main-menu screen poll | `poll()` on every present reads the main menu's `activeScreen` and `nextScreen` | same global, `idMainMenu+0x70` -> `idMenu+0x90/0x94` | same screens as S7, through a different mechanism (state, not the change event) | [static-verified] offsets |
| C | Command line | `GetCommandLineW()`, program path skipped | n/a | `+connect_lobby` and `+steam_invitationCookie`/`+steam_inviteSenderId` are what Steam passes when an invite starts the game (format strings at 0x2E8F230, 0x2E8F248, 0x2E8FD80); `game/pvp/` maps, network and lobby commands | [static-verified] strings |

Several signals fire for each way into online play: opening BATTLEMODE trips S7 and S8 before any lobby
exists; matchmaking adds S5, a match S6 and S1 (a `game/pvp/` map); a Steam invite accepted in the campaign
trips S2 or S3 (and S4) before the lobby, and S5 to S8 follow. So no single fragile point decides.

### Signatures and cross-checks

Every signature matches exactly once in `.text` of this build (`??` masks rel32, RIP displacements and,
where stated, structure displacements, which are then checked on their own).

| Point | Signature | Checks made before hooking |
|---|---|---|
| S1 map load (0x6D3FED) | `49 8B 57 20 48 8D 8D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 95 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ??` | `mov rdx,[rbp+Y]` reads the data pointer of the `idStr` built at `[rbp+X]` (Y = X + 8); the `lea rcx` target is exactly `"----------- LoadMap(%s) ------------\n"`. Hook at +23 |
| S2/S3 registration (0x1BC3AB6) | `BA 4D 01 00 00 48 8D 05 ?? ?? ?? ?? 48 89 01 48 83 C1 10 48 8D 05 ?? ?? ?? ?? 48 89 01 48 8D 05 ?? ?? ?? ?? 48 89 41 18 40 88 71 08 89 71 0C 48 89 59 10 FF 15 ?? ?? ?? ?? 48 8D 4B 30 BA 51 01 00 00 48 8D 05 ?? ?? ?? ?? 40 88 71 08 48 89 01 48 8D 05 ?? ?? ?? ?? 48 89 41 18 89 71 0C 48 89 59 10 FF 15 ?? ?? ?? ??` | callback ids 333 and 337 are in the pattern; RTTI of the three vtables: `.?AVidSteamOnlineSessionInviteProvider@@`, `.?AV?$CCallback@VidSteamOnlineSessionInviteProvider@@UGameLobbyJoinRequested_t@@$0A@@@`, `...UGameRichPresenceJoinRequested_t@@$0A@@@`; handlers from the `lea rax` at +29 and +80 |
| S2 handler (0x1BC4110) | `4C 8B DC 56 41 57 48 83 EC 58 48 8B 41 08 4C 8B FA 48 8B F1 48 85 C0 0F 84 ?? ?? ?? ?? 48 83 78 08 00 0F 84 ?? ?? ?? ?? 8B 4A 04 8B C1 25 00 00 F0 00 3D 00 00 80 00` | must match at the registered address (the Steam ID account-type check for a lobby) |
| S3 handler (0x1BC4290) | none of its own: its prologue (`40 55 41 54 41 57 48 8D AC 24 ?? ?? ?? ?? B8 ?? ?? ?? ?? E8`) matches five functions, so it is reached only through the registration above | a `lea rdx` to exactly `"+connect_lobby"` within its first 0x200 bytes (at +0xFC) |
| S4 ConsumeInvite (0x1A2C7A0) | anchor: the only `lea` to `"idOnlineSessionInviteManager::ConsumeInvite: ignoring unknown invite handle"` (0x1A2C860), function start through the unwind data (chains followed); prologue `40 55 53 56 57 41 54 41 55 41 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 33 C4` | the string occurs once, is referenced once, the reference is within 0x400 bytes of the start, and the prologue matches there. The prologue alone matches twice in `.text` (also 0x1A2DDC0), so it is never searched on its own |
| S5 constructor (0x101BC40) | `48 89 5C 24 08 57 48 83 EC 30 C7 41 20 00 00 05 00 48 8D 05 ?? ?? ?? ?? 48 89 01` | RTTI of the vtable it installs: `.?AVidLobbyUISessionCasualBattleArena@@` |
| S6 factory (0x1514629) | `BA 3E 00 00 00 B9 70 47 00 00 E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 74 23 48 8B 56 08 48 8D 05 ?? ?? ?? ?? 48 8D 4B 08 48 89 03 E8 ?? ?? ?? ??` | RTTI of the factory's vtable: `.?AVidBattleModePlayState@session@@`; the called constructor matches the next row, and the vtable it installs (at +117) is `.?AVidBattleArenaGameSession@@` |
| S6 constructor (0x14A2860) | `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 40 4C 8D 79 58 48 89 49 38 48 89 51 48 48 8D B9 ?? ?? ?? ?? 48 89 79 08 48 8D A9 ?? ?? ?? ?? 48 89 69 10 48 8D B1 ?? ?? ?? ?? 48 89 71 18 4C 8D B1 ?? ?? ?? ?? 4C 89 71 20 48 8D 05 ?? ?? ?? ?? 48 89 41 30 4C 8B E1 41 C6 47 0A 00 48 8D 05 ?? ?? ?? ?? 48 89 41 28 4C 8B EA 48 8D 05 ?? ?? ?? ?? 48 89 01` | (above) |
| S7 screen change (0x11CBD2C) | `48 8B 03 48 8B CB 8B 96 ?? ?? ?? ?? FF 90 ?? ?? ?? ?? 44 8B 8E ?? ?? ?? ?? 48 8B CE 8B 96 ?? ?? ?? ?? 44 8B 86 ?? ?? ?? ?? 48 8B 06 C7 86 ?? ?? ?? ?? FF FF FF FF 44 89 86 ?? ?? ?? ?? FF 50 70` | the six displacements are `idMenu` `transitionType` 0x98, `activeScreen` 0x90, `nextScreen` 0x94 (type info); the site is within 0x100 bytes after the only `lea` of `"idMenu::Update() - Next Screen ID '%d' does not have an associated screen class"`. Hook at +18 (`mov r9d,[rsi+0x98]`), `rsi` = the menu |
| S7/S8 main menu (0x174EF70) | `48 83 EC 28 48 8B 05 ?? ?? ?? ?? 48 85 C0 75 2D 8D 50 53 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 85 C0 74 14 48 8B C8 E8 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??` | the load and the store name the same global (0x46AE140); the allocation size is 0xD80 = `sizeof(idMainMenu)`; the exe's timestamp is this build's (the screen ids come from its type info) |

The layer checks RTTI names through the complete-object locator before each vtable (signature 1, self
RVA matching), every address against the module's bounds, and reads game memory in the hooks and the poll
under SEH, so a freed or changed object means "no signal", never a crash.

## 3. Classifications

**Map paths** (`mp_policy::classifyMap`). The map name is normalised (case, `\`, repeated `/`, a leading
`/`, `./` or `maps/`, a `.map` or `.entities` suffix). Online: any path segment containing `pvp`,
`invasion` or `battlemode`, a segment `mp`, or `tutorial_demons`. Single-player: `game/sp/`, `game/dlc/`,
`game/dlc2/`, `game/hub/`, `game/horde/`, `game/shell/` followed by a name, and
`game/tutorials/tutorial_sp`. Everything else, including `..` segments and an empty name, is unknown and
trips (fail closed). The installed folders under `base/game/` agree: `sp`, `dlc`, `dlc2`, `hub`,
`horde`, `shell`, `tutorials` (`tutorial_sp`, `tutorial_demons`, `tutorial_pvp_laser`) and `pvp` (nine
`pvp_*` maps).

**Main-menu screens** (`mainMenuElementID_t`, enum table at 0x406FB00 area): 6 `MATCH_BROWSER`, 8-12
`BATTLE_ARENA`, `_LOBBY`, `_LOBBY_OPTIONS`, `_UPGRADE_PREVIEW`, `_WEAPON_WHEEL`, 18 `INVASION`, 21
`MULTIPLAYER` (the root menu's BATTLEMODE entry, `idMainMenu_Screen_Root::GoToMultiplayer`), 22
`PLAY_ONLINE`, 23-26 private and public match screens, 28 `PVP_SERIES`, 40
`BATTLE_ARENA_LEADERBOARD_SELECTION`. Not online: start, root, difficulty, customize, campaign, mission
select, master levels, cheat codes, seasons, milestones, tutorials, codex, social, settings, extras, Horde,
credits, leaderboards.

**Command line** (`mp_policy::screenCommandLine`). The launcher's `ArgumentPolicy` normalisation
(`launcher/data/refused-args.txt`: quotes removed, `\` read as `/`, repeated slashes
collapsed, `+ cmd`, `+set cmd`, `+seta cmd` read as `+cmd`, case ignored) with its patterns, plus
`+steam_invit`, `+com_gamemode`, `+restartmapwithlobby`, `battlearena`, `+pvp_`, `/pvp`, `pvp_`,
`tutorial_demons` and `invasion`. `+connect` covers `+connect_lobby` and
`+ConnectOrHostCasualBattleArenaResult`; `+join` covers `+JoinShellLobby`.

## 4. Live experiments (offline; none needs a network connection or a second account)

Run with `ETERNALVR_ENABLE_LAYER=1`, the log directory set, and the PC's network adapter disabled or the
game started with Steam in offline mode, so the BATTLEMODE screens can open but nothing can connect.
Nothing here is run by this change.

1. **Armed at the main menu.** Start the game, wait for the main menu. Expect `mp guard: ... armed`, seven
   `hook at RVA` lines matching section 2, `map load 'game/shell/shell' (single-player)`, and no trip.
   Confirms S1's register and path form, and that no signal fires in normal start-up (S5 is not built at
   start-up; S6 is built once, before the first map load, and logged as not a signal, section 4a).
2. **Campaign load.** Load a campaign save or mission select. Expect `map load 'game/sp/...'
   (single-player)`, no trip, head tracking and head aim working. Repeat for a DLC mission, the hub,
   Horde and a Master Level (their paths must classify single-player).
3. **BATTLEMODE menu (S7, S8).** From the root menu choose BATTLEMODE, then back out. Expect `TRIPPED by
   online menu screen (main menu next screen 21)` (or the screen the root actually opens), then `head:
   the multiplayer guard is tripped`; head aim, view writes and the skip key stop and stay off after
   backing out and loading a campaign mission; `xr: multiplayer guard tripped: the game is shown on the flat
   cinema screen` appears once and the headset shows the cinema quad.
4. **BATTLEMODE tutorial (S5, S1).** In a new process, start the playable BATTLEMODE tutorial from the
   tutorials screen (offline vs bots). Expect a trip; with S7 disabled for the test, S5 (`lobby session`)
   and then S1 (`map 'game/tutorials/tutorial_pvp_laser'`) trip.
5. **`game/pvp/` load (S1).** With the console unlocked, `devmap game/pvp/pvp_laser/pvp_laser` (or
   `map ...`) offline. Expect `TRIPPED by map load` before the map's first frame. Also check how the name
   arrives (with or without `maps/` and extension).
6. **Command line (C).** Launch with `+connect_lobby 1` and separately with `+map game/pvp/pvp_zap/pvp_zap`:
   the log shows the refusal and nothing else from the layer; the game runs flat.
7. **Steam callbacks (S2, S3, S4)** without a second account: with a debugger (or a development build),
   call the registered handler at 0x1BC4110 / 0x1BC4290 with a zeroed struct, or set the `connect_lobby`
   cvar at start-up without the command line (for example from a config) so the game's own start-up path
   calls the rich presence handler. Expect the trip. The real-invite test stays dropped (D-043).
8. **Refusal.** Corrupt one signature in a development build (or run a different exe build): expect
   `REFUSED` naming the point, no camera hook, no key injection, no keep-active.
9. **Test trip.** `ETERNALVR_GUARD_TEST_TRIP_MS=20000` in a campaign mission: after 20 s head aim and view
   writes stop, the headset switches to the cinema quad and the log names signal `test`. With
   `ETERNALVR_SKIP_CINEMATICS=1` and the trip timed inside a cutscene (while the skip key is held), the log
   shows `released the held key 0x52` once and the game sees R released.

## 4a. Live results (2026-09-26, rig, build 25216728, OpenXR-Simulator)

Run folders are under `<workspace>\runs` (rig), layer logs next to the staged layers in
`<workspace>\tmp-vr\<label>-logs`.

- **Start-up false trip, fixed.** The first run (`mpg-e1`) tripped at the main menu: `TRIPPED by BATTLEMODE
  game session (idBattleArenaGameSession created)` 8 s after start, before the first present and before
  `game/shell/shell` loaded. The return addresses on the stack at the constructor's entry (`mpg-diag1`) are
  the play-state factory (0x1514657), 0x14B1EE7, 0x14A9051, 0x6761F7 (the function naming
  `BattleArenaGlobalSettings`) and 0x43276F (the function naming `DOOMEternal initialization`): the game
  builds one `idBattleArenaGameSession` while it initialises, in every process. With the trip disabled
  (`mpg-diag1`, `mpg-diag2`) it is built exactly once per process: not again for the main menu, a campaign
  map, the hub, Horde, exiting a mission to the main menu, or opening the BATTLEMODE menu. S6 now counts
  only from the first map load on; a launch straight into online play never reaches a map load with the
  layer loaded (the command line is screened first).
- **1. Armed at the main menu: pass** (`mpg-e1b`). Seven hook lines at the RVAs of section 2, `armed`,
  `idBattleArenaGameSession built during start-up, before any map load: not a signal`, `map load
  'game/shell/shell' (single-player)`, no trip.
- **2. Campaign load: pass** for `game/sp/e1m1_intro/e1m1_intro` (`mpg-e2`: single-player, head-tracked
  views, `head aim on`, the skip key read by the game, gameplay reached), `game/hub/hub` (`mpg-e2hub`) and
  `game/horde/e6m1_cult_horde/e6m1_cult_horde` (`mpg-e2horde`), all single-player with head tracking and
  head aim. DLC missions and Master Levels were not run (the DLC is not owned on the rig).
- **3. BATTLEMODE menu: pass** (`mpg-e1b`, and `mpg-diag1` with S6 disabled). Choosing BATTLEMODE 2.0 on
  the root menu: `TRIPPED by online menu screen (main menu next screen 21)` and `xr: multiplayer guard
  tripped: the game is shown on the flat cinema screen` once each; back out with ESC: the game keeps
  running and responding, no further trip lines. Nothing past the BATTLEMODE root screen was opened.
- **4, 5, 7: not run.** The BATTLEMODE tutorial builds a lobby session and may contact servers; a
  `game/pvp/` load needs the console unlocked (on the command line it is refused before the layer loads);
  the Steam callbacks need a debugger.
- **6. Command line: pass.** `+connect_lobby 1` and `+set connect_lobby 1` were given to a minimal Vulkan
  program named `DOOMEternalx64vk.exe` (so the real game never acted on them): `TRIPPED by command line
  (argument "+connect" requests a multiplayer connection)`, `the layer does not load`, and
  `vkCreateInstance` succeeds without the layer; with ordinary arguments the layer loads. The real game with
  a harmless refused argument (`+set evr_probe_invasion 1`, run `mpg-e6`): the same two lines, nothing
  else from the layer, and the game runs flat.
- **8. Refusal: pass** (`mpg-e8`, a build with one byte of the S5 signature changed): `BATTLEMODE lobby
  session signature matched 0 time(s)`, `lobby session MISSING`, `REFUSED`, then `head: the multiplayer
  guard is refused; no camera hook, head aim or key injection`, `ETERNALVR_WINDOW ignored` and `the game's
  focus handling is left alone`.
- **9. Test trip: pass** (`mpg-e9`, `ETERNALVR_GUARD_TEST_TRIP_MS=13600` with the cinematic skip): the trip
  landed 0.6 s into the first skip-key hold of the e1m1 intro cutscene: `TRIPPED by test`, `released the
  held key 0x52` (once), `the game read injected key 0x52 up`, the cinema fallback line and `head: the
  multiplayer guard is tripped`. The head-tracked frame count stays where it was for the rest of the run
  and the cutscene is no longer skipped.

## 5. Open points

- Whether S1 sees every map load (for example a server-directed load in a match) is [inferred]; S6 and
  S7/S8 cover a match independently.
- The screen id set is tied to this build's enum; another build is refused until the table is re-read.
- `idOnlineSessionInviteManager::ReceivedInvite` (an invite arriving, not accepted) is deliberately not a
  signal, so an invite during the campaign does not end VR unless it is accepted.
- Closed for the comfort set in stereo (2026-10-02): `pm_noBob`, `view_skipKicks`, `view_skipShakes`,
  `view_skipDamageEffect`, `view_showPlayerDamageViewEffect`, `view_damageBlur`, `g_skipViewEffects`,
  `hands_fovScale`, `meatHook_playerViewOverrideMode` and the post effects are no longer on a stereo launch's
  command line: the layer's hold (`stereoComfortCvars`) is the only writer, from Route S's first present, so
  the cvar book keeps the player's values and a trip gives them back. Still open: the launcher's remaining
  command-line cvars after a trip (`r_hdrDisplay`, the stereo set, and the comfort set in mono, where the layer
  holds nothing) keep their VR values unless the
  layer had to write them. Setting them back needs each one's flat value, and none of the sources is good
  enough yet: the registration's default (the `lea r8` of each cvar's registration, read by
  `analysis/ui-layer/cvars.py`) disagrees with the 2024 cvar dump (`reference/idtech7/typeinfo/
  kex-cvarlist-2024.tsv`) for `view_skipShakes` (0 / 1), `hands_fovScale` (0 / 1.15), `r_motionblur`,
  `r_chromaticAberration` and `rs_enable` (1 / 0), so the registration value is not necessarily the value a
  flat game runs with; the dump's values are not guaranteed defaults either; and the idCVar reset string's offset is not known (`idCVar::SetString`
  substitutes the values block's +0x30 for a null value, which fits a reset string but is not verified).
  What would close it: each cvar's value read on the rig in a launch without the forced cvars (for
  example `ETERNALVR_DEBUG_CVARS=pm_noBob=?;...`, which only logs), recorded here as a table the layer can
  use, or the launcher handing the layer the player's own values from its config snapshot.
