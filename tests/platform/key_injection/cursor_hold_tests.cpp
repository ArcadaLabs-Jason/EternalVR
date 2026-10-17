#include "platform/key_injection/cursor_hold.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <ostream>

// GitHub issue 30: a click into the game's window while the layer keeps the game active trapped the cursor
// until the game quit. What the layer gives back when the window loses the foreground, and puts back in
// front.

using evr::key_injection::CursorHold;
using evr::key_injection::CursorPoint;
using evr::key_injection::CursorRect;
using evr::key_injection::CursorRelease;
using evr::key_injection::CursorRestore;
using evr::key_injection::kRawCaptureMouse;

namespace {

constexpr std::uint32_t kNoLegacy = 0x30; // RIDEV_NOLEGACY
constexpr std::uint32_t kCaptured = kNoLegacy | kRawCaptureMouse;
constexpr CursorRect kWindow{100, 50, 1100, 650}; // the client area on the screen
constexpr CursorPoint kOrigin{100, 50};
constexpr CursorRect kDesktop{0, 0, 2560, 1440}; // no clip: the whole desktop

// The game in front took the mouse: clipped the cursor to its window and captured the raw mouse.
CursorHold tookTheMouse() {
    CursorHold hold;
    hold.gameRegisteredMouse(kCaptured, false);
    hold.gameClipped(kWindow, kOrigin);
    return hold;
}

} // namespace

TEST_CASE("cursor hold: losing the foreground gives back the game's clip, capture and raw mouse capture") {
    CursorHold hold = tookTheMouse();
    const CursorRelease release = hold.lostForeground(kWindow, kCaptured, true);
    CHECK(release.unclip);
    CHECK(release.releaseCapture);
    REQUIRE(release.mouseFlags.has_value());
    CHECK(*release.mouseFlags == kNoLegacy);
    CHECK(release.any());
    // A second deactivation message for the same change (WM_ACTIVATE after WM_ACTIVATEAPP) finds nothing.
    const CursorRelease again = hold.lostForeground(kDesktop, kNoLegacy, false);
    CHECK_FALSE(again.any());
}

TEST_CASE("cursor hold: another program's clip in force is left alone") {
    CursorHold hold = tookTheMouse();
    const CursorRect theirs{0, 0, 800, 600};
    const CursorRelease release = hold.lostForeground(theirs, kNoLegacy, false);
    CHECK_FALSE(release.unclip);
    CHECK_FALSE(release.any());
}

TEST_CASE("cursor hold: a clip the game let go of itself is not touched, nor a mouse without capture") {
    CursorHold hold;
    hold.gameClipped(kWindow, kOrigin);
    hold.gameClipped(std::nullopt, kOrigin);
    hold.gameRegisteredMouse(kNoLegacy, false);
    CHECK_FALSE(hold.lostForeground(kDesktop, kNoLegacy, false).any());
    CHECK_FALSE(hold.regainedForeground(kOrigin).any());
}

TEST_CASE("cursor hold: in front again the clip comes back where the window is now, and the raw capture") {
    CursorHold hold = tookTheMouse();
    hold.lostForeground(kWindow, kCaptured, false);
    const CursorRestore restore = hold.regainedForeground({300, 150}); // the window moved by (200, 100)
    REQUIRE(restore.clip.has_value());
    CHECK(*restore.clip == CursorRect{300, 150, 1300, 750});
    REQUIRE(restore.mouseFlags.has_value());
    CHECK(*restore.mouseFlags == kCaptured);
    // Once only.
    CHECK_FALSE(hold.regainedForeground({300, 150}).any());
    // And it is the game's again: the next loss gives it back again.
    CHECK(hold.lostForeground(CursorRect{300, 150, 1300, 750}, kCaptured, false).unclip);
}

TEST_CASE("cursor hold: what the game takes again itself on activation is not put back twice") {
    CursorHold hold = tookTheMouse();
    hold.lostForeground(kWindow, kCaptured, false);
    // The game's own activation handler clips and registers again before the layer looks.
    hold.gameClipped(kWindow, kOrigin);
    hold.gameRegisteredMouse(kCaptured, false);
    CHECK_FALSE(hold.regainedForeground(kOrigin).any());
}

TEST_CASE(
    "cursor hold: a clip and a raw capture kept off the desktop while not in front come back in front") {
    CursorHold hold;
    hold.clipKept(kWindow, kOrigin);
    hold.gameRegisteredMouse(kCaptured, true);
    // Nothing of the game's is in force while not in front.
    CHECK_FALSE(hold.lostForeground(kDesktop, kNoLegacy, false).any());
    const CursorRestore restore = hold.regainedForeground(kOrigin);
    REQUIRE(restore.clip.has_value());
    CHECK(*restore.clip == kWindow);
    REQUIRE(restore.mouseFlags.has_value());
    CHECK(*restore.mouseFlags == kCaptured);
}

TEST_CASE("cursor hold: a raw capture registered before the guard is still given back and put back") {
    CursorHold hold; // the book never saw the registration
    const CursorRelease release = hold.lostForeground(kDesktop, kCaptured, false);
    REQUIRE(release.mouseFlags.has_value());
    CHECK(*release.mouseFlags == kNoLegacy);
    const CursorRestore restore = hold.regainedForeground(kOrigin);
    REQUIRE(restore.mouseFlags.has_value());
    CHECK(*restore.mouseFlags == kCaptured);
    CHECK_FALSE(restore.clip.has_value());
}

TEST_CASE("cursor hold: the window's mouse capture is released whenever it has one") {
    CursorHold hold;
    const CursorRelease release = hold.lostForeground(kDesktop, std::nullopt, true);
    CHECK(release.releaseCapture);
    CHECK_FALSE(release.unclip);
    CHECK_FALSE(release.mouseFlags.has_value());
}

namespace {

// Calls that come back into the book on the same thread, as the game's window procedure would:
// ReleaseCapture's WM_CAPTURECHANGED makes the game clip the cursor again (the guard records it).
class ReenteringCalls final : public evr::key_injection::CursorCalls {
public:
    explicit ReenteringCalls(evr::key_injection::CursorBook& book) : book_(book) {}
    bool unclip() override {
        ++unclips;
        return true;
    }
    bool releaseCapture() override {
        ++captureReleases;
        book_.gameClipped(kWindow, kOrigin); // re-enters: the book's lock must not be held here
        return true;
    }
    bool registerMouse(std::uint32_t flags) override {
        registered = flags; // the layer calls the real RegisterRawInputDevices: the guard does not see it
        return true;
    }
    bool clip(const CursorRect& rect) override {
        clipped = rect;
        book_.gameClipped(rect, kOrigin);
        return true;
    }
    std::optional<CursorRect> clipInForce() override { return clipped; }

    int unclips = 0;
    int captureReleases = 0;
    std::optional<std::uint32_t> registered;
    std::optional<CursorRect> clipped;

private:
    evr::key_injection::CursorBook& book_;
};

} // namespace

TEST_CASE(
    "cursor book: the calls run with the book unlocked, so a call that comes back into it goes through") {
    evr::key_injection::CursorBook book;
    book.gameRegisteredMouse(kCaptured, false);
    book.gameClipped(kWindow, kOrigin);
    ReenteringCalls calls(book);
    const evr::key_injection::CursorCallsDone released = book.release(kWindow, kCaptured, true, calls);
    CHECK(released.unclip);
    CHECK(released.unclipped);
    CHECK(released.uncaptured);
    CHECK(released.registered);
    CHECK(calls.unclips == 1);
    CHECK(calls.captureReleases == 1);
    REQUIRE(calls.registered.has_value());
    CHECK(*calls.registered == kNoLegacy);
    // The game clipped again from inside the release: that clip is the game's, and the next loss gives it
    // back.
    ReenteringCalls again(book);
    CHECK(book.release(kWindow, kNoLegacy, false, again).unclipped);
    // In front, the clip is put back through a call that also comes back into the book.
    ReenteringCalls front(book);
    const evr::key_injection::CursorCallsDone restored = book.restore(kOrigin, front);
    CHECK(restored.clipped);
    REQUIRE(front.clipped.has_value());
    CHECK(*front.clipped == kWindow);
    CHECK(restored.registered);
    REQUIRE(front.registered.has_value());
    CHECK(*front.registered == kCaptured);
}
