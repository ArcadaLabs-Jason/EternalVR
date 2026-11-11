#pragma once

// Internal to per-eye TAA (taa_hooks.hpp), shared by taa_hooks.cpp (the cvars, the hooks' install, the
// counters) and taa_eye_slot.cpp (eye R's slot, built with the device context, and the accumulation
// selectors).

#include "vkcore/cvar_book.hpp"
#include "vkcore/mid_hook.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace evr::vkcore::taa_detail {

constexpr const char* kTag = "seq-taa";

// ---- Engine layouts (docs/rig-findings/stereo-temporal.md) ----

constexpr std::size_t kRenderSystemDeviceContext = 0xF58;
// The device context's per-view slot i starts at +0x8 + i * 0xA8; in a slot, the accumulation render
// targets are at +0x58 and +0x60 (images at +0x30 and +0x38), and +0x0 is the index its image names use.
constexpr std::size_t kDeviceContextSlot0 = 0x8;
constexpr std::size_t kSlotSize = 0xA8;
constexpr std::size_t kSlotAccumulationTargets = 0x58;
constexpr std::size_t kSlotOpaqueTarget = 0x68;
constexpr std::uint32_t kSecondSlotIndex = 1; // its images are named "_accumulationBuffer10" and so on

using SelectorFn = void* (*)(void* renderSystem, const std::byte* renderView);
using SlotBuilderFn = void (*)(void* deviceContext, void* slot, std::uint64_t deviceContextIndex);
using SetCvarFn = cvar_book::SetStringFn;

// ---- State of both files (taa_hooks.cpp) ----

extern std::byte* g_secondSlot;
extern std::atomic<std::byte*> g_secondSlotContext;
extern SelectorFn g_outputOriginal;
extern SelectorFn g_historyOriginal;
extern SelectorFn g_opaqueOriginal;
extern std::atomic<bool> g_perEye;
extern std::atomic<bool> g_failedClosed;

struct Counters {
    std::atomic<std::uint64_t> picks[2]{};
    std::atomic<std::uint64_t> enginePicks{0};
    std::atomic<std::uint64_t> secondPairBuilds{0};
    std::atomic<int> loggedPicks{0};
    std::atomic<std::uint64_t> opaquePicks{0};
    std::atomic<std::uint64_t> distortionBinds{0};
    // Output picks by the tag's eye (L, R) and by the side the latched projection is shifted to (left of
    // centre, centred, right): each tag eye should see one side only.
    std::atomic<std::uint64_t> tagVsView[2][3]{};
};
extern Counters g_counters;

// ---- The hooks installTaaHooks installs (taa_eye_slot.cpp) ----

void* outputSelector(void* renderSystem, const std::byte* renderView);
void* historySelector(void* renderSystem, const std::byte* renderView);
void* opaqueSelector(void* renderSystem, const std::byte* renderView);
void onDistortionBind(HookRegisters& regs);

} // namespace evr::vkcore::taa_detail
