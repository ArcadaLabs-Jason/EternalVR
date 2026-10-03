#pragma once

// Per-eye DLSS for Route S (part of taa_hooks.hpp; docs/rig-findings/stereo-temporal.md, "DLSS").
//
// The game links NGX statically and exports its entry points; it creates one DLSS feature
// (NVSDK_NGX_VULKAN_CreateFeature from RVA 0x1CC5760, recreated when the quality mode or the sizes change),
// evaluates it once per render (NVSDK_NGX_VULKAN_EvaluateFeature_C from RVA 0x1CC7AA0, in the backend
// frame's post-process job) and releases it before a recreate or at shutdown, all with one parameter block
// (RVA 0x66E8B28). The feature keeps DLSS's own history, so with two renders per tick both eyes would
// accumulate into one. Evaluate and release are detoured: eye R's evaluations (the frame's eye tag) go to
// a twin of the game's feature, created on eye R's first evaluation from the same parameter block, with the
// "Reset" parameter raised on its first use and after eye R missed a tick; a release releases both. A twin
// that cannot be created makes DLSS fall back to TAA until it is tried again (stereo_seq/ngx_twin_retry.hpp):
// after a wait, at once when the game releases that feature, or when the player chooses DLSS in the menu.
//
// The evaluation's eye is the render's own (stereo_seq/ngx_eye.hpp): the tag its output selector picked the
// `Output` image for, not the tag in flight, which names the next render once the render thread's swap came
// in between. The per-eye hook's history resets raise "Reset" on that render's evaluation, eye L's game
// feature included, so both eyes reset together under DLSS as under TAA.

#include "stereo_seq/eye_tags.hpp"

#include <cstdint>

namespace evr::vkcore {

// Hooks the evaluate and release exports once; create, SetI and GetI are called directly. All five are
// found by name in the game module's export table and checked to lie in its .text. False (logged) when
// any is missing.
bool installNgxTwins();

// Twins are used only while this is true (per-eye TAA on and the multiplayer guard armed).
void setNgxTwinsActive(bool active);

// True while a twin could not be created and is not being tried again: eye R would share the game's
// feature, so the caller falls back from DLSS to TAA.
bool ngxTwinFailed();

// Each stereo tick, before ngxTwinFailed is read: when a try is due, the failed twins are forgotten and the
// fallback ends, so DLSS is held again and eye R's next evaluation creates its feature.
void ngxTwinsTick();

// The player chose DLSS in the game's video menu during a fallback: a try at once, the count started over.
// False (nothing changes) without a fallback.
bool retryNgxTwins();

// The output selector picked `target` (an accumulation render target) for the render tagged `tag`: its
// VkImage is noted with the tag, for that render's evaluation to find by its `Output` image.
void noteNgxOutput(const void* target, const stereo_seq::RenderTag& tag);

// The per-eye hook reset `eye`'s history for game frame `gameFrame`: that render's evaluation raises "Reset".
void noteNgxReset(stereo_seq::Eye eye, std::uint64_t gameFrame);

struct NgxCounters {
    std::uint64_t twinCreates = 0;
    std::uint64_t twinFailures = 0;
    std::uint64_t evaluates[2] = {};
    std::uint64_t evaluatesNoTwin = 0;
    std::uint64_t twinResets = 0;
    std::uint64_t leftResets = 0; // eye L evaluations with "Reset" raised (the per-eye hook's resets)
    std::uint64_t releases = 0;
    // Evaluations whose eye came from their own output image, and those by the tag in flight instead (no own
    // tag found for a render the tag in flight names eye L or eye R).
    std::uint64_t ownTags = 0;
    std::uint64_t inFlightTags = 0;
    // Of the own ones: the tag in flight named the other eye (the swap came in between).
    std::uint64_t inFlightDiffers = 0;
};
NgxCounters ngxCounters();

} // namespace evr::vkcore
