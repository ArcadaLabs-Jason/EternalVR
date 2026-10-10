#pragma once

// The Sentinel Crystal's wave (a tester's idea, public issue #1: the crystals shock you, so a wave from the
// centre of the vest out in all directions, about 2 s in all), played for a Praetor Suit token too (a rune
// plays its own shock, below). A ring grows from the centre of each side's grid, front and back alike: a
// motor rises as the ring comes near it and fades behind it, the middle of the chest and back first, the
// corners last, then both sleeves as it passes the shoulders. A step every kCrystalStepSeconds with a slight
// flicker on top (BodyHaptics::crystal). Pure.

namespace evr::bhaptics {

// The wave's start into the pickup animation, its length, and a step every kCrystalStepSeconds. The
// animation runs about 3.3 s from the upgrade menu closing, and the Slayer's hand takes the crystal about 2 s
// in (timed in a headset session, 2026-09-30); the wave starts a little before for the suit's latency.
inline constexpr double kCrystalDelaySeconds = 1.9;
// A Praetor Suit token plays the same wave. Its animation runs about 3.1 s from Use (no menu), and the
// Slayer's hands close on the coin 0.1 to 0.2 s in and hold it until about 2.6 s (simulator shots,
// 2026-10-01); the wave starts with them.
inline constexpr double kTokenDelaySeconds = 0.1;
// A rune plays a shock of its own (a tester's ask, public issue #25: the Slayer is shocked right after the
// perk is picked). Its sync (interact/rune/use_sync) starts as the rune's menu closes, as the crystal's
// does, and runs about 7.6 s (a player's log, 0.1.33). The moment of the shock in it is not timed yet: the
// shock starts soon after the menu, as the tester describes it, until a headset session times it. The Slayer
// is shocked for about 4 to 5 s. The crystal's wave played twice (0.1.37) felt like the token's, repeated, so
// on the tester's ask a rune now crackles all over the suit as one effect: every kCrystalStepSeconds a random
// kRuneSparkShare of the vest's and both sleeves' motors, each at a random level, kRuneShockSeconds in all,
// rising at once and fading over the last kRuneShockFadeSeconds (BodyHaptics::runeShock).
inline constexpr double kRuneDelaySeconds = 1.0;
inline constexpr double kRuneShockSeconds = 4.0;
inline constexpr double kRuneShockRiseSeconds = 0.2;
inline constexpr double kRuneShockFadeSeconds = 1.0;
inline constexpr float kRuneSparkShare = 0.3f;
inline constexpr double kCrystalWaveSeconds = 2.0;
inline constexpr double kCrystalStepSeconds = 0.08;

// Distances in motors (a column or a row apart is 1). The ring starts at kCrystalStartRadius (the two middle
// motors of a side) and moves out evenly until its tail has left the sleeves. A motor is felt from
// kCrystalLead ahead of the ring to kCrystalTrail behind it, strongest under it. The sleeves count as
// kCrystalSleeveDistance from the centre, past the vest's corners (2.5).
inline constexpr float kCrystalStartRadius = 0.5f;
inline constexpr float kCrystalLead = 0.6f;
inline constexpr float kCrystalTrail = 0.8f;
inline constexpr float kCrystalSleeveDistance = 3.0f;

// A vest motor's distance from the centre of its side's grid (the grid is symmetric, so either column count
// gives the same).
float crystalDistance(int row, int column);

// The share of the wave's peak, 0..1, `distance` from the centre `sinceStart` seconds into the wave; 0
// outside the wave and for values that are not finite.
float crystalWaveShare(double sinceStart, float distance);

// The rune's shock's strength, 0..1, `sinceStart` seconds into it: from 0.3 up to 1 over
// kRuneShockRiseSeconds, then down to 0 over the last kRuneShockFadeSeconds; 0 outside it and for values that
// are not finite.
float runeShockShare(double sinceStart);

} // namespace evr::bhaptics
