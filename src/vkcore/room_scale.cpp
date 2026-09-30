// Room-scale v1, recenter, posture and eye height (room_scale.hpp, docs/VR_ROOMSCALE.md).

#include "vkcore/room_scale.hpp"

#include "features/input/tap_hold.hpp"
#include "game/eternal/player_dimensions.hpp"
#include "vkcore/log.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace evr::vkcore {

namespace {

constexpr float kDegrees = 57.29578f;
// The game's eye height above the player's origin, in game units (pm_normalViewHeight, R10 section 1.2).
constexpr float kGameEyeUnits = game::kSlayerEyeHeightMetres;
// A penetration older than this (no game views: menus, loading) no longer fades the view.
constexpr double kPenetrationStaleSeconds = 0.25;
// Past the lean cap the view starts to fade after this much more (a small overshoot stays visible) and is
// black at this plus the fade's full depth (0.10 m).
constexpr float kLeanFadeStartMetres = 0.05f;
// A re-anchor waits this long after it is asked for, so the blink (the fade rises in 0.10 s) has the view
// black before the jump, and the blink holds a little longer before it clears (0.25 s).
constexpr double kBlinkLeadSeconds = 0.12;
constexpr double kBlinkHoldSeconds = 0.05;
// After a posture change the lean-cap fade stays off this long: body follow walks the body to the head.
constexpr double kPostureGraceSeconds = 1.0;
// The input mapper stopped calling for this long (menus, a stall): the recenter binding counts as released.
constexpr double kBindingGapSeconds = 0.25;

std::optional<std::string> envLookup(std::string_view name) {
    const std::wstring wide(name.begin(), name.end());
    std::wstring value;
    if (!readEnv(wide.c_str(), value)) {
        return std::nullopt;
    }
    std::string out;
    out.reserve(value.size());
    for (const wchar_t c : value) {
        out.push_back(c > 0 && c < 0x80 ? static_cast<char>(c) : '?');
    }
    return out;
}

const roomscale::RoomScaleSettings& loadSettings() {
    static const roomscale::RoomScaleSettings settings = [] {
        const auto parsed = roomscale::parseRoomScaleSettings(&envLookup);
        for (const auto& issue : parsed.issues) {
            EVR_LOG("room: %s='%s': %s", issue.name.c_str(), issue.value.c_str(), issue.message.c_str());
        }
        const roomscale::RoomScaleSettings& s = parsed.settings;
        EVR_LOG("room: posture %s, height %s, auto anchor %s, recenter hold %.2f s, lean cap %.2f m, head "
                "collision %s, fade %s, IPD %s",
                roomscale::postureOverrideName(s.posture), roomscale::heightModeName(s.height),
                s.autoAnchor ? "on" : "off", s.recenterHoldSeconds, s.limits.leanCapMetres,
                s.collision ? "on" : "off", s.fade ? "on" : "off",
                s.ipdMetres > 0.0f ? (std::to_string(s.ipdMetres * 1000.0f) + " mm").c_str()
                                   : "the runtime's");
        if (s.follow.enabled) {
            EVR_LOG("room: body follow on: deadzone %.3f m, walk %d and creep %d of 127, coast %.2f s, up to "
                    "%.2f m/s",
                    s.follow.deadzoneMetres, s.follow.walkCommand, s.follow.creepCommand,
                    s.follow.coastSeconds, s.follow.maxSpeed);
        } else {
            EVR_LOG("room: body follow off (ETERNALVR_BODY_FOLLOW=0)");
        }
        if (s.testSteps) {
            std::string list;
            for (const float m : s.testSteps->metres) {
                list += (list.empty() ? "" : ", ") + std::to_string(m);
            }
            EVR_LOG("room: test steps %s m %s, each leg held %.1f s, from one hold after the first anchor%s",
                    list.c_str(), s.testSteps->sideways ? "right" : "forward", s.testSteps->holdSeconds,
                    s.follow.enabled ? "" : "; body follow is off, so the body will not follow them");
        }
        if (s.testOffset) {
            EVR_LOG("room: test head offset (%.2f %.2f %.2f) m, period %.1f s", s.testOffset->metres.x,
                    s.testOffset->metres.y, s.testOffset->metres.z, s.testOffset->periodSeconds);
        }
        return parsed.settings;
    }();
    return settings;
}

// The recenter binding's long press. The mapper's hold starts after the hold time, so the long press
// counts the rest of the configured duration.
std::mutex g_bindingMutex;
std::optional<roomscale::LongPress> g_bindingPress;
double g_bindingLast = -1.0;  // the last mapper call
double g_bindingStart = -1.0; // when the binding became active (the mapper's hold already counted)
bool g_bindingFired = false;
std::atomic<bool> g_userRecenter{false};
std::atomic<posture::Posture> g_posture{posture::Posture::Unknown};

std::string metresText(std::optional<float> metres) {
    if (!metres) {
        return "(no floor space)";
    }
    char text[32];
    std::snprintf(text, sizeof text, "%.2f m", static_cast<double>(*metres));
    return text;
}

std::optional<float> lifted(std::optional<float> metres, float lift) {
    if (metres) {
        *metres += lift;
    }
    return metres;
}

double nowSeconds() {
    LARGE_INTEGER t;
    LARGE_INTEGER f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return static_cast<double>(t.QuadPart) / static_cast<double>(f.QuadPart);
}

} // namespace

const roomscale::RoomScaleSettings& roomScaleSettings() {
    return loadSettings();
}

posture::Posture roomPosture() {
    const posture::PostureOverride forced = roomScaleSettings().posture;
    if (forced != posture::PostureOverride::Auto) {
        return posture::effectivePosture(forced, posture::Posture::Unknown);
    }
    return g_posture.load(std::memory_order_relaxed);
}

void noteRecenterBinding(bool active) {
    const float hold = roomScaleSettings().recenterHoldSeconds;
    if (hold <= 0.0f) {
        return;
    }
    std::lock_guard lock(g_bindingMutex);
    if (!g_bindingPress) {
        g_bindingPress.emplace(std::max(0.05f, hold - input::kDefaultHoldSeconds));
    }
    const double now = nowSeconds();
    if (g_bindingLast >= 0.0 && now - g_bindingLast > kBindingGapSeconds) {
        // The mapper was not running: whatever was held then does not count toward this press.
        g_bindingPress->update(false, now);
        g_bindingStart = -1.0;
        g_bindingFired = false;
    }
    g_bindingLast = now;
    if (active && g_bindingStart < 0.0) {
        g_bindingStart = now;
    }
    if (g_bindingPress->update(active, now)) {
        g_bindingFired = true;
        g_userRecenter.store(true, std::memory_order_release);
        EVR_LOG("room: recenter binding held for %.2f s", hold);
    }
    if (!active && g_bindingStart >= 0.0) {
        if (!g_bindingFired) {
            // A hold too short to recenter (the pause menu takes it): say how long it was.
            EVR_LOG("room: recenter binding released after %.2f s; hold it %.2f s to recenter",
                    now - g_bindingStart + input::kDefaultHoldSeconds, hold);
        }
        g_bindingStart = -1.0;
        g_bindingFired = false;
    }
}

void RoomScale::anchorOn(const Input& in, roomscale::RecenterKind kind, const char* why, float lift) {
    const roomscale::RoomScaleSettings& cfg = roomScaleSettings();
    const roomscale::RoomAnchor before = anchor_;
    Pose head = in.localHead;
    head.position.y += lift;
    const std::optional<float> above = lifted(in.headAboveFloor, lift);
    anchor_ = roomscale::recenter(anchor_, head, kind);
    if (kind == roomscale::RecenterKind::YawAndOrigin) {
        EVR_LOG("room: %s: heading %.1f -> %.1f deg, origin (%.3f %.3f) LOCAL, height kept at %.3f", why,
                before.yaw * kDegrees, anchor_.yaw * kDegrees, anchor_.origin.x, anchor_.origin.z,
                anchor_.origin.y);
        return;
    }
    anchorAboveFloor_ = above;
    clearance_.reset();
    const bool autoPosture = cfg.posture == posture::PostureOverride::Auto;
    posture::Posture effective = posture::effectivePosture(cfg.posture, postureTracker_.current());
    if (kind == roomscale::RecenterKind::Full) {
        // Every full anchor detects the posture again from the head's height now.
        postureDetector_.reset();
        effective = posture::effectivePosture(cfg.posture, postureDetector_.update(above));
        anchorTaken_ = true;
        if (firstAnchorSeconds_ < 0.0) {
            firstAnchorSeconds_ = in.seconds;
        }
    }
    g_posture.store(effective, std::memory_order_relaxed);
    postureTracker_.reset(autoPosture ? effective : posture::Posture::Unknown, above);
    const float eyeUnits =
        cfg.height == roomscale::HeightMode::Real && above ? *above * in.unitsPerMetre : kGameEyeUnits;
    EVR_LOG("room: %s: posture %s (%s), head %s above the floor; anchored at (%.3f %.3f %.3f) LOCAL (height "
            "was %.3f), heading %.1f deg (was %.1f); eye height %.3f unit(s) = %.3f m at world scale %.2f",
            why, roomscale::postureName(effective), autoPosture ? "detected" : "override",
            metresText(above).c_str(), anchor_.origin.x, anchor_.origin.y, anchor_.origin.z, before.origin.y,
            anchor_.yaw * kDegrees, before.yaw * kDegrees, eyeUnits, eyeUnits / in.unitsPerMetre,
            in.unitsPerMetre);
}

void RoomScale::scheduleReanchor(roomscale::RecenterKind kind, const char* why, double now) {
    if (reanchor_.active && reanchor_.kind == roomscale::RecenterKind::Full &&
        kind != roomscale::RecenterKind::Full) {
        return; // a full re-anchor already on its way covers the height
    }
    reanchor_.active = true;
    reanchor_.kind = kind;
    reanchor_.why = why;
    reanchor_.at = now + kBlinkLeadSeconds;
    blinkUntil_.store(reanchor_.at + kBlinkHoldSeconds, std::memory_order_release);
}

void RoomScale::trackPosture(const Input& in, float lift, double now) {
    if (roomScaleSettings().posture != posture::PostureOverride::Auto || !anchorTaken_ || !in.positionValid ||
        reanchor_.active) {
        return;
    }
    const std::optional<float> above = lifted(in.headAboveFloor, lift);
    if (const auto change = postureTracker_.update(above, in.seconds)) {
        EVR_LOG("room: posture change: %s -> %s (head %.2f m above the floor for %.1f s); re-anchoring the "
                "height",
                roomscale::postureName(change->from), roomscale::postureName(change->to),
                static_cast<double>(change->heightMetres), static_cast<double>(change->heldSeconds));
        scheduleReanchor(roomscale::RecenterKind::Height, "posture change", now);
    }
}

RoomScale::Head RoomScale::head(const Input& in) {
    const roomscale::RoomScaleSettings& cfg = roomScaleSettings();
    if (firstSeconds_ < 0.0) {
        firstSeconds_ = in.seconds;
    }
    const double now = nowSeconds();
    const float lift =
        cfg.testOffset ? roomscale::testOffsetAt(*cfg.testOffset, in.seconds - firstSeconds_).y : 0.0f;
    // The runtime's recenter: the anchor follows the move at once (nothing jumps), then re-anchors fully
    // (heading, position, height and posture) on the head a moment later, behind the blink.
    {
        std::lock_guard lock(mutex_);
        if (spaceChanged_) {
            spaceChanged_ = false;
            if (spaceChange_) {
                anchor_ = roomscale::afterSpaceChange(anchor_, *spaceChange_);
            }
            scheduleReanchor(roomscale::RecenterKind::Full, "runtime recenter", now);
        }
    }
    if (in.positionValid) {
        bool user = g_userRecenter.exchange(false, std::memory_order_acq_rel);
        if (!user && cfg.testRecenterSeconds > 0.0f && !testRecenterDone_ &&
            in.seconds - firstSeconds_ >= cfg.testRecenterSeconds) {
            testRecenterDone_ = true;
            user = true;
        }
        if (user) {
            scheduleReanchor(roomscale::RecenterKind::Full, "user recenter", now);
        } else if (cfg.autoAnchor && !anchorTaken_ && !reanchor_.active) {
            posture::HeadSample sample;
            sample.seconds = in.seconds;
            sample.pose = in.localHead;
            sample.tracked = true;
            sample.focused = in.focused;
            if (detector_.update(sample)) {
                anchorOn(in, roomscale::RecenterKind::Full, "first stable head pose", lift);
            }
        }
    }
    trackPosture(in, lift, now);
    if (reanchor_.active && in.positionValid && now >= reanchor_.at) {
        reanchor_.active = false;
        anchorOn(in, reanchor_.kind, reanchor_.why, lift);
        if (reanchor_.kind == roomscale::RecenterKind::Height) {
            postureGraceUntil_ = now + kPostureGraceSeconds;
        }
    }

    // The test offsets, then body follow: what the body moved toward the head shifts the anchor first.
    Vec3 testOffset = testStep(in.seconds);
    if (cfg.testOffset) {
        testOffset = testOffset + roomscale::testOffsetAt(*cfg.testOffset, in.seconds - firstSeconds_);
    }
    follow(in, testOffset);

    Head out;
    out.roomFromLocal = roomscale::roomFromTracking(anchor_);
    out.room = roomscale::toRoom(anchor_, in.localHead);
    if (!in.positionValid) {
        out.room.position = {}; // orientation-only tracking: the head stays on the game's eye
    }
    out.game = out.room;
    out.game.position = out.game.position + testOffset;
    roomscale::HeadOffsetInput offsetIn;
    offsetIn.roomHead = out.game.position;
    offsetIn.anchorAboveFloor = anchorAboveFloor_;
    offsetIn.height = cfg.height;
    offsetIn.gameEyeUnits = kGameEyeUnits;
    offsetIn.unitsPerMetre = in.unitsPerMetre;
    out.offset = roomscale::headOffset(offsetIn, cfg.limits);
    // While the game drives the view its eye is an animated one on the Slayer's body: the camera stays on
    // it rather than where the head was from the body when the game took over (the player saw the Slayer's
    // own shoulders in standing glory kills).
    const Vec3 full = out.offset.offset;
    out.offset.offset = driven_.update(full, in.forcedView || in.cutscene, in.seconds);
    if (driven_.began() && ++drivenEpisodes_ <= 20) {
        EVR_LOG("room: the game drives the view (%s); the head's offset (%.3f %.3f %.3f) m eases out, the "
                "camera stays on the game's eye (episode %llu)",
                in.cutscene ? "cutscene" : "forced view", full.x, full.y, full.z,
                static_cast<unsigned long long>(drivenEpisodes_));
    }
    // Standing up or sitting down is not a lean: while the head moves between postures (and for a moment
    // after the height re-anchor, while body follow walks the body to the head) the cap still holds the
    // camera, but the view does not fade for it. Geometry still fades.
    const bool postureMoving = postureTracker_.changing() || postureTracker_.pending() || reanchor_.active ||
                               now < postureGraceUntil_;
    leanExcess_.store(out.offset.leanClamped && !postureMoving ? out.offset.requestedLean - out.offset.lean
                                                               : 0.0f,
                      std::memory_order_relaxed);
    leanSeconds_.store(nowSeconds(), std::memory_order_release);

    maxLean_ = std::max(maxLean_, out.offset.requestedLean);
    if (out.offset.leanClamped && !leanClamped_) {
        ++leanClamps_;
        if (in.seconds - lastClampLog_ >= 1.0) {
            lastClampLog_ = in.seconds;
            EVR_LOG("room: lean %.3f m clamped to %.3f m (clamp %llu)", out.offset.requestedLean,
                    out.offset.lean, static_cast<unsigned long long>(leanClamps_));
        }
    }
    if (out.offset.leanClamped) {
        clampPeak_ = std::max(clampPeak_, out.offset.requestedLean);
        clampHeld_ = std::max(clampHeld_, out.offset.lean);
    } else if (leanClamped_ && leanClamps_ <= 200) {
        // The whole clamp: the furthest lean asked for and the offset it was held at.
        EVR_LOG("room: lean clamp %llu ended: peak lean %.3f m held at %.3f m",
                static_cast<unsigned long long>(leanClamps_), clampPeak_, clampHeld_);
        clampPeak_ = 0.0f;
        clampHeld_ = 0.0f;
    }
    leanClamped_ = out.offset.leanClamped;
    if (!loggedFirst_) {
        loggedFirst_ = true;
        EVR_LOG("room: first head in room space (%.3f %.3f %.3f), offset (%.3f %.3f %.3f) m%s",
                out.game.position.x, out.game.position.y, out.game.position.z, out.offset.offset.x,
                out.offset.offset.y, out.offset.offset.z,
                anchorTaken_ ? "" : "; not anchored yet (LOCAL as the runtime set it)");
    }
    logStats(in.seconds);
    return out;
}

roomscale::ClearanceStep
RoomScale::clearance(Vec3 desiredOffset, std::optional<float> hitFraction, float unitsPerMetre) {
    ++sweeps_;
    const roomscale::ClearanceStep step = clearance_.update(desiredOffset, hitFraction, unitsPerMetre);
    if (step.blocked && !blocked_) {
        ++blocks_;
        if (blocks_ <= 20) {
            EVR_LOG("room: head in geometry (%.3f m deep); shots start at the first contact (%.2f %.2f %.2f)",
                    step.penetrationMetres, step.validOffset.x, step.validOffset.y, step.validOffset.z);
        }
    } else if (!step.blocked && blocked_ && blocks_ <= 20) {
        EVR_LOG("room: head clear of geometry");
    }
    blocked_ = step.blocked;
    penetration_.store(step.penetrationMetres, std::memory_order_relaxed);
    penetrationSeconds_.store(nowSeconds(), std::memory_order_release);
    return step;
}

void RoomScale::onSpaceChange(std::optional<Pose> newInPrevious) {
    std::lock_guard lock(mutex_);
    spaceChanged_ = true;
    spaceChange_ = newInPrevious;
}

float RoomScale::fade(double seconds) {
    const double dt = lastFadeSeconds_ < 0.0 ? 0.0 : seconds - lastFadeSeconds_;
    lastFadeSeconds_ = seconds;
    const double at = penetrationSeconds_.load(std::memory_order_acquire);
    const float inGeometry =
        seconds - at > kPenetrationStaleSeconds ? 0.0f : penetration_.load(std::memory_order_relaxed);
    const double leanAt = leanSeconds_.load(std::memory_order_acquire);
    const float pastCap = seconds - leanAt > kPenetrationStaleSeconds
                              ? 0.0f
                              : leanExcess_.load(std::memory_order_relaxed) - kLeanFadeStartMetres;
    // The blink over a re-anchor, and a glory kill shown as a fade, fade out fully whatever the head does.
    const bool glory = seconds < holdBlackUntil_.load(std::memory_order_acquire);
    const bool blink = seconds < blinkUntil_.load(std::memory_order_acquire) || glory;
    const float depth = blink ? 1.0f : std::max(inGeometry, pastCap);
    const float before = fade_.value();
    const float value = fade_.update(depth, dt);
    if (before <= 0.0f && value > 0.0f) {
        contactSeconds_ = seconds - dt;
        deepSeconds_ = -1.0;
        loggedFull_ = false;
        fadeCause_ = glory                   ? "a glory kill started (shown as a fade)"
                     : blink                 ? ""
                     : inGeometry >= pastCap ? "the head entered geometry"
                                             : "the head went past the lean cap";
    }
    if (deepSeconds_ < 0.0 && contactSeconds_ >= 0.0 && roomscale::fadeTarget(depth, {}) >= 1.0f) {
        deepSeconds_ = seconds - dt; // the head reached full depth
    }
    if (value >= 1.0f && !loggedFull_ && contactSeconds_ >= 0.0) {
        loggedFull_ = true;
        // The first time depends on how fast the head moved in; the second is the fade's own delay. A blink
        // is not logged (the re-anchor it hides is); a glory kill's fade gives the first time only.
        if (glory) {
            EVR_LOG("room: fade full %.0f ms after %s", (seconds - contactSeconds_) * 1000.0, fadeCause_);
        } else if (*fadeCause_ != '\0') {
            EVR_LOG("room: fade full %.0f ms after %s, %.0f ms after it was 0.10 m deep",
                    (seconds - contactSeconds_) * 1000.0, fadeCause_,
                    deepSeconds_ >= 0.0 ? (seconds - deepSeconds_) * 1000.0 : 0.0);
        }
    }
    return value;
}

void RoomScale::logStats(double seconds) {
    if (seconds - lastStats_ < 10.0) {
        return;
    }
    lastStats_ = seconds;
    EVR_LOG("room: %s, posture %s; lean max %.3f m, %llu clamp(s); %llu sweep(s), %llu time(s) in geometry",
            anchorTaken_ ? "anchored" : "not anchored", roomscale::postureName(roomPosture()), maxLean_,
            static_cast<unsigned long long>(leanClamps_), static_cast<unsigned long long>(sweeps_),
            static_cast<unsigned long long>(blocks_));
    maxLean_ = 0.0f;
    logFollowStats();
}

} // namespace evr::vkcore
