# Campaign online features with EternalVR loaded (DECISIONS open question 6)

Do the campaign's online features (sign-in, events, stats, account linking) react to a client running EternalVR?
This page holds the rig check of 2026-10-12 on the development PC and the static inventory it rests on. Steam
build 25216728, Game Pass 1.0.56.0, EternalVR 0.1.49 (the packaged layer and launcher, `918e16b`), OpenXR
Simulator as the headset, both platforms signed in to Bethesda.net.

Evidence labels: [rig] seen in these runs, [log] the game's own logs (`Saved Games\id Software\DOOMEternal\base\`
`qconsole.log` and `structured.log`), [string] the exe's strings or decl names, [web] the source named, [inferred]
a reading of the others.

## 1. What was run

Runs with the same script, each holding the rig lock for itself, in turn with the other rig users:

| Run | Platform | Route | Layer |
|---|---|---|---|
| A | Steam | the rig's launch route (`tools/rig/launch-ht.ps1`), `ETERNALVR_DISABLE_LAYER=1` | off: the loader leaves it out |
| B | Steam | the same route with the 0.1.49 launcher's default environment, mono | on, mono |
| C | Steam | the same, stereo, the launcher's default settings (the CPU Saver's default item, `is_cacheGreedily 0`) | on, stereo |
| D | Steam | as A | off |
| E | Game Pass | the 0.1.49 launcher (Check, Launch VR) with `ETERNALVR_DISABLE_LAYER=1` in its environment | off (the launcher reported "The mod did not load") |
| F | Game Pass | the 0.1.49 launcher, default settings (stereo) | on, stereo |

E still gets the launcher's stereo cvars on its command line (`+r_TAASafeMode 0` and the rest): the launcher adds
them before it knows the layer stays out. It is the vanilla exe with the layer absent, not a plain Game Pass start.

**The script** (about 9 minutes): the title screen, the main menu (the news tiles), Events (the event summary and
its challenges), Campaign > Game Slot 1 > Mission Select > Hell on Earth (Steam: the 100% campaign save, put back
before each run with Steam's cloud record resynced) or Continue Game (Game Pass: that account's own save, early in
Hell on Earth), the opening cinematic skipped, 5 minutes of play at the mission's start: fire bursts sweeping left
and right, short walks, melee presses for glory kills, jumps; then pause > Exit to Main Menu, Customize > Slayer
(skins), Campaign > Milestones, and Exit from the main menu. BATTLEMODE, Horde and the social screen were never
opened: the menu is walked up from Campaign.

**Where the script differed.** A probe run (layer off) mapped the menus first; three early starts of A that took
a wrong menu path were stopped and started again. On Game Pass the first Events visit showed "Loading Event
Info..." for longer than the script waited, so E's keys landed on the loading screen: its challenges were opened
by hand and its script resumed at Campaign. Game Pass's Campaign menu has fewer entries for that save, so E and F
did not reach Milestones (the step loaded the save again) and were ended by the run's stop step, not from the menu.
D and F were each run a second time (section 4).

**Input.** Keyboard and mouse through Windows `SendInput` to the game window in the foreground, the same for every
run, so it reaches the game with or without the layer. The layer's runs also had the play's controller input
(trigger, sticks) through `ETERNALVR_TEST_INPUT`, which the layer read in B and C; the Game Pass runs, started by
the launcher, had keyboard and mouse only.

**Captures per run** (the game process only, no packet capture): a copy of `qconsole.log`, the run's slice of
`structured.log` (from its own start header), the layer log, `Get-NetTCPConnection -OwningProcess <game>` every 2 s
(remote address, port, state), the DNS cache every 10 s and at the end (to put names on the game's addresses), and
window screenshots at every step.

**Extra arguments** `+logFile 1 +http_verbose 1 +onlineservice_verbose 1` in every run. `+logFile 1` is honoured.
`http_verbose` and `onlineservice_verbose` exist in the exe and are accepted (no "Unknown command"), but the retail
build logs nothing more with them [rig]. There is no per-request HTTP log on retail: the game logs failed requests
only, as `http_status_error` lines in `structured.log` with the URI and the status [log]. The runs therefore
compare the failures, the sign-ins, and the hosts and ports the game process connects to.

## 2. Per-run results

| Run | Sign-ins | Failed requests (`structured.log`) | Game connections (all TCP) | Cheat, achievement or prohibition lines | Guard | Ended |
|---|---|---|---|---|---|---|
| A | Bethesda.net user signed in; PlayFab login at start and again past the title screen | none | 3 hosts, port 443 | none | (no layer) | quit from the menu |
| B | the same | none | the same 3, port 443 | none | armed; map loads single-player; no trip | quit from the menu |
| C | the same | none | the same 3, port 443 | none | armed; no trip; CPU Saver holds `is_cacheGreedily 0` | quit from the menu |
| D | the same as A | `api.bethesda.net/beam/accounts/me` 504 Gateway Time-out x2 (layer off) | the same 3, port 443 | none | (no layer) | quit from the menu |
| D, repeated | the same as A | none | the same 3, port 443 | none | (no layer) | quit from the menu |
| E | Game Pass account signed in; PlayFab login twice, after the title screen | `GetPlayerCombinedInfo` 400 x6, right after the login | the 3 above plus Xbox and Microsoft Store services, 443 (and one connection to the home router's UPnP port 1900) | none | (no layer) | the Milestones step loaded the save again; the game was stopped by the run's end step |
| F | the same as E | `GetPlayerCombinedInfo` 400 x6 at the same point; one `inventory.xboxlive.com` request that never connected (-1 CANNOT_CONNECT) | the same services as E, 443 | none | armed; no trip | as E |
| F, repeated | the same as E | `GetPlayerCombinedInfo` 400 x6 at the same point; nothing else | the same as E (and `social.xboxlive.com`, `achievements.xboxlive.com`), 443 | none | armed; no trip | as E |

"None" in the flags column: no `qconsole.log` line about cheats (other than the names of the campaign's cheat-code
collectibles, written in every run), no "achievements disabled", no "Not allowed" or "Resetting cheat cvar", no
prohibition or ban line, in any run. The only achievement line is the game's own "Calling GetAchievementState on
profile load complete", the same in every run. The cvars the layer holds in stereo (the per-eye temporal set, the
CPU Saver's item, the comfort set) show up in C as "Cvar 'x' was modified" lines (78 cvars against 55 in A); the game
logs no other reaction to them.

**Play.** The checkpoint at Hell on Earth's start is reached on arrival ("Checkpoint reached" on the HUD), and the
layer log records the game's checkpoint saves in B, C and F. The fire bursts killed zombies at the start (blood and fewer
demons in the screenshots; no log counts kills). A glory kill is not confirmed: neither the game's logs nor the
layer log name one, so this check does not show whether a glory kill reports anything. The event challenge on screen
("Kill 150 demons while in the air") stayed at 29 of 150 on the Steam account from the first run to the last, and at
1 of 150 on the Game Pass account.

## 3. Hosts

Every connection the game process made, by address, with the names the DNS cache gave those addresses:

| Address | Names | Runs |
|---|---|---|
| CloudFront, 443 | `api.bethesda.net` | all |
| Azure Front Door, 443 | `e6125.playfabapi.com`, `ghostrc.game.idtech.services` and `ghoststatic.game.idtech.services` (the game's runtime config), `fern.xboxservices.com` (one shared edge address: which of them each connection was cannot be told without packet capture) | all |
| Azure, 443 | `westus.tts.speech.microsoft.com` (Azure text to speech, held open for the whole session) | all |
| Xbox and Microsoft Store services (`inventory`, `profile`, `userpresence`, `achievements`, `social` on `xboxlive.com`; `collections.md.mp.microsoft.com`, `displaycatalog.mp.microsoft.com`), 443 | | E and F (Game Pass); which of them a session reaches varies from one session to the next, with the layer or without it |
| the home router, port 1900 (UPnP) | | E only |

No other port than 443 (and E's one UPnP connection, with the layer off). The publisher's services (Bethesda.net,
PlayFab, id's runtime config) and the speech service were the same in every run, with the layer or without it; the
layer adds no connection of its own.

## 4. Failures

- **Steam (A to D):** no failed request in the layer's runs (B, C) [rig, log].
- **Game Pass `GetPlayerCombinedInfo` 400:** six times within about 15 s of the PlayFab login, in E (layer off) as in
  F (layer on), with the same request and answer sizes (877 or 878 bytes sent, 109 bytes back). It is the same failure the logs show since 2026-10-11 22:46 on this
  Game Pass account [log]. It does not come from the layer: E has no layer in the process. Whatever the request
  asks that this account or platform does not allow, the rest of the session works (events, inventory, the save).
- **F's one `inventory.xboxlive.com` CANNOT_CONNECT:** a connection to a Microsoft Store service that never
  opened (no HTTP status), once, the same endpoint E reached; the logs show this endpoint failing at the transport
  level before (TIMEOUT, empty) [log]. At the same moment F's console logged `WARNING: Xboxlive CheckXboxliveOnlineEntitlement: transport error`,
  between its two PlayFab logins: the same transport failure seen from the entitlement check, not a refused
  entitlement [log, inferred]. F was repeated once to see it again: the repeat had neither, only the six 400s.
- **D's Bethesda.net 504 Gateway Time-out** (twice, `beam/accounts/me`): a server timeout in a run without the
  layer; D was repeated once, as the plan asks for a transient Bethesda.net 5xx, and the repeat was clean.
- No 401 or 403 and no PlayFab telemetry failure in any run.

## 5. Verdict

The pass rules of the static check, run by run:

| Rule | Result |
|---|---|
| The same hosts and ports as A | pass: B, C and D contacted the same three services on 443 as A; F the same publisher services as E, and Xbox and Store services from the same set |
| Sign-ins succeed | pass: Bethesda.net and PlayFab in every run, on both platforms |
| No new HTTP failure class (no 401/403, telemetry 2xx) | pass on Steam (no failure with the layer; the one 5xx was in layer-off D and gone in its repeat); Game Pass: the 400 is the same with and without the layer, and F's one store connection failure has no HTTP status, was seen before on that endpoint and did not come back in F's repeat |
| No cheating, achievements-disabled or prohibition lines | pass in every run, also in C with the stereo cvar holds |
| D clean | pass: D's two Bethesda.net 504s (a server timeout, with the layer off) went away in its repeat, which was clean |
| Game Pass E against F `GetPlayerCombinedInfo` | the same: 400 x6 in E, in F and in F's repeat, so it is not the layer's |

So, on the rig: the campaign's online features do not react to a client running EternalVR. The game signs in,
reads its event, inventory and profile data, and talks to the same services in the same way with the layer loaded
(mono or stereo) as without it, and logs nothing about cheats, achievements or bans.

**Limits.** This is rig-level evidence from the client side: what the game logs and which servers it talks to.
A judgement the servers make later on the data they receive (telemetry, event progress) cannot be excluded by a
client-side check, and nothing here sees inside the HTTPS traffic. The runs cover one Steam account and one Game Pass
account for about 9 minutes each; the play was scripted (kills and a checkpoint, a glory kill not confirmed).
Re-check if id ships an anti-cheat, a new protector or a mod-detection rule, or if a player reports a flag.

## 6. The campaign's online features (static check, 2026-10-12)

From the game's own strings and logs (build 25216728) and public sources:

| Feature | Live in 2026? | What it checks or sends | Risk for a modified client | Evidence |
|---|---|---|---|---|
| Bethesda.net sign-in and account link (`api.bethesda.net`) | yes | the platform login exchanged for a Bethesda.net session; the link's rewards | none found: no client-integrity field | [log] [string] [rig] |
| PlayFab profile, title E6125 (`e6125.playfabapi.com`): login, player data, inventory, cosmetic rewards, CloudScript | yes | the account's data, unlocks and event progress | none found; bans ("prohibitions") are server data with two types only, online play and voice chat | [log] [string] [rig] |
| PlayFab gameplay telemetry (`WriteTelemetryEvents`) | yes | about 90 gameplay event types, a header with the Bethesda.net account, device and session, and an OS, CPU, RAM and GPU summary | no module or layer list was found in it | [string] [inferred] |
| Events and their challenges (main menu, Events) | served: a monthly event and its challenges were on screen in every run on both accounts | challenge progress counted by the client and sent to the profile | progress is client-reported, so VR play can move it: the advice stays not to use VR to earn Event or challenge progress | [rig] [web: Bethesda support article 75320] |
| News tiles (main menu) | yes | read only (title data) | none | [string] [rig] |
| Runtime configuration (`ghostrc` / `ghoststatic.game.idtech.services`) | yes | read only | none | [rig] [log] |
| Azure text to speech (`westus.tts.speech.microsoft.com`) | a connection held open in every session | the accessibility speech service of the online stack [inferred] | none: the same with and without the layer | [rig] |
| Slayer Points and Slayer Club rewards | ended 2023-07-25 | | | [web: slayersclub.bethesda.net, "Slayers Club is changing"] |
| Boosters | removed 2023-09-05, with Denuvo Anti-Tamper | | | [web: dsogaming, Denuvo removal] |
| Denuvo Anti-Cheat | removed 2020-05-27 | | | [web: Bethesda support article 50870] |
| Steam achievements and stats | yes | Steam's own; no VAC in this game | none known | [string] [web: Steam store, app 782330] |
| Bethesda.net achievements code | present in the exe | | none known | [string] |
| Game-file integrity | yes | resource hashes against `build-manifest.bin` | none: EternalVR changes no game file | [string] [web: wiki.eternalmods.com, official mod support details] |
| Crash reports | local | dumps stay on the PC | none | [string] [inferred] |
| Bans or resets for single-player mods | none reported | | | [web: ResetEra update 6.66 Rev 2.2 thread; Steam community discussions for app 782330] |

**What EternalVR already does that matters here** [code]: the layer never touches game files; it refuses
BATTLEMODE (the multiplayer guard latches every game-touching feature off on any multiplayer signal and fails closed,
`docs/rig-findings/mp-guard.md`); the launcher refuses multiplayer arguments (`launcher/data/refused-args.txt`); an
anti-cheat tripwire keeps VR off if an anti-cheat module is in the process or the game folder (DECISIONS T-109).
