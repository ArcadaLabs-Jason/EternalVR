// Parallel Eye Rendering: the clone log (view_clone_map.hpp). After each build one line per clone (where the
// engine keeps its object, its name, size, format and bytes) and a summary by where the objects are kept;
// while ETERNALVR_TEST_VIEW_CLONE_LOG is not 0, the census: what view 1's stores, binds and write marks did
// with each clone (counted by the hooks of view_clone_binds.cpp and view_one_passes.cpp), reported once at
// the first periodic report 120 s or more after a build. A clone view 1 never used is strong evidence, not
// proof, that view 1 does without it: the image A/B with ETERNALVR_TEST_VIEW_CLONE_SKIP decides.

#include "vkcore/clone_census.hpp"
#include "vkcore/log.hpp"
#include "vkcore/swapchain_entry.hpp"
#include "vkcore/view_clone_map.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace evr::vkcore::view_clone {

namespace {

constexpr const char* kTag = "view-clones";

// ---- Build 25216728 (RVAs) ----

// [ ] = the game's VkDevice: the first argument of the engine's device calls (vkCreateImage at 0x1C48077,
// the target destructor's vkDestroy* at 0x1C73DB9).
constexpr std::uint32_t kEngineDevice = 0x667E248;
// Image: its options at +0x58 (format +0x5C, width +0x64, height +0x68, depth +0x6C, mips +0x70 and layers
// +0x74, as the allocation 0x1C48020 and the resize 0x1C4AE30 read them); flags +0x9C, bit 8 a set of
// images, else the VkImage at +0xE8 (the purge 0x1C4A910).
constexpr std::size_t kImageFormat = 0x5C;
constexpr std::size_t kImageWidth = 0x64;
constexpr std::size_t kImageHeight = 0x68;
constexpr std::size_t kImageDepth = 0x6C;
constexpr std::size_t kImageMips = 0x70;
constexpr std::size_t kImageLayers = 0x74;
constexpr std::size_t kImageFlags = 0x9C;
constexpr std::uint32_t kImageSet = 0x100;
constexpr std::size_t kImageVk = 0xE8;
constexpr std::size_t kNameLimit = 0x100;
constexpr std::size_t kListedNames = 40; // names in a census summary line

std::string nameAt(std::uintptr_t image) {
    const auto chars = image ? read<const char*>(image + kImageName) : nullptr;
    return chars ? std::string(chars, strnlen(chars, kNameLimit)) : std::string();
}

std::uint64_t vkBytesOf(std::uintptr_t image) {
    if (read<std::uint32_t>(image + kImageFlags) & kImageSet) {
        return 0;
    }
    const auto vkImage = read<VkImage>(image + kImageVk);
    DeviceData* dev = findDeviceData(read<void*>(baseAddress() + kEngineDevice));
    if (!vkImage || !dev || !dev->vk.GetImageMemoryRequirements) {
        return 0;
    }
    VkMemoryRequirements req{};
    dev->vk.GetImageMemoryRequirements(dev->device, vkImage, &req);
    return req.size;
}

const char* groupText(Group g) {
    switch (g) {
    case Group::GlobalImages:
        return "global image slots";
    case Group::GlobalTargets:
        return "global targets";
    case Group::ContextFields:
        return "device context fields";
    default:
        return "view 1's device context";
    }
}

std::vector<clone_census::Entry> entriesOf(const std::vector<Clone>& clones,
                                           const std::unique_ptr<std::atomic<std::uint64_t>[]>& uses) {
    std::vector<clone_census::Entry> entries(clones.size());
    for (std::size_t i = 0; i < clones.size(); ++i) {
        const Clone& c = clones[i];
        entries[i].name = c.engineName.empty() ? c.name : c.engineName;
        entries[i].bytes = c.vkBytes ? c.vkBytes : c.bytes;
        entries[i].target = c.target;
        entries[i].parts = c.parts;
        for (int u = 0; u < clone_census::kUses; ++u) {
            entries[i].uses[u] = uses ? uses[i * clone_census::kUses + u].load() : 0;
        }
    }
    return entries;
}

std::vector<std::pair<std::uintptr_t, std::uint32_t>> indexOf(const std::vector<Clone>& records) {
    std::vector<std::pair<std::uintptr_t, std::uint32_t>> index;
    for (std::size_t i = 0; i < records.size(); ++i) {
        index.emplace_back(records[i].image, static_cast<std::uint32_t>(i));
    }
    std::sort(index.begin(), index.end());
    return index;
}

int findIn(const std::vector<std::pair<std::uintptr_t, std::uint32_t>>& index, std::uintptr_t p) {
    const auto it = std::lower_bound(index.begin(), index.end(), std::make_pair(p, 0u));
    return it != index.end() && it->first == p ? static_cast<int>(it->second) : -1;
}

void countAt(const std::unique_ptr<std::atomic<std::uint64_t>[]>& uses, int i, clone_census::Use use) {
    if (i >= 0 && uses) {
        uses[static_cast<std::size_t>(i) * clone_census::kUses + use].fetch_add(1, std::memory_order_relaxed);
    }
}

// What a build's per-clone lines say, to log them only when they differ from the last build's (each map load
// makes the same set again).
std::string signatureOf(const Map& map) {
    std::string text;
    for (const Clone& c : map.clones) {
        text += c.source + "|" + c.engineName + "|" + std::to_string(c.width) + "x" +
                std::to_string(c.height) + "|" + std::to_string(c.format) + "|" +
                std::to_string(c.parts.size()) + ";";
    }
    return text;
}
std::string g_lastSignature; // render thread only
int g_lastLogged = -1;

using ull = unsigned long long;

} // namespace

std::string engineImageName(std::uintptr_t image) {
    return nameAt(image);
}

std::uint64_t engineImageBytes(std::uintptr_t image) {
    return clone_census::imageBytes(
        read<std::int32_t>(image + kImageWidth), read<std::int32_t>(image + kImageHeight),
        read<std::int32_t>(image + kImageDepth), read<std::int32_t>(image + kImageLayers),
        read<std::int32_t>(image + kImageMips),
        clone_census::formatOf(read<std::uint32_t>(image + kImageFormat)).bytes);
}

void describeImage(Clone& clone, std::uintptr_t engineImage) {
    const std::uintptr_t c = clone.image;
    clone.engineName = nameAt(engineImage);
    clone.format = read<std::uint32_t>(c + kImageFormat);
    clone.depth = read<std::int32_t>(c + kImageDepth);
    clone.mips = read<std::int32_t>(c + kImageMips);
    clone.layers = read<std::int32_t>(c + kImageLayers);
    clone.bytes = clone_census::imageBytes(read<std::int32_t>(c + kImageWidth),
                                           read<std::int32_t>(c + kImageHeight), clone.depth, clone.layers,
                                           clone.mips, clone_census::formatOf(clone.format).bytes);
    clone.vkBytes = vkBytesOf(c);
}

void indexClones(Map& map) {
    map.byClone = indexOf(map.clones);
    map.uses.reset(new std::atomic<std::uint64_t>[map.clones.size() * clone_census::kUses]());
    map.byLeftOut = indexOf(map.leftOut);
    map.leftUses.reset(new std::atomic<std::uint64_t>[map.leftOut.size() * clone_census::kUses]());
}

void logBuild(const Map& map, int build) {
    struct Sum {
        int images = 0;
        std::uint64_t bytes = 0;
    };
    std::array<Sum, 4> groups{};
    Sum all;
    Sum vk;
    int targets = 0;
    std::string signature = signatureOf(map);
    const bool lines = signature != g_lastSignature;
    if (lines) {
        g_lastSignature = std::move(signature);
        g_lastLogged = build;
    } else {
        EVR_LOG("%s: build %d: the same %zu clones as build %d", kTag, build, map.clones.size(),
                g_lastLogged);
    }
    for (std::size_t i = 0; i < map.clones.size(); ++i) {
        const Clone& c = map.clones[i];
        if (c.target) {
            ++targets;
            if (!lines) {
                continue;
            }
            std::string parts;
            for (const std::uint32_t p : c.parts) {
                parts += (parts.empty() ? "" : ", ") + std::to_string(p);
            }
            EVR_LOG("%s: build %d clone %zu: target (%s) %dx%d of '%s', image clone(s) %s", kTag, build, i,
                    c.source.c_str(), c.width, c.height, c.engineName.c_str(),
                    parts.empty() ? "none" : parts.c_str());
            continue;
        }
        const clone_census::Format f = clone_census::formatOf(c.format);
        const std::string vkText = c.vkBytes ? " (vk " + clone_census::megabytes(c.vkBytes) + ")" : "";
        if (lines) {
            EVR_LOG(
                "%s: build %d clone %zu: '%s' (%s) %dx%dx%d, %d layer(s), %d mip(s), format %u (%.*s), %s%s",
                kTag, build, i, c.engineName.c_str(), c.source.c_str(), c.width, c.height, c.depth, c.layers,
                c.mips, c.format, static_cast<int>(f.name.size()), f.name.data(),
                f.bytes ? clone_census::megabytes(c.bytes).c_str() : "size unknown", vkText.c_str());
        }
        const std::uint64_t bytes = c.vkBytes ? c.vkBytes : c.bytes;
        Sum& g = groups[static_cast<std::size_t>(c.group)];
        ++g.images;
        g.bytes += bytes;
        ++all.images;
        all.bytes += bytes;
        if (c.vkBytes) {
            ++vk.images;
            vk.bytes += c.vkBytes;
        }
    }
    std::string split;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        split += std::string(split.empty() ? "" : ", ") + groupText(static_cast<Group>(g)) + " " +
                 std::to_string(groups[g].images) + " / " + clone_census::megabytes(groups[g].bytes);
    }
    EVR_LOG("%s: build %d: %d image clone(s), %s (VkImage sizes read for %d): %s; %d target(s)", kTag, build,
            all.images, clone_census::megabytes(all.bytes).c_str(), vk.images, split.c_str(), targets);
}

bool censusOn() {
    static const bool on = parallelEyesSettings().cloneCensus;
    return on;
}

int cloneIndex(const Map& map, std::uintptr_t clone) {
    return findIn(map.byClone, clone);
}

void countUse(const Map& map, std::uintptr_t clone, clone_census::Use use) {
    if (censusOn()) {
        countAt(map.uses, findIn(map.byClone, clone), use);
    }
}

void countMiss(const Map& map, std::uintptr_t object, clone_census::Use use) {
    if (censusOn()) {
        countAt(map.uses, findIn(map.byClone, object), clone_census::kAsClone);
        countAt(map.leftUses, findIn(map.byLeftOut, object), use);
    }
}

void reportCensus(const Map& map, int build) {
    if (!censusOn()) {
        return;
    }
    const std::vector<clone_census::Entry> entries = entriesOf(map.clones, map.uses);
    std::size_t images = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& u = entries[i].uses;
        images += entries[i].target ? 0 : 1;
        EVR_LOG(
            "%s: census of build %d: clone %zu '%s' %s: stored %llu, target binds %llu, image binds %llu, "
            "marks %llu, as the clone %llu, other contexts %llu",
            kTag, build, i, entries[i].name.c_str(), clone_census::megabytes(entries[i].bytes).c_str(),
            static_cast<ull>(u[clone_census::kStored]), static_cast<ull>(u[clone_census::kTargetBinds]),
            static_cast<ull>(u[clone_census::kImageBinds]), static_cast<ull>(u[clone_census::kMarks]),
            static_cast<ull>(u[clone_census::kAsClone]), static_cast<ull>(u[clone_census::kOtherContexts]));
    }
    const clone_census::Summary unused = clone_census::unusedImages(entries, kListedNames);
    EVR_LOG("%s: census of build %d: %zu of %zu image clone(s) never used by view 1, %s%s%s; %zu of %zu "
            "target(s) never stored or bound",
            kTag, build, unused.count, images, clone_census::megabytes(unused.bytes).c_str(),
            unused.count ? ": " : "", unused.names.c_str(), clone_census::unusedTargets(entries),
            entries.size() - images);
    const clone_census::Summary other = clone_census::otherContexts(entries, kListedNames);
    EVR_LOG("%s: census of build %d: %zu clone(s) on other command contexts than view 1's%s%s", kTag, build,
            other.count, other.count ? ": " : "", other.names.c_str());
    // The objects left out (ETERNALVR_TEST_VIEW_CLONE_SKIP): any use here is view 1 working on view 0's.
    const std::vector<clone_census::Entry> left = entriesOf(map.leftOut, map.leftUses);
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto& u = left[i].uses;
        EVR_LOG("%s: census of build %d: left out '%s' (%s) %s: view 1 stored %llu, target binds %llu, image "
                "binds %llu, marks %llu",
                kTag, build, left[i].name.c_str(), map.leftOut[i].source.c_str(),
                clone_census::megabytes(left[i].bytes).c_str(), static_cast<ull>(u[clone_census::kStored]),
                static_cast<ull>(u[clone_census::kTargetBinds]),
                static_cast<ull>(u[clone_census::kImageBinds]), static_cast<ull>(u[clone_census::kMarks]));
    }
    if (!left.empty()) {
        const clone_census::Summary used = clone_census::touched(left, kListedNames);
        EVR_LOG("%s: census of build %d: view 1 used %zu of %zu left-out object(s) and image(s)%s%s", kTag,
                build, used.count, left.size(), used.count ? ": " : "", used.names.c_str());
    }
}

} // namespace evr::vkcore::view_clone
