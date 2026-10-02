#pragma once

// Health and armor pickups (a tester's idea, public issue #1): the player's health and armor, read once a
// game frame, going up. DOOM Eternal's health and armor do not come back on their own, so a rise is
// something picked up: a health or armor pickup, a glory kill's drops, the Flame Belch's armor shards.
//
// Rises close together are one gain (a pickup may move the value over more than one frame): a gain ends
// kPickupMergeSeconds after its last rise, or kPickupMaxMergeSeconds after its first. Not felt, only
// reported for the log:
// - rises in the first kPickupSettleSeconds of the readings (the detector starting, after a gap in the
//   readings, which is a load: loading screens run no game view) and after the player comes back from
//   death; health and armor are set then;
// - rises while dead, and rises from 0 or below (a respawn, an extra life).
// Gains under kMinPickup are float noise and not reported at all. A health gain of kMegaHealthGain or more
// is a Mega Health. Pure.
//
// PickupWave is the effect: a band of motors moving along the vest's rows, up for health, down for armor.

#include <cstdint>
#include <optional>
#include <vector>

namespace evr::bhaptics {

enum class PickupKind : std::uint8_t { Health, Armor };

const char* pickupKindName(PickupKind kind);

// Why a gain is not felt.
enum class PickupSkip : std::uint8_t {
    None,     // felt
    Settling, // just after the readings started, a load or a respawn
    Dead,     // while dead
    FromZero, // from 0 or below: a respawn or an extra life
};

const char* pickupSkipName(PickupSkip skip);

// Readings closer than this are the same pickup.
inline constexpr double kPickupMergeSeconds = 0.1;
// A gain ends this long after its first rise, even while the value keeps going up.
inline constexpr double kPickupMaxMergeSeconds = 0.4;
// Rises this long after the readings start (or after a respawn) are not pickups.
inline constexpr double kPickupSettleSeconds = 0.5;
// Readings further apart than this (a load, the game stalled) start over.
inline constexpr double kPickupReadingGapSeconds = 0.5;
// A step up smaller than this is float noise, and a gain smaller than kMinPickup is not reported.
inline constexpr float kPickupStepNoise = 0.01f;
inline constexpr float kMinPickup = 1.0f;
// A Mega Health gives 100 (less float rounding). Health above 100 alone is no sign of one: the Sentinel
// Crystals' upgrades raise the maximum.
inline constexpr float kMegaHealthGain = 99.5f;

struct Pickup {
    PickupKind kind = PickupKind::Health;
    float amount = 0.0f;      // the highest value reached less `from`
    float from = 0.0f;        // the value before the first rise
    float to = 0.0f;          // the value after the last
    int steps = 0;            // readings that rose
    double spanSeconds = 0.0; // from the first rise to the last
    bool mega = false;        // health only: amount of kMegaHealthGain or more
    PickupSkip skip = PickupSkip::None;
};

// One reading: `inPlay` is false in menus and when the player could not be read.
struct PickupReading {
    double seconds = 0.0; // a steady clock
    bool inPlay = false;
    float health = 0.0f;
    float armor = 0.0f;
    bool dead = false;
};

class PickupDetector {
public:
    // The gains that ended at this reading, at most one of each kind (a repeat of the same time changes
    // nothing).
    std::vector<Pickup> update(const PickupReading& reading);

    // Forgets everything; the next reading starts over.
    void reset();

private:
    struct Track {
        float value = 0.0f;
        bool pending = false;
        Pickup gain;
        double firstRise = 0.0;
        double lastRise = 0.0;
    };

    void step(Track& track,
              PickupKind kind,
              float value,
              double seconds,
              PickupSkip skip,
              std::vector<Pickup>& out);

    bool primed_ = false;
    double lastSeconds_ = 0.0;
    double settleUntil_ = 0.0;
    bool dead_ = false;
    Track health_;
    Track armor_;
};

// Adds a gain to one not yet taken (the layer's hand-over between threads).
void addPickup(std::optional<Pickup>& pending, const Pickup& pickup);

// The wave: one row of the vest after the other, each for kPickupRowSeconds (kMegaHealthSlower times that
// for a Mega Health), the row just left at kPickupTrailShare of the band. Its intensity grows with the
// amount, from kPickupLight plus kPickupPerPoint a point up to kPickupStrong; a Mega Health's is
// kMegaHealthIntensity.
inline constexpr double kPickupRowSeconds = 0.05;
inline constexpr double kMegaHealthSlower = 1.5;
inline constexpr float kPickupLight = 20.0f;
inline constexpr float kPickupPerPoint = 2.4f;
inline constexpr float kPickupStrong = 80.0f;
inline constexpr float kMegaHealthIntensity = 100.0f;
inline constexpr float kPickupTrailShare = 0.4f;
// Each row's frame lasts this much longer than its step, so the wave does not stutter.
inline constexpr int kPickupOverlapMillis = 20;

float pickupIntensity(const Pickup& pickup);

struct WaveStep {
    PickupKind kind = PickupKind::Health;
    bool mega = false;
    int row = 0;            // the band, row 0 at the top
    int trailRow = -1;      // the row it just left, or -1
    float intensity = 0.0f; // the band's, 0..100 before the strength
    int millis = 0;
};

class PickupWave {
public:
    explicit PickupWave(int rows);

    // Starts the wave for `pickup` from `seconds`, in place of one still running.
    void start(const Pickup& pickup, double seconds);
    // The step to play when the band reached a new row by `seconds` (a row passed between two calls is
    // left out); nullopt otherwise and once the wave is over.
    std::optional<WaveStep> step(double seconds);
    void stop() { active_ = false; }
    [[nodiscard]] bool active() const { return active_; }

private:
    int rows_;
    bool active_ = false;
    PickupKind kind_ = PickupKind::Health;
    bool mega_ = false;
    double start_ = 0.0;
    double rowSeconds_ = kPickupRowSeconds;
    float intensity_ = 0.0f;
    int done_ = -1; // the last step played, counted from the wave's first row
};

} // namespace evr::bhaptics
