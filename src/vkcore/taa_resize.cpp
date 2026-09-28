#include "vkcore/taa_resize.hpp"

#include "vkcore/log.hpp"

#include <cstring>

namespace evr::vkcore {

namespace {

constexpr std::size_t kTargetOffsets[kSlotTargetCount] = {0x58, 0x60, 0x68, 0x70, 0x78};
constexpr const char* kTargetNames[kSlotTargetCount] = {"accumulation 0", "accumulation 1", "opaque",
                                                        "view colour", "distortion"};

// The target's width and height, {0, 0} when the slot has none.
void targetSize(const std::byte* slot, std::size_t i, int out[2]) {
    out[0] = 0;
    out[1] = 0;
    void* target = nullptr;
    std::memcpy(&target, slot + kTargetOffsets[i], sizeof(target));
    if (target) {
        std::memcpy(out, target, 2 * sizeof(int));
    }
}

} // namespace

unsigned slotSizeMismatches(const std::byte* engineSlot, const std::byte* ourSlot) {
    unsigned mask = 0;
    for (std::size_t i = 0; i < kSlotTargetCount; ++i) {
        int engine[2];
        int ours[2];
        targetSize(engineSlot, i, engine);
        targetSize(ourSlot, i, ours);
        const bool bothSet = (engine[0] || engine[1]) && (ours[0] || ours[1]);
        if (bothSet && (engine[0] != ours[0] || engine[1] != ours[1])) {
            mask |= 1u << i;
        }
    }
    return mask;
}

void logSlotSizes(const char* tag,
                  const std::byte* engineSlot,
                  const std::byte* ourSlot,
                  const int* size,
                  const int* upscaledSize) {
    EVR_LOG("%s: resize to %dx%d (upscaled %dx%d); slot targets, engine vs eye R:", tag, size ? size[0] : 0,
            size ? size[1] : 0, upscaledSize ? upscaledSize[0] : 0, upscaledSize ? upscaledSize[1] : 0);
    for (std::size_t i = 0; i < kSlotTargetCount; ++i) {
        int engine[2];
        int ours[2];
        targetSize(engineSlot, i, engine);
        targetSize(ourSlot, i, ours);
        const bool differs = engine[0] != ours[0] || engine[1] != ours[1];
        const bool used = (kEyeRTargets >> i) & 1u;
        EVR_LOG("%s:   +0x%02zX %-14s %dx%d vs %dx%d%s", tag, kTargetOffsets[i], kTargetNames[i], engine[0],
                engine[1], ours[0], ours[1],
                !differs ? "" : (used ? "  DIFFERS" : "  differs (not used by eye R)"));
    }
}

} // namespace evr::vkcore
