#pragma once

// The weapon wheel's pointer as the game reads it (docs/rig-findings/menus.md section 4).
//
// With the keyboard and mouse as the active device (the virtual gamepad off) the game's wheel
// (idSWFWidget_WeaponWheel) selects with the menu cursor: every frame it takes the cursor's offset from the
// wheel's centre, clamps the cursor to swf_wheel_mouse_clampRadius (200 GUI pixels) around the centre and,
// once the offset passes swf_wheel_mouse_deadZone (50 pixels) on either axis, highlights the segment in
// that direction. The cursor moves by relative mouse motion, 1:1 in GUI pixels, whether or not it is shown.
// Letting go of _changeWeapon picks the highlighted weapon.
//
// So while the wheel is held the pointer stick's direction becomes relative mouse motion that puts the
// cursor on the wheel's rim in that direction. The router keeps a model of the cursor's offset (with the
// game's clamp) and sends the difference to the new rim point when the direction turns, and a short push
// outward now and then while it is held, which the clamp absorbs and which corrects the model if the game
// disagreed with it. A stick back near the centre sends nothing: the highlight stays where it was, and the
// release that follows picks it.
//
// No motion is sent until the wheel has had time to open: until the game opens it (its own delay after the
// button, weaponWheel_HoldTimeForOpeningWheel, 180 ms by default) mouse motion still turns the view.
//
// Pure: no Windows calls. Screen axes: x right, y down (the stick's y is up).

#include "features/input/axis2.hpp"

#include <cstdint>

namespace evr::input {

struct WheelMouseSettings {
    // Seconds from the wheel button going down to the first motion.
    float openDelaySeconds = 0.25f;
    // The stick deflection that points at a segment; below it the highlight stays.
    float selectThreshold = 0.5f;
    // The rim, in GUI pixels from the wheel's centre (swf_wheel_mouse_clampRadius).
    float reachPixels = 200.0f;
    // A new motion when the direction turns by more than this.
    float turnDegrees = 3.0f;
    // While the direction holds, a push outward of this many pixels every repeat interval.
    float pinPixels = 40.0f;
    float pinSeconds = 0.1f;
};

// The eight directions of the log, counter-clockwise from the right as seen on the wheel.
enum class WheelDirection : std::uint8_t {
    Right,
    UpRight,
    Up,
    UpLeft,
    Left,
    DownLeft,
    Down,
    DownRight,
    None,
};

const char* wheelDirectionName(WheelDirection direction);

// The direction a stick points at, None below `threshold` or for a non-finite stick.
WheelDirection wheelDirection(Axis2 stick, float threshold);

struct WheelMouseOutput {
    bool move = false; // send (dx, dy) as relative mouse motion
    int dx = 0;
    int dy = 0;
    bool opened = false;                           // the first frame motion may be sent
    bool released = false;                         // the wheel button went up after it opened
    WheelDirection pointed = WheelDirection::None; // the direction this motion aims at
    bool directionChanged = false;                 // `pointed` differs from the last motion's
    std::uint32_t moves = 0;                       // motions sent while this wheel was held
};

class WheelMouse {
public:
    // Settings that are not finite or out of range fall back to the defaults as a whole.
    explicit WheelMouse(WheelMouseSettings settings = {});

    // Once per command build. `held`: the wheel button (_changeWeapon from the weapon_wheel action) is
    // held; `pointer`: the stick that points at the wheel (zero when none); `dtSeconds`: the time since the
    // last call (non-finite or negative counts as zero).
    WheelMouseOutput update(bool held, Axis2 pointer, float dtSeconds);

    [[nodiscard]] bool open() const { return open_; }
    // The model of the cursor's offset from the wheel's centre, in GUI pixels.
    [[nodiscard]] Axis2 offset() const { return offset_; }
    [[nodiscard]] const WheelMouseSettings& settings() const { return settings_; }

private:
    void reset();

    WheelMouseSettings settings_;
    bool held_ = false;
    bool open_ = false;
    float heldSeconds_ = 0.0f;
    float sincePush_ = 0.0f;
    bool aimed_ = false;
    Axis2 aim_;    // the unit direction of the last motion (screen axes)
    Axis2 offset_; // the cursor's modelled offset (screen axes)
    WheelDirection pointed_ = WheelDirection::None;
    std::uint32_t moves_ = 0;
};

} // namespace evr::input
