#include "stereo_seq/stereo_taa.hpp"

#include <algorithm>
#include <string>

namespace evr::stereo_seq {

AccumPick pickAccumulation(AccumRole role, const RenderTag* tag, bool secondPair) {
    AccumPick pick;
    if (!tag || !secondPair) {
        return pick;
    }
    // Eye L and mono frames alternate within the engine's pair, eye R within the second one. The output
    // is the image the eye's previous frame read, so this frame's history is what that frame wrote.
    pick.engine = false;
    pick.pair = eyeIndex(tag->eye);
    const std::uint32_t parity = tag->eyeSeq & 1u;
    pick.index = static_cast<int>(role == AccumRole::Output ? parity ^ 1u : parity);
    return pick;
}

std::uint8_t taaSubSample(std::uint64_t gameFrame, int numSubSamples) {
    const int n = std::clamp(numSubSamples, 1, 255);
    return static_cast<std::uint8_t>(gameFrame % static_cast<std::uint64_t>(n));
}

int ExposurePlanner::indexFor(const RenderTag& tag) {
    if (tag.eye == Eye::Right) {
        return lastLeft_;
    }
    lastLeft_ = static_cast<int>(tag.eyeSeq & 1u);
    return lastLeft_;
}

bool TaaResetPlanner::onLeft(std::uint64_t gameFrame) {
    leftFrame_ = gameFrame;
    leftDecision_ = lastRight_ == 0 || gameFrame != lastRight_ + 1;
    return leftDecision_;
}

bool TaaResetPlanner::onRight(std::uint64_t gameFrame) {
    const bool reset = gameFrame != leftFrame_ || leftDecision_;
    lastRight_ = gameFrame;
    return reset;
}

void NgxTwins::created(std::uintptr_t primary, std::uintptr_t twin) {
    if (primary == 0) {
        return;
    }
    for (Entry& e : entries_) {
        if (e.primary == primary) {
            e = Entry{primary, twin, 0};
            return;
        }
    }
    entries_.push_back(Entry{primary, twin, 0});
}

bool NgxTwins::known(std::uintptr_t primary) const {
    return std::any_of(entries_.begin(), entries_.end(),
                       [primary](const Entry& e) { return e.primary == primary; });
}

std::uintptr_t NgxTwins::twinOf(std::uintptr_t primary) const {
    for (const Entry& e : entries_) {
        if (e.primary == primary) {
            return e.twin;
        }
    }
    return 0;
}

std::uintptr_t NgxTwins::released(std::uintptr_t primary) {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [primary](const Entry& e) { return e.primary == primary; });
    if (it == entries_.end()) {
        return 0;
    }
    const std::uintptr_t twin = it->twin;
    entries_.erase(it);
    return twin;
}

bool NgxTwins::resetTwin(std::uintptr_t primary,
                         std::uint64_t gameFrame,
                         std::uint64_t minStep,
                         std::uint64_t maxStep) {
    for (Entry& e : entries_) {
        if (e.primary == primary) {
            const bool reset =
                e.lastFrame == 0 || gameFrame < e.lastFrame + minStep || gameFrame > e.lastFrame + maxStep;
            e.lastFrame = gameFrame;
            return reset;
        }
    }
    return true;
}

const std::vector<CvarExpectation>& stereoTaaForcedCvars() {
    // Each of these keeps its own history, picked by the backend frame's parity like the TAA images, so
    // with two renders per tick each eye would read the other's: TAA's anti-ghosting mask pair
    // (_taaGhostingMask0/1), SSDO, the light-scattering volumes, depth of field, water reflections and
    // grid, refraction and the ray-traced reflections' temporal upscale. Dynamic resolution stays off.
    static const std::vector<CvarExpectation> cvars = {
        {"r_TAAAntiGhosting", "0"},
        {"r_SSDOTemporalAA", "0"},
        {"r_lightScatteringTAA", "0"},
        {"r_dofTAA", "0"},
        {"r_waterReflectionsTAA", "0"},
        {"r_waterGridTAA", "0"},
        {"r_refractionTAA", "0"},
        {"r_raytracedReflectionsTemporalUpscaleQuality", "0"},
        {"rs_enable", "0"},
    };
    return cvars;
}

const std::vector<CvarExpectation>& stereoTaaFailClosedCvars() {
    static const std::vector<CvarExpectation> cvars = {
        {"r_antialiasing", "0"},
        {"r_TAASafeMode", "1"},
    };
    return cvars;
}

const std::vector<CvarExpectation>& stereoTaaCommandLineCvars() {
    static const std::vector<CvarExpectation> cvars = {
        {"r_TAASafeMode", "0"},
        {"rs_enable", "0"},
        {"r_swapInterval", "0"},
    };
    return cvars;
}

int dlssQualityValue(std::string_view value) {
    std::string lower;
    for (const char c : value) {
        lower.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    }
    if (lower == "ultra_performance" || lower == "0") {
        return 0;
    }
    if (lower == "performance" || lower == "1") {
        return 1;
    }
    if (lower == "balanced" || lower == "2") {
        return 2;
    }
    if (lower == "quality" || lower == "3") {
        return 3;
    }
    return -1;
}

bool switchValue(std::string_view value, bool fallback) {
    std::string lower;
    for (const char c : value) {
        lower.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    }
    if (lower == "0" || lower == "false" || lower == "off") {
        return false;
    }
    if (lower == "1" || lower == "true" || lower == "on") {
        return true;
    }
    return fallback;
}

int heldAntialiasing(int current, bool dlssOption, bool dlssPerEye) {
    int mode = current == 1 || current == 2 ? current : 1;
    if (dlssOption) {
        mode = 2;
    }
    return mode == 2 && !dlssPerEye ? 1 : mode;
}

const char* taaMissingPiece(const TaaReadiness& readiness) {
    if (!readiness.selectors) {
        return "the accumulation selector hooks";
    }
    if (!readiness.secondPair) {
        return "eye R's accumulation images (built with the device context)";
    }
    if (!readiness.subSamples) {
        return "r_TAANumSubSamples";
    }
    if (!readiness.exposure) {
        return "the auto-exposure index hook";
    }
    if (!readiness.cvarSetter) {
        return "the cvar setter or a forced cvar";
    }
    return nullptr;
}

} // namespace evr::stereo_seq
