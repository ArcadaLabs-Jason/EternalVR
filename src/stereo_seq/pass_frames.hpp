#pragma once

// The backend frame a render pass is recorded for (foveated rendering, vkcore/vrs_nv.hpp): its eye picks the
// pass's rate image, so a wrong frame gives the pass the other eye's pattern.
//
// The backend frame counter moves on once while a frame's jobs still record (the render thread's swap): read
// at a pass it is the pass's own frame or the next one (the tag in flight, seq_hooks.hpp). The render-view
// job reads it for its render and keeps that read in its contexts (the exposure and scattering hooks note
// it); a pass recorded before that job of its frame sees the previous render's read. Where the two agree the
// pass is in that frame for sure (Agreed). Only that frame is used: every other pass keeps full rate
// (vrs_pass_eye.cpp; the rig test knob ETERNALVR_TEST_VRS_PARITY=1 uses the two guesses below as well).
//
// Where they differ the command buffer gives a guess, which the inputs cannot check:
// - SameRecording. A recording is what a command buffer holds since its last vkBeginCommandBuffer (begin), up
//   to a reset of it or its pool (reset). A recording that agreed at a counter guesses that counter for its
//   later passes. Wrong for a recording that goes on into the next frame: its passes there before that
//   frame's render-view job (counter c + 1, read c) look like the late passes after the swap.
// - Parity. The engine records each frame into one of two sets of command buffers by the frame's parity
//   (docs/rig-findings/vk-pools-per-context.md, section 2), so a command buffer is recorded every second
//   frame. Once two recordings in a row agreed, two frames apart, a new recording before its first agreement
//   guesses the frame two after the last agreed one (c), if that is the counter now or the one before and
//   the counter now is at most one past the render-view read. Wrong for a buffer recorded at another
//   interval: the first pass after the swap of a recording in frame c + 1 (counter c + 2, read c + 1) and a
//   pass before the render-view job of frame c + 3 after an idle frame (c + 3, read c + 2) look like the
//   steady early pass.
// - The engine's parity link is global: an agreement against a command buffer's learned parity (even after a
//   gap), a recording in the frame of its last agreed one or the next with a pass before the swap, or a
//   parity guess its recording's agreement contradicts, is a parity break, and every command buffer's parity
//   is learned again (an epoch).
// A recording that agreed at a second counter, or at another frame than its parity guessed, is counted as
// contradicted; a wrong guess its recording never agrees after is not seen. Anything else (nothing learned, a
// recording whose begin was not seen, a buffer idle for a frame pair, a recording without an agreement
// before this one): no frame. No Vulkan or engine code here, so it is tested on every platform. Not
// thread-safe: the caller locks.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace evr::stereo_seq {

class PassFrames {
public:
    // The game records from a few hundred command buffers (21 contexts, 4 chunks, 2 parities, twice).
    static constexpr std::size_t kCapacity = 4096;
    // Recordings in a row, two frames apart, that agreed before a command buffer answers by parity.
    static constexpr std::uint32_t kParityRecordings = 2;

    enum class Source : std::uint8_t {
        Agreed,        // the counter now and the render-view job's read are the same
        SameRecording, // a guess: the command buffer's recording agreed at its counter earlier
        Parity,        // a guess: the command buffer's parity picks the counter now or the one before
        Unknown,       // nothing learned: no frame
    };

    struct Answer {
        std::optional<std::uint32_t> counter; // the backend counter the frame found read (its tag: + 1)
        Source source = Source::Unknown;

        // The frame the pass's eye is taken from: an agreed one, a guess only with `guesses` (the rig test
        // knob); nullopt: none, full rate.
        [[nodiscard]] std::optional<std::uint32_t> used(bool guesses) const {
            return (source == Source::Agreed || guesses) ? counter : std::nullopt;
        }
    };

    struct Stats {
        std::uint64_t agreed = 0;
        std::uint64_t sameRecording = 0; // this and `parity`: guesses, used or not
        std::uint64_t parity = 0;
        std::uint64_t unknown = 0;
        std::uint64_t contradicted = 0; // recordings whose later agreement contradicts their frame
        std::uint64_t parityBreaks = 0; // learned parities found broken (each one starts all over)
        std::size_t commandBuffers = 0;

        // The passes kept at full rate for want of a frame (Answer::used).
        [[nodiscard]] std::uint64_t fullRate(bool guesses) const {
            return unknown + (guesses ? 0 : sameRecording + parity);
        }
    };

    // vkBeginCommandBuffer on `commandBuffer` (any opaque value, its handle): a new recording.
    void begin(std::uint64_t commandBuffer);
    // vkResetCommandBuffer, or vkResetCommandPool of its pool: its recording ends.
    void reset(std::uint64_t commandBuffer);
    // vkFreeCommandBuffers, or vkDestroyCommandPool of its pool: forgotten (a new buffer may get its handle).
    void forget(std::uint64_t commandBuffer);

    // A render pass recorded into `commandBuffer` while the backend counter reads `now`; `renderView`: the
    // counter the latest render-view job read (nullopt: none noted).
    Answer find(std::uint64_t commandBuffer, std::uint32_t now, std::optional<std::uint32_t> renderView);

    // kCapacity command buffers are known: others are not learned until some are forgotten.
    [[nodiscard]] bool full() const { return entries_.size() >= kCapacity; }

    [[nodiscard]] Stats stats() const;

private:
    struct Entry {
        std::uint32_t counter = 0;  // the frame of its last agreement
        std::uint32_t streak = 0;   // recordings in a row that agreed, two frames apart (0: the last did not)
        std::uint32_t epoch = 0;    // the parity link's epoch the streak and the parity were learned in
        bool parityLearned = false; // a streak of kParityRecordings was reached in that epoch, with `parity`
        std::uint8_t parity = 0;
        bool begun = false;                       // this recording's begin was seen
        bool learned = false;                     // this recording agreed (at `counter`)
        std::optional<std::uint32_t> parityGuess; // the frame this recording's parity guessed first
        bool contradicted = false;                // this recording was counted as contradicted
    };

    void endRecording(Entry& e);
    void breakParity();
    void agree(Entry& e, std::uint32_t now);
    void contradict(Entry& e);

    std::unordered_map<std::uint64_t, Entry> entries_;
    std::uint32_t epoch_ = 0;
    Stats stats_;
};

} // namespace evr::stereo_seq
