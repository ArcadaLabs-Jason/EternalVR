# EternalVR architecture

Status: design baseline, 2026-09-25, revised the same day after four design QA reviews (new and
superseding decisions T-021 to T-077 and D-028 to D-037 in `DECISIONS.md`). Derived from the research
notes in `docs/research/` (cited as R01..R15; requirements in `SCOPE.md` are REQ-01..REQ-20). Items
marked **[rig]** depend on measurements or experiments on real hardware and may change the design;
section 16 lists them in the order we intend to run them.

## 1. Goals and non-goals

**Goals**

- Full DOOM Eternal campaign in VR: The Ancient Gods 1 and 2, Master Levels, Horde Mode and the Fortress hub.
- Single-frame stereo. Both eyes come from one engine frame, and the headset pose is used every frame.
- Tracked motion controllers. The weapon aims from the hand, independently of the head.
- **Seated play is first-class** and a primary tested configuration. Standing and room-scale are
  supported.
- Zero-setup posture: seated or standing needs no configuration; posture is detected automatically,
  with an optional launcher override (section 6.2).
- VR-native UI. The HUD and menus leave the eye buffers and are shown on OpenXR composition layers:
  a wrist-glance HUD, and subtitles, notifications, pop-ups and tutorial prompts that are fully
  readable (D-035).
- **Survives game patches.** The engine is reached by name through the game's own type information, not
  through fixed addresses.
- **Vendor-neutral performance features:**
  - DLSS 4.x as the baseline, with DLSS 5 as a stretch goal.
  - AMD and Intel through OptiScaler (FSR 3.1, XeSS; FSR 4 where the GPU supports it, if time
    allows), with the game's DLSS option unlocked by our gate patch (T-072) and the game's native TAA
    as the fallback (D-031). No FSR1.
  - Fixed foveation on every headset. Eye-tracked foveation where gaze is available is a v1 if time
    allows item.
- A desktop launcher is the primary configuration surface. It keeps savable per-player profiles and
  per-profile bindings.
- Clean, maintainable code (section 15).

These goals are the Must requirements in `SCOPE.md`; items marked "if time allows" are its Should
items and do not block v1.

**Non-goals (v1)**

- BATTLEMODE and any online mode. VR refuses to start, or every VR feature turns off and the game carries on
  flat (section 4a).
- Alternate-eye rendering as a player-facing mode. It may exist only as a bring-up and debug aid.
- DLSS frame generation. It adds latency and uses a stale head pose.
- Full-body avatar. Hands only.
- In-headset settings menus, beyond an optional small quick menu much later.

## 2. Principles

1. **Resolve by name, fail per feature.** Every engine touchpoint goes through a resolver that returns
   either a verified handle or a reasoned failure. A failure turns off only the feature that depends on
   it, and the launcher and log show exactly which ones are off. No fixed addresses outside a last-resort
   per-build table. **The one exception is multiplayer safety:** the BATTLEMODE guard and the
   anti-cheat tripwire fail closed, so if they cannot be resolved VR does not start (section 4a, T-109).
2. **Engine-agnostic core, thin engine adapter.** OpenXR, the Vulkan stereo machinery, UI composition,
   input mapping, comfort and settings know nothing about DOOM. Only `engine/eternal` (engine internals)
   and `game/eternal` (game-design data such as actions and player dimensions) do. This lets
   the Vulkan core run under capture replay without the game (R09), and keeps the door open for later
   id Tech 8 work.
3. **The headset owns view rotation.** The game may move the camera but never rotates it or changes its
   projection. Authored rotation is stripped per classified game state (R06).
4. **Measure before optimising, cache before shipping.** Pipeline work happens at the game's own
   pipeline-creation time and is cached on disk. Target overhead for our stereo work is under 0.5 ms
   GPU and 0.5 ms CPU per frame for multiview (R11, T-075). The presenter's interop copies are a
   separate budget line, 1.0 ms GPU at Quest 3 size as a starting value, checked in M3 (T-040).
5. **Headset-free by default.** Every subsystem is testable without a headset, and most without the
   game (section 14).

## 3. Process and component overview

```
 Launcher (C#, WinForms, .NET Framework 4.8)            tools/rig scripts (dev only: local launch)
   profiles, preflight, runtime choice, DLSS DLL mgmt,
   OptiScaler download, diagnostics bundle, launch with per-process env
          |
          | starts DOOMEternalx64vk.exe or doomSandBox/DOOMSandBox64vk.exe
          v
 DOOM Eternal process
   +--------------------------------------------------------------------------------+
   | EternalVR layer DLL (Vulkan implicit layer VK_LAYER_ETERNALVR, env-gated)      |
   |                                                                                |
   |  platform/   settings (TOML, hot reload), logging, diagnostics, kill switches  |
   |  vkcore/     layer dispatch, object tracking, image classes, stereo transform, |
   |              SPIR-V patcher, pipeline cache, queue lease, capture tool         |
   |  xr/         OpenXR instance/session, frame loop (present hook), presenter     |
   |              (D3D12), spaces, actions,                                         |
   |              composition layers (projection + depth, UI quads/cylinders)       |
   |  features/   ui compositor, comfort, hands, haptics, upscaling,                |
   |              foveation, posture/calibration                                    |
   |  engine/eternal/                                                               |
   |              resolver (anchors, RTTI, typeinfo, cvars, console unlock),        |
   |              hooks, game-state classifier, camera control, usercmd injection,  |
   |              weapon/muzzle, UI render-target capture, audio listener,          |
   |              NGX (DLSS) interception                                           |
   +--------------------------------------------------------------------------------+
```

## 4. Injection and launch

- **Layer:** a Vulkan implicit layer named `VK_LAYER_ETERNALVR`, dormant unless
  `ETERNALVR_ENABLE_LAYER=1` (`ETERNALVR_DISABLE_LAYER=1` always wins). Its scope and gating are T-109:
  active only in `DOOMEternalx64vk.exe`, pass-through in every other process (the idStudio sandbox exe
  included, until v1.x), both variables read in code as well as in the manifest, and `gfxrecon-replay`
  a host only in development builds with `ETERNALVR_REPLAY=1`. The full set of environment variables
  is in section 15.
  - The launcher registers the manifest under `HKCU\...\Khronos\Vulkan\ImplicitLayers` for the launch
    and removes it afterwards, so nothing stays registered between launches. This is the end-user route.
  - For development, `tools/rig/run.ps1` loads the layer through the process environment:
    `VK_ADD_IMPLICIT_LAYER_PATH` pointing at the build's manifest folder, so development runs exercise
    the implicit-layer path, the manifest gating and the ordering among other implicit layers that
    players get (T-079). Fallback if the layer does not appear in the loader's layer list:
    `VK_LAYER_PATH` plus `VK_INSTANCE_LAYERS`, recorded in the run. The loader ignores these variables
    in an elevated process, so neither the scripts nor the game ever run elevated (T-108). Until the
    launcher exists (M4.5), every launch is a development launch (T-068).
- **Launch** (T-109):
  - The launcher starts `DOOMEternalx64vk.exe` directly with `SteamAppId=782330` and the game root as
    the working directory, only with Steam running and logged in; a hand-off gives a named error, never
    a silent flat launch. The Electron launcher (`idTechLauncher.exe`) is not involved (R03); it counts
    as the game for the "already running" check, and its layer check (`CheckBlacklistedLayers`) is one
    more reason a stale registration must never outlive a launcher start (T-093).
  - It chooses the OpenXR runtime per launch via `XR_RUNTIME_JSON`, so the user's global runtime choice
    is never touched (R01). The default is the system's active runtime; the runtimes it can offer are
    found by the discovery rule of T-110 (the registry keys read-only, SteamVR through the Steam
    libraries, known install paths, a browse option); the choice is remembered per machine with a
    per-profile override, and a refusal names the runtime it tried and how to switch (T-094).
  - It checks that the runtime is up and the headset connected before launching, because the layer
    needs an OpenXR system when the game creates its Vulkan instance (section 6, T-082).
  - It isolates known-bad OpenXR API layers through their `disable_environment` variables: OpenXR
    Toolkit, the ReShade OpenXR layer, and Virtual Desktop's compatibility layer when the chosen
    runtime is not VDXR (R01, R02, T-110).
- **Game builds** (T-094). The launcher hashes the exe. A known hash (listed in the release notes and
  `build-info.json`) is green. An unknown hash gets a warning and the resolver runs; if any core
  feature does not resolve (the camera hook, the present path, the BATTLEMODE guard), VR is refused
  with a message naming the feature and asking the player to wait for an update. Other features fail
  on their own (principle 1).
- **Coexistence:**
  - Never occupy the `XINPUT1_3.dll` proxy slot; Meathook uses it (R03).
  - idStudio/decl mods load normally.
  - OptiScaler is downloaded by our launcher, with the user's consent, into the EternalVR data folder
    (section 11). A copy the user installed in the game folder as a proxy DLL conflicts with ours;
    preflight names it.
  - Other implicit layers (RTSS, OBS game capture, ReShade's Vulkan layer, Overwolf and similar) are
    either tested alongside ours or named and disabled for the launch through their
    `disable_environment` variables; the known-bad list is data, not code (T-095).
  - `+r_allowBlackListedLayers 1` is available if the game's layer warning ever fires (R02).
- **Preflight** (launcher): Steam running and logged in,
  elevation (silently disables HKCU layers; the launcher refuses to run elevated), an unwritable data
  folder (a program folder under `Program Files` only warns, T-106), game build detection, runtime
  present, conflicting mods and overlays, GPU vendor/driver, free VRAM, and **HAGS**. The owner
  observed that hardware-accelerated GPU scheduling (`HwSchMode` = 2) caused frame hitching in
  another VR mod on his RTX 4080, and that turning it off fixed it completely. Warn and link to the
  fix; never change it silently (T-021).
- **Settings restore** (the one home for the product's rule; the rig's procedure is T-108): any game
  setting we force is applied through the command line and cvars at runtime; we never edit the
  player's saved config ourselves. Every forced cvar is on one authoritative list kept as data in
  `game/eternal`, with a CI check (T-092). **Primary defence:** at the engine's profile-save point the
  adapter writes the player's own values for the forced keys, so VR values never reach the saved
  profile. **Backstop:** the launcher snapshots the game's config files before launch and restores the
  forced keys after exit, and again at its next start if the previous session did not exit cleanly
  (T-036, R14). **Where the files are:** the active Steam user's `userdata\<id>\782330\remote\PROFILE\`
  and `Saved Games\id Software\DOOMEternal`; every location that exists is used; if neither is found
  the launcher logs it and refuses to force settings (T-094, T-099). **Saves:** before each VR launch
  the launcher copies the save slots into five rotating, checksummed backups, with a "Restore saves"
  action (T-092). Which file each forced cvar persists to, and how Steam Cloud treats a restore, are
  M1 results **[rig: M1]**.

## 4a. Multiplayer and online safety

Players must never be put at risk of a ban, and online play must never be affected (D-021). The
decisions are T-109, T-114 and T-117; this section explains them.

- **Opt-in per launch only.** The layer is dormant unless our launcher starts the game with
  `ETERNALVR_ENABLE_LAYER=1`; the launcher removes its registry entry afterwards and, at every start,
  any entry a crashed launcher left behind. A normal Steam launch is the untouched game. The layer is
  pass-through outside `DOOMEternalx64vk.exe`, and Steam must be running first, so our environment
  cannot reach Steam or other games.
- **No files changed.** Nothing in the game folder is modified or added: no proxy DLLs, no patched
  archives, no `steam_appid.txt`, no DLSS DLL (a newer DLSS DLL is loaded from the EternalVR folder
  through our NGX interposition, T-094). Forced settings are runtime cvars kept out of the saved
  profile (section 4). On the development rig, Meathook runs only in a lab copy (T-108).
- **BATTLEMODE is refused.** A command line that asks for multiplayer (a `game/pvp/` map, network or
  lobby commands, Steam's `+connect_lobby` from an invite) keeps the layer from loading. In the game,
  the signals are the Steam join callbacks the game registers, an accepted invite, the BATTLEMODE lobby
  and game sessions, the BATTLEMODE and online menu screens, and any map load that is not a known
  single-player path (`docs/rig-findings/mp-guard.md`). Any one **latches every game-touching feature
  off for the rest of the process** (T-114): no camera writes, head aim, key injection or keep-active;
  one key-up is posted for any key the layer was holding; the presenter falls back to the flat cinema
  quad; the log says to relaunch without VR for multiplayer. The game is not quit and keeps running
  flat. The hooks stay installed and only watch; we never unhook live. Each signal is tested by
  injection; the real-invite test is not run (D-043). On the Game Pass build there are no Steam
  callbacks: the guard watches the Xbox invite path instead (the invite callback the game registers
  and the invitation decoder), and the launcher starts that build without Steam (T-117).
- **Fails closed.** Every game-touching feature asks the guard before acting and acts only while it is
  armed. If a signal or the anti-cheat check cannot be resolved, the guard refuses: those features stay
  off for the process and the log names the missing point. This is the stated exception to principle 1.
- **Online features are left alone.** No network traffic is touched or generated. Input injection only
  changes the local player command in single-player sessions. Whether campaign-side online features
  (events, stats, Bethesda.net linking) care about modified clients is checked before public release
  [U].
- **Anti-cheat tripwire.** Anti-cheat modules loaded in the game process, or anti-cheat binaries in the
  game folder (Easy Anti-Cheat, BattlEye, Denuvo Anti-Cheat), refuse VR with a clear message; Steam's
  `installscript.vdf` metadata and other games' system-wide installs do not count. Neither DOOM Eternal
  exe contains or imports anti-cheat code today; the check future-proofs us.

## 5. Engine access (`engine/eternal`)

Resolution order (R03, R04):

1. **Anchors.** A handful of long-lived strings (assert and log text) and MSVC RTTI class names are
   located with kananlib and traced to their functions or vtables.
   - Example: `idParticleParm` leads to `FindClassInfo`, which gives the type-info tables.
2. **Type info.** The game's own reflection data covers about 12,000 types with field names, offsets and
   sizes. All struct field offsets are looked up by class and field name at startup.
   - Layouts have survived patches that moved every code address; `idPlayer::hideHudForCinematic` sits
     at +0x84A6 on both Rev 3 and Rev 3.2.
3. **Cvars and commands** go through the engine's own cvar system, found by name. Console command
   restrictions are lifted by the one-byte patch that three existing tools use.
4. **Last resort:** a per-build table of signatures, each verified before use and failing closed
   when it does not match.

Hooks use safetyhook (inline and mid-function). Every hook declares its target resolver, a calling
signature and the feature it belongs to. The hook table can be bisected by config, because a wrong
signature can go unnoticed for weeks. A `evr_selftest` console command and a launcher
diagnostic print every anchor, field and hook with its status.

**Resolver checks (T-060):** a rig script runs the resolver against the current retail exe (and the
archived exes) and fails if coverage regresses (R04). It runs after every game update and before every external
build. The September 2026 updates left the retail exe's hash unchanged (R03 section 2), so the current
retail exe is Rev 3.2. Checks against older retail exes (Rev 3, Rev 3.1) are v1.x. No self-hosted
GitHub runner is used in v1.

Known engine facts we build on:
- **Comfort cvars** (R03, R10): `pm_noBob`, `view_skipKicks/Shakes/Blur`, `hands_offset*`,
  `hands_fovScale`, `hands_show`, `g_showHud`, `r_znear`.
- **Aim cvars:** aim assist is on by default for joysticks and gets forced off (R13).
- **Tick rate:** one game tick per rendered frame (`com_fixedTic 1`), so the head pose updates every
  frame (R11).
- **Units:** 1 game unit = 1 metre **[rig: confirm]** (R10).

### 5.1 Engine-native stereo and VR remnants [rig, decision point]

The retail game carries two inherited systems (R15):

1. **A stereo render scaffold (id Tech 5/6 lineage, also in DOOM 2016):**
   - `stereoRender_*` cvars
   - `multiView_60Hz` ("render both each frame")
   - `r_debugInvert2ndView` ("invert the second render view in order to debug multiview")
   - second-view fields such as `worldViews`, `screenViews`, `viewIndex` and `guiOriginOffset`
2. **The DOOM VFR device and input layer:**
   - `vr_*` cvars
   - `idVRSystem`, `idVRHeadMountedDisplay`, `idVRInput`, `idVRController`
   - `K_STEAMVR_*` and `K_PSMOVE_*` key codes

   Only `_Dummy` backend classes have vtables in retail; there is no OpenVR, Oculus or PSVR backend. The
   same set ships in Indiana Jones and the Great Circle, so it is live shared id Tech 7 code.

Estimated outcomes before testing, as a judgement:

| Outcome | Estimate |
|---|---|
| Working hardware backend | about 3% |
| Parts present | about 60%: either the dummy device plus `vr_enable` renders a second view, or the switch does nothing |
| Names only | about 37% |

The architecture keeps **stereo production behind an interface**. `StereoSource` is one of three:
- `MultiviewTransform` (section 7), the default.
- `EngineNativeStereo` drives the engine's own second view with OpenXR poses by hooking the dummy HMD's
  virtual functions, then copies the two eye targets. If the engine can render both views, multiview
  becomes an optimisation rather than the foundation.
- `SynchronizedSequential` (D-032, D-037, T-066, T-069), the candidate fallback: the engine's render
  entry is re-entered once per eye within one game tick, with the camera hook supplying each eye's
  pose and projection (the UEVR and REFramework pattern, R04 sections 1.1 and 1.2). It is **unproven on
  id Tech 7**: R04 shows it only on Unreal and RE Engine and recommends it as a debug fallback (R04
  section 5 item 1). Conditions (T-069):
  - The second entry must not advance per-frame state a second time: frame index, per-frame command
    pools and rings, GPU simulation and particle steps. Temporal history (TAA, exposure,
    previous-frame matrices) is kept per eye; Umbra visibility is queried per eye, or occlusion is off.
  - "No view-constant patching" holds only if the render view accepts an arbitrary asymmetric
    projection; otherwise the projection is overridden in the view the hook builds, or a symmetric
    frustum enclosing the eye's is rendered and cropped.
  - The present hook fires once per engine frame and carries both eyes; both renders share one frame
    serial and ring slot; each eye's NGX evaluate gets its own feature instance.
  - M1 finds the render entry point, the state it advances, and whether the dormant `viewIndex` /
    `multiView_60Hz` scaffold already loops over views.

  About twice the render-thread CPU. Not alternate-eye rendering, so consistent with D-004. It is
  built only if the stereo spike (section 7) or the M4 oracles call for it, and M4 lists its own
  criteria.

Everything downstream (presenter, UI, upscaling, foveation) consumes the same per-eye outputs either way.
The 7-step rig test (about 2 hours) is in R15 section 6.

## 6. OpenXR (`xr/`)

- **Presenter interface** (R01, R02); v1 has one implementation:
  - **`D3D12Presenter` (default).** Our own D3D12 device matched by adapter LUID owns the OpenXR
    session.
    - This avoids the runtime sharing DOOM's queue and the whole family of shared-device
      problems: nested SteamVR instance, DEVICE_LOST on VD/Meta, locks around runtime calls.
    - It gives WMR support.
    - **Data path (T-040, provisional).** The game allocates its images itself, and an image cannot be
      made exportable after it is allocated. Every image that reaches the runtime therefore costs
      **two copies**: a Vulkan copy into a shared image, then a D3D12 copy (a small shader that crops,
      R01 section 7 D10) into the XR swapchain image **[rig: measure]**.
    - **Colour encoding (T-080, provisional).** The game's final tonemapped image is taken to be
      sRGB-encoded (R01 marks this unverified; M1 reconnaissance records the final eye image's format
      and encoding, and the rule below is revisited if it is a linear or pre-encode target). The
      presenter picks an `_SRGB` swapchain format, whose D3D12 images are typeless, and writes the
      game's bytes unchanged through a `*_UNORM` render-target view (or reads through an `_SRGB` view,
      which decodes, and writes through an `_SRGB` view, which encodes). The encoding is preserved,
      never applied a second time: a second encode gives a washed-out image. The game's HDR output is
      forced off while VR is active (`r_hdrDisplay 0`), because a PQ or scRGB final image would break
      this path.
      Three kinds of image cross: each eye's colour; each eye's depth whenever the depth layer is
      submitted (on by default for Meta and VDXR); and the UI target for the UI layers (section 8). The
      slot layout grows as each first crosses: colour in M3, depth in M4, UI in M6 (T-056). Allocating
      the final stereo target as exportable, which would save the first colour copy, is a later
      experiment.
    - **Depth (T-057).** `vkCmdCopyImage` cannot copy the depth aspect of a D32S8 image into another
      format, and D3D12 compute cannot write a depth swapchain. Depth therefore crosses as `R32_FLOAT`:
      a Vulkan shader copies it into the shared image, and a D3D12 draw writes it into the depth
      swapchain image. `nearZ` and `farZ` are submitted in metres, which are game units divided by
      (unit scale × world size), since game units = metres × `world_scale` × world size (R10 section
      2.1; T-080); they match the eye projection `P_e`'s near plane. Reversed-Z: for an infinite far
      plane `nearZ = +inf` and `farZ` is the near plane (R01 section 7 D10).
    - **Ownership.** The shared images and both fences are created on the D3D12 side with shared NT
      handles and imported into Vulkan (D3D12 resource and D3D12 fence handle types; the fences become
      Vulkan timeline semaphores), as R01 D1 describes. Vulkan does not guarantee that D3D12 can open
      its opaque Win32 handles (the external-memory compatibility rules cover only the same driver and
      device UUID), so this direction is the one used.
    - **Import rules (T-080).** Each shared image is imported as a dedicated allocation
      (`VkMemoryDedicatedAllocateInfo`; for a D3D12 resource handle the allocation size is ignored).
      The `VkImage` is created with `VkExternalMemoryImageCreateInfo` and the same format, extent, mip
      count, layer count and optimal tiling as the D3D12 resource. Every usage is checked first with
      `vkGetPhysicalDeviceImageFormatProperties2` plus `VkPhysicalDeviceExternalImageFormatInfo`:
      `TRANSFER_DST` for colour, `STORAGE` or `COLOR_ATTACHMENT` for the `R32_FLOAT` depth, and the D3D12
      resource carries the matching `ALLOW_UNORDERED_ACCESS` or `ALLOW_RENDER_TARGET` flag.
    - **Ownership transfer (T-081).** Each frame, on DOOM's queue: an acquire barrier from
      `VK_QUEUE_FAMILY_EXTERNAL` to the graphics family before writing the slot (`oldLayout`
      `UNDEFINED`, since the slot is overwritten), then after the copy or shader write a release
      barrier from the graphics family to `VK_QUEUE_FAMILY_EXTERNAL` into the agreed layout (`GENERAL`
      unless the driver documents another). Without the release the D3D12 reader sees undefined
      contents; on NVIDIA the release is what resolves compression metadata. On the D3D12 side the
      resource starts in `COMMON`, is transitioned to `PIXEL_SHADER_RESOURCE` for the read and back to
      `COMMON` before "slot read" is signalled.
    - **Ring and fences.** The shared images form a 3-slot ring; each slot holds every image of one
      frame. Vulkan signals "slot written" after its copies, and D3D12 waits for it on its own queue.
      D3D12 signals "slot read" for **every** written slot, including frames it skips or times out on
      (then without copying). DOOM's queue never waits on the D3D12 side: before recording the Vulkan
      copies, the present hook reads the slot's "read" value on the CPU (`vkGetSemaphoreCounterValue`)
      and, if the slot is still in use, skips the copies for that frame and counts the drop.
      `xrWaitSwapchainImage` has a bounded timeout, with the state machine the OpenXR rules require
      (T-081): after `XR_TIMEOUT_EXPIRED` the image stays "acquired, not yet waited"; the next frame
      waits on that same image again and does not acquire another (a naive acquire per frame runs out
      of images and gets `XR_ERROR_CALL_ORDER_INVALID`); an image is released only after a wait that
      succeeded, never after a timeout. A timed-out or skipped frame is ended without copying and
      **repeats the last good image**: since `xrEndFrame` uses the most recently released image, the
      projection layer is submitted again with it (logged as a repeat). The layer is never dropped
      while a good image exists, because alternating repeats and empty frames strobes the whole field
      of view. After a long run of repeats the view fades to a static dim layer until good frames return
      (T-091; the timing is an M7 comfort target, T-111). When SteamVR reports a display time no later
      than the previous one, the frame resubmits the last good image with the new time (a named quirk
      flag, T-110). The frame is logged,
      and the slot's "read" value is still signalled. A slow runtime can therefore cost frames in the
      headset, never a stall, a flicker or a lost device in the game.
    - **Present-hook copy (T-081).** What is copied is the game's final eye image: the render-sized
      target before any scale to the window (T-031 decouples the two), located in the M1 capture; if
      it turns out to be the swapchain image, the layer adds `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` at
      `vkCreateSwapchainKHR` after checking `supportedUsageFlags`. When the source is a swapchain image,
      the layer's submit waits on the present's wait semaphores, transitions `PRESENT_SRC_KHR` to
      `TRANSFER_SRC_OPTIMAL` and back, and signals a binary semaphore of its own, which replaces the
      originals in the `vkQueuePresentKHR` call (waiting on a binary semaphore unsignals it, so the
      present must not wait on the game's semaphores again). The layer keeps one such semaphore per
      swapchain image, indexed by the presented image index, and reuses it only after that image is
      acquired again, so a semaphore is never signalled while a previous present still waits on it
      (T-091).
    - **Adapter match.** The D3D12 device must sit on the adapter the runtime names
      (`xrGetD3D12GraphicsRequirementsKHR::adapterLuid`), and DOOM's Vulkan device must be on the same
      one. The layer steers DOOM's physical-device choice to that LUID where the game makes it: it
      filters or reorders `vkEnumeratePhysicalDevices` and `vkEnumeratePhysicalDeviceGroups` by
      `VkPhysicalDeviceIDProperties::deviceLUID` (T-057). That needs an OpenXR instance and system
      before enumeration: the layer creates them in its `vkCreateInstance`, not inside
      `vkEnumeratePhysicalDevices`, which runs under the loader's lock (T-082). A thread-local
      re-entrancy guard is set around our OpenXR calls, so any `VkInstance` a runtime or an OpenXR API
      layer creates from inside them (such as a nested SteamVR instance) passes straight through
      our layer. The runtime must be running and the headset connected when the game starts. If they
      are not, or steering fails, VR refuses to start with a clear message (R01 open question 2). The
      rig has two GPUs (D-044): until steering lands in M4.5, the game's per-application GPU preference
      puts it on the RTX 4080 and the LUID is checked at session creation, a mismatch refusing VR
      (T-111). One OpenXR instance per process, reused if the game creates another `VkInstance`
      (T-110). No cross-adapter copies in v1.
  - **`VulkanPresenter` (v1.x, T-059).** `XR_KHR_vulkan_enable2`, with the runtime given the
    next-layer proc address so its calls bypass our hooks. Built only if M3 or M4.5 measurements show a
    latency or copy-cost problem it would fix.
  - **Instance and device additions (T-082).** Regardless of presenter:
    - At `vkCreateInstance`: if the game's `apiVersion` is below 1.1, the layer enables
      `VK_KHR_get_physical_device_properties2`, `VK_KHR_external_memory_capabilities` and
      `VK_KHR_external_semaphore_capabilities`, which `deviceLUID` and the external-format and
      semaphore queries need.
    - At `vkCreateDevice`: `VK_KHR_external_memory` and `VK_KHR_external_memory_win32`,
      `VK_KHR_external_semaphore` and `VK_KHR_external_semaphore_win32`, `VK_KHR_dedicated_allocation`
      with `VK_KHR_get_memory_requirements2` (on a 1.0 device), `VK_KHR_timeline_semaphore` on a
      device below 1.2, `VK_KHR_multiview` below 1.1, and `VK_KHR_fragment_shading_rate` when the
      foveation path uses it (T-037), which below 1.2 also needs `VK_KHR_create_renderpass2`, and that
      below 1.1 needs `VK_KHR_multiview` and `VK_KHR_maintenance2` (T-091).
    - Features are merged into the structs the game already chains, never added beside them: if the
      game chains `VkPhysicalDeviceVulkan11Features` or `VkPhysicalDeviceVulkan12Features`, the layer
      sets `multiview` or `timelineSemaphore` there (adding the per-feature struct as well is invalid:
      VUID-VkDeviceCreateInfo-pNext-02829 for the 1.1 struct and `VkPhysicalDeviceMultiviewFeatures`,
      VUID-VkDeviceCreateInfo-pNext-02830 for the 1.2 struct). Features needed: `timelineSemaphore`; `multiview`, plus
      `multiviewGeometryShader` / `multiviewTessellationShader` if the game uses those stages;
      `attachmentFragmentShadingRate` for foveation. Each is checked as supported first; a missing
      one disables the feature that needs it, with a logged reason.
    - The game's `apiVersion`, enabled extensions and feature chain are recorded by the M1 menu-scene
      capture (R01 open question 1; `RIG_BRINGUP.md` section 4 step 1).
  - **OpenXR version and extensions (T-110).** OpenXR 1.1 is requested with a fallback to 1.0; each
    extension the design uses (D3D12 enable, depth and cylinder layers, `XR_EXT_local_floor` on 1.0,
    user presence, eye gaze, visibility mask, display refresh rate) is enabled only if the runtime lists
    it, and every feature that needs one has a fallback.
- **Frame loop:** shape 1, inside the present hook, with no dedicated XR thread (section 6.1, T-022,
  T-059).
- **Spaces:** see section 6.2, which also covers `ReferenceSpaceChangePending` (T-045, T-063).
- **Layers:**
  - Projection layer with depth (`XR_KHR_composition_layer_depth`, reversed-Z, on by default for Meta
    and VDXR).
  - UI on cylinder layers where supported, quads elsewhere (SteamVR, WMR and Varjo lack cylinders).
- **Input** uses the action system with separate `gameplay` and `menu` action sets. Suggested bindings
  for every major profile come from one data file (Touch, Index, Vive, WMR/Reverb, Pico). PSVR2 binds
  via Touch/Index on SteamVR.
- **Runtime quirks are data:** runtime name and version map to named quirk flags, overridable in
  config. Startup logs the runtime, version, manifest path, extensions and active implicit layers.
- **Support tiers v1:** SteamVR, Meta PC (Link/Air Link), VDXR. Best effort: Pimax Play, Varjo, WMR
  (via D3D12). In-house testing is on a Quest 3 (VDXR, Meta Link); SteamVR with Index and PSVR2 are
  verified by testers (D-027).

### 6.1 Frame loop and pipelining (T-022, T-023)

id Tech 7 simulates frame N+1 on the game thread while the render thread builds frame N, and the GPU
may still be finishing frame N-1 (R11 section 8.2). The XR calls sit at these points (k is the XR frame
begun just before engine frame N is built):

| Call | Call site | Thread |
|---|---|---|
| `xrEndFrame(k-1)` | Present hook of engine frame N-1, after its D3D12 copy is submitted and the swapchain image released | Presenting thread |
| `xrWaitFrame(k)`, `xrBeginFrame(k)` | Same present hook, immediately after `xrEndFrame(k-1)` | Presenting thread |
| `xrLocateViews` at `predictedDisplayTime(k)` | Same hook; publishes snapshot k | Presenting thread |
| Snapshot read | Engine camera hook, where the render view for frame N is built | The thread that builds the render view (M1, PLAN 1.17) |
| Per-eye constants | Written from frame N's record into its ring slot (section 7) | Render thread |
| `xrEndFrame(k)` | Present hook of frame N, with the poses stored in frame N's record | Presenting thread |

Rules:
- **The render side never waits.** The camera hook takes the latest published snapshot (generation
  counter, no lock) and stores it in frame N's record. If nothing new was published since frame N-1, it
  reuses the last snapshot and counts the reuse.
- **Submit what was rendered.** `xrEndFrame` uses the begun frame's `predictedDisplayTime`, but the
  projection views carry the pose and FOV from the frame record, even when that snapshot is older. The
  runtime's reprojection absorbs the difference (R02 section 9).
- **Frames in flight.** At most one XR frame is open. At most 2 engine frames are in GPU flight, so
  every per-frame resource (frame records, constant buffers, shared images) is a 3-slot ring.
- **No lock across `xrWaitFrame`.** The queue lease and every mutex of ours are released first.
  Blocking there throttles the game to display rate by design; pacing must be even, because the game's
  tick follows frame duration (R11 section 8.1).
- **Session state (T-039).** While the session is running (from `xrBeginSession` to `xrEndSession`:
  READY, SYNCHRONIZED, VISIBLE, FOCUSED), the hook makes every frame call. When `shouldRender` is
  false it still calls `xrWaitFrame`, `xrBeginFrame` and `xrEndFrame` with zero layers, as the OpenXR
  spec requires, and skips only the copies. Before the session begins, in IDLE, in STOPPING once
  `xrEndSession` is called, in LOSS_PENDING and in EXITING, it makes no frame calls and never blocks;
  the game keeps rendering to its window. Recovery from session and instance loss is T-110, built as
  follows (`vkcore/presenter_reconnect.cpp`, `common/xr_recovery.hpp`): after LOSS_PENDING, an instance
  loss event, or `XR_ERROR_SESSION_LOST`, `_INSTANCE_LOST` or `XR_ERROR_RUNTIME_FAILURE` from any call,
  the XR worker (never the present thread) waits for its last D3D12 copy, destroys every OpenXR object
  including the instance, and tries again after 1, 2, 3, 4, 5 s and then every 5 s: a new instance,
  `xrGetSystem`, the runtime's adapter LUID checked against the presenter's D3D12 device, and a new
  session with its spaces, controllers, swapchains and room objects. The D3D12 device, its fences and
  the shared ring stay, so nothing is imported into the game's Vulkan device again; the ring's size is
  kept, and a headset whose maximum swapchain is smaller waits. The game's presents pass through
  meanwhile and the new LOCAL space re-anchors yaw as after a recenter. EXITING is the runtime closing
  the application's VR and stays flat. `ETERNALVR_TEST_XR_LOSS=<seconds>` takes the running session as
  lost once, which exercises the whole path on the OpenXR Simulator (rig run rc1: back in 1.1 s,
  head-tracked frames and controller input as before).
- **Why not a dedicated XR thread.** R01 section 7 D3 proposed one. R02 section 9 and R11 section 8.2
  recommend starting with this shape, and a separate XR worker thread risks Win32 message deadlocks
  (seen with the OpenXR Simulator's lifecycle thread). A dedicated-thread variant is v1.x (T-059),
  built only if measurements show pose-age problems it would fix.

### 6.2 Posture and height (T-029)

- **Default: zero-setup Auto** (D-018). At session start (first stable head pose) and on every user
  recenter, the current head height is mapped to Slayer eye height (1.657 m, times world size). It
  anchors on the real head, not on the game's view, so the game's crouch state at load time does not
  matter.
- **First stable head pose (T-045).** The session is FOCUSED, the user is present
  (`XR_EXT_user_presence` where the runtime has it), and the head pose is valid, tracked and moving
  less than 2 cm over 1 s. A headset lying on a desk must never anchor: without user presence, a pose
  with no motion above tracking noise for several seconds counts as not worn. The thresholds are
  starting values for M3.
- **Automatic recenters** (gameplay entry, R10 section 3.3) are yaw-only and never touch height, so a
  level that loads mid-crouch cannot capture a bad height.
- **A user recenter** (our long-press) re-anchors height as well as yaw. In zero-setup mode it is the
  only way to fix a bad anchor or to switch between sitting and standing. This departs from R10 section
  3.3, which assumes an explicit calibration is the primary height path.
- **Runtime events** (`ReferenceSpaceChangePending`) also fire on boundary resets and tracking
  reconnects, not only on a user recenter, so they re-anchor yaw only. v1 does not try to tell them
  apart (T-063): height is re-anchored only by the user's explicit recenter through our binding.
- **Floor spaces** (`LOCAL_FLOOR`, then `STAGE`, R01 section 7 D6) are used where available to detect
  posture (seated or standing, which picks posture-dependent defaults) and to flag an implausible
  anchor. `LOCAL` is the tracking anchor either way.
- **Launcher options:** an override (Auto / Seated / Standing), a height mode (Slayer height / Real
  height, R10 section 2.2) and an explicit calibration ("stand straight, press both grips"). In the
  calibrated modes recenter is yaw-only, as R10 recommends.
- **Seated defaults (T-074).** When posture is seated, detected or overridden: holster and gesture
  zones sit at shoulder height, not the hip; a hands-jump gesture, if one is built, is off by default;
  the physical-punch threshold works with seated arm motion; the default seated weapon offset and
  controller rest pose keep the hands clear of a desk. The owner's own profile overrides posture to
  Seated; every other profile starts on Auto.
- Provisional until the M4 and M7 playtests.

## 7. Stereo rendering (`vkcore/`)

**Camera model (R02).** The engine renders from one centred camera whose frustum encloses both eye
frusta. Each eye gets an exact 4x4 clip-space transform derived from the game's own projection and the
OpenXR per-eye pose and FOV (handles canted displays and depth).
- Culling, light/cluster binning and CPU-read-back gameplay visibility stay single-pass, in the
  enclosing camera's space.

**Culling (T-038, provisional).** Frustum culling is conservative for both eyes through the enclosing
camera. The rest needs care, because DOOM Eternal culls in three places:

| Culling | Input | Stereo rule |
|---|---|---|
| CPU occlusion (Umbra, `r_useUmbraCulling`) | Umbra query from the camera position | A single apex is not conservative for eyes about 32 mm to either side of it; see the options below |
| GPU triangle culling (compute prepass; `r_gpuTriangleCullingOptions` bits 1 frustum, 2 backface, 4 small triangles, 8 occlusion) | Occlusion uses "a depth mip chain generated from Umbra's SW buffer" (SIGGRAPH 2020 slides), not rendered depth | Forced off on every vendor (T-073), because it reportedly runs only on AMD and consoles and cannot be validated on the RTX 4080; an AMD tester's culling test can re-enable a chosen option |
| Light and decal binning | Hi-Z of the rendered depth (Coenen); the fine raster uses a min/max depth downsample | Fine-tile min/max depth forced to near/far, because a promoted depth downsample would give the binning layer 0 (the left eye) only. The cluster list then wins where tiles would be wrong (R02 section 4) |

Umbra options, chosen on the rig **[rig: M1 cost, M4 culling test]**:
- (a) occlusion off: `r_useUmbraCulling 0`, and bit 8 cleared in `r_gpuTriangleCullingOptions` (or
  `r_skipGPUTriangleCulling 1`). The default until measured.
- (b) Umbra queried once per eye, the visible sets united.
- (c) occluders dilated so the single query is conservative for the eye offsets.

The cvar names are from the 2024 cvar list; whether they apply at runtime and what the chosen option
costs is measured in M4 (T-073). `r_umbraJobKickoff` only schedules the query and is not a switch.
`r_skipGPUTriangleCulling`'s help text says GPU culling only works on consoles and AMD cards, so the
RTX 4080 cannot exercise it; it is therefore forced off (`r_skipGPUTriangleCulling 1`) on every vendor
unless an AMD tester validates an option with the M4 culling test (T-073).
The earlier plan (T-026) to rebuild a mono Hi-Z from both eyes' depth is dropped: that Hi-Z feeds light
and decal culling, not occlusion.

**Image classes.** Fixed when an image is created, with an override table keyed by format, extent
ratio, usage and debug name:
- **stereo:** render-resolution targets, which become 2-layer arrays
- **mono:** shadow atlas, probes, LUTs, simulation, UI
- **ignored:** runtime-owned and NGX-owned

Promotion rules (T-052, provisional):
- **Views.** Eternal is bindless, so a view's image type is fixed per binding: every 2D sampled or
  storage view that can land in a shared descriptor array becomes `2D_ARRAY`, and mono images read
  layer 0 (R02 section 4, bindless paragraph).
- **Closure.** Any image attached in the same multiview pass as a stereo image is promoted too.
- **Memory.** Every promoted image gets layer-owned memory. The engine aliases images in its heaps, so
  a doubled image left in engine memory would overlap its neighbours.

**NGX pass-through (T-033).** NGX records its own compute dispatches into the game's command buffers
and creates its own pipelines. A thread-local "inside NGX" flag, set around every original NGX
create/evaluate/release call, makes every Vulkan intercept pass through: no SPIR-V rewrite, no view
promotion, no barrier translation (R07 section 4.1 step 4, section 9 item 5). Whether NGX's calls
route through our layer at all depends on the `vkGetDeviceProcAddr` it was given **[rig: R07 open
question 4]**; the flag costs nothing if they do not.

**Pipelines.**
- Every graphics pipeline gets a mono twin and a multiview twin (view mask 0b11).
- Stereo compute gets two spec-constant eye variants, or a doubled Z dispatch.
- Render pass v1/v2, dynamic rendering, synchronization2, imageless framebuffers, secondary command
  buffers and query remap are all covered, whichever the game turns out to use **[rig: capture]**.

**Shader patching.** Direct SPIR-V patching with small helper functions written in GLSL, compiled
offline and linked in (the validation-layer GPU-AV technique):
- add `MultiView`
- promote sampled/storage images of stereo resources to arrays indexed by `ViewIndex` (sampled reads
  clamp by spec; fetch/read/write get explicit clamps)
- apply per-eye clip transforms at the identified view-constant loads

**Finding the view constants (T-050, T-070, provisional).** Structural matching: the camera hook knows
`P_c` and `V_c` for the frame. At draw and dispatch time the layer scans the uniform-buffer ranges bound
to a pipeline for 4x4 matrices (row- or column-major). Because the engine may store a combined
view-projection and add TAA jitter, a candidate `M` matches when `P_c⁻¹·M` (or `M·P_c⁻¹`, by
convention) is rigid within tolerance, with the two jitter terms of `P_c` left free; a plain `V_c` or
`P_c` is matched directly. A match gives buffer and offset, which maps to the SPIR-V block member
through its `OpMemberDecorate ... Offset`; patches go at that member's load sites. Results are cached
per pipeline. Debug names are only hints.

**Data-patch fallback (T-071).** For compute that runs the same shader once per eye and reads the
constants in a way we cannot patch, the layer patches the data: between the two dispatches it rewrites
that uniform region with the eye's values using `vkCmdUpdateBuffer`, and restores the original values
after the second dispatch so later readers see the engine's data. Rules: recorded outside any render
pass; at most 65536 bytes, offset and size multiples of 4; the buffer carries `TRANSFER_DST` usage,
which the layer adds at `vkCreateBuffer` to every uniform buffer; a barrier before each update orders it
after the previous dispatch's uniform reads, and one after it makes the write visible to the next
dispatch; another queue that reads the region waits on a semaphore signalled after the update.

**Stereo spike (T-049, T-070, M1.5).** Before any of this is built into the game, the M1 captures
(taken with TAA off and jitter frozen) are replayed offline twice, once per eye, with only the
`gl_Position` patch. `C_e = P_e·E·P_c⁻¹`, where `E` is the eye's fixed offset from the enclosing camera,
so it is hard-coded and needs no `V_c`. **Clip conventions (T-083):** `C_e` is correct only if `P_e`
uses exactly the clip conventions of the game's `P_c`: the Y sign (a negative-height viewport under
`VK_KHR_maintenance1`, or a Y flip in the matrix), the depth mapping (reversed or not, infinite far
plane or not), the near plane, and the depth compare op. Otherwise the image flips or the depth test
inverts; the no-harm check (T-043) assumes the same. M1 reconnaissance records all four from the
capture, and `P_e` is built to match them. The census counts the modules that need semantic patches
(anything beyond `gl_Position` and mechanical image-array promotion) and the images and shaders that
promotion touches. The decision: if the share of distinct SPIR-V modules bound by draws in the two
world capture scenes that need a semantic patch, or the share of those draws, exceeds 15.0%,
synchronized sequential stereo (section 5.1) is reweighed; counts and denominators are recorded
(T-105). The menu scene is mono and has no world view, so it is left out of the
spike and the oracles (T-070).

**Shader classes (T-027, T-051, T-053).** `C_e` is only correct for vertices that went through the
game's centred projection `P_c`. Every shader is classified before patching:

| Class | How it is recognised | Patch |
|---|---|---|
| World | Position built from the view-projection constants | `gl_Position = C_e * gl_Position` |
| Viewmodel | Same shaders as the world; `hands_fovScale 1` is forced permanently, so it uses `P_c` (T-053) | As world. Per-draw handling only if M1 or the spike shows a distinct projection |
| Sky | Rotation-only view (no translation rows), depth at the far plane | Per-eye rotation and projection only, no eye translation |
| Screen-space | Fullscreen triangle or quad from vertex index, no matrix | No clip transform; inputs array-promoted; reconstruction constants patched at their load sites |
| Bin lookup | Forward shaders that index the light and decal bins by `gl_FragCoord` (R02 section 4, binning row) | Remap each eye's fragment coordinate into the enclosing camera's screen space. Classified per use: other `gl_FragCoord` uses, such as fetches from promoted screen-sized images, are not remapped (T-051) |
| Previous clip | Outputs a previous-frame clip position for motion vectors (R02 section 4, temporal row) | `C_e(prev)` on that output (T-051) |
| UI | idSWF passes | Not stereo (section 8) |

A shader that fits no class is logged and left mono, and the feature report names it.

The viewmodel goes through the same depth prepass and shaders as the world, and `hands_fovScale`
(default 1.15) changes constants, not shaders. Forcing it to 1 permanently makes viewmodel draws use
`P_c`. A depth hack done through the viewport depth range acts after clipping, so `C_e` still holds;
one done in the projection matrix would show as a distinct projection in the M1 capture or the spike
census, and only then is per-draw identification built (T-053) **[rig: M1 capture]**.

Supporting rules:
- `spirv-val` runs in debug builds.
- A per-module transform log is kept, and patched modules are cached on disk keyed by input hash and
  patcher version.
- SPIRV-Cross is for offline diagnosis only.
- Shaders are recognised by **structure** (reflection, layout, entry-point interface), never by vendor
  SPIR-V hash, because AMD drivers produce different SPIR-V. Name-based hints are used when debug names exist **[rig]**.

**Per-eye constants (T-041, provisional).** They live in a **reserved top descriptor set** (the GPU-AV
pattern, R02 section 5, item 1): the layer reports `maxBoundDescriptorSets - 1` to the game, appends
its set layout at the top index of every pipeline layout, and re-binds its set lazily before a draw or
dispatch whenever a game bind or layout change could have disturbed it. The game must not use that slot (R02
open question 8, answered in M1). **Padding and limits (T-084):** the indices between the game's
highest set and ours are filled with an empty set layout, since `pSetLayouts` entries may be
`VK_NULL_HANDLE` only with graphics pipeline libraries and independent sets
(VUID-VkPipelineLayoutCreateInfo-graphicsPipelineLibrary-06753). The per-stage and per-layout limits
count every set, so the layer lowers the limits it reports (`maxPerStageDescriptorUniformBuffers`,
`maxDescriptorSetUniformBuffers`, `maxPerStageResources` and the others its set uses) by its own set's
descriptor counts, as GPU-AV does. One buffer updated at "the frame's first submit" would race with the
previous frame's async compute (R11 section 8.2), so:
- The constants are a **3-slot ring** of small buffers, each with its own descriptor set.
- **Writes.** A slot (host-coherent memory) is written from the host before the first submit that
  reads it; queue submission makes those writes visible on every queue. A GPU-timeline update
  (`vkCmdUpdateBuffer`) is not ordered by command order even on its own queue: it needs pipeline
  barriers there, and any other queue that reads the slot must wait on a semaphore signalled after it
  (R02 section 5, item 2; corrected by T-071).
- **Frame association.** The camera hook for frame N assigns frame serial N (section 6.1), and each
  command buffer binds the slot of the frame it belongs to. Which frame that is cannot be taken from
  the serial current at recording time: async compute for frame N+1 is recorded and runs while frame N
  is current. It must come from the engine's own frame context (for example the per-frame command pool
  or frame index the command buffer was allocated from) **[rig: M1 capture]**. Fallback: async compute
  off for stereo-dependent passes (`r_asyncPostProcess 0`), at a cost to be measured.
- **Check.** Debug builds store the frame's `V_c` in the slot. For every draw and dispatch that reads
  our slot, the stored `V_c` must equal the view the same draw reads from the engine's view constants;
  a mismatch is a wrong serial (ROADMAP M4).
- A slot is reused only after the layer's timeline semaphore shows its previous frame finished on every
  queue, async compute included. With at most 2 frames in flight this is never a wait in practice.
- There is no `vkDeviceWaitIdle` in the frame.

**Initially disabled, re-enabled one at a time with per-eye support:** RT reflections, motion blur,
lens flares, chromatic aberration, DoF, film grain, and possibly the temporal SSR/SSAO paths (R02, R11).
Game DLSS is handled by section 11.

**Memory.** Every image promoted to two layers is logged with its size and allocation path, and every
one gets layer-owned memory (T-052), not only those that would overflow an engine pool; a
128 MiB engine pool is easy to exhaust (R11). If that allocation fails, the feature stops with a reserved exit code the
launcher understands. The layer runs its own VRAM check, because the game's check cannot see the stereo
doubling.

**Synchronisation.** One queue lease covers every queue-touching call:
- submits, waits, idle
- sparse binds
- our interop copies and signals

The KHR alias of `vkQueueSubmit2` calls its own downstream pointer.

**Debug-only stereo peek (M3, T-068).** Alternate-eye rendering through the camera hook, or the M1.5
`C_e` patch applied live, gives an early look at stereo and validates the camera and projection hooks
before the multiview patcher exists (D-004 allows it for debugging). Never exposed to players and never
in a build that leaves the rig. Each eye holds its last rendered image while the other eye renders; an
eye is never blanked between its frames, which would flicker at half the display rate (T-091).

## 7a. Render size and aspect (T-031)

- **Decoupled from the window.** The game's render size is set independently of the desktop window, by
  hooking the engine's size accessors or by present scaling, with a real window resize as the fallback. The desktop window can stay small.
- **Per eye, near-square.** Each eye renders at the runtime's recommended size
  (`xrEnumerateViewConfigurationViews`) times our render scale, clamped per axis to the runtime's
  maximum image and swapchain sizes (T-110), never a 16:9 carrier (R11 section 11
  item 2). On a Quest 3 that is close to square.
- **UI target.** idSWF lays out for 16:9, so the UI renders into its own target at a size and aspect we
  choose (about 2048 x 1152), independent of the eye size, and is shown on its own layer (section 8).
  Whether the engine lets the UI target size differ from the scene size is **[rig: M1 capture]**; if not,
  the UI is rendered at the eye aspect and the layer crops or letterboxes it.
- M3 checks the decoupling; M4 checks per-eye sizing and the UI target.
- **As built** (docs/rig-findings/render-size.md): the engine's size accessors are its `GetClientRect` calls
  for the swapchain and the output size, answered with the render size, with present scaling of the real
  swapchain into the small window. `_gui` has the output size, so it is near-square like the eye; the UI
  layer shows its centred 16:9 band, where idSWF lays the screens out.

## 8. UI (`features/ui`, `engine/eternal` capture)

The UI is idSWF (Flash), drawn into its own 8-bit render target and composited during tone mapping
(R03). Tiered design (R04), each tier falling back to the one below:

- **Tier A (v1).** Capture the idSWF target before composition, suppress it in the eye buffers, and
  present it on OpenXR layers.
  - Menus and full-screen UI go on a curved panel at a fixed distance (2 m).
  - The gameplay HUD is split by fixed screen rectangles (data per aspect ratio) of the one captured
    target onto two layers (D-035, T-077): a **wrist panel** with the status widgets (health, armor,
    ammo), shown when the off-hand wrist turns toward the face; and a
    **message panel** in the lower centre of view with lazy follow, carrying subtitles, notifications,
    pop-ups and tutorial prompts. Regions not mapped are not shown (the screen-centre crosshair; aim
    follows the hand). The body-locked HUD with look-down reveal remains an option.
  - Menus use a laser pointer driving the engine's cursor input.
  - Cinematics go on a cinema screen when that presentation is selected.
- **Tier B (v1 if time allows).** Ammo on a weapon panel, and a reticle projected at the hit surface or
  a laser in place of the crosshair.
- **Tier C (stretch).** Separate widgets identified through type info and idSWF sprite names, for full
  per-element placement.

**Legibility (D-035, T-077).** Players of existing VR mods find notifications and subtitles "barely
readable at best", so every text element is an acceptance item, not a polish item. High-resolution capture (about
2048 px), panels sized so that the smallest text's cap height is at least 0.5 degrees of visual angle at
the default placement (a starting value), placement calibrated per profile, and legibility scaling.
The M6 protocol (`docs/test-protocols/ui-legibility.md`) has the reader read each item aloud while it
is shown, without leaning in or moving the panel: 10 wrist glances, 10 subtitle lines, 5
notifications, 3 pop-ups and 3 tutorial prompts, each read correctly and completely. Crosshair and HUD
options are forced through cvars and decl data where no cvar exists (R03).

**Screen-projected markers (T-058).** Objective and POI markers, and any prompt drawn at an object's
screen position (R12 section 4.1), are placed for the centred camera, so on a layer 2 m away they point
at the wrong place. v1 hides the objective markers (`g_setting_objectiveMarkers 0`). The interact
prompt is checked in M6 and hidden (`g_setting_interact_prompt 0`) if it misaligns. Reprojecting
markers to world depth is v1.x.

## 9. Input and aim (`engine/eternal`, `xr/`)

Layered injection (R13):

| Layer | Mechanism | Role |
|---|---|---|
| L0 | cvars | Aim assist and snapping off, mixed-input focus, meathook auto-orient off |
| L1 | usercmd hook (`engine_t::usercmdGen`) | **Primary.** Button bits (64-bit mask), analog move, view-angle *delta* |
| L2 | `XInputGetState/SetState` import patch (`XINPUT1_3`) | Rumble capture, weapon wheel and menu navigation, degraded fallback |
| L3 | engine input event queue | Menu keys and clicks from the laser pointer |

- **No ViGEm, no SendInput.** The usercmd layout is not in type info, so recovering it is an RE task
  **[rig]**.
- **Decoupled aim.**
  - The player's view angles follow the weapon controller while the render camera follows the
    headset. Everything keyed to the view (hitscan, projectiles, meathook, lock-on, glory-kill focus,
    use prompts) then follows the hand.
  - **Closed loop (T-055).** The usercmd written during frame N feeds the simulation of frame N+1, while
    the weapon renders at frame N's hand pose, so open-loop deltas lag and drift. Each frame the layer
    reads back the player's view angles and sends the delta to the current hand ray. In states where
    the game forces the angles (sync kills, meathook pull, cutscenes) it yields and sends no aim
    delta.
  - **Fallback, convergence aim.** Set the view angles so that the eye ray hits the point the hand ray
    hits (one collision trace per frame). It needs no muzzle fields.
  - Shots leave the tracked muzzle via the weapon fields `useMuzzleAsFireAxis`,
    `useMuzzleDirForFireAxis` and `fireFromMuzzle`, or a fire hook. Check the
    `hands_adjustFirePosDistCheck` fallback, which can pull shots back to the head (R10, R13).
  - The equipment launcher and Flame Belch aim from the head (R06).
- **Locomotion and turning:**
  - Stick movement relative to the head (default) or the off hand.
  - Smooth or snap turning applied in the game camera, with full 360 degrees available for seated
    play.
  - Weapon selection never shares the turn stick in a way that causes accidental swaps (R14).
- **Seated (T-074):** holster and gesture zones at shoulder height, any hands-jump gesture off by
  default, a punch threshold that works with seated arm motion, and desk-safe weapon offsets (section
  6.2).
- **Haptics (v1 if time allows):** game rumble (`XInputSetState`) goes to `xrApplyHapticFeedback`, per
  hand where attributable. bHaptics, PSVR2 adaptive triggers and Index finger curl are stretch goals,
  as optional modules.
- **Bindings are data.** Suggested OpenXR bindings and the game-action map come from one data file;
  each profile stores its own remaps, and the launcher's bindings editor rejects conflicts with a
  message naming both actions (REQ-11).

## 9a. Hands, weapon and audio listener (`features/hands`, `engine/eternal`)

- **Hands are the game's own in v1 (T-054).** The game's arms and weapon are moved to the controller
  grip joint with per-weapon offsets (the DOOM VFR approach, R04 section 1.12), with `hands_fovScale`
  forced to 1 (T-053). Because the game draws them, they keep its jitter, motion vectors and lighting,
  so DLSS and TAA treat them like the world. During sync animations (glory kills, chainsaw, Crucible)
  the viewmodel goes back to the game's own placement. The off hand has no model of its own in v1.
- **Our own GLB hands are v1.x.** Drawn into the frame before upscaling, they would write no motion
  vectors or jitter and would ghost under DLSS and TAA (R07 section 3.3).
- **Room-scale (T-062).** v1 is a head offset: the rendered head follows the real head within the 0.6 m
  lean cap, with a head-collision fade when the head enters geometry, and shots come from the last
  valid head position. Body follow is "v1 if time allows", as a superset: it feeds small physical head
  displacements into the usercmd movement we already inject, so the engine's own movement and collision
  handle walls, and the head offset still covers what the body cannot follow. It is gated on a rig
  test of usercmd movement precision (Eternal's acceleration may swallow or overshoot small steps; R10
  section 3.2). Fallback: head offset only.
- **Collision.** The head-collision fade, the lean cap, "fire from the last valid head position" and
  convergence aim need a safe collision trace. Finding it is an RE item in M1 (R10 section 7 items 3
  and 4, open question 6).
- **Audio listener.** The Wwise listener takes the rendered head position (clamped, collision-valid) and
  the headset orientation, set just before the engine's listener update and restored after, so gameplay
  code never sees it (R10 section 5). Scheduled early (M5) because it is small and noticeable.

## 10. Camera, game state and comfort (`engine/eternal`, `features/comfort`)

A **game-state classifier** publishes one state per frame. A minimal version (gameplay / camera
takeover / menu or loading) lands with stereo in M4, so rotation stripping never depends on the full
classifier; M7 completes the list below. Signals come from type-info fields and hooks
(for example `hideHudForCinematic`) and from player velocity. States:

- gameplay
- glory kill / chainsaw / crucible / hammer
- meathook pull
- swing / monkey bar / wall climb
- jump pad / dash
- first-person script
- third-person cutscene
- pre-rendered video
- menu / loading
- death / respawn

Per state, **camera policy** decides:
- **Translation:** follow or damp.
- **Rotation:** authored rotation is stripped; only the headset rotates the view.
- **Presentation:** immersive, cinema window, or fade/"blink".

Comfort effects are event-driven:
- a speed-scaled vignette, with pulses on dash, jump pad, hook and slam; pulses ramp smoothly (no
  luminance steps) and are rate-limited to at most two per second (T-096)
- optional brief fades
- landing-arc marker

Four presets (Comfortable, Recommended, Advanced, Intense) seed profiles (section 12). A new profile
starts from **Recommended**, with the vignette on. Glory-kill presentation is the key playtest decision
(R06).

**Health notice and flashes (T-096).** On first launch, and on each new profile, the launcher shows a
short health notice: photosensitivity (DOOM Eternal has intense flashes, stronger at full field of
view), motion sickness, and clear space around a seated or standing player. A "reduce flashes" comfort
option dims full-screen flashes where the engine exposes them (muzzle flash, explosions and damage
flashes through their cvars or post-process constants, found in M6); where it cannot, the notice and the
documentation say so.

## 11. Performance features (`features/upscaling`, `features/foveation`)

**Upscaling (R07).** One `Upscaler` interface, fed by the game's own DLSS call:
- We intercept NGX create, evaluate and release in `nvngx_dlss.dll`. The game's one DLSS instance
  becomes two, one per eye, each fed a view of one layer.
  - Whether NGX honours a layer view is **[rig]**; the fallback is per-eye copies.
  - NGX's own pipelines and dispatches pass through our layer untouched (section 7, T-033).
- **AMD and Intel: OptiScaler (D-031, T-065, T-072, provisional).** On non-NVIDIA GPUs the game never
  offers DLSS or calls NGX by itself, and a vendor-ID spoof does not change that: OptiScaler's Vulkan
  spoof, including driver vendor, name and info, fails on DOOM Eternal, and Goghor's DLSS Unlocker is
  the only known way (`reference/_cache/OptiScaler/Features.md`, `Changelog.md`, `Spoofing.md`). Our
  engine adapter therefore patches the game's own DLSS-availability gate, as the Unlocker does, found
  through hooks and type info (the readers of `ui_settings_dummy_dlss_enabled` and the gate function;
  **[rig: M9 RE]**), and sets `ui_settings_dummy_dlss_enabled 1`. If the gate also needs NVIDIA's
  vendor ID, the layer reports it and then strips NVIDIA-only Vulkan extensions the game requests (for
  example `VK_NVX_binary_import`), fakes or disables their entry points, and keeps NVIDIA-only paths off
  (the NV shading-rate image; the KHR path is used, T-037). Our per-eye interposition (above) then
  works as on NVIDIA, and underneath it OptiScaler (GPL-3.0) answers as the NGX provider and runs FSR
  3.1, XeSS (including DP4a) or FSR 4 through its own D3D12 interop.
  - The launcher downloads OptiScaler's official release into the EternalVR data folder, never the
    game folder, only after asking the user, and shows its version; the same policy as the DLSS DLL.
    Its code is never copied or bundled. The version is **pinned**: each mod release names the
    OptiScaler version that passed its tests, with the release archive's SHA-256, in `build-info.json`;
    the launcher verifies the hash before use and refuses a mismatch. An "update OptiScaler" action
    offers only versions a newer mod release has tested (T-094).
  - The gate patch (and any vendor report) is active only when OptiScaler is the provider.
  - Rig tests on the RTX 4080 with OptiScaler's FSR backend forced: it loads from our folder rather
    than as a game-folder proxy; two feature instances each evaluate a per-eye view; and a proxy test
    with the driver's `_nvngx.dll` blocked for the process, `ui_settings_dummy_dlss_enabled 1` and
    `r_antialiasing 2` shows the game offering DLSS under our gate patch and routing its NGX calls to
    OptiScaler **[rig: M9]**. An AMD tester confirms the DLSS option appears under the gate patch.
  - Fallback, if the rig proxy test or the AMD tester check fails: the game's native TAA on AMD in v1.
    Our own NGX shim (R07 section 4.3, option C1) is v1.1.
- History is reset for both eyes on level load, cuts, glory-kill snaps, teleports, snap turns, recenter
  and menus (`r_dlssForceReset`).
- Motion vectors are per eye and include head motion. Jitter is applied without clobbering the
  off-axis terms.
- **Backends:**
  - DLSS 4.x (default on NVIDIA) from a 310.x DLL the user supplies (presets K/M); the game ships
    `nvngx_dlss.dll` 2.3.0 for each exe, which gives DLSS 2 (T-103).
  - Through OptiScaler: FSR 3.1, XeSS 2.x and FSR 2; FSR 4 on the GPUs OptiScaler supports it on (v1
    if time allows, verified by an RDNA 3 or RDNA 4 tester). Our own D3D12 round trip for FSR 4 (T-034)
    is dropped.
  - The game's native TAA, when no upscaler is available.
  - FSR1 is dropped (D-031).
  - A DLSS 5 slot as a stretch goal (very expensive; first test is whether both eyes get consistent
    detail).
- **The launcher never ships `nvngx_dlss.dll`, and never writes one into the game folder.** It shows
  the game's installed version, and lets the user pick a newer DLL they already have; that copy is kept
  in the EternalVR data folder and loaded from there through our NGX interposition (the feature path
  list we already control) during VR sessions only, so flat and BATTLEMODE play use the game's own DLL.
  It can force presets (T-094, superseding T-019's swap clause).

**Foveation (R08).** `VK_KHR_fragment_shading_rate` with a 2-layer shading-rate image; the layer is
selected by `ViewIndex`.
- **Fixed foveation is on by default for every headset.** Regions are defined in degrees and centred per
  eye on head-forward projected into that eye's frustum.
- Presets: Off / Subtle / Balanced / Aggressive. Pancake-lens headsets default one notch gentler.
- **Eye-tracked foveation** from `XR_EXT_eye_gaze_interaction` where available (v1 if time allows;
  verified by a tester with a gaze-capable headset, D-027).
- **Centre and width.** The owner's requirement is fixed foveation "just centered and perhaps wider than
  you'd make it with eye tracking" (D-013). Degree-based regions, per-eye lens centring and the presets
  are the technical design (T-012, R08 section 4).
- **One shading-rate path per session (T-037).** If `r_VRSEnabled` makes the engine request NVIDIA's
  shading-rate-image feature, the KHR attachment path cannot be enabled on the same device (R08
  section 6.2). The layer picks one at device creation; KHR is the default.
- **Per-eye rate layers** need `layeredShadingRateAttachments`; without it both eyes share one union
  pattern (R08 section 6.2) **[rig: M1 `vulkaninfo`; testers for AMD]**.
- **Later:** foveated upscaling through DLSS sub-rectangles.
- **The game's own `r_VRSEnabled`** is measured in M1 (baselines, and which extension it enables), not
  used as the foveation path: the KHR path is the default (T-037, which supersedes T-012's "try it
  first").

**Performance gates and targets (REQ-20, T-075, R11 section 5.2).** Two hard gates protect players:
after the first run, no frame longer than 250 ms outside loading screens, and p99 frame time within two
display periods on each benchmark route. Everything else is a target, measured as p99 frame time over
the benchmark routes (ROADMAP) and recorded.
"Quest 3 size" means 2496 x 2688 pixels per eye at render scale 1.0, the size VDXR recommended in a
tester log (R11 section 4). The recommended size depends on runtime settings (VDXR's quality
setting, Meta Link's render resolution), so each benchmark run sets the runtime to produce that size,
records the setting and the size the runtime actually reports in `docs/rig-findings/bench-routes.md`,
and is labelled with the real size if the two differ.
- **72 Hz at Quest 3 size on an RTX 4080 with RT off, native:** the M4 target (the gates are required
  there).
- **90 Hz:** native is at the limit; the gates are required at the default settings (M9), and the
  target is recorded separately with DLSS Quality alone and with Balanced fixed foveation alone
  (T-105).
- **120 Hz:** needs DLSS Quality plus fixed foveation plus about 0.85 render scale (v1 if time allows).
- **RT on:** 72 Hz with DLSS Quality.
- **Stereo overhead (T-075):** stereo frame time minus a mono reference, GPU by timestamps and
  render-thread CPU by timers, on the same route and settings. The mono reference is the same build
  with the layer on and the stereo transform off, rendering one view at the enclosing camera's FOV at a
  render size with the pixel count of both eyes together. Budgets per `StereoSource`:
  `MultiviewTransform` 0.5 ms GPU and 0.5 ms CPU; `EngineNativeStereo` the same for our own work, the
  engine's second-view cost reported separately; `SynchronizedSequential` GPU at most 30% over the
  reference and render-thread CPU at most twice the reference's.
- **Interop copies (T-040, T-075):** at most 1.0 ms GPU per frame at Quest 3 size for all copies, a
  starting value from R11 section 3 item 3; M3 records the measured value.

## 12. Configuration and profiles (`platform/settings`, launcher)

- **One keyed TOML settings file,** written by the launcher and hot-reloaded by the layer. Hot reload
  while playing was accepted in principle by the owner (D-028, given alongside D-015); the details are
  T-032 and T-061.
- **Every setting has a class (T-032)**, shown in the launcher:
  - **live:** HUD placement, weapon offsets, vignette, turn settings, foveation preset, haptic strength
  - **next level load:** settings that touch graphics state, because changing graphics quality while a
    level is loaded can freeze or crash
  - **restart:** render scale, upscaler backend and mode, presenter, OpenXR runtime, world size
- **Single writer.** Only the launcher writes the settings file; the layer never does. Values tuned in
  game go into a separate pending-edits file written by the layer, which the launcher merges into the
  active profile when the game is not running (see precedence). Both files are written atomically
  (write, then rename).
- **Precedence (T-061).** The layer's effective value for a key is the settings file overlaid with the
  pending-edits file, so in-game edits never revert while the launcher is closed. When the layer
  reloads and finds that a key's value in the settings file changed since its last load, it drops its
  own pending edit for that key, so a later launcher edit wins. The launcher merges pending edits into
  the active profile only while the game is not running (at game exit, or at its next start after an
  unclean exit), then deletes the pending-edits file.
- **Profiles are named and savable, one per player.** A profile is a base preset plus explicit
  overrides.
  - The launcher marks what changed from the preset and offers reset per setting or reset all.
  - Edits made during play reach the active profile through the pending-edits merge after the game
    exits.
  - Save, duplicate, rename and delete; the last used profile is remembered.
- **Posture is automatic by default** (section 6.2). An optional Seated/Standing/Auto override exists.
  World size is 0.85-1.20, set in the launcher only and applied at the next launch, never during play
  (R10 section 2.1).
- **No positional-number config files, no scattered marker files or environment flags**. The
  launcher passes one path to the settings file (`ETERNALVR_SETTINGS`). Environment variables are
  limited to the fixed set in section 15; features are switched in settings, never by environment.
- **Where things live (T-093).** The program folder (wherever the user extracts the release) is
  treated as read-only. All user data lives under `%LOCALAPPDATA%\EternalVR\`: settings, profiles,
  pending edits, logs, the patched-module and pipeline caches, downloads (OptiScaler, a user-picked
  DLSS DLL), config snapshots and save backups. So an update that extracts over the old folder, or into
  a new one, keeps profiles and bindings. The settings file carries a `schema_version`; the launcher
  migrates older versions (keeping a backup of the old file) and refuses a newer one with a message.
  The launcher and the layer DLL check each other's version at launch and refuse a mismatch.
- **Updates (T-093, R09 section 10).** Once a day the launcher checks GitHub Releases for a newer
  version, disclosed in the launcher and switchable off; it never downloads the mod itself, it links to
  the release.
- **Uninstall and clean-up (T-093).** A launcher action removes any layer registration, completes any
  pending config restore, deletes downloaded OptiScaler and DLSS copies, and lists what is left (the
  data folder, which the user may keep for profiles) before deleting it on request. At every start the
  launcher also removes a stale registration and completes a pending restore left by a crash.

## 13. Diagnostics and degradation (`platform/diagnostics`)

- **Structured log.** Covers the build, runtime, GPU/driver, the HAGS state (`HwSchMode`, read only,
  T-085), resolver report, enabled features, and the reasons for each disabled feature.
- **Kill switches** for every feature in settings.
- **Crash handling (T-112).** Nothing is configured in the registry for end users (no WER LocalDumps
  or AeDebug settings). **Release builds** install no unhandled-exception filter of their own (R09
  section 2.6): a vectored handler logs the faulting module and offset and continues the search, and
  the launcher records the exit code and the log tail. That is the v1 crash data; an out-of-process
  dump handoff is v1.x. **Development builds** add an in-process handler that writes a minidump into
  the log folder. On the rig, ProcDump attached to the game's process ID writes dumps (D-042, T-108).
- **"Export report" in the launcher** (T-095). One click writes a zip with an exact, documented
  manifest: the layer and launcher logs, preflight results, settings and the active profile, the
  resolver report, the game exe hash, the mod version and `build-info.json`, the OpenXR runtime name
  and version, GPU and driver, the DLSS and OptiScaler versions, the HAGS state and the crash data above (no
  memory dump in v1, T-112). Before saving, the user
  profile path and the Steam account ID are replaced by placeholders, and the launcher shows the file
  list and total size (capped well under GitHub's attachment limit). A GitHub issue template asks for
  the report, so testers do not have to zip logs by hand (R14).
- **A startup watchdog** tells "VR never appeared" apart from a slow first launch (the top failure
  mode, R14).
- **"Untested on this hardware" notice** (T-076, T-095). Where the launcher can detect an entry of the
  release matrix that no tester verified (GPU vendor and generation, OpenXR runtime), it shows a
  one-line notice.

## 14. Testing (R09)

| Tier | Where | What |
|---|---|---|
| 1 | Any OS, CI | Unit tests for portable logic: math, projection, policies, settings, profile merge, resolver on synthetic images, SPIR-V patcher on sample modules |
| 2 | Rig GPU, no game | GFXReconstruct replay (`-m rebind`) of captured Eternal frames through `vkcore`: the M1.5 stereo spike (T-049), then the checks: **depth-reprojection oracle** at 64 mm IPD, a "does no harm" check with each eye set to the enclosing camera, a motion-vector oracle (T-051), golden PSNR/SSIM, no new validation messages, per-pass diffs (T-043) |
| 3 | Rig GPU, no game; tester GPUs | Fossilize corpus: every captured pipeline patched and compiled on the driver. AMD testers run the same check offline on their own corpus (T-035) |
| 4 | Rig, game, no headset | Short scripted runs against OpenXR-Simulator through the rig scripts (T-108); per-eye PNGs, logs, dumps |
| 5 | Headset | Acceptance, comfort playtests |

**Replay oracles (T-043, provisional thresholds).**
- **Oracle captures.** The M1 captures are taken with our layer off: they prove the harness and feed
  reconnaissance, but carry no sidecar and no enclosing camera. The oracle set is re-taken in M4, once
  the engine adapter has the camera hook, with the enclosing camera active (so culling matches stereo
  runs) and a sidecar holding `P_c` and the pose for every frame (so replay needs no adapter). Oracle
  captures run with TAA off (`r_antialiasing 0`), jitter frozen (`r_jitter 0`), HUD hidden
  (`g_showHud 0`), and SSR and RT reflections off, so temporal history and screen-space effects do not
  blur the comparison.
- **No-harm check.** Each eye's pose and FOV are set equal to the enclosing camera's, so `C_e` is the
  identity by construction; both eye images must match the mono replay (at most 1/255 per channel on
  99.9% of pixels). This is not the same as zero IPD: at zero IPD with the headset's FOV, `C_e` is not the
  identity, because the enclosing camera sits behind the eyes with a wider FOV. The check shows only
  that promotion and patching do no harm; it cannot catch a missed transform.
- **Stereo oracle (depth reprojection).** At 64 mm IPD, warp the left eye to the right using left-eye
  depth and the known eye transforms, and compare with the rendered right eye. Masked out:
  disocclusions, pixels written by blended draws (transparents, particles), and anything drawn by
  UI-class shaders. View-dependent specular stays in, covered by the tolerance. Pass: at least 99% of
  unmasked pixels within 4/255.
- **Planted faults.** An unpatched world shader and a wrong eye transform must each drop the score below
  95%.
- **Motion-vector oracle (T-051).** The oracles above run with TAA off and jitter frozen, so they cannot
  see motion-vector errors. A short TAA-on sequence per scene is added: each eye's frame N-1 is
  reprojected with that eye's motion vectors and compared with frame N, with the same masks and
  starting thresholds. A planted fault (`C_e` in place of `C_e(prev)`) must fail it.
- Replay uses `gfxrecon-replay -m rebind`, because promoted 2-layer images outgrow the recorded
  allocations.
- The thresholds are starting values, tuned on the first oracle captures; any change is recorded in
  `docs/rig-findings/replay.md`.

- **Scope.** The oracles run on the two world capture scenes. The menu scene is mono and has no world
  view, so it is excluded (T-070).

Release gate (ROADMAP M10, D-027): the owner covers VDXR and Meta Link on NVIDIA with the Quest 3;
testers cover SteamVR with Index, PSVR2, AMD on VDXR, Meta Link and SteamVR, and Intel with XeSS (R14).
A combination without a volunteer ships listed as untested, never silently (T-076). GPU tests skip
cleanly without a GPU.

## 15. Code layout and standards

Directories that exist today are marked `*`; the rest are planned and appear with the milestone that
needs them.

```
src/
  common/*          small value types and math: vectors, quaternions, mat4, pose, Result (no engine or API deps)
  xr_math/*         FOV, projections, enclosing FOV, per-eye clip transforms (portable)
  platform/         settings/* (profiles, presets, schema), logging, diagnostics, threading utilities
  vkcore/*          layer entry (layer_entry.cpp, manifest template today; split into layer/ in M2),
                    dispatch, tracking, stereo transform, spirv/, pipeline cache, capture
  xr/               session, frame loop, presenters/, spaces, actions, layers
  features/         foveation/*, input/*, posture/*, ui/, comfort/, hands/, haptics/, upscaling/
  game/eternal/*    game-design data: actions, default bindings, player dimensions
  engine/eternal/   resolver/* (PE image, patterns, strings, RIP xrefs), hooks/, state/, camera/,
                    input/, weapon/, ui_capture/, audio/, ngx/
launcher/           EternalVR.sln: EternalVR.Launcher (WinForms), EternalVR.Launcher.Core
                    (netstandard2.0, tested on any OS)
tools/              check_includes.py*, fetch_references.sh*, rig/ (local PowerShell scripts: run,
                    stop, cleanup, display, collect, replay, lab copy; dev only, T-108), resolver-cli/,
                    fossilize runner, offline patcher check
tests/*             mirrors src/, plus support/ (shared test helpers); doctest
```

The C++ tree builds with CMake (presets in `CMakePresets.json`). The launcher is a separate .NET
solution, built with `dotnet build launcher/EternalVR.sln` (or Visual Studio), not by
CMake; the portable `EternalVR.Launcher.Core` tests run with `dotnet test` on any OS.

Environment variables (the complete set; everything else is a setting):

| Variable | Set by | Meaning |
|---|---|---|
| `ETERNALVR_ENABLE_LAYER` | launcher, rig scripts | `1` activates the implicit layer for this process |
| `ETERNALVR_DISABLE_LAYER` | anyone | `1` keeps the layer inert even when enabled |
| `ETERNALVR_SETTINGS` | launcher, rig scripts | Path of the settings file |
| `ETERNALVR_LOG_DIR` | launcher, rig scripts | Directory for logs, captures and dumps of this run (default `%LOCALAPPDATA%\EternalVR\logs\`) |
| `ETERNALVR_REPLAY` | rig scripts | `1` lets a development build's layer run its Vulkan core in `gfxrecon-replay`; ignored by release builds (T-109) |

Standards (the clean-code rule; `CONTRIBUTING.md` has the working details):
- C++20, warnings as errors on every compiler. clang-format 18.1.8 is checked in CI (the portable job on
  Linux). clang-tidy is configured (`.clang-tidy`) and runs locally as advisory until its CI job lands in
  M4.5, the release track (ROADMAP).
- One responsibility per file. Around 300 lines is the soft upper bound; a file past about 600 lines is
  split. A CI check enforces the 600-line limit from M4.5.
- Engine internals (offsets, hooks, type names) only in `engine/eternal`; game-design data only in
  `game/eternal`. Features stay generic and take game data as plain values. A CI check enforces this
  from M4.5.
- Names over comments; comments explain why.
- Every hook and resolver has a unit-testable policy part.

Libraries:

| Library | Use |
|---|---|
| safetyhook, kananlib | Boost licence |
| OpenXR SDK loader | Apache-2.0 |
| Vulkan-Headers, SPIRV-Headers, SPIRV-Tools | Vulkan headers and SPIR-V tooling |
| doctest | Tests |
| spdlog | Logging |
| toml++ | Settings |
| nlohmann/json | JSON |
| Dear ImGui | Debug overlay only |

All dependencies are pinned via FetchContent. Code adapted from MIT-licensed projects such as REFramework carries
origin headers and is listed in `THIRD_PARTY_NOTICES.md`. That file also covers every library we
redistribute, statically linked or shipped (the ones above, the OpenXR loader, and the launcher's own
packages), with each full licence text and NOTICE where the licence requires one, and it is included in
every release zip (T-095). UEVR `src/` and GPL projects are reference
only. OptiScaler (GPL-3.0) is not a dependency: the launcher downloads its official release at run
time, and its code is never copied or bundled (D-031).

## 16. Rig experiments, in order

The open questions themselves are tracked in `DECISIONS.md`; this is the order in which the rig answers
them. Items 1 to 11 are M1 (`PLAN_M1_M3.md`, `RIG_BRINGUP.md`), 12 is M1.5, 13 is M3, 14 is M4, 15 is
M5 and 16 is M9. M1 items needed only by M1.5, M4 or M5 may finish in parallel with M2 and M3 (T-068).

1. Local rig scripts launch, collect and stop the game with a per-process environment, and their test
   suite checks the T-108 contract; the lab copy of the install runs (T-108).
2. Record game build and install layout in `builds.md`; archive the current retail and sandbox exes for
   the resolver checks (T-060).
3. Do runtime-set cvars persist into the game's saved config (T-036), and into which file of either
   location (T-099)? The procedure and values are T-100.
4. The render-view build point for the camera hook, and the render entry point with the per-frame
   state it advances (T-069).
5. **Dormant VR subsystem:** unlock the console in the lab copy, try `vr_dummyDevice 1` / `vr_enable 1`
   at launch with `vr_logLevel 4`, inspect the log and a RenderDoc capture for a second view, and check
   whether the `viewIndex` / `multiView_60Hz` scaffold loops over views (R15, T-069).
6. **Vulkan reconnaissance checklist** (the one home for it; `vulkan-recon.md`). From the menu-scene
   capture taken before M2: the game's `apiVersion`, enabled extensions and feature chain (T-082), and
   which image is its final eye image, with its format and whether it is sRGB-encoded (T-080, T-081).
   From the world captures, with TAA off and jitter frozen: render-pass API, debug names on shaders and
   objects, view-constant block layout, pipeline and module counts, queue usage and how command buffers
   on each queue map to engine frames (T-041), where the idSWF UI target is composited, whether
   viewmodel draws bind a projection other than `P_c` and whether their depth hack is in the viewport
   depth range or the projection (T-053), which passes do light and decal binning and what depth they
   read (T-038), which shaders read the bins by `gl_FragCoord` or output a previous-clip position
   (T-051), each world capture's FOV, aspect and `r_znear` (T-050), the highest descriptor-set slot any
   pipeline layout uses (R02 open question 8), and the viewport Y sign, depth format, depth compare op
   and whether the projection is reversed and infinite (T-083).
7. Flat baselines in two configurations, with the current HAGS state recorded (T-075, D-036). The
   frame-rate cap and dynamic-resolution cvars (T-047). Define the benchmark routes.
8. Unit scale (noclip distance), eye height, usercmd layout, fire-axis fields (R10, R13).
9. `vulkaninfo` shading-rate features on both GPUs and which extension `r_VRSEnabled` enables (R08,
   T-037).
10. Pipeline corpus (Fossilize, Steam's cache or our capture tool) for patcher work (R02, R09).
11. Collision trace on the player physics (R10).
12. Offline stereo spike on the M1 captures: structural matching, per-eye replays and the census
    (T-049, T-050, T-070).
13. OpenXR: the head-tracked view through the camera hook; the D3D12 presenter's colour copies against
    the interop budget, fence waits, the stalled-presenter timeout test, D3D12 handle import into
    Vulkan, pose age as defined by T-111 with the current HAGS state recorded; the LUID check on the
    two-GPU rig, with steering in M4.5 (R01, T-040, T-057, T-068, T-111).
14. Culling: the chosen Umbra option's cost and runtime effect, and the culling test (T-038, T-073).
15. Usercmd movement precision for room-scale body follow (T-062), once the usercmd hook works.
16. NGX: does DLSS read a layer view? Do NGX's calls route through our layer (R07 open question 4)?
    Per-eye instance cost (R07). OptiScaler from our folder with its FSR backend forced, and two
    per-eye instances (T-065). The DLSS gate patch and its proxy test with the driver's `_nvngx.dll`
    blocked (T-072).

If RenderDoc fails on the game (it crashed on DOOM Eternal before, R09 section 3.5), captures fall back
to Nsight Graphics and our own capture layer. The rig is shared with the owner's play; the rig scripts
refuse to run while he plays, clean up after themselves even when a run is killed, and snapshot his
settings and saves before any run that may enter gameplay (T-108). Other risks are listed in ROADMAP.

Outcomes are recorded in `docs/rig-findings/` (the dormant-VR write-up is `dormant-vr.md` there, not
the `reference/` path R15 suggests) and fed back into this document.
