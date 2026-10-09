#pragma once

// Controller vibration (docs/VR_CONTROLLERS.md): what the layer knows each frame turned into short pulses
// per hand, for xrApplyHapticFeedback.
//
// Sources and what each one gives:
// - fire: the fire action going down pulses the weapon hand, and while it stays down a lighter pulse
//   repeats every kFireRepeatSeconds;
// - punch: a physical punch (punch_detector.hpp) pulses the hand that punched;
// - menu: a light tick on the pointing hand when its ray comes onto the menu panel, and on a click;
// - capture: the capture chord (capture_chord.hpp) pulses both hands to confirm the capture;
// - wheel: the thumb-rest wheel (rest_wheel.hpp) ticks the picking stick's hand lightly when picking starts,
//   and more firmly when a weapon is picked;
// - game: the game's own rumble as it mixes it each frame, two motors: the low-frequency one on both hands,
//   the high-frequency one on the weapon hand only (it is the sharp, weapon-like one). Played as pulses of
//   kRumbleHoldSeconds renewed while the level lasts, so it ends by itself if the frames stop coming, and
//   stopped when the level drops to nothing.
//
// Each hand gets at most one command a frame, the strongest pulse asked for. A pulse does not cut short a
// stronger one from another source still playing on that hand; a source always replaces its own. Menu
// ticks on one hand are at least kMenuGapSeconds apart. Amplitudes are scaled by the strength
// (ETERNALVR_HAPTICS); at strength 0 nothing is ever asked for. Pure: the XR worker feeds it once a frame
// and sends what it returns.

#include "features/input/controller_state.hpp"
#include "features/input/rest_wheel_output.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::input {

// ETERNALVR_HAPTICS: 0 off, 1 full strength.
inline constexpr float kDefaultHapticStrength = 0.6f;

inline constexpr double kFireRepeatSeconds = 0.15;
inline constexpr double kMenuGapSeconds = 0.06;
inline constexpr float kRumbleHoldSeconds = 0.1f;

enum class HapticSource : std::uint8_t {
    Fire,
    Punch,
    Menu,
    Game,
    Capture,
    Wheel,
    Count,
};

inline constexpr std::size_t kHapticSourceCount = static_cast<std::size_t>(HapticSource::Count);

const char* hapticSourceName(HapticSource source);

enum class MenuTick : std::uint8_t {
    None,
    Enter, // the pointing hand's ray came onto the panel
    Click,
};

// The game's rumble motors this frame: 0..1 each, 0 when it has none.
struct GameRumble {
    float low = 0.0f;
    float high = 0.0f;
};

struct HapticsFrame {
    double seconds = 0.0; // a steady clock
    Hand weaponHand = Hand::Right;
    bool fireHeld = false;            // the fire action is down (and sent to the game)
    std::array<bool, 2> punch{};      // per hand (indexed by Hand): a punch this frame
    bool capture = false;             // the capture chord fired since the last frame
    std::array<MenuTick, 2> menu{};   // per hand
    std::array<WheelTick, 2> wheel{}; // per hand
    GameRumble rumble;
};

enum class HapticCommandKind : std::uint8_t {
    None,
    Pulse,
    Stop, // stop what plays on the hand (the game's rumble ended)
};

struct HapticCommand {
    HapticCommandKind kind = HapticCommandKind::None;
    HapticSource source = HapticSource::Fire;
    float amplitude = 0.0f; // 0..1, the strength applied
    float seconds = 0.0f;
};

class HapticsPolicy {
public:
    // A strength that is not finite or not in [0, 1] falls back to kDefaultHapticStrength.
    explicit HapticsPolicy(float strength = kDefaultHapticStrength);

    // One command per hand (indexed by Hand).
    std::array<HapticCommand, 2> update(const HapticsFrame& frame);

    // Forgets what plays and the fire action's state (the session lost focus, or the controllers went).
    void reset();

    [[nodiscard]] float strength() const { return strength_; }

private:
    struct Playing {
        HapticSource source = HapticSource::Fire;
        float amplitude = 0.0f;
        double until = 0.0;
    };
    struct HandHaptics {
        std::optional<Playing> playing;
        double lastMenu = -1.0e9;
    };

    void offer(std::optional<HapticCommand>& best, HapticSource source, float amplitude, float seconds) const;
    // The game's rumble on one hand at `level` (0..1, before the strength); true when it has ended there.
    bool
    offerRumble(std::optional<HapticCommand>& best, const HandHaptics& hand, float level, double now) const;
    static HapticCommand
    decide(HandHaptics& hand, std::optional<HapticCommand> best, bool rumbleEnded, double now);

    float strength_;
    std::array<HandHaptics, 2> hands_;
    bool fireWasHeld_ = false;
    double nextFire_ = 0.0;
};

} // namespace evr::input
