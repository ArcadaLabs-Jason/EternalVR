# Lights dropped by Umbra: the Fortress of Doom stairs going dark and bright

In the headset, the short curved steps up to the Fortress of Doom's command dais (the round platform with the
ARC and Mission Select screens, the planet window behind) switched between dark and lit as the player moved,
with Parallel Eye Rendering on or off and ray tracing on or off. This page records how it was reproduced and
isolated on the test PC (RTX 3080 Ti, OpenXR-Simulator, Steam build 25216728) and the fix. RVAs are in that
build.

## 1. Reproduction

- The late-campaign hub (Continue from a finished campaign; `-Map game/hub/hub` loads the early hub, where it
  does not show). Jason's view is `setviewpos 0.5 21.96 5.0 90`: the player lands on the step below the dais,
  view origin z 4.16 to 4.24. The dais itself sits around (0.4, 28): `teleport interact_hub_mission_select_1`
  gives 3.78 26.39, `interact_hub_arc_console_1` -2.97 30.11, `interact_hub_master_level_panel` -3.53 26.01.
- Static head, the position stepped up the approach (`setviewpos` every 3 s, a capture every second; each
  capture's `.txt` records the game position). Two captures at one spot agree within about 1 (the control).
  Standing on the lower step (y up to about 22.2, view z up to 4.32) the steps, the dais floor and the weapon
  are dark; one step up (y 22.4, z 4.40), or in the air while falling onto the step, they are lit: the steps'
  brightness doubles (eye L 30 to 65, eye R 38 to 88) while the emissive hologram globe above stays the same, so
  it is not exposure. The weapon, which moves with the camera, switches too.
- It follows the game's view origin (the body), not the drawn eye: raising the head 25 cm with
  `ETERNALVR_TEST_HEAD_OFFSET` (the image moved about 50 pixels) did not switch it at that spot. It also shows
  in a mono launch, so it is the game's own culling and not the stereo routes.

## 2. Which culling

One cvar per run (`ETERNALVR_DEBUG_CVARS`), the same two spots (dark: 0.5 21.67, bright: 0.5 22.44), the bright
spot's brightness over the dark spot's, eye L / eye R:

| Run | Bright / dark |
|---|---|
| Defaults | 2.15 / 2.30 |
| `r_useUmbraCulling 0` | 0.99 / 1.04 (the dark spot lit like the bright one) |
| `r_skipLightCPUCulling 1` | 1.95 / 2.03 |
| `r_skipLightRangeCulling 1` | 2.01 / 2.09 |
| `r_environmentProbes 0`, `r_lightGrid 0`, `r_skipLightGPUCulling 1`, `r_skipAreaGPUCulling 1`, `r_staticShadowsCullWithUmbra 0`, `r_skipUmbraStaticModelCulling 1`, `r_useUmbraSubspaceOcclusion 0`, `r_umbraAdaptiveOcclusionThreshold 0`, `r_umbraAddWorldAreasToScene 0` | 2.12 to 2.22 / 2.25 to 2.32 (no change) |

`r_umbraSkipLocalLightQuery 0` also reads about 1, but most of the world's geometry disappears (the view shows
space through the dais): not a fix. `r_skipAreaCPUCulling 1` makes the game exit about 11 s after start.

Umbra off fixes the picture but costs about half the frame rate (e1m2, Route S, 150 s of scripted play, median
game ticks a second after 60 s: 1440x1552 without ray-traced reflections 120 to 47, 2048x2208 with them 80
to 42).

## 3. The light gather

The per-view light gather (RVA 0x1C782C0; called from RVA 0x1C7FFD0 and also run as a job) walks each listed
area's lights. After the view mask, enabled, colour and range checks (each of which drops the light at
0x1C78549):

```
1C7851E  mov rax, [rdi+18h]          ; the cull settings (filled by RVA 0x1C5DFD0)
1C78522  cmp byte [rax+24h], 0       ; useUmbra (r_useUmbraCulling and a tome loaded)
1C78526  je  1C7896B                 ; Umbra off: the view frustum test (RVA 0x1C88920)
1C7852C  mov edx, [rbx+54h]          ; the light's Umbra object
1C7852F  test edx, edx
1C78531  je  1C78612                 ; none: gate B
1C7853C  call 1D17E50                ; gate A: is it in the set of visible Umbra light objects?
1C78541  test al, al
1C78543  jne 1C787EF                 ; yes: kept; no: dropped at 0x1C78549
...
1C787E2  call 2272270                ; gate B: its box, clipped to the area, against the occlusion buffer
1C787E7  test eax, eax
1C787E9  je  1C7895A                 ; hidden: dropped, its done bit cleared (another area may keep it)
1C787EF  ...                         ; kept by Umbra: for some lights (types 0, 1, 4 with a flag at +0x308)
1C7894B  call 1D18A80                ; a sphere around the light against Umbra
1C78950  test al, al
1C78952  je  1C78549                 ; hidden: dropped
...
1C7896B  cmp byte [rax+2Bh], 0       ; the Umbra-off path: r_skipLightCPUCulling
1C78971  ...  call 1C88920 ...       ; the frustum test
1C789C3  test al, al
1C789C5  jne 1C78549                 ; outside the view: dropped
1C789CB  mov r10, [rsp+38h]          ; kept: the light goes on into the view's list
```

With Umbra on, a light is never tested against the view: it is kept only when Umbra's query reported its
light object visible (objects of type 1, `umbraObjectType_t` LIGHT, collected when the query finishes at RVA
0x1D1A5A0), or, without an object, when its box passes the occlusion buffer, and then the sphere test. From
the lower step Umbra reports the dais light hidden although its light falls on the steps in view, and the
gather drops it. `r_skipLightGPUCulling` does nothing here because Umbra forces that flag on (settings + 0x2C).

## 4. The fix

`ETERNALVR_LIGHT_FRUSTUM=1` (`src/vkcore/light_cull.cpp`) gives the lights Umbra hides a second opinion from
the frustum test the game uses with Umbra off. Mid hooks at the three drops (0x1C78541, 0x1C787E7, 0x1C78950)
send a light Umbra hides to the frustum test (0x1C78971, past its `r_skipLightCPUCulling` check, which the
hook makes itself), and a hook at the frustum verdict (0x1C789C3) sends a light the frustum drops too to the
drop of the gate that asked (gate B's clears the done bit, as before). A light is dropped only when Umbra and
the frustum both say hidden; every light Umbra keeps stays. Geometry, decals and areas keep Umbra. The
signature, the code at every hooked offset and the PE timestamp are checked first, and it works only while the
multiplayer guard is armed (checked at install and for every light Umbra hides). Every 10 s the log counts the
lights Umbra hid that the frustum kept, per gate.

The first version changed one byte (the `je` at 0x1C78526 to `jae`): every light took the frustum test alone
and Umbra's verdict was dropped. It fixed the dais the same way, but it could drop a light Umbra kept, so it
was replaced.

Measured with the fix (same matched-view pair as section 2): bright / dark 0.99 / 1.03 (2.15 / 2.30 without);
the dark spot reads 65.7 / 85.0 against 65.6 / 84.5 with Umbra off, and the pictures match the Umbra-off ones
(geometry intact, both eyes). In a mono launch the same pair reads 1.44 without the fix (dark step 32.1,
bright 46.3) and 1.01 with it (69.7 / 70.6). On the dais approach the frustum keeps about 3,500 to 5,700
lights a second that Umbra hid (gate A) and 850 to 1,500 (gate B); the sphere test never hid a light the
frustum kept.

Cost: none measured (e1m2, Route S, 150 s of scripted play, median game ticks a second after 60 s, each pair
interleaved: 1440x1552 without ray-traced reflections 133.4 / 129.8 without, 133.7 / 129.4 with; 2048x2208
with them 85.2 / 84.5 without, 83.8 / 85.3 with). A gate B light both tests drop is tested again in each
later area of the view, as the game itself does for gate B.

## 5. Elsewhere

Settled tours (12 s at each teleport target, a capture every 3 s, the mean of the captures taken at least 5 s
after arriving), without the fix, with it, and with `r_useUmbraCulling 0`: e3m1 (Taras Nabad,
`checkpoints_player_start_cp_02` to `_17`, 8 spots compared) and e4m1 (Atlantica, `checkpoints_player_start_2`
to `_32`, 10 spots) match within the spots' own noise everywhere. Two spots needed a look at the pictures: an
Atlantica room (start 8) with a periodic bright flash, and a spot (start 28) where a different weapon was in
hand in one run.

A tour that stays only 4 s at a spot measures auto-exposure still adjusting from the previous spot: an earlier
tour read a Taras Nabad spot (cp_08, 158.0 236.0) 81 without the first version and 65.5 with it; settled, over
22 s, the two read 69.1 and 69.2 (it swings 58 to 87 by itself as clouds pass).

e1m3 (Cultist Base, checkpoint `cp_03_shoot_gate`, the teleports `game_player_start_1` to `_10`, static head)
with Umbra on, with `r_useUmbraCulling 0` and with the fix: one of the ten spots differs between Umbra on and
off (0.0 -150.4 14.8, yaw 90: 22% of the pixels; with Umbra off part of the snowy floor is darker, as if
shadowed). There the fix matches Umbra on (all ten spots within 1.4% of the pixels), so that difference is not
a light dropped by the gather; a shadow caster or occluder culled by Umbra is the next thing to test
(`r_staticShadowsCullWithUmbra` at that spot).

## 6. Open

- The fix in the headset at the dais, and with Parallel Eye Rendering, before it is turned on by default.
- The e1m3 spot above.
- Why `r_skipLightCPUCulling 1` brightens the dark spot by 10% with Umbra on (without the fix the gather reads
  that flag only on its Umbra-off path; with it, also for the lights Umbra hides).
