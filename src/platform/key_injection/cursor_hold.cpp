#include "platform/key_injection/cursor_hold.hpp"

namespace evr::key_injection {

void CursorHold::gameClipped(std::optional<CursorRect> applied, CursorPoint origin) {
    clip_ = applied;
    clipOrigin_ = origin;
    clipReleased_ = false;
}

void CursorHold::clipKept(CursorRect requested, CursorPoint origin) {
    clip_ = requested;
    clipOrigin_ = origin;
    clipReleased_ = true;
}

void CursorHold::gameRegisteredMouse(std::uint32_t flags, bool captureKept) {
    mouseFlags_ = flags;
    mouseReleased_ = captureKept && (flags & kRawCaptureMouse) != 0;
}

CursorRelease CursorHold::lostForeground(std::optional<CursorRect> currentClip,
                                         std::optional<std::uint32_t> mouseFlags,
                                         bool hasCapture) {
    CursorRelease release;
    // Only the game's own clip, still in force: a program now in front may have clipped the cursor itself.
    if (clip_ && !clipReleased_ && currentClip && *currentClip == *clip_) {
        release.unclip = true;
        clipReleased_ = true;
    }
    release.releaseCapture = hasCapture;
    if (mouseFlags && (*mouseFlags & kRawCaptureMouse) != 0) {
        release.mouseFlags = *mouseFlags & ~kRawCaptureMouse;
        if (!mouseFlags_ || (*mouseFlags_ & kRawCaptureMouse) == 0) {
            mouseFlags_ = *mouseFlags; // registered before the guard was installed
        }
        mouseReleased_ = true;
    }
    return release;
}

CursorRestore CursorHold::regainedForeground(CursorPoint origin) {
    CursorRestore restore;
    if (clipReleased_ && clip_) {
        const std::int32_t dx = origin.x - clipOrigin_.x;
        const std::int32_t dy = origin.y - clipOrigin_.y;
        restore.clip = CursorRect{clip_->left + dx, clip_->top + dy, clip_->right + dx, clip_->bottom + dy};
        clip_ = restore.clip;
        clipOrigin_ = origin;
        clipReleased_ = false;
    }
    if (mouseReleased_ && mouseFlags_) {
        restore.mouseFlags = *mouseFlags_;
        mouseReleased_ = false;
    }
    return restore;
}

void CursorBook::gameClipped(std::optional<CursorRect> applied, CursorPoint origin) {
    std::lock_guard lock(mutex_);
    hold_.gameClipped(applied, origin);
}

void CursorBook::clipKept(CursorRect requested, CursorPoint origin) {
    std::lock_guard lock(mutex_);
    hold_.clipKept(requested, origin);
}

void CursorBook::gameRegisteredMouse(std::uint32_t flags, bool captureKept) {
    std::lock_guard lock(mutex_);
    hold_.gameRegisteredMouse(flags, captureKept);
}

CursorCallsDone CursorBook::release(std::optional<CursorRect> currentClip,
                                    std::optional<std::uint32_t> mouseFlags,
                                    bool hasCapture,
                                    CursorCalls& calls) {
    CursorRelease release;
    {
        std::lock_guard lock(mutex_);
        release = hold_.lostForeground(currentClip, mouseFlags, hasCapture);
    }
    CursorCallsDone done;
    done.unclip = release.unclip;
    done.unclipped = release.unclip && calls.unclip();
    done.releaseCapture = release.releaseCapture;
    done.uncaptured = release.releaseCapture && calls.releaseCapture();
    done.mouse = release.mouseFlags.has_value();
    done.registered = release.mouseFlags && calls.registerMouse(*release.mouseFlags);
    return done;
}

CursorCallsDone CursorBook::restore(CursorPoint origin, CursorCalls& calls) {
    CursorRestore restore;
    {
        std::lock_guard lock(mutex_);
        restore = hold_.regainedForeground(origin);
    }
    CursorCallsDone done;
    if (restore.clip) {
        done.clip = true;
        done.clipped = calls.clip(*restore.clip);
        if (done.clipped) {
            const std::optional<CursorRect> applied = calls.clipInForce();
            std::lock_guard lock(mutex_);
            hold_.gameClipped(applied, origin);
        }
    }
    done.mouse = restore.mouseFlags.has_value();
    done.registered = restore.mouseFlags && calls.registerMouse(*restore.mouseFlags);
    return done;
}

} // namespace evr::key_injection
