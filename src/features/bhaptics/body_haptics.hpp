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
//   sleeves; a sync that is a pickup's animation (a rune, a mod bot, a Praetor token: syncKindOf) does not;
// - Sentinel Crystal: its pickup animation (the Slayer grabs the crystal and takes its energy) plays a
//   wave (crystal.hpp) from the centre of the vest, front and back, out to its edges and then both
//   sleeves, over kCrystalWaveSeconds from kCrystalDelaySeconds into the animation (a tester's idea,
//   public issue #1). The animation starts as the upgrade menu closes, when `gameplay` is not back yet
//   (the menu's held-back controls, stale reads), so its start is seen in every update and the wave is
//   timed from it once play is back (kCrystalDelaySeconds after the start, nothing once
//   kCrystalWaveSeconds of it would be over);
// - Praetor Suit token: its pickup animation (no menu: the Slayer's hands take the coin and hold it up) plays
//   the crystal's wave from kTokenDelaySeconds into it, as the hands close on the coin (a tester's idea,
//   public issue #1; crystalWaveDelay);
// - Flame Belch and equipment launcher: both sit on the Slayer's left shoulder, so a belch (a shot while its
//   button is held) and an equipment launch pulse the top of the left side, front and back; the launch
//   lighter and shorter (a tester's suggestion, public issue #1);
// - death: the player dying fills both sides of the vest once;
// - landing: coming down from a fall of kLandingDrop or more (the layer's LandingDetector, run on every game
//   frame) pulses the bottom row of the vest, front and back, harder for a longer fall (a tester's idea,
//   public issue #1);
// - portal: going through a teleporter, a portal or a level exit (the layer's hooks, teleportKindOf) sweeps a
//   crackle down the vest from the top row to the bottom, front and back, with the sleeves buzzing, over
//   kPortalSeconds (a tester's idea, public issue #1);
// - pickups: health going up (the layer's PickupDetector, run on every game frame) sends a wave up the vest
//   from the bottom row to the top, front and back; armor the same wave down from the top. Harder for a
//   bigger gain; a Mega Health at full strength and slower (PickupWave). A row is left out while a hit,
//   a glory kill, death, a landing, the crystal or a portal plays on the vest (a tester's idea, public
//   issue #1);
// - launch: a jump pad or a booster launching the player (the layer's hooks, LaunchFilter) shoves the bottom
//   row of the vest, front and back, strong and then fading while the row above joins in, over
//   kLaunchSeconds (a player's idea, public issue #1).
//
// Only while `gameplay` holds (no menu, no loading, the reads fresh); outside it the edges are forgotten, so
// nothing fires on the way back in (the crystal's start excepted, above). Every intensity is scaled by the
// strength (ETERNALVR_BHAPTICS_INTENSITY). Pure: the layer feeds it from its own thread and sends what it
// returns. No Windows or network here.

#include "features/bhaptics/crystal.hpp"
#include "features/bhaptics/landing.hpp"
#include "features/bhaptics/launch.hpp"
#include "features/bhaptics/pickups.hpp"
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
    Belch,
    Equipment,
    Landing,
    Crystal,
    Portal,
    Health,
    MegaHealth,
    Armor,
    Launch,
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
// A landing is felt from a drop this high (game units, about a metre each): on the rig a jump drops 1.4,
// a double jump 3.2 to 3.3 (both left out), a fall from a ledge more.
inline constexpr float kLandingDrop = 3.5f;

// What a sync (the player's sync master set) is, from its sync entity's entityDef name: a Sentinel Crystal's
// pickup (interact/argent_cell/use_sync), a Praetor Suit token's (the game spells it
// interact/preator_suit_token/preator_suit_token_sync), another pickup (interact/...: runes, mod bots,
// batteries), or anything else, taken for a glory kill (as before, also when the name could not be read).
enum class SyncKind : std::uint8_t { GloryKill, Pickup, Crystal, Token };

SyncKind syncKindOf(std::string_view entityDefName);
const char* syncKindName(SyncKind kind);

// How far into a sync of `kind` the crystal's wave starts: kCrystalDelaySeconds for a Sentinel Crystal,
// kTokenDelaySeconds for a Praetor token, none for the other kinds (crystal.cpp).
std::optional<double> crystalWaveDelay(SyncKind kind);

// What a trigger teleport of the player is (idTrigger_Teleporter, and its _Fade kind that fades out first):
// a portal or pad, or one of the same classes the maps use to put the player back after a fall (a hazard
// with a damage decl, or out of bounds with the falling stinger for its fade sound). The hub's secret
// teleporter plays the falling stinger too; an entity named '...secret...' counts as a portal.
enum class TeleportKind : std::uint8_t { Portal, Hazard, OutOfBounds };

TeleportKind teleportKindOf(bool fade, bool damage, std::string_view fadeSound, std::string_view entityName);
const char* teleportKindName(TeleportKind kind);

// The portal's sweep: its length, and a step every kPortalStepSeconds.
inline constexpr double kPortalSeconds = 0.6;
inline constexpr double kPortalStepSeconds = 0.06;

// What the layer read this update. Levels, except `shots` (counted since the last update) and the hit.
struct BodySignals {
    double seconds = 0.0;  // a steady clock
    bool gameplay = false; // the player is in play: no menu, not loading, the reads below fresh
    float health = 0.0f;
    float armor = 0.0f;
    bool dead = false;
    bool sync = false;                       // a sync or glory kill runs
    SyncKind syncKind = SyncKind::GloryKill; // what the running sync is (with `sync`)
    std::uint32_t shots = 0;                 // shots since the last update (the Flame Belch's not among them)
    std::uint32_t belches = 0;   // shots while the Flame Belch's button was held, since the last update
    std::uint32_t equipment = 0; // presses of the equipment launcher's button since the last update
    WeaponClass weapon = WeaponClass::Medium;
    input::Hand weaponHand = input::Hand::Right;
    // The newest hit the game recorded: a number that changes with each one, and the yaw of its source in
    // degrees from where the player faces (0 ahead, 90 to the left, -90 to the right, 180 behind), when the
    // game gave a direction.
    std::uint32_t hitSerial = 0;
    std::optional<float> hitYawDegrees;
    // The largest landing seen since the last update (LandingDetector on the game's frames), if any.
    std::optional<Landing> landing;
    std::uint32_t portals = 0;  // portals, pads and level exits gone through since the last update
    std::uint32_t launches = 0; // jump pad and booster launches since the last update (LaunchFilter's)
    // The health and armor gains to feel that the layer's PickupDetector found since the last update.
    std::optional<Pickup> healthGain;
    std::optional<Pickup> armorGain;
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
    // Landings seen (felt or not) and the newest one, for the layer's log.
    [[nodiscard]] std::uint64_t landings() const { return landings_; }
    [[nodiscard]] std::optional<Landing> lastLanding() const { return lastLanding_; }

private:
    void add(std::vector<Frame>& out, Effect effect, Device device, int millis, std::vector<Dot> dots);
    void shot(std::vector<Frame>& out, const BodySignals& signals);
    // The top two rows of the wearer's left two columns, front and back: the left shoulder.
    void leftShoulder(std::vector<Frame>& out, Effect effect, float intensity, int millis);
    void damage(std::vector<Frame>& out, float amount, std::optional<float> yawDegrees);
    void heartbeat(std::vector<Frame>& out, const BodySignals& signals);
    void landing(std::vector<Frame>& out, const BodySignals& signals);
    // The crystal's wave (crystal.cpp).
    void crystal(std::vector<Frame>& out, double seconds);
    void portal(std::vector<Frame>& out, double seconds);
    // The launch's curve (launch.cpp).
    void launch(std::vector<Frame>& out, double seconds);
    // The pickups' waves; a row is left out while an effect in `out` or one still playing holds the vest.
    void pickups(std::vector<Frame>& out, const BodySignals& signals);
    // xorshift32: the patterns only have to look random.
    std::uint32_t random();
    // A level from `low` to `high`, at random.
    float randomLevel(float low, float high);
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
    double nextBelch_ = 0.0;
    double nextBeat_ = 0.0;
    bool dub_ = false; // the next beat is the second of the pair
    std::uint64_t landings_ = 0;
    double nextCrystal_ = 0.0;      // the wave's next step
    double crystalUntil_ = -1.0;    // no step from here on
    bool crystalSync_ = false;      // a crystal's or a token's sync ran at the last update, gameplay or not
    bool crystalPending_ = false;   // that sync started and its wave is not timed yet
    double crystalStart_ = 0.0;     // when that sync started
    double crystalDelay_ = 0.0;     // its wave's delay (crystalWaveDelay)
    double crystalWaveStart_ = 0.0; // and when its wave starts
    double portalStart_ = 0.0;
    double nextPortal_ = 0.0;   // the sweep's next step
    double portalUntil_ = -1.0; // no step from here on
    double launchStart_ = 0.0;
    std::size_t launchNextStep_ = 0; // the curve's next step to play
    double launchUntil_ = -1.0;      // no step from here on
    PickupWave healthWave_{kVestRows};
    PickupWave armorWave_{kVestRows};
    double vestHeldUntil_ = -1.0; // a stronger effect plays on the vest until then
    std::uint32_t random_ = 0x2545F491u;
    std::optional<Landing> lastLanding_;
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
