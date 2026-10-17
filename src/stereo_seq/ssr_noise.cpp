#include "stereo_seq/ssr_noise.hpp"

namespace evr::stereo_seq {

std::uint8_t ssrNoiseIndex(std::uint32_t counter) {
    const unsigned v = counter & 15u;
    return static_cast<std::uint8_t>(((v & 1u) << 3) | ((v & 2u) << 1) | ((v & 4u) >> 1) | ((v & 8u) >> 3));
}

std::uint8_t ssrTickCounter(std::uint64_t tick, bool alternate) {
    return static_cast<std::uint8_t>((alternate ? tick / 2 : tick) & 15u);
}

std::uint32_t ssrCounterFor(const std::optional<RenderTag>& tag,
                            bool alternateEyes,
                            bool perEyeHistory,
                            std::uint32_t engineCounter) {
    if (!perEyeHistory || !tag || !drawsEyeView(*tag) || tag->tick == 0) {
        return engineCounter;
    }
    return ssrTickCounter(tag->tick, alternateEyes && !tag->pairInTick);
}

} // namespace evr::stereo_seq
