#pragma once

// Book-keeping for key presses the layer delivers to the game from inside its process
// (vkcore/key_inject.hpp). Portable: no Windows types.
//
// An injected key event travels as a WM_INPUT whose raw input handle encodes the event; the layer's
// GetRawInputData answers it. The game also polls key state (GetAsyncKeyState, GetKeyState,
// GetKeyboardState) while it believes its window is active, and a key that raw input reported down but
// the polled state reports up is treated as released. So the layer's replacements of those calls
// report every injected key that is held as down as well.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace evr::key_injection {

struct KeyEvent {
    std::uint8_t virtualKey = 0;
    bool down = false;
};

// Handles of injected events: kHandleTag | down << 8 | virtual key. Real raw input handles are kernel
// handle values, far below this range.
inline constexpr std::uint64_t kHandleTagMask = 0xFFFFFFFFFFFF0000ull;
inline constexpr std::uint64_t kHandleTag = 0x00007E5700000000ull;

std::uint64_t encodeHandle(KeyEvent event);
// The event a handle carries, or nothing for a real raw input handle.
std::optional<KeyEvent> decodeHandle(std::uint64_t handle);

// Mouse events travel the same way (the game reads the mouse through raw input too): the handle is
// kHandleTag | kMouseHandleBit | slot, and the event itself waits in a small ring of slots until the
// game asks for it. Motion is relative (the game's own raw mouse path has no absolute positions);
// `buttonFlags` are raw input's RI_MOUSE_* bits and `wheel` is in WHEEL_DELTA units (120 per notch).
struct MouseEvent {
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    std::uint16_t buttonFlags = 0;
    std::int16_t wheel = 0;
};

inline constexpr std::uint64_t kMouseHandleBit = 0x200u;
inline constexpr std::size_t kMouseSlots = 256;

// A ring of pending mouse events. push() stores the event and returns the handle to post; take() answers
// the game's GetRawInputData for a handle. A slot is reused only after kMouseSlots more events, far more
// than the game leaves unread between two message pumps. Safe to use from any thread.
class MouseEventRing {
public:
    std::uint64_t push(const MouseEvent& event);
    // The event a handle carries, or nothing for a handle that is not a mouse event of this ring.
    [[nodiscard]] std::optional<MouseEvent> take(std::uint64_t handle) const;

private:
    struct Slot {
        std::atomic<std::uint64_t> packed{0};
    };
    std::array<Slot, kMouseSlots> slots_{};
    std::atomic<std::uint32_t> next_{0};
};

// Whether a handle carries an injected mouse event (the slot is not checked).
bool isMouseHandle(std::uint64_t handle);

// Which virtual keys the layer currently holds down. Safe to use from any thread.
class HeldKeys {
public:
    void set(std::uint8_t virtualKey, bool down);
    [[nodiscard]] bool isDown(std::uint8_t virtualKey) const;

    // GetAsyncKeyState / GetKeyState result with injected keys added: the high bit (down) is set for a
    // held key; other bits (toggle, pressed-since-last-call) are kept. `virtualKey` outside 0..255 is
    // passed through.
    [[nodiscard]] std::int16_t mergeKeyState(int virtualKey, std::int16_t polled) const;
    // GetKeyboardState's 256-byte array: the high bit is set for every held key.
    void mergeKeyboardState(std::uint8_t* keys256) const;

    // Clears every held key and returns the ones that were held, in key order. Each key is returned by
    // exactly one call, however many threads race, so each gets exactly one release.
    std::vector<std::uint8_t> releaseAll();

private:
    std::array<std::atomic<bool>, 256> down_{};
};

// Turning injection off for good (the multiplayer guard, vkcore/mp_guard.hpp): new key-downs are refused
// at once, the layer then posts one key-up for each key releaseAll() returns, and from then on only
// key-up events are delivered to the game. A key-up for a hold the layer made is cleanup that puts the
// game's key state back to the real keyboard's, not new input.
// Whether an injected event may reach the game while injection is `allowed` or not.
inline bool deliverable(KeyEvent event, bool allowed) {
    return allowed || !event.down;
}

} // namespace evr::key_injection
