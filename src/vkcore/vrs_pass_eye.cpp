// The eye a render pass draws for (vrs_nv.hpp): the eye of the backend frame its commands belong to.
//
// The tag in flight (seq_hooks.hpp) reads the backend counter at the pass; once the render thread's swap
// moved it on while the frame's jobs still record, it names the next frame, the other eye's under Route S,
// and the pass would get the other eye's pattern (the race the DLSS evaluation avoids by its output image,
// stereo_seq/ngx_eye.hpp). A pass gets an eye only where the counter now and the render-view job's own read
// agree (stereo_seq::PassFrames), the one case where its frame is sure; every other pass keeps full rate. The
// guesses from each command buffer's recordings (vrs_command_buffers.cpp) are counted, and used only with the
// rig test knob ETERNALVR_TEST_VRS_PARITY=1: they can give a pass the other eye's pattern.

#include "stereo_seq/eye_tags.hpp"
#include "vkcore/log.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/vrs_nv_impl.hpp"

#include <cstdint>
#include <mutex>
#include <optional>

namespace evr::vkcore::vrs_nv {

namespace {

int eyeOf(const std::optional<stereo_seq::RenderTag>& tag) {
    return tag && stereo_seq::drawsEyeView(*tag) ? stereo_seq::eyeIndex(tag->eye) : kNoEye;
}

} // namespace

int passEye(VrsDevice& d, VkCommandBuffer commandBuffer) {
    const std::optional<std::uint32_t> now = seqBackendCounter();
    if (!now) {
        return kNoEye;
    }
    stereo_seq::PassFrames::Answer answer;
    bool full = false;
    {
        std::lock_guard lock(d.framesMutex);
        answer = d.frames.find(reinterpret_cast<std::uintptr_t>(commandBuffer), *now, seqRenderViewCounter());
        full = d.frames.full();
    }
    if (full && !d.framesFullLogged.load(std::memory_order_relaxed) && !d.framesFullLogged.exchange(true)) {
        EVR_LOG("vrs: %zu command buffers noted for the render passes' frames, the most kept; the "
                "recordings of others are not followed (no guesses)",
                stereo_seq::PassFrames::kCapacity);
    }
    if (!answer.counter) {
        return kNoEye;
    }
    const int eye = eyeOf(seqTagForBackendFrame(*answer.counter + 1u));
    // The self-check: what the tag in flight would have given this pass, which differs only where the
    // counter has moved on past the frame found (a guess's, used or not).
    if (*answer.counter != *now && eye != kNoEye) {
        const int inFlight = eyeOf(seqTagForBackendFrame(*now + 1u));
        if (inFlight != kNoEye && inFlight != eye) {
            ++d.inFlightOther;
        }
    }
    return answer.used(d.usesGuesses) ? eye : kNoEye; // a guess not used: full rate
}

void logPassFrames(VrsDevice& d) {
    stereo_seq::PassFrames::Stats s;
    {
        std::lock_guard lock(d.framesMutex);
        s = d.frames.stats();
    }
    const char* const use = d.usesGuesses ? "the passes by neither; guesses used: ETERNALVR_TEST_VRS_PARITY=1"
                                          : "the passes whose counters did not agree";
    EVR_LOG("vrs: render pass frames: %llu by the render-view job's counter, %llu by their command "
            "buffer's recording, %llu by its parity, %llu by neither; %llu at full rate for it (%s); %llu "
            "recording(s) contradicted later, %llu parity break(s), %zu command buffer(s); %llu pass(es) the "
            "tag in flight would have given the other eye%s",
            static_cast<unsigned long long>(s.agreed), static_cast<unsigned long long>(s.sameRecording),
            static_cast<unsigned long long>(s.parity), static_cast<unsigned long long>(s.unknown),
            static_cast<unsigned long long>(s.fullRate(d.usesGuesses)), use,
            static_cast<unsigned long long>(s.contradicted), static_cast<unsigned long long>(s.parityBreaks),
            s.commandBuffers, static_cast<unsigned long long>(d.inFlightOther.load()),
            seqRenderViewCounter() ? "" : "; no render-view counter noted (no exposure hook)");
}

} // namespace evr::vkcore::vrs_nv
