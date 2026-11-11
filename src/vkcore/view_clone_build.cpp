// Parallel Eye Rendering: a build of view 1's clones (view_clones.cpp, view_clone_make.hpp): what
// ETERNALVR_TEST_VIEW_CLONE_SKIP leaves out, the clones of the global slots' and the device context's
// objects, view 1's device context and the shadow of the global region, and the map of them all.

#include "vkcore/view_clones_impl.hpp"

#include "vkcore/log.hpp"
#include "vkcore/runtime_cvars.hpp"
#include "vkcore/view_clone_make.hpp"
#include "vkcore/view_clone_map.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace evr::vkcore::view_clone {

namespace {

constexpr std::size_t kDcDepthPyramidViews = 0x300; // one per mip, from 0x1C48E30
constexpr int kDepthPyramidMips = 5;
constexpr std::uint32_t kImageMipView = 0x1C48E30; // (image, mip)
using ImageMipViewFn = void* (*)(void* image, int mip);

struct Source {
    std::uintptr_t at;    // where the engine keeps it
    std::uintptr_t value; // the object
    std::int32_t width;   // its size when the clones were made
    std::int32_t height;
};
std::vector<Source> g_sources; // render thread only
constexpr std::uint64_t kCensusAfterMs = 120000;

template <typename T>
T at(std::uint32_t rva) {
    return reinterpret_cast<T>(const_cast<std::byte*>(g_base + rva));
}

// ---- What a build leaves out, and the build ----

template <typename List, typename T>
bool contains(const List& list, T value) {
    return std::find(std::begin(list), std::end(list), value) != std::end(list);
}

// What ETERNALVR_TEST_VIEW_CLONE_SKIP leaves out of this build: the depth of field targets only while the
// layer holds r_dof 0; never kKeptSlots or kKeptFields.
struct Skips {
    std::vector<std::uint32_t> slots;
    std::vector<std::size_t> fields;
    bool gui = false;

    bool slot(std::uint32_t rva) const { return contains(slots, rva); }
    bool field(std::size_t offset) const { return contains(fields, offset); }
};

Skips skipsNow() {
    const parallel_eyes::CloneSkip& knob = parallelEyesSettings().cloneSkip;
    Skips s;
    if (knob.groups & parallel_eyes::kSkipSlot0) {
        s.fields.insert(s.fields.end(), std::begin(kSlot0Targets), std::end(kSlot0Targets));
    }
    if ((knob.groups & parallel_eyes::kSkipDof) && runtime_cvars::holds("r_dof", "0")) {
        s.fields.insert(s.fields.end(), std::begin(kDofTargets), std::end(kDofTargets));
        s.slots.insert(s.slots.end(), std::begin(kDofSlots), std::end(kDofSlots));
    } else if (knob.groups & parallel_eyes::kSkipDof) {
        EVR_LOG("%s: build %d: the depth of field clones stay: the layer does not hold r_dof 0 (or was "
                "writing its cvars)",
                kTag, g_builds);
    }
    s.gui = (knob.groups & parallel_eyes::kSkipGui) != 0;
    if (knob.groups & parallel_eyes::kSkipFlares) {
        s.slots.insert(s.slots.end(), std::begin(kFlareSlots), std::end(kFlareSlots));
    }
    if (knob.groups & parallel_eyes::kSkipMotionBlur) {
        s.slots.insert(s.slots.end(), std::begin(kMotionBlurSlots), std::end(kMotionBlurSlots));
    }
    if (knob.groups & parallel_eyes::kSkipRefraction) {
        s.fields.insert(s.fields.end(), std::begin(kRefractionFields), std::end(kRefractionFields));
    }
    for (const std::uint32_t rva : knob.slots) {
        if ((contains(kImageSlots, rva) || contains(kTargetSlots, rva)) && !contains(kKeptSlots, rva)) {
            s.slots.push_back(rva);
        } else if (g_builds == 0) {
            EVR_LOG(
                "%s: ETERNALVR_TEST_VIEW_CLONE_SKIP: 0x%X is not a slot the clones may leave out; ignored",
                kTag, rva);
        }
    }
    for (const std::uint32_t f : knob.fields) {
        if ((contains(kContextFields, f) || contains(kRefractionFields, f) || contains(kDcCloneFields, f) ||
             contains(kDcCloneTargets, f)) &&
            !contains(kKeptFields, f)) {
            s.fields.push_back(f);
        } else if (g_builds == 0) {
            EVR_LOG("%s: ETERNALVR_TEST_VIEW_CLONE_SKIP: dc+0x%X is not a field the clones may leave out; "
                    "ignored",
                    kTag, f);
        }
    }
    return s;
}

// The object at `slotAddress` (a global slot or a device context field), watched for changes; left shared
// with `leaveOut`.
void add(Builder& b, std::uintptr_t slotAddress, Kind expected, bool leaveOut) {
    const auto value = read<std::uintptr_t>(slotAddress);
    if (leaveOut) {
        if (value) {
            b.noteShared(b.source, value);
        }
        return;
    }
    if (!value) {
        // Made later by the engine (dc+0x548 is null at the first build): watched, so its arrival rebuilds
        // the clones.
        g_sources.push_back({slotAddress, 0, 0, 0});
        return;
    }
    const Kind kind = kindOf(value);
    if (kind == Kind::None || (expected != Kind::None && kind != expected)) {
        if (g_builds == 0) {
            EVR_LOG("%s: slot %p holds %p, not a%s; left shared", kTag, reinterpret_cast<void*>(slotAddress),
                    reinterpret_cast<void*>(value),
                    expected == Kind::Image    ? "n image"
                    : expected == Kind::Target ? " target"
                                               : " target or image");
        }
        return;
    }
    std::int32_t w = 0;
    std::int32_t h = 0;
    sizeOf(value, kind, w, h);
    g_sources.push_back({slotAddress, value, w, h});
    if (kind == Kind::Target) {
        b.cloneTarget(value);
    } else {
        b.cloneImage(value);
    }
}

} // namespace

bool sourcesChanged() {
    for (const Source& s : g_sources) {
        const auto value = read<std::uintptr_t>(s.at);
        if (value != s.value) {
            return true;
        }
        const Kind kind = kindOf(value);
        std::int32_t w = 0;
        std::int32_t h = 0;
        if (kind != Kind::None) {
            sizeOf(value, kind, w, h);
        }
        if (w != s.width || h != s.height) {
            return true;
        }
    }
    return false;
}

// The first clone the engine has freed or reused (a map load frees unreferenced scratch images), or null.
const Clone* goneClone(const Map& map, const char*& why) {
    for (const Clone& c : map.clones) {
        why = c.target ? nullptr : cloneGone(c);
        if (why) {
            return &c;
        }
    }
    return nullptr;
}

namespace {

// View 1's device context: its own depth pyramid (with the pyramid's mip views) and light scattering history,
// and every field whose object has a clone.
void buildDc1(Builder& b, std::uintptr_t dc, Map& map, const Skips& skips) {
    b.group = Group::View1Context;
    for (const std::size_t f : kDcCloneFields) {
        const auto image = read<std::uintptr_t>(dc + f);
        b.source = fieldText("dc1+", f);
        if (skips.field(f)) {
            b.noteShared(b.source, image);
        } else if (kindOf(image) == Kind::Image) {
            b.cloneImage(image);
        }
    }
    for (const std::size_t f : kDcCloneTargets) {
        const auto target = read<std::uintptr_t>(dc + f);
        b.source = fieldText("dc1+", f);
        if (skips.field(f)) {
            b.noteShared(b.source, target);
        } else if (kindOf(target) == Kind::Target) {
            b.cloneTarget(target);
        } else if (g_builds == 0) {
            EVR_LOG("%s: dc+0x%zX %p is not a target; left shared", kTag, f, reinterpret_cast<void*>(target));
        }
    }
    for (std::size_t off = 0; off < kDcSize; off += 8) {
        const auto engine = read<std::uintptr_t>(dc + off);
        if (const std::uintptr_t c = engine ? b.mapped(engine) : 0) {
            map.dcFields.emplace_back(off, c);
        }
    }
    if (const std::uintptr_t c = b.mapped(read<std::uintptr_t>(dc + kDcDepthPyramid))) {
        for (int mip = 0; mip < kDepthPyramidMips; ++mip) {
            const auto view = reinterpret_cast<std::uintptr_t>(
                at<ImageMipViewFn>(kImageMipView)(reinterpret_cast<void*>(c), mip));
            map.dcFields.emplace_back(kDcDepthPyramidViews + 8 * static_cast<std::size_t>(mip), view);
        }
    }
    if (const std::uintptr_t c = b.mapped(read<std::uintptr_t>(dc + kDcScreenTarget))) {
        map.screenImage = read<std::uintptr_t>(c + kTargetColors);
    }
    map.entries.emplace_back(dc, reinterpret_cast<std::uintptr_t>(g_dc1));
}

} // namespace

bool build() {
    const std::uintptr_t dc = deviceContext();
    if (!dc) {
        return false;
    }
    g_sources.clear();
    const Skips skips = skipsNow();
    Builder b;
    b.build = g_builds;
    b.stableNames = g_stableNames;
    if (const auto gui = read<std::uintptr_t>(baseAddress() + kGuiTarget);
        skips.gui && kindOf(gui) == Kind::Target) {
        for (std::size_t k = 0; k < 8; ++k) {
            if (const auto image = read<std::uintptr_t>(gui + kTargetColors + 8 * k)) {
                b.sharedImages.push_back(image);
                b.noteShared(fieldText("", kGuiTarget) + ".c" + std::to_string(k), image);
            }
        }
    }
    b.group = Group::GlobalImages;
    for (const std::uint32_t s : kImageSlots) {
        b.source = fieldText("", s);
        add(b, reinterpret_cast<std::uintptr_t>(g_base + s), Kind::Image, skips.slot(s));
    }
    b.group = Group::GlobalTargets;
    for (const std::uint32_t s : kTargetSlots) {
        b.source = fieldText("", s);
        add(b, reinterpret_cast<std::uintptr_t>(g_base + s), Kind::Target, skips.slot(s));
    }
    b.group = Group::ContextFields;
    for (const std::size_t f : kContextFields) {
        b.source = fieldText("dc+", f);
        add(b, dc + f, Kind::None, skips.field(f));
    }
    for (const std::size_t f : kRefractionFields) {
        b.source = fieldText("dc+", f);
        add(b, dc + f, Kind::None, skips.field(f));
    }
    // The shadow of the global region: the engine's values with the clones in their place, and the addresses
    // of the cloned slots (the setups hand some arrays over by address) mapped into it.
    const std::size_t regionSize = kRegionEnd - kRegionStart;
    std::memcpy(g_shadow, g_base + kRegionStart, regionSize);
    auto* map = new Map;
    for (std::size_t off = 0; off < regionSize; off += 8) {
        const auto engine =
            read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(g_base + kRegionStart + off));
        if (const std::uintptr_t c = engine ? b.mapped(engine) : 0) {
            std::memcpy(g_shadow + off, &c, sizeof(c));
            map->entries.emplace_back(reinterpret_cast<std::uintptr_t>(g_base + kRegionStart + off),
                                      reinterpret_cast<std::uintptr_t>(g_shadow + off));
        }
    }
    if (g_dc1) {
        buildDc1(b, dc, *map, skips);
    }
    map->entries.insert(map->entries.end(), b.entries.begin(), b.entries.end());
    std::sort(map->entries.begin(), map->entries.end());
    makeStencilView(*map, dc, g_builds);
    map->clones = std::move(b.clones);
    map->leftOut = std::move(b.leftOut);
    const auto finalTarget = read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(g_base + kFinalTarget));
    if (const std::uintptr_t finalClone = finalTarget ? b.mapped(finalTarget) : 0) {
        map->finalImage = read<std::uintptr_t>(finalClone + kTargetColors);
    }
    EVR_LOG("%s: build %d: %d image(s), %d target(s) cloned, %d failed; %zu map entries, %zu device context "
            "field(s); view 1's final image %p, screen image %p",
            kTag, g_builds, b.images, b.targets, b.failed, map->entries.size(), map->dcFields.size(),
            reinterpret_cast<void*>(map->finalImage), reinterpret_cast<void*>(map->screenImage));
    indexClones(*map);
    logBuild(*map, g_builds);
    if (b.stableNames) {
        const int waiting = queueOrphans(b);
        EVR_LOG("%s: build %d: image clones by name: %d kept, %d allocated again, %d new, %d made beside one "
                "still allocated at another size; %d wait for the engine's next resize to be purged",
                kTag, g_builds, b.kept, b.remade, b.made, b.beside, waiting);
    } else if (g_builds > 0) {
        EVR_LOG(
            "%s: build %d: new names (ETERNALVR_TEST_VIEW_CLONE_NAMES=build); the clones made before stay "
            "allocated until a map load frees them",
            kTag, g_builds);
    }
    if (b.sharedCount) {
        EVR_LOG(
            "%s: build %d: %d object(s) left shared (ETERNALVR_TEST_VIEW_CLONE_SKIP=%s), %s not cloned: %s",
            kTag, g_builds, b.sharedCount,
            parallel_eyes::cloneSkipText(parallelEyesSettings().cloneSkip).c_str(),
            clone_census::megabytes(b.sharedBytes).c_str(), b.shared.c_str());
    }
    g_builtAt = GetTickCount64();
    g_censusAt = censusOn() ? g_builtAt + kCensusAfterMs : 0;
    refreshDc1(*map); // before the map hands it out
    g_map.store(map, std::memory_order_release);
    ++g_builds;
    return true;
}

} // namespace evr::vkcore::view_clone
