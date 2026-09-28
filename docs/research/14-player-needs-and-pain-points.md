# 14: What players want and what frustrates them

Research notes, 2026-09-25. Tags: **[C]** = confirmed from a source we fetched or read (quote or
figure is in `reference/community/`); **[U]** = inference, secondary report, or something we could
not open. Reference extracts: `reference/community/` (see its `MANIFEST.part.md`).

Scope: complaints about the existing DOOM Eternal VR options, the public feedback we could find on
a full-VR mod for DOOM (2016), the flat2VR wishlist, install expectations, and the hardware our players actually own.
This is not a survey. It is what vocal players wrote in public threads, weighted by how often the same
thing came up and by whether a mod author had to write an FAQ entry for it.

Method and limits:

- **The DOOM 2016 full-VR mod has no issue tracker.** GitHub issues and discussions are disabled on
  its repository and on its only public fork [C]. Feedback goes to the Flat2VR Discord,
  which we did not read. We rebuilt the feedback set from Reddit launch threads (22 threads,
  ~180 comments, via the pullpush.io archive until it rate-limited us), the mod's Steam DOOM (2016)
  thread, its FAQ and known-issues list, its release and test notes, and its development history,
  which records what testers reported with each log
  bundle [C]. Counts in section 2 are therefore "distinct public reports", not issue counts.
- Reddit pages cannot be fetched directly from here; the archive has comment text but not votes.
  YouTube comments and the Flat2VR Discord were not reachable. The mtbs3d Vk3DVision thread returned
  HTTP 403.
- Luke Ross's DOOM Eternal support is evidenced only by 2025 Reddit posts; the Patreon page does not
  list games without login [C for what we saw; U for tier details].

---

## 1. Complaints about the existing DOOM Eternal VR options

Three routes existed before a full-VR DOOM (2016) mod made "full VR" the expectation. All three are head-aim or
gamepad experiences, and that is the single loudest complaint.

### 1.1 Luke Ross R.E.A.L. (AER, Patreon)

- **Head aim is the deal-breaker.** "having to aim with your head is a deal-breaker. novelty wears
  out in a few minutes" (r/PSVR2onPC, June 2025) [C]. The mod deliberately keeps gamepad/head aim
  (author README) [C].
- **AER ghosting.** Alternate-eye rendering leaves one eye a frame old. Players notice: "if you
  switch back to the old legacy AER - some ghosting become visible again" [C]; the DOOM 2016 full-VR mod's own AER
  period drew "a bit of eye strain" and "AER just doesn't cut it" (section 2.5).
- **Display glitches on modest hardware.** A Quest 3 / GTX 1660 user got "big black horizontal lines
  ... very immersion breaking"; the help post was removed by moderators, i.e. no support path [C].
- **Paywall.** R.E.A.L. versions ship on the author's Patreon [C, MIXED 2025]. Players accept it for
  games with no alternative ("some of these games, you can't play in VR otherwise") [C] but it is an
  obvious gap for a free, open mod to fill [U].

### 1.2 Helifax Vk3DVision "DOOM Eternal Virtual Reality v0.90"

- **Not "full VR".** "It's not a 6DOF mod - it just makes the visuals 3D in a vr headset - still have
  to use regular controller - not amazing" (r/flat2vr) [C]. Motion controllers are mapped to a virtual
  Xbox pad through ViGEm [C, release notes].
- **Setup is a seven-step recipe** including a kernel driver (ViGEmBus), a separate profile download,
  a PDF of settings, Link, SteamVR started from inside Link, then the game [C]. The black screen "like
  it's in theater mode" was the first reply-worthy failure [C].
- **Aim broken on some builds.** GitHub issue #3 (the only DOOM Eternal issue on the repo): "looking
  up makes the gun angle downwards ... bullets do not go towards the crosshair"; on Steam "I can only
  use VR headset to aim vertical ... I don't know where I'm shooting" [C].
- **Hardware and fragility.** RTX-only, OpenVR-only, one eye often starts black until window mode is
  toggled 2-3 times [C, README notes]. Still, players rate its image highly: "superior visuals, though
  it is not 'Full VR'" [C]. That is a warning for us: single-pass stereo quality is the bar players
  already know exists for this exact game.

### 1.3 vorpX / ReShade Depth3D

- **Doesn't hook Vulkan.** "VorpX does not support Vulkan games" (vorpX forum 2021); players chain
  ReShade Depth3D side-by-side into the vorpX Desktop Viewer [C]. "I can't get this game too hook with
  vorpx" (Steam 2023) [C].
- **Paid and a screen, not a world.** "this isnt free, its 40 bucks" [C]; "So in the headset we see a
  2d screen ... like 3d movies?" / "No not with 6DOF" [C]; the 2020 ask was for "proper vr not just
  huge virtual screen" [C].
- **Daunting.** "having never messed with any of the stuff involved its a little daunting" [C].

### 1.4 DOOM VFR as the reference point

Players still measure DOOM VR against VFR's teleport: "Doom VFR for me was a disappointment due to
it's teleportation movement" (Steam 782330) [C]; "the official VR release had teleport only glory
kills while this one allows physical melee" (2026, about the DOOM 2016 full-VR mod) [C]. Road to VR's review lists
inconsistent teleport, no re-centre, hard-coded handedness, "clipping that renders close-up combat
impossible", and a 45-second fight with a lift button [C]. Every one of those is a direct requirement
for us (smooth locomotion default, recenter, left-hand mode, close-range weapon handling, reliable
interaction).

### 1.5 What the complaints add up to

Across all three tools, the recurring words are: head aim, gamepad, not 6DoF, black screen, setup
steps, paid, ghosting. Motion sickness in glory kills is **not** a documented complaint against these
tools, because none of them put the player's hands or body into the glory kill; players raised
comfort mainly as a doubt that fast DOOM works in VR at all ("Doom is all about speed and VR is not
there yet") [C].

---

## 2. Feedback on a full-VR mod, categorized

Public feedback on the DOOM 2016 full-VR mod, which is the closest thing to what we are building.
Summary:

| Category | Distinct reports | Typical example |
|---|---|---|
| Install / launch | 11 | Steam or launcher running as admin; Luke Ross mod still installed; ReShade OpenXR layer; stale NVIDIA driver; multi-GPU laptops; Linux |
| Performance / pacing | 10 | "I am on a 5090 and the gameplay is pretty choppy"; "first time I start it up it'll be in the 40-80 range"; relaunch fixes it |
| Controls / input | 8 | weapon swap on the turn stick "is the dealbreaker for me right now"; laser sight bugs; "most of the buttons don't work" |
| Rendering artifacts (test builds) | 8 | one-eye sparks and lens flare, LOW AMMO duplicated, weapon one-frame jumps on gratings |
| Image quality (AER) | 7 | AA resets, blur, "AER just doesn't cut it", eye strain |
| HUD / UI | 7 | health/ammo "at the bottom of the headset view ... unplayable when the pressure ramps up" |
| Runtime-specific | 6 | VD foveated streaming blur (Quest Pro), Pimax OpenXR refused, PSVR2 shows SteamVR home |
| AMD-specific | 5 | one RX 9070 XT tester, four log rounds: compile failures, low-res right eye, lighting flips, HUD unclassified |
| Comfort | 5 | "I need to take regular breaks for my old-man VR legs"; "wait if you get motion sick easy" |
| Antivirus | 3 | Defender ML detections on both executables; lag on map load until excluded |
| Crashes / freezes | 2 | changing graphics quality mid-level (known issue since 0.5) |
| Community / comms | 1 | launch thread derailed by an argument about how the trailer was made; post deleted |

### 2.1 Install and launch

The single biggest bucket. The FAQ's own "Quick Troubleshooting Checklist" has ten steps before
"Launch the mod again": disable OpenXR Toolkit, remove Luke Ross, update drivers, disable overlays,
fix ReShade's OpenXR layer in regedit, disable VD foveated streaming, SSD, force dedicated GPU,
restart the streamer [C]. Every one is an environment problem that the launcher could detect.
The mod's launcher already blocks conflicting DOOM mods and the Pimax runtime [C], which confirms the direction: detect and explain, don't document.

Admin elevation surfaced twice (Steam running as admin; UAC disabled) [C]. Linux/Proton was asked
twice with no answer [C]; Steam's Linux share is 3.90% [C].

### 2.2 Runtime-specific

Quest via VD/VDXR is the path players recommend to each other ("Virtual Desktop over Steam
Link/Meta Link ... VDXR runtime") [C]. VD's foveated streaming blurs the image on Quest Pro [C].
PSVR2 goes through SteamVR, and the one PSVR2 tester's first failure was "SteamVR's environment
rather than game imagery" even though the session was FOCUSED and frames were accepted [C]. Pimax's
own OpenXR runtime was refused outright; Pimax users play via SteamVR [C]. Steam Frame buyers are
already asking whether controls will map "out of the box" [C].

### 2.3 AMD

Every AMD report came from one tester (RX 9070 XT + PSVR2 + SteamVR) [C]. That is the lesson: AMD
coverage in a volunteer project is one person deep. The defects were structural, not tuning: AMD's
shader binaries hash differently, so a hash-keyed shader profile matched **zero** modules on AMD
("567 shader variants, zero profile matches") and the HUD classifier matched nothing ("zero
screenUi=1 records") [C]. Anything we key by NVIDIA-captured hashes will silently fall back on AMD.

### 2.4 Performance

Two distinct problems hide under "choppy":

1. **First-launch / focus throttling.** "Sometimes the first time I start it up it'll be in the 40-80
   range and I just need to make sure the window is active"; "if it seems to run badly I suggest
   relaunching" [C]. The mod author's log analysis found ~66 ms/frame on the slow run vs 11-15 ms on the next,
   and still lists low first-launch FPS as a known issue in 1.1 [C].
2. **CPU bound at high refresh.** "It's super CPU bound. I'm just barely getting 120 fps with a
   9800x3d" [C]. Owners of 4090/5090 cards were the ones complaining, which says the bottleneck is
   not GPU.

Also: Defender scanning on map load caused lag until excluded [C]; HDD installs stutter [C]; nobody
answered "what is the minimum and recommended GPU? I have a 1070" [C].

### 2.5 Image quality and stereo

AER drew consistent criticism once players had it side by side with expectations: "AER just doesn't
cut it", "eye strain", blur while moving with the stick, "feels like ... reprojection" [C]. The mod
made single-frame stereo the default in 1.0 in response [C]. The AA setting silently reverting and a
hard error above 110% render scale also frustrated people, because the launcher and the game
disagreed about who owned the setting [C].

### 2.6 HUD / UI

Three independent players could not read health and ammo: too low, too far left, stuck at the bottom
of the view "so when I'd look down on them, they stay out of view" [C]. On PSVR2 the lens edge makes
peripheral UI unreadable ("never liking how cyberpunk's in game UI elements were in the blurry edge of
the psvr2") [C]. The mod's 1.0 wrist HUD was noticed and praised within days ("health and stats on
your left arm") [C]. One player's counterpoint ("for Doom you really don't need to see much of the
UI") suggests an option to hide it, not a reason to skip it.

### 2.7 Controls

The most detailed complaint is worth reading in full: weapon swapping
bound to the same stick as turning "leads to lots of unwanted weapon swapping while mid fights", and
the player asks for a button-only swap and a hold-to-open wheel. The same player asked for an
optional crosshair dot. UEVR's tracker shows the same request independently ("dedicated crosshair
... easier to align 6dof weapons", issues #175, #332) [C]. Partial stick travel moving too slowly
needed a dedicated 1.03 fix [C]. One user reported all buttons dead except turning [C], the classic
symptom of an interaction profile or binding mismatch. A PSVR2 player asked for DualSense play [C];
DOOM Eternal on gamepad is the flat default, so a seated gamepad mode costs little.

### 2.8 Comfort

Comfort comments were fewer than expected and mostly self-reported tolerance: breaks, "VR legs",
"wait if you get motion sick easy" [C]. Nobody who played complained about glory kills; the mod runs
them as physical punches plus an optional flat "Cine Window" for authored camera motion [C]. The
doubt comes from people who had not tried it ("too fast paced") [C].

### 2.9 Antivirus and trust

Defender ML detections on both of the mod's executables are documented in every release note [C]. A
player posted VirusTotal 1/71 and a third-party source review to reassure others [C]. The installer
hub hit the same issue with Bitdefender and fixed it by restructuring its scripts until VirusTotal
read 0/65 [C]. Open source helps here only if builds are reproducible and signed [U].

### 2.10 What testers praised

- Install: "so simple to install and use. Absolute gold standard"; "You literally just download the
  mod and run the launcher" [C].
- Feel: "plays like a native"; "One of the best VR Mods ever created"; "wipes the floor with the
  official 'made for VR' Doom game" [C].
- Physical melee/glory kills, PSVR2 adaptive triggers and haptics, the wrist HUD [C].
- Free and open source was used by defenders as the reason critics should be patient [C].

---

## 3. Wishlist and deal-breakers

Features players ask for, with where the ask came from:

| Feature | Evidence |
|---|---|
| Tracked motion controls, 6DoF, two-handed weapons | The universal baseline; absence is the #1 complaint about R.E.A.L., Vk3DVision, vorpX [C]; full-VR mods lead their feature lists with it [C] |
| Physical melee / glory kills (not teleport) | Explicit contrast with VFR [C] |
| Smooth locomotion by default, snap/smooth/no turn | VFR teleport complaint [C]; existing full-VR mods ship Smooth/Snap/No turn [C] |
| Readable, body-anchored HUD; option to hide | Section 2.6 [C] |
| Optional crosshair / laser, correct in alt-fire | Section 2.7, UEVR #175/#332 [C] |
| Weapon selection that cannot fire by accident | Section 2.7 [C] |
| Left-hand mode | VFR review [C]; existing full-VR mods ship a left-hand mode with stick swap [C] |
| Recenter and aim-angle offset | VFR "no re-centre" [C]; UEVR #59/#91 [C] |
| PSVR2 adaptive triggers, bHaptics | Positive reaction where a mod supports them [C] |
| Native controls on non-Quest controllers (Index, Pico, WMR, Frame) | UEVR #14 (33 comments) [C]; r/SteamFrame [C] |
| Seated / gamepad play | DualSense ask [C]; Vk3DVision users played this way [C] |
| Native single-pass stereo, not AER | Section 2.5 [C] |
| Linux / Proton | two asks on the DOOM 2016 mod, UEVR #280 [C] |
| Texture/visual upgrades | one ask [C] |

Full body, finger tracking and holsters did not come up in the DOOM threads we read [C for absence
in our sample]. Given DOOM's weapon wheel, holsters are a low priority [U].

Deal-breakers named by players, in their words: "aim with your head" [C]; weapon swapping on the turn
stick [C]; HUD they cannot see "when the pressure ramps up" [C]; "most of the buttons don't work" [C];
AER eye strain [C]; for the doubters, speed itself [C].

---

## 4. Setup and install expectations

What players praise and what trips them, merged:

1. **Extract and run one launcher** is the expectation existing full-VR mods set and players celebrate [C]. It also
   says: do not copy into the game folder [C]. The launcher must find the Steam install itself [C].
2. **Clean uninstall that restores settings.** "like the pirate torrent guys give you an uninstall
   revert all settings" (installer hub #44, after a mod's resolution change broke flat play) [C].
   One mod's FAQ admits resolution settings need re-adjusting after VR play [C].
3. **Detect conflicts, don't document them.** Luke Ross files, ReShade/OpenXR Toolkit layers, overlays,
   admin elevation, wrong GPU, stale driver, VD foveated streaming [C].
4. **Clear failure text.** A launcher hanging at "Doom started. Verifying VR image..." is the bad example;
   a Pimax message ("Incompatible Runtime ... Please switch to SteamVR") is the good one [C].
5. **One-click log bundle.** Testers of the DOOM 2016 mod zipped logs by hand (`Logs.zip`, `logs2.zip`, ...) and
   investigations stalled on missing shader dumps and unknown build provenance [C, history].
6. **Survive game and mod updates.** Installer hub issues #29/#34 and #60 show how often upstream
   changes break instructions [C].
7. **Minimum and recommended specs stated up front.** Unanswered in the DOOM 2016 mod's thread [C].
8. **Coexist with other mods** where possible; at least name the ones that cannot coexist [C].

---

## 5. Hardware landscape

From the August 2026 Steam Hardware Survey (full table in `hardware-landscape-2026.md`) [C]:

- Quest family 65.99% of headsets (Quest 2 26.97, Quest 3 26.01, Quest 3S 11.50, Pro 1.51). All
  Meta/Oculus 71.21%.
- Index 11.07%, Pico 4 4.48%, Rift S 3.39%, Vive 2.39%, **PSVR2 2.24%**, WMR 2.08%, Rift 1.83%,
  **Bigscreen Beyond 1.25%**.
- **Steam Frame** launched 2026-09-18 (Wikipedia), too late for the August survey. Wireless via Steam
  Link, eye tracking, gamepad-like controllers [C for the listing; U for its eventual share].
- GPUs: 24 of the top 25 entries are NVIDIA or integrated; the top discrete AMD part is the RX 9070 XT
  at 1.46% [C]. The one AMD tester in section 2.3 had exactly that card [C].
- 1.49% of Steam users have a headset [C].

Implications:

- **Quest over a streaming runtime is the majority case.** That means VDXR (Virtual Desktop),
  Meta's PC OpenXR runtime (Link / Air Link), and SteamVR (Steam Link) as three separate runtimes for
  the same headset. Players prefer VDXR [C]. Quest 2 users (27%) are likely on older GPUs [U].
- **SteamVR carries everything else**: Index, Pico (via VD or Pico Connect into SteamVR [U]), PSVR2,
  Beyond, Vive, WMR via its driver, and now Frame.
- **AMD is small in share but a known failure multiplier.** One AMD tester found four structural
  defects in the DOOM 2016 mod [C].
- Pimax's own OpenXR runtime is small and was refused by the DOOM 2016 mod; supporting SteamVR on Pimax is
  enough for 1.0 [U].

### 5.1 Testing-priority matrix

Priority: **P0** gates every release, **P1** before a public release, **P2** best effort / community
testers, **P3** later.

| Runtime | Headset (examples) | NVIDIA RTX 30/40/50 | AMD RDNA3/RDNA4 | Intel Arc |
|---|---|---|---|---|
| VDXR (Virtual Desktop) | Quest 3/3S/2, Quest Pro, Pico 4 | **P0** | **P1** | P3 |
| Meta PC OpenXR (Link / Air Link) | Quest 3/3S/2 | **P0** | P1 | P3 |
| SteamVR (lighthouse) | Index, Vive, Beyond | **P0** | **P1** | P3 |
| SteamVR (PSVR2 adapter) | PSVR2 (+ PSVR2 Toolkit) | P1 | **P1** (known failure history) | P3 |
| SteamVR (Steam Link) | Steam Frame, Quest | P1 | P2 | P3 |
| SteamVR (WMR driver) | Reverb G2 etc. | P2 | P2 | P3 |
| Pimax OpenXR / Pimax via SteamVR | Crystal, Crystal Light | P2 (SteamVR path) | P3 | P3 |
| Linux: Proton + Monado/WiVRn/SteamVR | Quest, Index | P3 | P3 | P3 |

Per cell, the minimum check is: reaches game image (not runtime home), both eyes correct, HUD
visible, all bindings respond, 90 Hz and 120 Hz frame time on first launch and on relaunch.

---

## 6. Prioritized requirements

Derived from sections 1-5. IDs are for cross-reference from design docs.

### Must

- **M1** Tracked 6DoF motion controls with two-handed weapons and physical melee/glory kills; head-aim
  is never the default.
- **M2** Synchronized single-pass stereo; no AER as the default path.
- **M3** A HUD that is always readable: body-anchored (wrist/off-hand) health/armor/ammo, nothing
  critical in the lens periphery; adjustable in headset; hideable.
- **M4** Weapon and equipment selection that cannot trigger from turning or aiming input; hold-to-open
  wheel; button cycling as an alternative.
- **M5** Extract-and-run launcher; nothing copied into the game folder; uninstall leaves the game and
  its settings exactly as before (back up and restore any config we change).
- **M6** Preflight checks with plain error text: game build, runtime and its vendor, conflicting mods
  (Luke Ross, other layers), ReShade/OpenXR Toolkit layers, overlays, admin elevation, GPU used by the
  game vs the headset, driver version floor.
- **M7** Correct behaviour on VDXR, Meta PC OpenXR and SteamVR, including a clear message if the
  runtime shows its own environment instead of our frames.
- **M8** Vendor-neutral shader handling: nothing correctness-critical keyed only by NVIDIA-captured
  shader hashes; AMD must not silently fall back.
- **M9** First-launch performance equal to relaunch performance (focus/foreground handling), and
  CPU cost budgeted for 120 Hz.
- **M10** Smooth locomotion default with snap/smooth/no turn, left-hand mode, recenter.
- **M11** One-click diagnostics bundle: logs, build hash, game build, runtime, GPU/driver, settings,
  optional frame capture.

### Should

- **S1** Optional crosshair dot and laser that stay correct through alt-fire modes.
- **S2** Interaction profiles beyond Touch: Index, Pico, WMR/Reverb, PSVR2 Sense, Frame; per-profile
  prompts; a rebinding file.
- **S3** Comfort options for authored camera motion (flat window for cinematics, vignette on dash and
  meathook) with immersive defaults.
- **S4** PSVR2 adaptive triggers and bHaptics as optional, never blocking startup.
- **S5** Stated minimum and recommended specs, per refresh rate.
- **S6** Signed releases and reproducible builds; a known-false-positive note with VirusTotal links.
- **S7** Launcher detects VD foveated streaming and warns, and warns on HDD install.
- **S8** Seated gamepad mode (DualSense / Xbox pad) with head-relative stereo only.

### Could

- **C1** Linux/Proton support notes, tested on at least one runtime.
- **C2** Texture/visual extras and upscaling choices.
- **C3** Fortress of Doom "sightseeing" interactions (one wish, low cost if the hub is already in VR).
- **C4** Pimax native OpenXR support.
- **C5** Eye-tracked foveation on Frame / Quest Pro via VDXR.

---

## 7. Top 10 failure modes to design against

1. **VR never appears**: runtime home or black screen while the game runs (PSVR2 SteamVR case;
   Vk3DVision black eye; stale NVIDIA driver). Detect "frames submitted but not visible" and say so.
2. **First launch runs at half speed** and players blame the mod or their PC (a known issue in the
   DOOM 2016 mod's 1.1). Owns foreground/focus handoff and measures it.
3. **HUD unreadable** under pressure: too low, off to the side, in the lens edge, or duplicated per eye.
4. **Accidental weapon swaps** from a stick shared with turning.
5. **Buttons dead** because of an interaction-profile or binding mismatch on a runtime or controller
   we did not test.
6. **AMD silently wrong**: hash-keyed fixes that never match AMD shaders, subgroup ops the
   translator does not handle.
7. **Environment conflicts**: leftover Luke Ross files, ReShade or OpenXR Toolkit layers, overlays,
   admin elevation, wrong GPU on laptops.
8. **Flat game left broken** after VR play (resolution, window mode, AA settings changed and not
   restored).
9. **Antivirus quarantine or scan lag** on unsigned executables.
10. **Support dead end**: no issue tracker, logs zipped by hand, unknown build provenance, launch posts
    derailed by off-topic arguments.

---

## 8. Implications for our design

- **Make the launcher a doctor, not a button.** Most pain in section 2 was environmental and the FAQ grew
  a ten-step checklist. Every FAQ line becomes a preflight check with a specific message and a fix
  button where safe (disable a layer, pick the dedicated GPU). Blocking should name the conflict.
- **Keep issues open on our repo** and give the launcher a "Copy diagnostics" button that writes a
  zip with build hash, game build, runtime name/version, GPU/driver, settings and the last two logs.
  Add issue templates that ask for runtime × headset × GPU up front, because the DOOM 2016 mod's own
  README says "include the headset, GPU and runtime when reporting a rendering issue".
- **Wrist HUD from day one**, with the headlocked fallback only for elements we cannot classify, and
  never in the bottom band of the view. In-headset adjustment, persisted per handedness.
- **Input design pass on selection**: no weapon change on any axis used for turning; wheel on a hold;
  equipment on buttons. Ship per-profile bindings for Touch, Index, Pico, WMR, PSVR2 Sense and Frame,
  with prompts that match the controller in hand.
- **AMD in CI from the first stereo milestone.** Capture and test at least one RDNA3/RDNA4 shader
  corpus alongside NVIDIA; treat "zero matches on this vendor" as a startup error in logs.
- **Budget CPU, not just GPU.** Players with 4090/5090s were the ones calling it choppy. Keep stereo
  single-pass and record CPU frame time per subsystem in the diagnostics bundle.
- **Restore everything we change** in the game's config on exit and on uninstall.
- **Release hygiene**: signed binaries, a plain gameplay trailer, plain README language, and VirusTotal
  links per release. Keep the project's public voice about the mod.
- **Test matrix** as in section 5.1; P0 cells gate every release. Recruit at least two AMD and one
  PSVR2 tester before public release, because one person carried all of the DOOM 2016 mod's AMD coverage.
- **Comfort defaults** follow doc 06; the evidence here says players who try it mostly adapt, and the
  loudest comfort worry is from people who have not tried it. Offer the options, do not water down
  the default.

---

## Sources

Community extracts (quotes, links, fetch dates) are in:
- `reference/community/existing-eternal-vr-options-feedback.md`
- `reference/community/flat2vr-tooling-feedback.md`
- `reference/community/hardware-landscape-2026.md`

Primary URLs:
- Steam DOOM (2016) VR mod thread: https://steamcommunity.com/app/379720/discussions/0/563668239243051471/
- Steam DOOM Eternal VR threads: https://steamcommunity.com/app/782330/discussions/search/?q=vr ;
  https://steamcommunity.com/app/782330/discussions/0/2278205083644529303 ;
  https://steamcommunity.com/app/782330/discussions/0/3881596897253177293/
- Steam DOOM (2016) VR threads: https://steamcommunity.com/app/379720/discussions/0/3887226396787323119/ ;
  https://steamcommunity.com/app/379720/discussions/0/3581993633017107435/
- Reddit (via https://api.pullpush.io archive): r/virtualreality 1we81ze, 1wijd97, 1wf14w7, 1wphtp3;
  r/PCVR 1we7zwq, 1we80h1, 1wmlhi9; r/PSVR2onPC 1we7a3e, 1wn9fhm, 1l5f8km; r/Doom 1wfcxof, 1we9yxw,
  12h9g6k, qlgihc, nal23r; r/DoomMods 1wjg5fe; r/uevr 1wk6pyl; r/SteamFrame 1wln15c, 1wpgavb;
  r/QuestPro 1wen00m; r/flat2vr 18vlh5l; r/OculusQuest p4sntf; r/VRGaming 1mn5dpa
  (each at https://reddit.com/comments/<id>/)
- Vk3DVision DOOM Eternal issue: https://github.com/helifax/Vk3DVision-Public/issues/3
- vorpX forum: https://www.vorpx.com/forums/topic/doom-eternal-desktop-viewer/
- UEVR issues: https://github.com/praydog/UEVR/issues (#14, #56, #59, #62, #91, #106, #110, #140,
  #175, #187, #280, #332, #348, #414)
- PCVR Mods Installer Hub issues: https://github.com/Mr-Nlce/PCVR-Mods-Installer-Hub/issues (#4, #17,
  #19, #29, #31, #34, #44, #46, #55, #60)
- Road to VR, DOOM VFR review: https://roadtovr.com/doom-vfr-review/
- MIXED, R.E.A.L. on Patreon: https://mixed-news.com/en/real-vr-mod-dlss-ray-reconstruction/
- PCVR Gamer on a DOOM (2016) VR mod: https://pcvrgamer.net/the-ultimate-doom-vfr-mod-a-real-deal-upgrade/
- Steam Hardware Survey Aug 2026: https://store.steampowered.com/hwsurvey/ ;
  https://store.steampowered.com/hwsurvey/videocard/
- UploadVR, SteamVR usage Feb 2026: https://www.uploadvr.com/steamvr-usage-february-2026-steam-hardware-survey/
- Steam Frame: https://en.wikipedia.org/wiki/Steam_Frame
