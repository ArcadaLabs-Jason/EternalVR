#pragma once

// The free off hand's 10 s line in the log ("offhand: last 10 s: ...", vkcore/offhand_stats.hpp), on by
// default: what the arm policy decided and why, the layer's share, the ticks the arm was refused, the off
// hand's tracking, and how far from where the layer meant it the glove was drawn.
//
// "Drawn" is the game's own read of the wrist in a frame (the skeleton it draws in that frame) placed by the
// arms model's placement of that frame; "meant" is the wrist as the layer wrote it, placed by the placement
// the layer posed it under (features/arm/drawn_wrist.hpp). The two differ when the model moves between the
// placement the arm was posed under and the one it is drawn with (the weapon hand's motion in between: the
// game draws the arm a frame after it is written), or when a write does not land. The step
// is the drawn wrist's move from the frame before, counted only while the meant wrist moved less than
// kStillMetres: the glove moving while its controller holds still.
//
// Pure: no game memory, no clock.

#include "common/vector.hpp"
#include "features/input/offhand_policy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace evr::input {

// One tick of the player's hands.
struct OffhandWatchTick {
    ArmReason reason = ArmReason::ModeGame;
    float weight = 0.0f;   // the layer's share after the blend
    bool offValid = false; // the off hand's pose was valid in the hands snapshot read
};

// One frame of the posed off hand (metres, the world's axes; any common origin).
struct OffhandWatchFrame {
    Vec3 drawn;
    Vec3 meant;
    // The hands snapshot the arm was posed from against the one the placement it was posed under came from
    // (ms): the hands of two frames when it was posed under the last frame's placement.
    double snapshotGapMs = 0.0;
};

class OffhandWatch {
public:
    static constexpr float kStillMetres = 0.002f;

    void noteTick(const OffhandWatchTick& tick);
    // The hook's count of refused ticks so far (it only grows); the line gives the growth since the last
    // reset.
    void noteRefused(std::uint64_t total);
    void noteFrame(const OffhandWatchFrame& frame);
    // A frame of the player's hands without a posed frame to measure: the next one starts a new chain of
    // steps.
    void noteBreak();
    // The line's text after "offhand: last 10 s: ".
    [[nodiscard]] std::string line() const;
    [[nodiscard]] bool empty() const { return ticks_ == 0; }
    // Starts the next 10 s; the chain of steps goes on across it.
    void reset();

private:
    static constexpr std::size_t kReasons = static_cast<std::size_t>(ArmReason::NoGrip) + 1;
    std::uint64_t ticks_ = 0;
    std::array<std::uint64_t, kReasons> reasons_{};
    float minWeight_ = 1.0f;
    float maxWeight_ = 0.0f;
    std::uint64_t untrackedTicks_ = 0;
    std::uint64_t drops_ = 0;
    bool lastValid_ = true;
    std::optional<std::uint64_t> refusedBase_;
    std::uint64_t refusedNow_ = 0;
    std::vector<float> offMetres_;
    double maxGapMs_ = 0.0;
    float maxStep_ = 0.0f;
    std::uint64_t steps_ = 0;
    std::optional<OffhandWatchFrame> last_;
};

} // namespace evr::input
