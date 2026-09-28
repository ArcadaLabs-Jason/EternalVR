# World GUIs missing in eye R under Route S

In a headset session, the "BLUE ACCESS" hologram on a door in the first DLC mission (`e4m1_rig`) was drawn in
eye L and missing in eye R. The door itself was drawn in both eyes. RVAs are in Steam build 25216728.

## 1. Cause

A GUI on a world surface (door holograms, terminal screens, gate text) is an `idRenderModelGui` hung on a
host model. The world commits it once per game frame. The commit (0x18D9FA0) builds its surfaces and stamps
the committed data with the world's frame number (`[rcx + 0xC] = worldData + 0xBFD8`, at 0x18DA7FA).

The backend draws a world surface's GUI through 0x1C74D00, which calls the GUI-surface check 0x1C75090 (call
at 0x1C74E03). When the surfaces live in the per-frame GUI buffer (bit 31 of the surface record), the check
draws them only while the stamp equals the world's current frame number:

```
0x1C750DC  bt   rbx, 0x1F
0x1C750E1  jae  ...                ; persistent buffer: no stamp check
0x1C750E3  test r8, r8             ; r8 = worldData (0 for the screen's own GUIs)
0x1C750E6  je   ...
0x1C750E8  mov  eax, [r8 + 0xBFD8] ; the world's frame number
0x1C750EF  cmp  [rdx + 0xC], eax   ; the commit stamp
0x1C750F2  jne  skip
```

The world's frame number goes up once per render (`inc [worldData + 0xBFD8]` at 0x18E50BB). Under Route S a
tick is two renders, so eye R's frame number is one ahead of the commit eye L's render made for the same game
frame, and every per-frame world GUI is skipped in eye R. GUIs whose surfaces sit in the model's own
persistent buffer do not take this check and were drawn in both eyes.

## 2. Fix: `src/vkcore/world_gui_hooks.*`, `src/stereo_seq/world_gui.*` (default on; `ETERNALVR_STEREO_WORLD_GUI=0` turns it off, `=count` only counts)

A mid hook on the `cmp` at 0x1C750EF (signature at 0x1C750CD, checked against the world-surface call at
0x1C74E03) gives eye R's render the stamp of the commit when it is exactly one frame behind, so eye R draws
the surfaces eye L's commit built in the same tick. Only calls from the world-surface callback are changed
(return address 0x1C74E08). The vertices stay valid: the per-frame GUI buffer advances once per tick.

The log counts, every 10 s, the world GUI draws per eye by buffer and stamp (persistent / per-frame current /
one behind / older), the eye R draws given eye L's commit, the screen's own GUI draws, and the GUI model
commits per eye (a probe after the stamp at 0x18DA7FD).

## 3. Checks

- Rig (Fortress of Doom and the `e1m2` start): no crash; persistent world GUI draws equal in both eyes; no
  per-frame world GUI in view there, so the fix itself could not be seen on the rig.
- Headset, `e4m1_rig`: eye L per-frame current draws matched by eye R one-behind draws, all given eye L's
  commit (for example 491 in 10 s near the door); in-game captures show the BLUE ACCESS hologram in both
  eyes.
- If a GUI ever turns out to be recommitted in eye R with an empty surface list, the commit counts will show
  it; the matching fix would restore the surface list at 0x194D11D in eye R's commit.
