# Collision query, eye height and the player's capsule (room-scale, T-062)

Static analysis only; the game was not run. Input: `DOOMEternalx64vk.exe`, Steam build 25216728 (Rev 3.2),
the same exe as `engine-facts.md` and `input-aim.md`. Tools: the analysis scripts of `engine-facts.md`
section 8, Ghidra 12.1 headless, the type-info reader. All addresses are RVAs in this build.

`[static-verified]`: read from the code or data end to end, and every signature below matches exactly once
in `.text` (`??` masks rel32, RIP displacements and structure displacements). `[inferred]`: a reading that a
live experiment must confirm (section 5).

Units: the type info tags the player cvars `units = m`, and `pm_normalViewHeight` defaults to 1.65735, so
DOOM Eternal's world units are metres.

## 1. The collision query

### Objects, from the camera hook (RVA 0x6A31B7, r15 = idPlayer) [static-verified]

| Object | Where | Notes |
|---|---|---|
| Map instance | the global qword at .data 0x45F7370, read by `mov rcx, [rip+disp32]` at the third-person camera's trace site (0x145378B) and the weapon's view trace (0x135F875); also r13 at the hook | set by the render-view build at 0x6A2C75 |
| Collision world (`idHavokCollision`, 0x178 bytes) | map instance + 0x99E68 | map-instance vslot 0x370 (0x69A2C0) is exactly `lea rax, [rcx+0x99E68]; ret`. The player's ground trace reaches the same object through `*(r15+0x8A50+0x3CB8)` [inferred: same object] |
| Ready-made shapes (`idHavokShape*`) | collision world +0xD0 sphere r 0.05; +0xE0 sphere r 0.45; +0xE8 `clipPoint` box ±0.00019; +0x100..+0x118 cubes ±0.08, ±0.16, ±0.24, ±0.32; +0x130..+0x140 cubes ±0.48, ±0.64, ±0.96; **+0x170 `clip16sphere`, r 0.16** | built by 0x4F7550; names from type info |
| The player's spawn id (the entity to ignore) | `*(int*)(r15 + 0x8A50 + 0x30)` (idHavokPhysics_Player); map-instance vslot 0x90 (0x6CB1D0) returns `*(int*)(mapInstance + 0x21AF8 + 4 * *(int*)(ent+0x10))`, 0x1FFFFFE for no entity | `ent+0x10` being the entity number is [inferred] |

### `idHavokCollision::Translation` (0x4FC0B0, 335 callers) [static-verified]

```
void Translation(idHavokCollision* hc,      // rcx  map instance + 0x99E68
    uint64_t* queryId,                       // rdx  always written (0 when synchronous)
    trace_t*  result,                        // r8   non-null: synchronous; null: deferred
    const idVec3* start, const idVec3* end,  // r9, [rsp+0x20]
    idHavokShape* shape,                     // [rsp+0x28] null: a ray; e.g. *(hc+0x170), the 0.16 m sphere
    const idMat3* axis,                      // [rsp+0x30] identity (the game passes 0x38A1CC8)
    int contents,                            // [rsp+0x38]
    int passSpawnId,                         // [rsp+0x40] the entity to ignore
    uint32_t group,                          // [rsp+0x48] 0
    const char* name,                        // [rsp+0x50] a static string; read by the debug draw
    void* collector,                         // [rsp+0x58] null: the built-in collector
    int unused);                             // [rsp+0x60] 0
```

- Synchronous path: a ray cast (0x25AEF90) or shape cast (0x25AF070) against the Havok world at
  `*(hc+0) + 0x21020`, then the hit conversion 0x4F4F80; no hit writes fraction 1.0, endpos = end and spawn
  id 0x1FFFFFE. It takes no lock (the deferred path takes the mutex at 0x45E0308).
- `trace_t` (0x80 bytes): fraction +0x00 (1.0 = no hit), endpos +0x04 (start + fraction * (end - start)),
  endAxis +0x10, contact type +0x34, contact point +0x38, normal +0x44, contentFlags +0x58, surfaceFlags
  +0x5C, surfaceType +0x60, hit spawn id +0x6C, body id +0x74.
- Contents (the game's name tables at 0x3ADF030 and 0x4080340): SOLID 0x1, OPAQUE 0x2, PLAYERCLIP 0x8, AI
  0x400, PLAYER 0x8000, SOLIDPUSHABLE 0x100000; MASK_SOLID 0x100001, MASK_PLAYERSOLID 0x108409 (includes
  monsters), MASK_PLAYERDEADSOLID 0x100009 (world and player clip), MASK_OPAQUE 0x100003, MASK_SHOT
  0x103085.
- Deferred: pass result null, keep `*queryId`, read the result later with 0x4F6E20 `(hc, trace_t* out,
  u64* queryId)`, which returns 1 once it is available.
- Overlap ("is this position inside solid"): 0x4F3AD0 `(hc, u64* queryId, trace_t* result, const idVec3* pos,
  idHavokShape* shape /* non-null */, const idMat3* axis, int contents, int passSpawnId, uint32_t group,
  const char* name)`; the player code uses it at 0x13FC823 with contents 0x100009 and tests `fraction < k`
  (that fraction < 1 means overlapping is [inferred]).

### The game's own calls from the view [static-verified]

| Where | Call | Shape | Contents | Ignored |
|---|---|---|---|---|
| Third-person camera (0x1453310) | 0x14537EC, 0x1453A80, 0x1453B7D | a box chosen by 0x13E81F0 from `p_thirdPersonCameraClipSize` | 0x13E81C0: `pm_thirdPersonClipContents` 0 → 0x100003, 1 → 0x800A, 2 → 0x800B | the player's spawn id |
| Spring-camera clip | 0xD38577 | the 0.08 box | 0x100001 | the camera's stored id |
| `GetWeaponFireInfo` view-to-muzzle | 0x135F8E7 (deferred variants pass result null) | `clipPoint` | 0x140001 or 0x143085 | 0x1FFFFFF |
| Body-reaction camera clip | 0x140DF9A | the 0.16 box | 0x108409 | the player's spawn id |

### What the layer uses (`src/vkcore/head_sweep.cpp`)

The synchronous Translation with the 0.16 m sphere, contents 0x100009 (world and player clip, not
monsters), the player's spawn id, an identity axis and a static name. The collision world comes from the
map-instance global through vslot 0x370, checked to be `lea rax, [rcx+disp32]; ret` in `.text`. A fault in
the call is caught and turns the sweep off for the session; a fraction of 0 (the eye itself in contact) is
ignored. When the call is made is the next section.

### When the world may be queried [static-verified unless marked]

Up to 0.1.87 the camera hook called Translation synchronously. Players' logs show the call faulting inside
the game's code (`room: the collision query faulted`: five sessions of four players' reports from 0.1.31 to
0.1.83, and a 0.1.84 player's session), and a 0.1.84 report has the game die of reads at 0x50D326 on several
of its threads at once: a collision-filter virtual reading `[body+0x90]` of a body the shape cast 0x25AF070
found in the broadphase. The reason for the layer's faults is the frame's order:

- `g_havokSyncOnSameFrame` (cvar object 0x45E1680, default 0, "Sync Havok task on the same frame before
  render commit") and `g_havokUseJobs` (default 1). The physics step runs as jobs across the end of the
  frame.
- The step is synced in the next frame: the frame's job list (0x6DF770) adds, before its other entries,
  0x50C790 (with map instance + 0x99C78 set and the byte at + 0x99CE1 set), which waits for the step's task
  (physics object + 0x21208) and runs the post-step job 0x502F30. 0x502F30 clears the byte at physics object
  + 0x21089 (set with + 0x21088 when the step starts) and runs and waits for its own jobs. 0x50C7D0 is the
  same sync outside a job list (idMapInstanceLocal vslot 0x310 at 0x6DC2C0, the same-frame sync at 0x506CA5,
  0x6E3960); a hook on vslot 0x310's call of it never ran in rig play [rig]. The physics object is map
  instance + 0x78C58 (0x6E1B8D, 0x6DF7FF); Translation reads its Havok world through `*collision world`
  (`mov r10, [rsi]; mov rax, [r10+0x21020]` at 0x4FC65F), the same +0x21020 the sync tests, and the layer
  reads `*collision world` = map instance + 0x78C58 [rig].
- The frame's phases are a job list (0x6DF770: 0x17E5FC0 adds an entry, 0x17E6020 runs each entry after the
  previous one). Entry 0x3892B90 (0x6E1A50) answers the queued queries (0x4F4AC0 on the collision world) and
  then starts the step (0x506B60 on map instance + 0x78C58, which sets +0x21088/+0x21089 to 1). The next
  entry, 0x3892B98 (0x6E1D20), is the render-view build 0x6A2C10, where the camera hook (0x6A31B7) runs. So
  the camera hook's queries ran while the step ran on other workers. Measured [rig, 2026-11-06, e1m3 under
  hand aim]: the step's byte was set at every one of 4824 camera-hook asks in 40 s, and clear at all 4 shots
  the fire hook swept.
- The game's own synchronous queries are made in the frame's update (the third-person camera, the weapon's
  view trace in `GetWeaponFireInfo`), after the sync. Its queued queries are answered in 0x6E1A50 before the
  step starts; their window opens at 0x4FC050 (entry 0x3892B40, early in the frame), so a query queued from
  the render-view build is outside the next window and its answer is not readable with 0x4F6E20.

What the layer does since: a query is made only with the world at rest, the byte at physics object +
0x21089 clear and the physics object's pending task (+0x21070, set by the step at 0x509D87, waited for by the
post-step job through 0x50C910 and the deferred-trace reads at 0x4F62F1) absent or finished. 0x50C910 reads
the task's state word (+0x24) and adds no waiter when its low three bits are 4: that is a finished task
[static]. The camera hook (head, aim dot, a cutscene's ray fan) sweeps at once when the world is at rest there
(no step this frame: a pause, `g_havokSyncOnSameFrame 1`), the same condition under which the frame's own
update queries; with a step in flight it asks, a mid hook at the end of the post-step job (0x502F9C, after its
own jobs were waited for; `rbx` = the physics object, checked to be `*collision world`) makes the asked sweeps
there, and the camera hook uses the answers a game frame later (`features/roomscale/late_trace.hpp`). An answer
is used while it lags at most three of its channel's asks (not a time: a slow frame keeps it) and its sweep
started near the current start (the head: within its offset + 5 cm; a ray: 0.5 m), so a pit reset or a scripted
move drops it. The fire hook (0x135D733, just after `GetWeaponFireInfo`'s own query) sweeps at once; held back
(not expected) it logs `a shot's sweep ... was not made`. `room: collision queries last 10 s: ...` counts the
camera hook's sweeps made at once and asked, the sweeps made after the sync, the post-steps that found the
pending task unfinished (expected: none), their time, and the fire hook's sweeps made or held back. The rig QA
scenario `head-sweep` (`tools/rig/qa/qa-scenarios-room.ps1`) checks them, the pause menu included.

| Site | RVA | Signature |
|---|---|---|
| Step sync (`this` = physics object) | 0x50C7D0 | `40 53 B8 70 38 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 60 38 00 00 48 8B D9 BA 01 00 00 00 48 8D 4C 24 20 E8 ?? ?? ?? ?? 48 83 BB ?? ?? ?? ?? 00 74 ?? 80 BB ?? ?? ?? ?? 00 74` (world disp at +0x36, step byte disp at +0x3F; at +0x57 `mov rdx, [rip+disp32]` loads the post-step job) |
| Task wait (`r8` = task) | 0x50C910 | `48 89 74 24 10 57 48 83 EC 20 49 8B F8 48 8B F2 41 8B 40 ?? 8B C8 80 E1 07 80 F9 04 75` (checked at the post-step job's call at +0x5D; the state word's disp at +0x13) |
| Post-step job (`this` = physics object) | 0x502F30 | `40 53 B8 70 38 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 60 38 00 00 48 8B D9 33 D2 48 8D 4C 24 20 E8 ?? ?? ?? ?? 4C 8B 83 ?? ?? ?? ?? C6 83 ?? ?? ?? ?? 00 48 8B 05 ?? ?? ?? ?? 48 89 83 ?? ?? ?? ?? 4D 85 C0 74 ?? 48 8B 8B ?? ?? ?? ?? 48 8D 54 24 20 E8 ?? ?? ?? ?? 48 8D 4C 24 20 E8 ?? ?? ?? ?? 48 8B 8C 24 60 38 00 00` (the pending task's disp at +0x33, the byte's at +0x39, the task wait's call at +0x5D, hook at +0x6C) |
| Translation's world load | 0x4FC65F (+0x5AF) | `49 8B 82 ?? ?? ?? ?? 48 8B 88` |

## 2. Eye height and the player's origin [static-verified]

- `pm_normalViewHeight`: cvar object 0x463A3C0, default 1.65735; the float value is at `[[obj]]+0xC`.
  `pm_crouchviewheight`: 0x463A340, default 0.8763.
- The eye height is not stored in a field: idPlayer vslot 0x690 (0x1452E80, `GetEyeOffset(idVec3* out)`)
  returns (0, 0, `pm_normalViewHeight`) on every call, or the crouch value when 0x13ECA60 or vslot 0x7A8
  reports crouching.
- Physics origin: player physics at `r15+0x8A50`; its vslot 0x670 (0x50E310) returns `phys+0x128`
  (futureOrigin) when the byte at `phys+0x1C8` has bit 0x20, else `phys+0xB0` (bodyOrigin). The origin is
  at the feet. Gravity normal: `phys+0x4668` (vslot 0xD8).
- `CalculateViewWithoutUpdates`, path B (0x1452320–0x145236F): `view = origin + gravityNormal * (-eyeOffset.z)`,
  plus the step-up spring (`player+0x8728`, when `p_useStepUpSprings` and `pm_doom4BobCycle`), plus
  `g_viewNodalX/Z` (both 0), plus the animated camera joint's delta (`p_applyAnimatedCamera`, default 1).
- Path A (the call at 0x1452316): when the hands model has a `player_camera_game` joint, `hands_updatePos`
  is on and bit 4 of `player+0x736E` is clear, the view comes from that joint instead. Which path normal
  play takes is [inferred]/unknown, so the effective eye height is best read at the hook as
  `players[0].view.vieworg.z - origin.z`, which includes crouch, the spring and the animated camera.

## 3. The player's capsule [static-verified]

| Cvar | Object | Default |
|---|---|---|
| `pm_shape` | 0x45E4F30 | 1 (capsule; 0 cylinder) |
| `pm_radius` | 0x45E2C10 | 0.395 |
| `pm_normalheight` | 0x45E2DB0 | 1.79 |
| `pm_crouchheight` | 0x45E2CB0 | 0.89 |
| `pm_deadheight` | 0x45E30B0 | 0.4 |
| `pm_proneheight` | 0x45E2FB0 | 0.29 |
| `pm_stepsize` | 0x4639340 | 0.3048 |

`pm_bboxwidth` (0x45E4B30, 0.9144) is not read by the shape builder. The builder 0x52E4B0 makes one capsule
per stance (9) and stores `idHavokShape*` at `phys+0x3CC8+8i` (rigid), `phys+0x3D10+8i` (trace, shrunk by
the keep distance at `+0x40F0`) and `phys+0x3D58+8i` (step-up); the stance index is at `+0x3CC0`. The trace
shapes can be passed to Translation [inferred]. So the eye is 0.395 m from a wall the capsule touches, and
the 0.16 m head sphere starts clear of it.

## 4. Signatures (each unique in `.text`)

| Name | RVA | Signature |
|---|---|---|
| Translation | 0x4FC0B0 | `48 8B C4 48 89 58 20 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ?? ?? ?? ?? 48 81 EC 00 02 00 00 0F 29 70 B8 0F 29 78 A8 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 ?? ?? ?? ?? 4C 8B B5 ?? ?? ?? ?? 4C 8B E2 4C 8B AD ?? ?? ?? ?? 49 8B D8 4C 8B BD ?? ?? ?? ?? 48 8B F1 48 89 54 24 30 48 8B 95 ?? ?? ?? ??` |
| Overlap query | 0x4F3AD0 | `40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC D8 01 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 70 48 8B 85 ?? ?? ?? ?? 4D 8B E9 4C 8B BD ?? ?? ?? ?? 49 8B D8` |
| Third-person trace site (map-instance global load; call at 0x14537EC) | 0x145378B | `48 8B 0D ?? ?? ?? ?? 8B 18 48 8B 01 FF 90 ?? ?? ?? ?? 45 33 E4 48 8D 0D ?? ?? ?? ?? 44 89 64 24 60 4C 8D 4D 80 4C 89 64 24 58 4C 8D 45 40 48 89 4C 24 50 48 8D 55 A8 44 89 64 24 48 48 8D 0D ?? ?? ?? ?? 89 5C 24 40 89 74 24 38 48 89 4C 24 30 48 8D 4D 90 4C 89 74 24 28 48 89 4C 24 20 48 8B C8 E8 ?? ?? ?? ??` |
| `GetWeaponFireInfo` immediate trace site | 0x135F875 | `48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 ?? ?? ?? ?? 44 89 64 24 60 4C 8D 4D A8 4C 89 64 24 58 4C 8D 45 F0 48 8B C8 48 8D 05 ?? ?? ?? ?? 48 89 44 24 50 8B 05 ?? ?? ?? ?? 44 89 64 24 48 89 44 24 40 48 8D 45 70 C7 44 24 38 01 00 14 00 48 89 44 24 30 48 8B 05 ?? ?? ?? ?? 48 8B 90 ?? ?? ?? ?? 48 8D 45 88 48 89 54 24 28 48 8D 54 24 78 48 89 44 24 20 E8 ?? ?? ?? ??` |
| Spring-camera trace site | 0xD38553 | `48 8B 82 ?? ?? ?? ?? 48 89 44 24 28 48 8D 45 80 48 89 44 24 20 4C 8D 4D C0 4C 8D 85 ?? ?? ?? ?? 48 8D 55 D0 E8 ?? ?? ?? ??` |
| Map-instance vslot 0x370 (unmasked) | 0x69A2C0 | `48 8D 81 68 9E 09 00 C3` |
| Third-person clip shape by size | 0x13E81F0 | `48 83 EC 28 48 8B 05 ?? ?? ?? ?? 8B 48 08 83 F9 08 7F 1C 48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 ?? ?? ?? ?? 48 8B 80 ?? ?? ?? ?? 48 83 C4 28 C3` |
| Third-person contents | 0x13E81C0 | `48 8B 0D ?? ?? ?? ?? 8B 51 08 83 EA 01 74 11 83 FA 01 74 06 B8 03 00 10 00 C3 B8 0B 80 00 00 C3 B8 0A 80 00 00 C3` |
| GetSpawnId | 0x6CB1D0 | `4D 85 C0 75 0A C7 02 FE FF FF 01 48 8B C2 C3 49 63 40 10 3D FE 3F 00 00 75 0B B8 FD FF FF 01` |
| Preset-shape builder | 0x4F7550 | `48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 56 41 57 48 8D A8 ?? ?? ?? ?? 48 81 EC 70 04 00 00 0F 29 70 D8 0F 29 78 C8 44 0F 29 40 B8` |
| `GetEyeOffset` (vslot 0x690) | 0x1452E80 | `48 89 5C 24 08 57 48 83 EC 30 33 C0 48 8B DA 48 89 02 48 8B F9 48 8B 05 ?? ?? ?? ?? 44 8B 40 0C 44 89 42 08 E8 ?? ?? ?? ?? 84 C0 75 10 48 8B 07 48 8B CF FF 90 ?? ?? ?? ?? 84 C0 74 26` |
| Eye = origin + gravity * (-eyeZ) | 0x1452320 | `48 8B 4C 24 58 48 8B 01 FF 90 ?? ?? ?? ?? 0F 57 35 ?? ?? ?? ?? 48 8B CF F3 0F 10 44 24 68 F3 0F 10 54 24 6C F3 0F 10 4C 24 70 F3 0F 59 C6 F3 0F 59 D6 F3 0F 58 00 F3 0F 59 CE F3 0F 58 50 04 F3 0F 58 48 08 F3 41 0F 11 06 F3 41 0F 11 56 04 F3 41 0F 11 4E 08` |
| Physics GetOrigin | 0x50E310 | `F6 81 ?? ?? ?? ?? 20 74 08 48 8D 81 ?? ?? ?? ?? C3 48 8B 01 33 D2 48 FF 60 78` |

## 5. Risks and live checks

1. Timing against the physics step: the call at 0x6A31B7 is not legal; the step is in flight there
   ("When the world may be queried" above). The deferred mode is no way out for the camera hook: its
   queries fall outside the window 0x4F6E20 reads.
2. Which eye path normal play takes: read `vieworg - origin` at the hook rather than rebuilding the eye.
3. Always pass a valid `name`: the debug draw at 0x4FD0E0 reads it under `g_showCollisionQueryFilter`.
4. `queryId` is always written: pass a writable qword.
5. The ignored spawn id excludes only the player's own body; MASK_PLAYERSOLID also hits monsters, which is
   why the head sweep uses 0x100009.

Live checks (docs/VR_ROOMSCALE.md, live test plan): `room: first head sweep: fraction 1.000, player spawn id
0x...` in a clear room; a fraction below 1 with the head leaned into a wall; no fault over a level.
