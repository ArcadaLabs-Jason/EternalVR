#pragma once

// When the eye tags may take a new base, and what the frame-end job does with a frame (Route S,
// docs/VR_STEREO.md, "Eye tags and pairing").
//
// The tag base is the backend frame counter (renderBackend + 0xB0) at a moment when the render thread
// has presented every frame handed to it. A counter that has not moved for a while is not proof of that:
// a frame whose backend work takes longer than the wait (a hitch while a map streams in) presents after
// the base, takes the first tag and shifts every pair by one frame. So the frame-end job also counts the
// frames it hands to the render thread (each presents once and raises the counter once), and a base
// taken while the counts agree is exact. Only without a reference (the first base, or after the counts
// stopped agreeing) is a long quiet period taken as idle.

#include <cstdint>

namespace evr::stereo_seq {

// Quiet time (backend counter unchanged, frontend held) after which the render thread is taken as idle
// when the frame count cannot tell.
inline constexpr std::uint64_t kUnverifiedIdleQuietMs = 100;

class RenderIdle {
public:
    enum class Verdict : std::uint8_t {
        Wait,           // frames handed over are still to present, or not quiet for long enough
        Idle,           // every frame handed over has presented (the counts agree)
        IdleUnverified, // no usable count: quiet for kUnverifiedIdleQuietMs
    };

    // A frame that will present (one backend frame) was handed to the render thread.
    void kicked() { ++kicks_; }

    // The backend counter reads `backend` and has not changed for `quietMs`.
    Verdict check(std::uint32_t backend, std::uint64_t quietMs) const;

    // A base was taken at `backend`: every frame kicked so far has presented by then.
    void based(std::uint32_t backend);

    // The counts cannot be trusted (a drain timed out): the next base comes from a quiet period alone.
    void forget() { reference_ = false; }

    bool hasReference() const { return reference_; }
    std::uint64_t kicks() const { return kicks_; }

private:
    std::uint64_t kicks_ = 0;
    bool reference_ = false;
    std::uint32_t referenceBackend_ = 0;
    std::uint64_t referenceKicks_ = 0;
};

// What the frame-end job wrapper does with a frame of the engine's own chain.
struct FrameEndInput {
    bool presents = false;     // the frame kicks a backend frame (and so one present)
    bool leftApplied = false;  // the per-eye hook wrote eye L's view into it
    bool wanted = false;       // the per-eye hook had a stereo view but the tags needed a base first
    bool synced = false;       // the eye tags are in step
    bool rebaseDue = false;    // stereo resumes after enough mono frames: a fresh base first
    bool drainAllowed = false; // no drain ran or failed too recently
};

struct FrameEndPlan {
    bool drain = false;  // hold the frame until the render thread is idle and take a new base first
    bool stereo = false; // the frame is eye L of a pair (with `drain`: only if the base was taken)
};

// A frame whose eye L view was written must be paired, so it drains whenever the tags need a base (the
// per-eye hook only writes eye L when they do not, so this is the rare case of a desync in between). A
// frame the per-eye hook only wanted stereo for is the game's own view: it drains, subject to the
// spacing, and stays mono, so the next tick starts stereo on a fresh base and no frame drawn for eye L
// is ever shown without its eye R.
FrameEndPlan planFrameEnd(const FrameEndInput& in);

} // namespace evr::stereo_seq
