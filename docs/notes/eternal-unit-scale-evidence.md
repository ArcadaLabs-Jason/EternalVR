# DOOM Eternal world units: evidence that 1 game unit = 1 metre

- Sources:
  - KEX cvar list, https://github.com/Official-KEX/doom-eternal-full-cvarlist
  - DOOM Eternal Archipelago mod map data, https://github.com/snowzzrra/DoomEternal-AP-Mod
    (`content/maps/*/onboarding.json`, values copied from the game's `.entities` files, target
    build "Steam 6.66 Rev 3.1")
  - Doom 3 BFG VR (Fully Possessed), `neo/vr/Vr.cpp` cvar descriptions
- Fetched: 2026-09-25
- License: cvar rows MIT (KEX); coordinates are game data quoted as numbers only; the rest is our analysis.

## Summary

| Engine | Unit | Evidence | Status |
|---|---|---|---|
| id Tech 4 / Doom 3 BFG | 1 unit = 1 inch | Doom 3 BFG VR describes `vr_normalViewHeight` "in real world inches" and uses 73 as the default; its world scale converts real inches directly | [C] (by that mod's design) |
| id Tech 6 / DOOM 2016 | 1 unit = 1 inch | a shipped DOOM 2016 VR mod uses a default world scale of 39.3701 units per metre (inches per metre) | [C] (shipped and played) |
| id Tech 7 / DOOM Eternal | 1 unit = 1 metre (very likely) | see below | [U] until measured on the rig |

## Evidence for metres in id Tech 7

1. `vr_metersToGameUnits` "How many game units one meter represents." default `1.0`. This belongs
   to the dormant VR subsystem that id left in the executable.
2. Descriptions written in metres without a units tag: `pm_minPlayerVel` "in meters/sec" 0.01,
   `ai_physics_stepUpHeight` "in meters" 0.4, `cm_havokShrinkDistanceTolerance` "(meters)" 0.10,
   `grenade_bounceSpeedThreshold` "meters per second" 10.0. These are raw values a programmer typed.
3. Untagged `pm_normalheight 1.79` and `pm_crouchheight 0.89` only make sense as metres
   (1.79 inches would be a 4.5 cm player).
4. The cvar parser understands a `_gu` suffix (`dp_meshHideDistance "60.0_gu"`,
   `g_propDropHeightOffset "40.0_gu"`) and unit tags like `400_kg_per_s2`. A suffix for game units
   only makes sense if game units are not the canonical unit.
5. Physics is Havok (`cm_havok*`, `pm_lineardamping_*` "on the havok body"), which works in SI units.
6. Entity `spawnPosition` values from shipped maps span kilometres if read as metres and only tens
   of metres if read as inches:
   - `e5m1_spear` (Immora), 14 positions: extent x 186.8, y 1989.8, z 1367.6 units.
     As metres that is a 2 km long level with 1.4 km of vertical travel (plausible for Immora's
     descent); as inches it would be 50 m x 35 m, far too small.
   - `e5m2_earth`, 18 positions: extent x 584.7, y 348.7, z 58.9 units.
7. Many metre defaults are exact inch conversions (0.9144 = 36 in, 1.65735 = 65.25 in,
   9.525 m/s = 375 in/s, 3.6195 m/s = 142.5 in/s, 1.3716 = 54 in). This shows tuning values were
   carried over from inch-based id Tech 6 and converted, which is why the numbers look odd.

## Consequence

- The DOOM 2016 scale of 39.3701 must not be carried over. Our starting world scale is 1.0 game unit per metre,
  kept configurable, and confirmed with the measurement below before anything is tuned.
- Measurement on the rig: noclip, `getviewpos` (or `mh_spawninfo`), walk a known number of
  seconds at `pm_walkspeed`, or teleport by a fixed delta and look at a known-size object; also
  read `idPlayer` origin via typeinfo while standing on flat ground: origin z to eye z should be
  1.657 (metres) or 65.25 (inches).
