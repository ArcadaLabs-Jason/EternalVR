#pragma once

// Room-scale v1, recenter, posture and eye height in the game view (docs/VR_ROOMSCALE.md, T-029, T-045,
// T-062, T-063).
//
// The camera hook feeds the head pose (LOCAL) every game frame and gets back the head in room space (the
// runtime's LOCAL under our own recenter transform) and the head's offset from the game's eye, with the
// lean capped. After it has swept the head against the world it reports the result, which gives the
// penetration depth the XR worker fades by and the first-contact offset shots start from. With body follow
// on (body_follow.hpp) it also gets the player's origin: what the body moved toward the head shifts the
// anchor before the head is placed, and the move for the next user command is published.
//
// Threads: head() and clearance() run on the camera hook (the game-frame thread) and own the anchor;
// onSpaceChange() and fade() run on the XR worker; noteRecenterBinding() runs wherever the input mapper
// runs. Cross-thread state is atomic or under `mutex_`.

#include "common/pose.hpp"
#include "features/posture/anchor_detector.hpp"
#include "features/posture/posture_detector.hpp"
#include "features/posture/posture_tracker.hpp"
#include "features/roomscale/body_follow.hpp"
#include "features/roomscale/driven_offset.hpp"
#include "features/roomscale/follow_test_steps.hpp"
#include "features/roomscale/head_fade.hpp"
#include "features/roomscale/head_offset.hpp"
#include "features/roomscale/long_press.hpp"
#include "features/roomscale/room_anchor.hpp"
#include "features/roomscale/roomscale_settings.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>

namespace evr::vkcore {

// The settings, read from the environment once (any thread) and logged with any issue.
const roomscale::RoomScaleSettings& roomScaleSettings();

// Input mapper (any thread): whether the recenter binding is active this mapper frame. Held for the
// configured time it asks for a user recenter.
void noteRecenterBinding(bool active);

// Any thread: the posture in force (the override, else the one detected at the last anchor or noticed
// since, when the player stood up or sat down; Unknown before the first anchor without an override).
posture::Posture roomPosture();

class RoomScale {
public:
    struct Input {
        Pose localHead; // the head in LOCAL (after any test sway)
        bool positionValid = false;
        double seconds = 0.0;                // monotonic
        bool focused = false;                // the session is focused
        std::optional<float> headAboveFloor; // from LOCAL_FLOOR or STAGE, when the runtime has one
        float unitsPerMetre = 1.0f;
        // Body follow: the player's origin (world, game units; nullopt: unknown or follow off) and what
        // stops it this frame.
        std::optional<Vec3> bodyOrigin;
        bool menu = false;
        // The game drives the view (with or without body follow): the head's offset eases out meanwhile
        // (driven_offset.hpp).
        bool cutscene = false;
        bool forcedView = false;
    };

    struct Head {
        Pose room;          // the head in room space (the controllers are located in the same space)
        Pose game;          // `room` with the test offset: what the game view is built from
        Pose roomFromLocal; // the recenter transform
        roomscale::HeadOffset offset; // the head's offset from the game's eye, room axes, metres
    };

    // Camera hook.
    Head head(const Input& in);
    // Camera hook, after the sweep: `desiredOffset` is the rendered head's offset from the game's eye in game
    // units, `hitFraction` the sweep's first contact along it (nullopt: clear or no sweep).
    roomscale::ClearanceStep
    clearance(Vec3 desiredOffset, std::optional<float> hitFraction, float unitsPerMetre);
    // Camera hook, once the body frame is known: its id Tech forward and left rows. The next frame turns
    // the origin's displacement into room axes with it.
    void noteBody(Vec3 forward, Vec3 left);

    // XR worker: the runtime moved LOCAL (ReferenceSpaceChangePending); `newInPrevious` when it said how.
    void onSpaceChange(std::optional<Pose> newInPrevious);
    // XR worker, once per XR frame: the fade to show now (0 clear, 1 black).
    float fade(double seconds);
    // Camera hook: the view fades to black and stays black until `untilSeconds` (qpcSeconds clock), with the
    // blink's timing; refreshed each game frame while a glory kill is shown as a fade (glory_view.hpp).
    void holdBlack(double untilSeconds) { holdBlackUntil_.store(untilSeconds, std::memory_order_release); }

private:
    // `lift`: the test head offset's height (ETERNALVR_TEST_HEAD_OFFSET), added to the head's height so a
    // scripted stand-up is anchored and detected like a real one; 0 outside tests.
    void anchorOn(const Input& in, roomscale::RecenterKind kind, const char* why, float lift);
    // A re-anchor a moment from now, with the view faded out over the jump (the blink).
    void scheduleReanchor(roomscale::RecenterKind kind, const char* why, double now);
    void trackPosture(const Input& in, float lift, double now);
    Vec3 testStep(double seconds);
    void follow(const Input& in, Vec3 testOffset);
    void logStats(double seconds);
    void logFollowStats();

    // Camera hook only.
    roomscale::RoomAnchor anchor_;
    posture::AnchorDetector detector_;
    posture::PostureDetector postureDetector_;
    posture::PostureTracker postureTracker_;
    struct PendingAnchor {
        bool active = false;
        roomscale::RecenterKind kind = roomscale::RecenterKind::Full;
        const char* why = "";
        double at = 0.0; // nowSeconds()
    };
    PendingAnchor reanchor_;
    double postureGraceUntil_ = -1.0; // the lean-cap fade stays off until then after a posture change
    std::optional<float> anchorAboveFloor_;
    bool anchorTaken_ = false;
    bool testRecenterDone_ = false;
    double firstSeconds_ = -1.0;
    bool leanClamped_ = false;
    std::uint64_t leanClamps_ = 0;
    double lastClampLog_ = -10.0;
    float maxLean_ = 0.0f;
    float clampPeak_ = 0.0f;
    float clampHeld_ = 0.0f;
    roomscale::HeadClearance clearance_;
    roomscale::DrivenViewOffset driven_; // the offset eased out while the game drives the view
    std::uint64_t drivenEpisodes_ = 0;
    bool blocked_ = false;
    std::uint64_t blocks_ = 0;
    std::uint64_t sweeps_ = 0;
    double lastStats_ = 0.0;
    bool loggedFirst_ = false;
    // Body follow (camera hook only).
    roomscale::BodyFollow follow_;
    bool followReady_ = false;
    std::optional<Vec3> lastOrigin_;
    Vec3 bodyForward_{1.0f, 0.0f, 0.0f};
    Vec3 bodyLeft_{0.0f, 1.0f, 0.0f};
    bool bodyKnown_ = false;
    std::array<std::uint64_t, static_cast<std::size_t>(roomscale::FollowBlock::Count)> followBlocks_{};
    std::uint64_t followFrames_ = 0;
    std::array<std::uint64_t, 4> followTiers_{}; // frames per FollowTier
    roomscale::FollowTier lastTier_ = roomscale::FollowTier::None;
    std::uint64_t tierLogs_ = 0;
    double followAbsorbed_ = 0.0;
    float followMaxGap_ = 0.0f;
    bool loggedFollowStart_ = false;
    bool loggedFollowTaken_ = false;
    bool loggedNoHook_ = false;
    std::uint32_t loggedBlocks_ = 0; // a bit per FollowBlock logged once
    std::uint64_t teleports_ = 0;
    double firstAnchorSeconds_ = -1.0;
    int stepLeg_ = -1;
    int testCommand_ = 0;
    roomscale::StepProbe probe_;

    // Worker -> camera hook.
    std::mutex mutex_;
    bool spaceChanged_ = false;
    std::optional<Pose> spaceChange_;

    // Camera hook -> worker.
    std::atomic<float> penetration_{0.0f};
    std::atomic<double> penetrationSeconds_{0.0};
    // How far the head is past the lean cap (metres): beyond it the camera stops following the head, so the
    // view fades as it does in geometry instead of the world moving with the player.
    std::atomic<float> leanExcess_{0.0f};
    std::atomic<double> leanSeconds_{0.0};
    // The blink over a re-anchor: the view is faded out until then (nowSeconds()).
    std::atomic<double> blinkUntil_{-1.0};
    std::atomic<double> holdBlackUntil_{-1.0}; // holdBlack()

    // Worker only.
    roomscale::HeadFade fade_;
    double lastFadeSeconds_ = -1.0;
    double contactSeconds_ = -1.0; // when the fade started rising from 0
    double deepSeconds_ = -1.0;    // when the penetration first reached full depth
    bool loggedFull_ = false;
    const char* fadeCause_ = ""; // what started the current fade
};

} // namespace evr::vkcore
