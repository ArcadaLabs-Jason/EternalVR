#pragma once

// What the game took of the desktop's mouse while its window was in front, and what the layer gives back
// when the window loses the foreground (vkcore/cursor_guard.cpp, GitHub issue 30). Portable: no Windows
// types.
//
// While VR runs the layer keeps the game believing its window is active (vkcore/keep_active.hpp): the game
// never sees its window deactivated, so it never lets go of what it takes while the window is in front. A
// click into the window brings it in front, the game clips the cursor to the window and registers the raw
// mouse with RIDEV_CAPTUREMOUSE (a click then activates no other window), and after an Alt+Tab both stayed
// until the game quit. The guard on the game's calls (vkcore/cursor_guard.cpp) keeps only new ones off the
// desktop.
//
// So when the window loses the foreground the layer undoes them: the cursor clip (only while the clip in
// force is still the game's: another program's own clip is left alone), the window's mouse capture, and the
// raw mouse capture (registered again without RIDEV_CAPTUREMOUSE). When the window is in front again what
// was undone goes back, unless the game took it again itself on the way: the clip moved with the window's
// client area, the raw mouse with the game's flags. A clip or a capture the guard kept off the desktop while
// the window was not in front goes back the same way.

#include <cstdint>
#include <mutex>
#include <optional>

namespace evr::key_injection {

// RIDEV_CAPTUREMOUSE.
inline constexpr std::uint32_t kRawCaptureMouse = 0x200;

struct CursorRect {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;

    friend bool operator==(const CursorRect&, const CursorRect&) = default;
};

struct CursorPoint {
    std::int32_t x = 0;
    std::int32_t y = 0;

    friend bool operator==(const CursorPoint&, const CursorPoint&) = default;
};

// What to undo as the window loses the foreground.
struct CursorRelease {
    bool unclip = false;                     // ClipCursor(nullptr)
    bool releaseCapture = false;             // ReleaseCapture()
    std::optional<std::uint32_t> mouseFlags; // register the raw mouse again with these flags

    [[nodiscard]] bool any() const { return unclip || releaseCapture || mouseFlags.has_value(); }
};

// What to put back once the window is in front again.
struct CursorRestore {
    std::optional<CursorRect> clip;          // ClipCursor(clip)
    std::optional<std::uint32_t> mouseFlags; // register the raw mouse again with these flags

    [[nodiscard]] bool any() const { return clip.has_value() || mouseFlags.has_value(); }
};

class CursorHold {
public:
    // The game's ClipCursor reached the desktop. `applied`: the clip in force after it (GetClipCursor), none
    // when the game let go; `origin`: the window's client area's top left on the screen then.
    void gameClipped(std::optional<CursorRect> applied, CursorPoint origin);
    // The game's ClipCursor was kept off the desktop (the window not in front): the clip it asked for.
    void clipKept(CursorRect requested, CursorPoint origin);
    // The game registered the raw mouse with `flags`; `captureKept`: RIDEV_CAPTUREMOUSE was kept off the
    // desktop (the window not in front), so the registration in force lacks it.
    void gameRegisteredMouse(std::uint32_t flags, bool captureKept);

    // The window lost the foreground. `currentClip`: the clip in force (GetClipCursor); `mouseFlags`: the raw
    // mouse's registration in force, if any; `hasCapture`: a window of the game's thread has the mouse
    // captured (GetCapture).
    CursorRelease lostForeground(std::optional<CursorRect> currentClip,
                                 std::optional<std::uint32_t> mouseFlags,
                                 bool hasCapture);
    // The window is in front again, after the game handled its activation; `origin` as above, now.
    CursorRestore regainedForeground(CursorPoint origin);

private:
    std::optional<CursorRect> clip_; // the game's clip (as it was applied, or asked for when kept)
    CursorPoint clipOrigin_;
    bool clipReleased_ = false;               // undone or kept off: goes back in front
    std::optional<std::uint32_t> mouseFlags_; // the flags the game asked for
    bool mouseReleased_ = false;              // its capture undone or kept off: goes back in front
};

// The calls a release or a restore makes (the layer's are Win32's, tests fake them). They may come back into
// the book on the same thread: ReleaseCapture sends WM_CAPTURECHANGED to the game's window procedure at once,
// and the game may call ClipCursor there, which the guard records.
class CursorCalls {
public:
    virtual ~CursorCalls() = default;
    virtual bool unclip() = 0;                           // ClipCursor(nullptr)
    virtual bool releaseCapture() = 0;                   // ReleaseCapture()
    virtual bool registerMouse(std::uint32_t flags) = 0; // the raw mouse again, with these flags
    virtual bool clip(const CursorRect& rect) = 0;       // ClipCursor(&rect)
    virtual std::optional<CursorRect> clipInForce() = 0; // GetClipCursor
};

// What a release or a restore did, for the log: per part, whether it was wanted, then whether it worked.
struct CursorCallsDone {
    bool unclip = false;
    bool unclipped = false;
    bool releaseCapture = false;
    bool uncaptured = false;
    bool mouse = false;
    bool registered = false;
    bool clip = false;
    bool clipped = false;

    [[nodiscard]] bool any() const { return unclip || releaseCapture || mouse || clip; }
};

// A CursorHold shared between threads: the game's calls (any of its threads) and the window's (de)activation
// (the window's thread). Each decision is taken under the book's lock, the calls are made without it, and the
// lock is taken again only to record what they did, so a call that comes back into the book never finds
// the lock held.
class CursorBook {
public:
    void gameClipped(std::optional<CursorRect> applied, CursorPoint origin);
    void clipKept(CursorRect requested, CursorPoint origin);
    void gameRegisteredMouse(std::uint32_t flags, bool captureKept);

    // CursorHold::lostForeground, then the calls it asks for.
    CursorCallsDone release(std::optional<CursorRect> currentClip,
                            std::optional<std::uint32_t> mouseFlags,
                            bool hasCapture,
                            CursorCalls& calls);
    // CursorHold::regainedForeground, then the calls; a clip put back is recorded as the game's.
    CursorCallsDone restore(CursorPoint origin, CursorCalls& calls);

private:
    std::mutex mutex_;
    CursorHold hold_;
};

} // namespace evr::key_injection
