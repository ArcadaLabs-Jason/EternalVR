# Console commands on a schedule (test rig)

Retail `DOOMEternalx64vk.exe`, Steam build 25216728. All addresses are RVAs in this build. The layer side is
`src/vkcore/debug_commands.cpp`; the schedule's parser is `src/vkcore/debug_script.cpp`.

## 1. Use

```
ETERNALVR_DEBUG_COMMANDS=10:where|12:ai_Show|14:nextActiveAI;ai_teleportToPlayer trace
```

Steps are separated by `|`, each `<seconds>:<command>[;<command>...]`. Seconds count from the moment the
player is in the map: the head aim logs `aim: head aim on` once it has matched the player's state for 60
frames, and the schedule's clock starts there (`debug-commands: the player is in the map`). How long the
launch took does not matter. Each command is logged as it runs (`debug-commands: 14.0 s: nextActiveAI`);
its output goes to the game's console log (`qconsole.log`, copied into the rig's run folder).

Unset (the default), nothing is located or hooked. Only while the multiplayer guard is armed.

The commands run with the console's restriction lifted, so only test commands run: the first word of each
command (any case) must be on the allow-list in `src/vkcore/debug_script.cpp` (the commands and cvars the rig
scripts and the QA suite use), and the command may hold only letters, digits, spaces and `_ - . /`. Anything
else is left out of the schedule and logged once (`debug-commands: left out of the schedule, step 2: bind ...
(not a test command)`); the rest runs. A new command or cvar for a rig test goes on that list first.

## 2. How it works

- The command system is `idCmdSystemLocal`. The engine names it in a log string just before a call:
  `lea rdx, "cmdSystem->ExecuteCommandText"` (0x431D68), then `mov rcx, [rip + global]` (0x431D78, the
  global at 0x4271BA8) and `call [rax + 0x40]` with `"BuildInfo"`. The layer finds the string, the load
  after it, and checks the object's vtable by its RTTI name.
- Vtable: +0x08 reads the calling thread's restriction level, +0x10 sets it, +0x40 `ExecuteCommandText`
  (tokenizes, then +0x78 runs the tokenized command), +0x48 appends to a command buffer, +0x58
  `ExecuteCommandBuffer`.
- The restriction level is thread-local (TLS slot +0x18). The tokenized path (0x179C500) prints
  `Not allowed` when the level is non-zero and the command lacks flag 2. Appending picks the unrestricted
  buffer (+0x38) or the restricted one (+0x10068) by the same level. A bind or key press runs with the level
  at 1 (0x17CCC90, the `mov edx, 1` that EternalPatcher's console unlock changes to 0).
- The hook is a mid hook at the start of `ExecuteCommandBuffer` (0x179C1F0), which the main loop calls every
  frame. Due commands run there through `ExecuteCommandText` with the thread's level set to 0 and restored
  right after: on the game's own thread, between frames, nothing patched in the game's code.

## 3. What was seen (rig, e1m2_battle start)

- `where` prints the view position and angles (`-81.3 308.4 14.66 45.0 -0.0`).
- `notarget` does not exist in this build (`Unknown command`); `noclip`, `god` untested.
- `nextActiveAI` and `nextAI` run (developer commands, formerly `Not allowed`). Right after the map loads
  `nextAI` fails with `no entities spawned`; 25 s in it no longer fails. `ai_teleportToPlayer` prints its
  usage (`use 'facing' to have AI face you, 'trace' to place them on geo under reticle`) each time.
- No demon came into view at the e1m2 start with `ai_Show`, `nextActiveAI` / `nextAI` and
  `ai_teleportToPlayer trace`: the demons there are not spawned until their encounter starts.

## 3a. What was seen (rig, e1m1_intro, 2026-09-28)

- The schedule's clock can start before the intro cutscene (head aim verifies in the few frames before it)
  or after it; steps from about 30 s on are in play either way.
- Console output reaches `qconsole.log` promptly (`+logFile 2`), but the rig copies the log when its watch
  ends: a watch too short for the schedule (the map is in play about 140 s after the launch) misses it.
- `g_dumpSpawnedEntities 1` prints every spawned entity's name with whether it is dormant (once; the cvar
  resets). `where` prints `x y z yaw pitch`. `teleport <entity>` moves the player onto that entity (a dormant
  pickup is picked up: `teleport barge_pickup_weapon_chainsaw_1` runs the chainsaw pickup),
  `teleportposition x y z yaw` onto a point (outside geometry if the point is). `p_debugAnimatedCamera 1` prints
  the hands camera joint every frame (docs/rig-findings/camera-animations.md).
- `trigger <name>` and `activatetargets <name>` on the Fortress door movers, relays and the chainsaw timeline
  did not open the doors; `gibalicious` ran; `noclip` is `Unknown command`.

## 4. Useful commands in the retail strings

`nextActiveAI` / `nextAI` / `prevAI` (debug target), `ai_teleportToPlayer [facing|trace]`,
`moveToFacingPlayer`, `ai_forceIdle`, `ai_forceFreeze`, `ai_forceAnim`, `ai_Show` / `ai_Hide`, `killAI`,
`removeAI`, `healAI`, `encounter_clearWait`, `setviewpos`, `teleport`, `teleportposition`, `activatetargets`,
`trigger`, `getviewpos`, `where`.
