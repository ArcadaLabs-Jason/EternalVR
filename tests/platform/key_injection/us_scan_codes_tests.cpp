#include "platform/key_injection/us_scan_codes.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <set>

using evr::key_injection::ScanCode;
using evr::key_injection::usScanCode;

namespace {

// The game's key number for a raw keyboard record: the scan code, plus 0x80 for an E0 key (DOOM Eternal
// 0x1DC1110).
int gameKey(std::uint8_t virtualKey) {
    const auto scan = usScanCode(virtualKey);
    REQUIRE(scan.has_value());
    return scan->make | (scan->e0 ? 0x80 : 0);
}

} // namespace

TEST_CASE("us scan codes: the keys the layer sends are the game's own key numbers") {
    // The game's key-name table (.rdata 0x38A54E0) and the automap's W/A/S/D/C (0xA54760).
    CHECK(gameKey('W') == 0x11);
    CHECK(gameKey('A') == 0x1E);
    CHECK(gameKey('S') == 0x1F);
    CHECK(gameKey('D') == 0x20);
    CHECK(gameKey('C') == 0x2E);
    CHECK(gameKey('E') == 0x12);
    CHECK(gameKey('R') == 0x13);
    CHECK(gameKey('Q') == 0x10);
    CHECK(gameKey('Z') == 0x2C);
    CHECK(gameKey(0x1B) == 0x01); // Escape
    CHECK(gameKey(0x0D) == 0x1C); // Enter
    CHECK(gameKey(0x20) == 0x39); // Space
    CHECK(gameKey(0x26) == 0xC8); // up
    CHECK(gameKey(0x25) == 0xCB); // left
    CHECK(gameKey(0x27) == 0xCD); // right
    CHECK(gameKey(0x28) == 0xD0); // down
}

TEST_CASE("us scan codes: every letter, digit and function key has its own code") {
    std::set<int> seen;
    for (int vk = 'A'; vk <= 'Z'; ++vk) {
        CHECK(seen.insert(gameKey(static_cast<std::uint8_t>(vk))).second);
    }
    for (int vk = '0'; vk <= '9'; ++vk) {
        CHECK(seen.insert(gameKey(static_cast<std::uint8_t>(vk))).second);
    }
    for (int vk = 0x70; vk <= 0x7B; ++vk) { // F1 to F12
        CHECK(seen.insert(gameKey(static_cast<std::uint8_t>(vk))).second);
    }
    CHECK(gameKey('1') == 0x02);
    CHECK(gameKey('0') == 0x0B);
    CHECK(gameKey(0x70) == 0x3B); // F1
    CHECK(gameKey(0x79) == 0x44); // F10
    CHECK(gameKey(0x7B) == 0x58); // F12
}

TEST_CASE("us scan codes: keys placed by the layout have none") {
    CHECK_FALSE(usScanCode(0xBA).has_value()); // OEM 1 (; on US)
    CHECK_FALSE(usScanCode(0xC0).has_value()); // OEM 3 (` on US)
    CHECK_FALSE(usScanCode(0x00).has_value());
    CHECK(usScanCode(0xA3) == ScanCode{0x1D, true}); // right Control is an E0 key
}
