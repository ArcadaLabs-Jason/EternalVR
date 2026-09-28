#include "platform/key_injection/injected_keys.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

using evr::key_injection::decodeHandle;
using evr::key_injection::encodeHandle;
using evr::key_injection::HeldKeys;
using evr::key_injection::KeyEvent;

TEST_CASE("an injected key event survives the raw input handle") {
    for (bool down : {false, true}) {
        const auto decoded = decodeHandle(encodeHandle({'R', down}));
        REQUIRE(decoded.has_value());
        CHECK(decoded->virtualKey == 'R');
        CHECK(decoded->down == down);
    }
    const auto escape = decodeHandle(encodeHandle({0x1B, true}));
    REQUIRE(escape.has_value());
    CHECK(escape->virtualKey == 0x1B);
}

TEST_CASE("real raw input handles are not taken for injected ones") {
    CHECK_FALSE(decodeHandle(0).has_value());
    CHECK_FALSE(decodeHandle(0x0000000000A10B2Full).has_value());
    CHECK_FALSE(decodeHandle(0xFFFFFFFF80001234ull).has_value());
    // The tag with bits outside the key and the down flag is not an event either.
    CHECK_FALSE(decodeHandle(evr::key_injection::kHandleTag | 0x0400u).has_value());
}

TEST_CASE("polled key state reports held injected keys as down") {
    HeldKeys keys;
    CHECK(keys.mergeKeyState('R', 0) == 0);
    keys.set('R', true);
    CHECK(keys.isDown('R'));
    CHECK((static_cast<std::uint16_t>(keys.mergeKeyState('R', 0)) & 0x8000u) != 0);
    // The toggle / pressed bit is kept.
    CHECK(static_cast<std::uint16_t>(keys.mergeKeyState('R', 1)) == 0x8001u);
    // Other keys, and codes outside a byte, pass through.
    CHECK(keys.mergeKeyState('E', 1) == 1);
    CHECK(keys.mergeKeyState(-1, 5) == 5);
    CHECK(keys.mergeKeyState(0x152, 7) == 7);
    keys.set('R', false);
    CHECK(keys.mergeKeyState('R', 0) == 0);
}

TEST_CASE("the keyboard state array gets the high bit for held injected keys") {
    HeldKeys keys;
    std::array<std::uint8_t, 256> state{};
    state['E'] = 0x80;
    state['R'] = 0x01; // toggled, up
    keys.set('R', true);
    keys.mergeKeyboardState(state.data());
    CHECK(state['R'] == 0x81);
    CHECK(state['E'] == 0x80);
    CHECK(state['Q'] == 0x00);
    keys.mergeKeyboardState(nullptr); // no crash
}

TEST_CASE("turning injection off releases each held key once, then delivers only releases") {
    HeldKeys held;
    held.set('R', true);
    held.set(0x10, true);
    held.set('W', true);
    held.set('W', false); // already released: not released again

    // While injection is allowed every event is delivered.
    CHECK(evr::key_injection::deliverable(KeyEvent{'R', true}, true));
    CHECK(evr::key_injection::deliverable(KeyEvent{'R', false}, true));

    // The guard trips: new key-downs are refused first...
    const bool allowed = false;
    CHECK_FALSE(evr::key_injection::deliverable(KeyEvent{'R', true}, allowed));
    // ...then every held key is handed out for exactly one release, and the held state is clear...
    const std::vector<std::uint8_t> released = held.releaseAll();
    CHECK(released == std::vector<std::uint8_t>{0x10, 'R'});
    CHECK_FALSE(held.isDown('R'));
    CHECK_FALSE(held.isDown(0x10));
    CHECK(held.mergeKeyState('R', 0) == 0);
    CHECK(held.releaseAll().empty());
    // ...and those releases still reach the game, while downs never do again.
    for (const std::uint8_t vk : released) {
        CHECK(evr::key_injection::deliverable(KeyEvent{vk, false}, allowed));
        CHECK_FALSE(evr::key_injection::deliverable(KeyEvent{vk, true}, allowed));
    }
}

TEST_CASE("each held key is released by exactly one of several racing calls") {
    HeldKeys held;
    for (int vk = 0; vk < 256; vk += 3) {
        held.set(static_cast<std::uint8_t>(vk), true);
    }
    std::vector<std::uint8_t> a;
    std::vector<std::uint8_t> b;
    std::thread first([&] { a = held.releaseAll(); });
    std::thread second([&] { b = held.releaseAll(); });
    first.join();
    second.join();
    std::vector<int> count(256, 0);
    for (const std::uint8_t vk : a) {
        ++count[vk];
    }
    for (const std::uint8_t vk : b) {
        ++count[vk];
    }
    for (int vk = 0; vk < 256; ++vk) {
        CHECK(count[static_cast<std::size_t>(vk)] == (vk % 3 == 0 ? 1 : 0));
    }
}

TEST_CASE("an injected mouse event survives its handle, and is not taken for a key") {
    evr::key_injection::MouseEventRing ring;
    evr::key_injection::MouseEvent e;
    e.dx = -250;
    e.dy = 1050;
    e.buttonFlags = 0x0001;
    e.wheel = -120;
    const std::uint64_t handle = ring.push(e);
    CHECK(evr::key_injection::isMouseHandle(handle));
    CHECK_FALSE(decodeHandle(handle).has_value());
    const auto back = ring.take(handle);
    REQUIRE(back.has_value());
    CHECK(back->dx == -250);
    CHECK(back->dy == 1050);
    CHECK(back->buttonFlags == 0x0001);
    CHECK(back->wheel == -120);
    // Motion beyond 16 bits is clamped, key handles and real handles are not mouse events.
    evr::key_injection::MouseEvent big;
    big.dx = 100000;
    big.dy = -100000;
    const auto clamped = ring.take(ring.push(big));
    REQUIRE(clamped.has_value());
    CHECK(clamped->dx == 32767);
    CHECK(clamped->dy == -32768);
    CHECK_FALSE(ring.take(encodeHandle({'R', true})).has_value());
    CHECK_FALSE(ring.take(0x0000000000A10B2Full).has_value());
    // Slots are reused round the ring.
    std::uint64_t first = 0;
    for (std::size_t i = 0; i < evr::key_injection::kMouseSlots + 1; ++i) {
        const std::uint64_t h = ring.push(e);
        if (i == 0) {
            first = h;
        } else if (i == evr::key_injection::kMouseSlots) {
            CHECK(h == first);
        }
    }
}
