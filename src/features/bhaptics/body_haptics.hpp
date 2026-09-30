#pragma once

// bHaptics suits and sleeves (docs/BHAPTICS.md): what the layer sees of the game turned into motor frames
// for the bHaptics Player's local WebSocket (bhaptics_message.hpp encodes them).
//
// Effects and what starts each one:
// - shot: every shot of the local player (the fire hook) pulses the weapon arm's sleeve and the upper
//   chest on the weapon side, harder for heavier weapons (weaponClassOf); at most one every kShotGapSeconds;
// - damage: health plus armor dropping between two updates. With the direction of a new hit seen within
//   kHitWindowSeconds (the yaw of its source from where the player faces) the two vest columns nearest that
//   direction play, front or back; without one the middle rows of both sides. Harder for more damage;
// - heartbeat: while health is above 0 and below kLowHealth, a lub-dub on the left of the chest, quicker as
//   health falls;
// - glory kill: the game's sync kill starting (a sync master appears) pulses the whole front and both
//   sleeves;
// - death: the player dying fills both sides of the vest once.
//
// Only while `gameplay` holds (no menu, no loading, the reads fresh); outside it the edges are forgotten, so
// nothing fires on the way back in. Every intensity is scaled by the strength (ETERNALVR_BHAPTICS_INTENSITY).
// Pure: the layer feeds it from its own thread and sends what it returns. No Windows or network here.

#include "features/input/controller_state.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace evr::bhaptics {

// The places a frame can play (the Player's "position" names, positionName).
enum class Device : std::uint8_t {
    VestFront,
    VestBack,
    ForearmL,
    ForearmR,
    Count,
};

inline constexpr std::size_t kDeviceCount = static_cast<std::size_t>(Device::Count);

const char* positionName(Device device);

// Motors per device in the Player's frame indices. A vest side is 4 columns by 5 rows, index = row * 4 +
// column, row 0 at the top; a sleeve has 6 (the Player maps these onto smaller suits by itself).
inline constexpr int kVestColumns = 4;
inline constexpr int kVestRows = 5;
inline constexpr int kVestMotors = kVestColumns * kVestRows;
inline constexpr int kSleeveMotors = 6;

int motorCount(Device device);

// On both sides of the vest, column 0 is the wearer's left: a tester's suit felt right-handed recoil on the
// left of the chest while the front was taken to count from the wearer's right (public issue #1). vestColumn
// gives the column of a side for a wearer-relative column (0 = the wearer's far left, 3 = far right).
int vestColumn(Device side, int wearerColumn);

struct Dot {
    std::uint8_t index = 0;
    std::uint8_t intensity = 0; // 0..100
};

enum class Effect : std::uint8_t {
    Shot,
    Damage,
    Heartbeat,
    GloryKill,
    Death,
    Count,
};

inline constexpr std::size_t kEffectCount = static_cast<std::size_t>(Effect::Count);

const char* effectName(Effect effect);

// One frame for one device: the dots play at their intensity for `durationMillis`, then stop. A frame with
// the same effect and device replaces one still playing (the message's key is built from both).
struct Frame {
    Effect effect = Effect::Shot;
    Device device = Device::VestFront;
    int durationMillis = 100;
    std::vector<Dot> dots;
};

// How hard a weapon kicks, from the held item's decl name ("weapon/player/shotgun").
enum class WeaponClass : std::uint8_t {
    Light,  // the Plasma Rifle, the Chaingun, the Heavy Cannon, the Unmaykr: many small shots
    Medium, // the Combat Shotgun and anything not known
    Heavy,  // the Super Shotgun, the Rocket Launcher, the Ballista
    Huge,   // the BFG 9000
};

WeaponClass weaponClassOf(std::string_view declName);
const char* weaponClassName(WeaponClass weapon);

inline constexpr double kShotGapSeconds = 0.07;
inline constexpr float kLowHealth = 30.0f;
// Damage below this (health plus armor) is not felt: regeneration noise and rounding.
inline constexpr float kMinDamage = 0.5f;
// A new hit's direction applies to damage seen this long after it (the two may arrive a frame apart).
inline constexpr double kHitWindowSeconds = 0.15;

// What the layer read this update. Levels, except `shots` (counted since the last update) and the hit.
struct BodySignals {
    double seconds = 0.0;  // a steady clock
    bool gameplay = false; // the player is in play: no menu, not loading, the reads below fresh
    float health = 0.0f;
    float armor = 0.0f;
    bool dead = false;
    bool sync = false;       // a sync or glory kill runs
    std::uint32_t shots = 0; // shots since the last update
    WeaponClass weapon = WeaponClass::Medium;
    input::Hand weaponHand = input::Hand::Right;
    // The newest hit the game recorded: a number that changes with each one, and the yaw of its source in
    // degrees from where the player faces (0 ahead, 90 to the left, -90 to the right, 180 behind), when the
    // game gave a direction.
    std::uint32_t hitSerial = 0;
    std::optional<float> hitYawDegrees;
};

class BodyHaptics {
public:
    // A strength that is not finite or not in [0, 1] counts as 1.
    explicit BodyHaptics(float strength = 1.0f);

    std::vector<Frame> update(const BodySignals& signals);

    // Forgets the last levels (the player changed, or the link restarted).
    void reset();

    [[nodiscard]] float strength() const { return strength_; }
    // Frames made per effect since the start, for the layer's summary.
    [[nodiscard]] const std::array<std::uint64_t, kEffectCount>& counts() const { return counts_; }

private:
    void add(std::vector<Frame>& out, Effect effect, Device device, int millis, std::vector<Dot> dots);
    void shot(std::vector<Frame>& out, const BodySignals& signals);
    void damage(std::vector<Frame>& out, float amount, std::optional<float> yawDegrees);
    void heartbeat(std::vector<Frame>& out, const BodySignals& signals);
    [[nodiscard]] std::uint8_t scaled(float intensity) const;

    float strength_;
    bool primed_ = false; // the levels below are from a gameplay update
    float health_ = 0.0f;
    float armor_ = 0.0f;
    bool dead_ = false;
    bool sync_ = false;
    std::uint32_t hitSerial_ = 0;
    std::optional<float> recentHitYaw_; // the direction of a hit seen within kHitWindowSeconds
    double recentHitSeconds_ = 0.0;
    double nextShot_ = 0.0;
    double nextBeat_ = 0.0;
    bool dub_ = false; // the next beat is the second of the pair
    std::array<std::uint64_t, kEffectCount> counts_{};
};

// The two vest columns nearest a hit from `yawDegrees` (see BodySignals) with their share of the hit, 0..1,
// on the side facing it. The torso is taken as 8 columns round a cylinder, 4 in front and 4 behind.
struct HitColumn {
    Device side = Device::VestFront;
    int wearerColumn = 0; // 0 = the wearer's far left
    float weight = 0.0f;
};
std::vector<HitColumn> hitColumns(float yawDegrees);

} // namespace evr::bhaptics
