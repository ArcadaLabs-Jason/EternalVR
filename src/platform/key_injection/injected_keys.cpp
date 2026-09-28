#include "platform/key_injection/injected_keys.hpp"

#include <cstddef>

namespace evr::key_injection {

std::uint64_t encodeHandle(KeyEvent event) {
    return kHandleTag | (event.down ? 0x100u : 0u) | event.virtualKey;
}

std::optional<KeyEvent> decodeHandle(std::uint64_t handle) {
    if ((handle & kHandleTagMask) != kHandleTag || (handle & 0xFE00u) != 0) {
        return std::nullopt;
    }
    return KeyEvent{static_cast<std::uint8_t>(handle & 0xFFu), (handle & 0x100u) != 0};
}

namespace {

// One slot holds the whole event in 64 bits so a reader never sees half of it: dx and dy clamped to
// 16 bits each, the button flags and the wheel.
std::uint64_t packMouse(const MouseEvent& e) {
    const auto clamp16 = [](std::int32_t v) {
        return static_cast<std::uint16_t>(
            static_cast<std::int16_t>(v < -32768 ? -32768 : (v > 32767 ? 32767 : v)));
    };
    return static_cast<std::uint64_t>(clamp16(e.dx)) | (static_cast<std::uint64_t>(clamp16(e.dy)) << 16) |
           (static_cast<std::uint64_t>(e.buttonFlags) << 32) |
           (static_cast<std::uint64_t>(static_cast<std::uint16_t>(e.wheel)) << 48);
}

MouseEvent unpackMouse(std::uint64_t v) {
    MouseEvent e;
    e.dx = static_cast<std::int16_t>(static_cast<std::uint16_t>(v & 0xFFFFu));
    e.dy = static_cast<std::int16_t>(static_cast<std::uint16_t>((v >> 16) & 0xFFFFu));
    e.buttonFlags = static_cast<std::uint16_t>((v >> 32) & 0xFFFFu);
    e.wheel = static_cast<std::int16_t>(static_cast<std::uint16_t>((v >> 48) & 0xFFFFu));
    return e;
}

} // namespace

bool isMouseHandle(std::uint64_t handle) {
    return (handle & kHandleTagMask) == kHandleTag && (handle & 0xFF00u) == kMouseHandleBit;
}

std::uint64_t MouseEventRing::push(const MouseEvent& event) {
    const std::uint32_t slot = next_.fetch_add(1, std::memory_order_relaxed) % kMouseSlots;
    slots_[slot].packed.store(packMouse(event), std::memory_order_release);
    return kHandleTag | kMouseHandleBit | slot;
}

std::optional<MouseEvent> MouseEventRing::take(std::uint64_t handle) const {
    if (!isMouseHandle(handle)) {
        return std::nullopt;
    }
    return unpackMouse(slots_[handle & 0xFFu].packed.load(std::memory_order_acquire));
}

void HeldKeys::set(std::uint8_t virtualKey, bool down) {
    down_[virtualKey].store(down, std::memory_order_release);
}

bool HeldKeys::isDown(std::uint8_t virtualKey) const {
    return down_[virtualKey].load(std::memory_order_acquire);
}

std::int16_t HeldKeys::mergeKeyState(int virtualKey, std::int16_t polled) const {
    if (virtualKey < 0 || virtualKey > 255 || !isDown(static_cast<std::uint8_t>(virtualKey))) {
        return polled;
    }
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(polled) | 0x8000u);
}

void HeldKeys::mergeKeyboardState(std::uint8_t* keys256) const {
    if (!keys256) {
        return;
    }
    for (int vk = 0; vk < 256; ++vk) {
        if (isDown(static_cast<std::uint8_t>(vk))) {
            keys256[vk] = static_cast<std::uint8_t>(keys256[vk] | 0x80u);
        }
    }
}

std::vector<std::uint8_t> HeldKeys::releaseAll() {
    std::vector<std::uint8_t> released;
    for (int vk = 0; vk < 256; ++vk) {
        if (down_[static_cast<std::size_t>(vk)].exchange(false, std::memory_order_acq_rel)) {
            released.push_back(static_cast<std::uint8_t>(vk));
        }
    }
    return released;
}

} // namespace evr::key_injection
