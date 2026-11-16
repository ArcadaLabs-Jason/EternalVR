# Handled access violations at a map load: the map name read past its page, and the arms' freed node (2026-11-14)

## Symptom

Two bench-combat runs of 2026-11-13 (of about 90 bench runs that day, 30 of them combat; both faults on the
combat route) failed the QA rule that no layer log may hold a crash note: a first-chance access violation,
handled, about 13.7 s into the log, logged in the same millisecond as and just before
`mp guard: map load 'game/sp/e1m3_cult/e1m3_cult'`, by the same thread:

```
crash note: first-chance exception (it may still be handled) 0xC0000005 (access violation, read of 0x26CDD9DA003)
  at EternalVR.dll+0x2B8FF1; stack: EternalVR.dll+0x106E9, DOOMEternalx64vk.exe+0x2A56633,
  DOOMEternalx64vk.exe+0x2895E97, EternalVR.dll+0x53DD2, ...
```

The second run read `0x275E7803013`. Nothing crashed and the route's data was complete.

## Cause 1: the map name copied past its page [code] [log]

The multiplayer guard's map load hook (`mp_guard.cpp`, S1 in `mp-guard.md`) copied the map name with one
guarded `memcpy` of 259 bytes and, when that faulted, read it again byte by byte. The name is the data of an
`idStr` the game builds for `LoadMap`; `game/sp/e1m3_cult/e1m3_cult` is 27 characters, too long for the
string's inline buffer, so it sits in a small heap block. When that block lies within 259 bytes of the end of
the heap's committed memory, the fixed-size copy runs on into a page that is not mapped: an access violation on
a good pointer, handled by the guard, and the byte-by-byte read then got the whole name. Both fault addresses lie
a few bytes past a 4 KiB boundary (`...A003`, `...3013`), as a copy running off the end of a mapped page would.

The fault was first put down to body follow's player read. It was not:

- The faulting `EternalVR.dll+0x2B8FF1` is the CRT's `memmove`; `+0x106E9` is the return into a guarded-copy
  helper. The release build folds every identical `__try { memcpy } __except` helper into one function (the
  linker's identical code folding): body follow's `copyGuarded`, the multiplayer guard's `safeCopy`, the head
  sweep's, the flares', the menu model's and others all share that one address, and the PDB names it after the
  first, `body_follow.cpp`. The fault's own address says nothing about which caller it was.
- The crash note's stack is a scan of the stack for words that point into the layer or the game, not an unwound
  chain. The next layer word, `+0x53DD2` (`+0x54132` in the other run's build), lies among the multiplayer guard's
  functions, and it moved between the two builds by exactly as much as `memmove` did (0x360), so both runs fault
  at the same call.
- Body follow reads the player from the camera hook, whose lines (body follow's own first one 1.6 s after the
  map load) are on another thread in the same log; bHaptics' frame, its other caller, is off in the bench runs.
  The fault was on the thread that logs the map load, at that line, with an unaligned read address (a string
  copy, not a pointer or float field).

Fix: `copyGameString` (`src/vkcore/game_string.hpp`) copies a string a page at a time up to its NUL. A page is
mapped and protected whole, so each chunk is all readable or not at all, and the copy never reads past the page
its NUL is on; a fault is left only for a page the string itself lies on (a bad pointer), which is what the guard
is for. The map load hook and the held weapon's decl name (`viewmodel_hook.cpp`, the same fixed-size copy of
127 bytes with the same fallback) use it. The multiplayer guard now also trips on a name not read to its NUL (a
later page unreadable, or longer than its buffer): such a prefix could pass as single-player with the online
segment in the part lost, which the byte-by-byte fallback before also allowed (`mp_policy::classifyMapRead`
classifies it as unknown). The unit tests put names at the end of a
page with a no-access page after it and count first-chance access violations: none for a name that ends on the
readable page, one for a name that really runs into the bad page.

## Cause 2: the arms' cached modifier node after a load [rig] [code]

The map load soak below (41 loads in one process, the build before the fix) did not show the name fault, but
after its 28th load it logged another handled access violation on the main thread, 1.3 s after the map load
line and once a tick from then on (16 lines in 0.14 s, the crash note's cap for one place):
`EternalVR.dll+0x2B9B2D` (`memmove`), called from `controllers::safeCopy`, from `offhand_mods::readList`
through `layerModsInPlace`, from `game_arm::releaseArm` (that build's PDB). The arms keep the hands they posed
and the hands' joint modifier node (`game_arm_cache.hpp`). `readArm` reads the node again from the hands each
time, but the release paths (`releaseArm`, `writeArm`'s check, `overridesLeft`) took the cached node whenever
the hands' address matched the cached one. After a map load the hands can have the old address while the old
node is freed: the release read a freed node, and once its page was given back, faulted every tick.

Fix: those paths take the cached node only while it is still the live hands' own (`liveNode` in
`game_arm_write.cpp`: the hands' modifier owner read again, its node compared). A new node holds none of the
layer's modifiers until `readArm` adds them, so there is nothing to release; the multiplayer guard's trip
release (both lists, memory writes only, called once) no longer writes through a freed node, and when the live
node is not the cached one it clears the live node if the layer's modifiers are found in its lists (the joint
check), else logs `arm release: node changed, nothing of ours found`.

## Class audit [code]

Guarded reads whose size can exceed the object they read (cause 1):

| Reader | Read | Verdict |
|---|---|---|
| `mp_guard.cpp` map load hook | the map name, 259 bytes at once | same fault (seen twice): fixed |
| `viewmodel_hook.cpp` `declName` | a decl name, 127 bytes at once | same fault (not seen in a log): fixed |
| `arm_model.cpp` `cString`, `hands_surfaces.cpp` `readName` | names, a byte at a time | safe: never past the NUL |
| `bhaptics_portal.cpp` fast travel text | the game's own length of its string, at most 95 | safe: within the string |
| `game_text.cpp` `stringAt` | strings in the exe's image | safe: bounded by the module |
| every other guarded read | a fixed-size field or struct at a known offset | not this class |

Cached game pointers read on a path that runs through a map load (cause 2):

| Reader | Cached | Verdict |
|---|---|---|
| `game_arm_write.cpp` release, write check, overrides left | the hands' modifier node | same fault (seen once): fixed |
| `game_arm.cpp` `readArm` | the node, the animator | safe: read again from the hands every call |
| body follow, bHaptics | the player | safe: the player is the hook's argument, checked by its vtable (`PlayerAim::isPlayer`) |
| head sweep | the map instance, its collision world | safe: the map instance read again from the exe's global every call, the collision world taken again when it changes |
| mp_guard's menu poll | the main menu | safe: read again from the exe's global every poll |
| `game_arm.cpp` `readHand` | the animator of the last `readArm` | same class (checked 2026-11-14, not seen in a log): it called the game's GetJointTransforms on that cached animator, which a map load can free while the hands keep their address, so a read could fault (a handled access violation, after which no call into the arms is made again) or read another object's joints. Its callers in play (`weapon_arm`, `offhand_fingers`) follow `readArm` in the same tick, which clears the cached animator and sets it again only after a good read; the rig dump's (`weapon_finger`, `arm_dump`) and a tick where `readArm` was not called did not. Fixed: `readHand` reads the animator from the hands' render model again (`liveAnimator`) and returns false unless it is the cached one |

Not checked: the crash notes in older bench and QA logs (other builds, their PDBs gone) cannot be put to a
function, so the audit does not say whether other places faulted the same way before.

## Measured [rig]

My test PC (RTX 3080 Ti, the OpenXR simulator at 90 Hz), the QA suite's `map-load-loop` (bench-combat's start
and environment, release builds), the build before the fixes against the build with them:

| Run | Before the fixes | With them |
|---|---|---|
| one process, 41 loads (the launch's and 40 from the debug schedule every 20 s) | map name 0, arms' node 1 (after the 28th load) | 0 crash notes |
| 33 and 32 processes, 2 loads each (the launch's and one reload), taken in turn | 0 crash notes | 0 crash notes |
| in all | 107 loads, 34 launches: map name 0, arms' node 1 | 105 loads, 33 launches: 0 |

The name fault did not come back in 107 loads, against 2 in 30 bench-combat launches on 2026-11-13: it needs
the name's heap block at the end of committed memory, and these runs' heaps (a different environment from the
bench routes' measuring switches) did not put it there. For it the evidence is the code and the unit tests; the
arms' fault came back once before the fixes and not after, a small count either way.

## After the readHand fix (2026-11-14) [rig]

`readHand` reads the hands' animator again and returns false unless it is the cached one. QA `map-load-loop`
on my test PC, release build with the fix: one process, 31 loads (the launch's and 30 from the debug schedule),
every load reaches the map load hook, 0 crash notes, no guard trip, no crash dump: pass. The stale animator itself was not seen to fault before the change (the fix rests on the code reading above), and this run did not turn the rig dump (`arm_dump`) on.
