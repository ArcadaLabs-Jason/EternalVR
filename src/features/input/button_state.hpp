#pragma once

// A digital button's level plus the edges that produced it this frame.

namespace evr::input {

struct ButtonState {
    bool down = false;
    bool pressed = false;  // Went down this frame.
    bool released = false; // Went up this frame.
};

constexpr ButtonState nextButtonState(bool wasDown, bool isDown) {
    return {isDown, isDown && !wasDown, wasDown && !isDown};
}

} // namespace evr::input
