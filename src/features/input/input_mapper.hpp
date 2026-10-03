#pragma once

// Combines a binding profile with the input policies into one GameInput per frame.
//
// The mapper is stateful across frames (hysteresis, tap/hold timing, snap re-arm, gesture arming)
// but uses no clock of its own: given the same frames and deltas it produces the same outputs, so
// whole input sequences can be replayed in tests.

#include "features/input/analog_button.hpp"
#include "features/input/arm_gestures.hpp"
#include "features/input/binding_profile.hpp"
#include "features/input/capture_chord.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/game_input.hpp"
#include "features/input/hands_jump.hpp"
#include "features/input/locomotion_direction.hpp"
#include "features/input/menu_release_latch.hpp"
#include "features/input/punch_detector.hpp"
#include "features/input/stick_chord.hpp"
#include "features/input/stick_response.hpp"
#include "features/input/tap_hold.hpp"
#include "features/input/turn_policy.hpp"
#include "features/input/turn_stick_arbiter.hpp"
#include "features/posture/posture_detector.hpp"

#include <array>
#include <string>

namespace evr::input {

// Longest frame delta the mapper counts. A hitch (a loading stall, a dropped compositor frame) is not
// time the player spent holding a button or the stick: counting all of it would turn a quick tap
// into a hold (opening the weapon wheel instead of a quick switch) and make smooth turning jump by
// the whole stall at once. 0.1 s is several frames at any refresh rate a headset runs at.
inline constexpr float kMaxFrameSeconds = 0.1f;

// Tuning for every policy. Values from a settings file are checked when the mapper is built: each
// policy falls back to its defaults for values it cannot use (see the policies' constructors).
struct MapperSettings {
    AnalogThresholds trigger = kTriggerThresholds;
    AnalogThresholds grip = kGripThresholds;
    float buttonHoldSeconds = kDefaultHoldSeconds;
    // The Menu button's tap (pause) still counts when released up to this long after the press (a player's
    // own map can put the recenter on its hold, which completes only after the recenter time). At most
    // kMaxHoldSeconds.
    float menuTapSeconds = 1.0f;
    // Both sticks pressed and held is the recenter chord (stick_chord.hpp): Recenter is down while it is.
    bool stickChordRecenter = true;
    // The buttons the capture chord takes (capture_chord.hpp): both sticks too where the runtime keeps the
    // Menu button.
    CaptureButtons captureButtons = CaptureButtons::Menu;
    StickResponse move = kMoveStickResponse;
    LocomotionFrame locomotionFrame = LocomotionFrame::Head;
    TurnSettings turn;
    TurnStickSettings turnStick;
    HandsJumpSettings handsJump;
    PunchSettings punch;
    // The throw and the overhead swing (arm_gestures.hpp), both off by default.
    ThrowSettings throwGesture;
    SwingSettings swing;
};

// Per-frame facts the mapper needs from outside the input system.
struct MapperContext {
    posture::Posture posture = posture::Posture::Unknown;
    // Yaw of the game's view in the tracking space, in radians (horizontalYaw convention). Under
    // decoupled aim this is the weapon's aim yaw (R13 section 6.1).
    float viewYawRadians = 0.0f;
    // True while the off hand holds the weapon's fore-grip for two-handed aiming. Its grip bindings
    // are suppressed meanwhile, and until that grip is next released, which is how the off-hand grip
    // can mean "support" near the weapon and its bound action away from it (R06 section 4.1). Nothing
    // sets it yet: no support grip is detected.
    bool supportHandOnWeapon = false;
    // A menu holds the controllers' gameplay input back this frame (the caller drops the actions). The
    // mapper still reads every input, for the menu's own uses of it (the pause, the capture, a popup's
    // keys); one still held when the hold ends stays out of gameplay until it is let go
    // (menu_release_latch.hpp).
    bool menuHold = false;
};

class InputMapper {
public:
    explicit InputMapper(BindingProfile profile, MapperSettings settings = {});

    // `dtSeconds` is the time since the previous frame; non-finite or negative values count as zero
    // and values above kMaxFrameSeconds count as kMaxFrameSeconds.
    GameInput update(const InputFrame& frame, const MapperContext& context, float dtSeconds);

    [[nodiscard]] const BindingProfile& profile() const { return profile_; }
    // The settings in use, after invalid values were replaced by defaults.
    [[nodiscard]] const MapperSettings& settings() const { return settings_; }
    // An input held through the end of a menu's hold is still kept out of gameplay.
    [[nodiscard]] bool heldFromMenu() const { return menuRelease_.anyLatched(); }
    // The turn stick as the last update read it: centred while it is held from a menu, or with no turn stick.
    [[nodiscard]] Axis2 turnStick() const { return turnStickRead_; }
    // For the log: the weapon hand, the move stick's hand and what forward on it follows, as in
    // "left-handed, move stick left, moving where the left hand points".
    [[nodiscard]] std::string summary() const;

private:
    using PerButton = std::array<bool, kButtonInputCount>;

    struct HandButtons {
        AnalogButton trigger;
        AnalogButton grip;
        std::array<TapHoldDetector, kButtonInputCount> tapHold;
        // Set while this hand steadies the weapon, and kept until its grip has been released once,
        // so a grip squeezed for support never turns into its bound action when support ends.
        bool gripHeldForSupport = false;
    };

    struct ButtonLevels {
        PerButton down{};
        PerButton tap{};
        PerButton hold{};
    };

    static ButtonLevels sampleButtons(HandButtons& buttons, const HandState& hand, float dtSeconds);
    void addButtonActions(Hand hand,
                          const ButtonLevels& levels,
                          bool gripSuppressed,
                          bool triggerSuppressed,
                          game::GameActionSet& down) const;
    void addStickGestureActions(const TurnStickOutput& gestures, game::GameActionSet& down) const;
    void consumeHeld();

    BindingProfile profile_;
    MapperSettings settings_;
    std::array<HandButtons, 2> hands_;
    TurnStickArbiter turnStick_;
    TurnPolicy turn_;
    LocomotionDirection locomotion_;
    HandsJumpDetector handsJump_;
    ArmGestures armGestures_;
    PunchDetector punch_;
    CaptureChord captureChord_;
    StickChord stickChord_;
    game::GameActionSet previousDown_;
    MenuReleaseLatch menuRelease_;
    Axis2 turnStickRead_;
};

} // namespace evr::input
