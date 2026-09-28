#pragma once

// When hand aim must leave the game's view angles alone (T-055, docs/rig-findings/input-aim.md section 2,
// "Forced-angle states").
//
// The game forces the view during sync and glory kills, the meathook pull, melee lunges, wall-climb
// release, scripted and photo cameras and more. Every one of those goes through idPlayer::SetViewAngles
// from a caller other than the per-tick view update, or sets the player's view inhibit flags, or runs
// as a cutscene. While any of these holds, and for a short time after, the layer sends no aim delta;
// fighting a forced view would jerk the camera and could push the player out of the animation.

#include <cstdint>

namespace evr::input {

// idPlayer::inhibitFlags bits that stop the game's own view update: VIEW (0x8) and VIEW_ONCE (0x100).
inline constexpr std::uint32_t kInhibitViewMask = 0x108;

struct ForcedAngleSignals {
    bool foreignSetViewAngles = false; // a SetViewAngles call not from the per-tick update since last frame
    std::uint32_t inhibitFlags = 0;    // idPlayer::inhibitFlags
    bool cutscene = false;             // renderView_t::inCutscene
};

enum class ForcedReason : std::uint8_t {
    None,
    SetViewAngles,
    Inhibit,
    Cutscene,
    Settling, // none holds now, but one did within the resume delay
};

const char* forcedReasonName(ForcedReason reason);

class ForcedAngleGate {
public:
    // `resumeFrames`: game frames to keep yielding after the last signal (the game can apply a forced
    // angle one tick after the call that set it).
    explicit ForcedAngleGate(int resumeFrames = 2);

    // One game frame. True: yield (send no aim delta this frame).
    bool update(const ForcedAngleSignals& signals);

    [[nodiscard]] ForcedReason reason() const { return reason_; }
    // Frames that yielded, and how many separate forced episodes began.
    [[nodiscard]] std::uint64_t yieldedFrames() const { return yielded_; }
    [[nodiscard]] std::uint64_t episodes() const { return episodes_; }

private:
    int resumeFrames_;
    int sinceSignal_;
    ForcedReason reason_ = ForcedReason::None;
    std::uint64_t yielded_ = 0;
    std::uint64_t episodes_ = 0;
};

} // namespace evr::input
