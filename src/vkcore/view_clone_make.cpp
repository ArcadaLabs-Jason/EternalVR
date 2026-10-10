// Parallel Eye Rendering: making view 1's clones (view_clone_make.hpp).

#include "vkcore/view_clone_make.hpp"

#include "vkcore/clone_names.hpp"
#include "vkcore/log.hpp"
#include "vkcore/view_clone_map.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace evr::vkcore::view_clone {

namespace {

constexpr const char* kTag = "view-clones";
constexpr std::uint32_t kNoClone = 0xFFFFFFFFu;

// ---- Build 25216728 (RVAs) ----

// Render target object: 0x230 bytes; width/height +0/+4 (+8/+0xC override); colour images +0x10 (8), depth
// +0x70, stencil +0x78. Image: options at +0x58 (0x50 bytes, width +0xC, height +0x10).
constexpr std::size_t kTargetSize = 0x230;
constexpr int kTargetTag = 0x45;
constexpr int kColorCount = 8;
constexpr std::size_t kTargetDepth = 0x70;
constexpr std::size_t kTargetStencil = 0x78;
constexpr std::size_t kImageOptions = 0x58;
constexpr std::size_t kImageOptionsSize = 0x50; // what the image keeps
// The options ScratchImage reads are longer: +0x50 holds 32 width/height pairs, one per dynamic-resolution
// level, used when options +0x38 has bit 0x80 (the depth pyramid's images). 0x1C4A310 copies them to the
// image's level block (+0xE8) at +4.
constexpr std::size_t kOptionsSize = kImageOptionsSize + 0x100;
constexpr std::size_t kOptionsFlags = 0x38;
constexpr std::uint8_t kOptionsLevelSizes = 0x80;
constexpr std::size_t kImageLevels = 0xE8;
constexpr std::size_t kImageWidth = kImageOptions + 0xC;
constexpr std::size_t kImageHeight = kImageOptions + 0x10;
constexpr std::size_t kImageDepth = kImageOptions + 0x14;
constexpr std::size_t kImageLayers = kImageOptions + 0x1C;
// The image's per-subresource state block (0x3453 bytes), allocated with its GPU image by 0x1C48020 and
// zeroed when the image is purged. The engine frees unreferenced scratch images at a map load and reuses
// their memory, so a clone whose block or name changed is gone.
constexpr std::size_t kImageStates = 0x130;
constexpr std::uint32_t kAllocate = 0x357240;      // (size, memory tag)
constexpr std::uint32_t kTargetCtor = 0x1C73CA0;   // (target)
constexpr std::uint32_t kTargetAttach = 0x1C740A0; // (target, colour, depth, stencil, 0)
// (target, width, height, depth): resizes every image (layers 1), then sets each colour's format (+0x80, a
// dword per colour, from image +0x128) and view (+0xA0, a qword per colour). Attach does that for colour 0
// only, so a target with more colours needs it after they are set.
constexpr std::uint32_t kTargetResize = 0x1C743C0;
// (image, width, height, depth, layers): purges and allocates again with the image's own options when the
// size differs, else returns false; with image +0xE0 set it only stores the size. 0x1C4B0C0 next to it only
// compares the size.
constexpr std::uint32_t kImageResize = 0x1C4AE30;
constexpr std::size_t kImageSizeOnly = 0xE0;
// (manager, name, options): finds the image by its name (0x1D69030); one it finds is purged (0x1C3D928) and
// allocated again with the options, else a new image is made (0x1C3D8E9). A name of 0x100 characters or more
// stops the game (0x1C3D8DB).
constexpr std::uint32_t kScratchImage = 0x1C3D890;
constexpr std::uint32_t kImageManager = 0x5BF13C8;
// () -> the resource manager (0x667F450), its image table at +8; (table, name, 0) -> the image of that name
// or null: ScratchImage's own lookup (0x1C3D8B7..0x1C3D8C6).
constexpr std::uint32_t kResourceManager = 0x1C3B860;
constexpr std::uint32_t kFindImage = 0x1D69030;
constexpr std::size_t kImageTable = 8;
// (image): its GPU image and views onto the per-frame garbage lists (0x1C4A910), destroyed three frame begins
// later; the image itself stays, without memory.
constexpr std::uint32_t kPurgeImage = 0x1C3C540;
constexpr std::size_t kImageFlags = 0x9C;
constexpr std::uint32_t kImageSet = 0x100; // flags: a set of images, not one VkImage

using AllocateFn = void* (*)(std::size_t size, int tag);
using TargetCtorFn = void* (*)(void* target);
using TargetAttachFn = void (*)(void* target, void* color, void* depth, void* stencil, int flags);
using TargetResizeFn = void (*)(void* target, int width, int height, int depth);
using ImageResizeFn = bool (*)(void* image, int width, int height, int depth, int layers);
using ScratchImageFn = void* (*)(void* manager, const char* name, const void* options);
using ResourceManagerFn = std::byte* (*)();
using FindImageFn = void* (*)(void* table, const char* name, bool flag);
using PurgeImageFn = void (*)(void* image);

template <typename T>
T at(std::uint32_t rva) {
    return reinterpret_cast<T>(const_cast<std::byte*>(base() + rva));
}

bool plausibleSize(std::int32_t w, std::int32_t h) {
    return w > 0 && h > 0 && w <= 16384 && h <= 16384;
}

// The engine's image of that name, or 0, as ScratchImage looks it up.
std::uintptr_t findImage(const std::string& name) {
    std::byte* manager = at<ResourceManagerFn>(kResourceManager)();
    void* table = manager ? read<void*>(reinterpret_cast<std::uintptr_t>(manager) + kImageTable) : nullptr;
    return table ? reinterpret_cast<std::uintptr_t>(at<FindImageFn>(kFindImage)(table, name.c_str(), false))
                 : 0;
}

// The same options but the size (width, height, depth at +0xC..+0x17, layers at +0x1C).
bool sameOptionsButSize(const std::byte* a, const std::byte* b) {
    return std::memcmp(a, b, 0xC) == 0 && std::memcmp(a + 0x18, b + 0x18, 4) == 0 &&
           std::memcmp(a + 0x20, b + 0x20, kImageOptionsSize - 0x20) == 0;
}

// The engine's builds and its resize run on its render thread; the lock only keeps the lists whole.
std::mutex g_namesMutex;
std::vector<std::string> g_lastNames; // the last build's image clone names (stable names)
std::vector<std::string> g_waiting;   // clones left allocated for releaseClones

bool allocated(std::uintptr_t image) {
    return image && read<std::uintptr_t>(image + kImageStates) != 0;
}

void wait(const std::string& name) {
    std::lock_guard lock(g_namesMutex);
    if (std::find(g_waiting.begin(), g_waiting.end(), name) == g_waiting.end()) {
        g_waiting.push_back(name);
    }
}

// Colours 1..7 of a clone: their formats and views, set by the engine's resize at the images' own size
// (which leaves the images as they are). It resizes with one layer, so a target whose images differ in size
// or have more layers keeps them unset, and says so.
void setColorViews(std::byte* clone,
                   const std::array<std::uintptr_t, kColorCount>& colors,
                   std::uintptr_t depth,
                   std::uintptr_t stencil) {
    const std::uintptr_t first = colors[0] ? colors[0] : depth;
    if (!first) {
        return;
    }
    const auto w = read<std::int32_t>(first + kImageWidth);
    const auto h = read<std::int32_t>(first + kImageHeight);
    const auto d = read<std::int32_t>(first + kImageDepth);
    auto same = [&](std::uintptr_t image) {
        return !image || (read<std::int32_t>(image + kImageWidth) == w &&
                          read<std::int32_t>(image + kImageHeight) == h &&
                          read<std::int32_t>(image + kImageDepth) == d &&
                          read<std::int32_t>(image + kImageLayers) <= 1);
    };
    bool ok = same(depth) && same(stencil);
    for (const std::uintptr_t c : colors) {
        ok = ok && same(c);
    }
    if (!ok) {
        EVR_LOG("%s: target %p has colours of different sizes or layers; colours 1-7 left without views",
                kTag, static_cast<void*>(clone));
        return;
    }
    at<TargetResizeFn>(kTargetResize)(clone, w, h, d);
}

} // namespace

Kind kindOf(std::uintptr_t p) {
    if (p < 0x10000) {
        return Kind::None;
    }
    __try {
        if (plausibleSize(read<std::int32_t>(p), read<std::int32_t>(p + 4))) {
            const auto color = read<std::uintptr_t>(p + kTargetColors);
            const auto depth = read<std::uintptr_t>(p + kTargetDepth);
            const auto image = color ? color : depth;
            if (image > 0x10000 && plausibleSize(read<std::int32_t>(image + kImageWidth),
                                                 read<std::int32_t>(image + kImageHeight))) {
                return Kind::Target;
            }
        }
        if (plausibleSize(read<std::int32_t>(p + kImageWidth), read<std::int32_t>(p + kImageHeight))) {
            return Kind::Image;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return Kind::None;
}

void sizeOf(std::uintptr_t p, Kind kind, std::int32_t& w, std::int32_t& h) {
    const std::size_t wo = kind == Kind::Image ? kImageWidth : 0;
    const std::size_t ho = kind == Kind::Image ? kImageHeight : 4;
    w = read<std::int32_t>(p + wo);
    h = read<std::int32_t>(p + ho);
}

std::uint64_t objectBytes(std::uintptr_t object, std::string& name) {
    name.clear();
    const Kind kind = kindOf(object);
    if (kind == Kind::Image) {
        name = engineImageName(object);
        return engineImageBytes(object);
    }
    std::uint64_t total = 0;
    // Colours, the images held past them, depth and stencil.
    for (std::size_t a = kTargetColors; kind == Kind::Target && a <= kTargetStencil; a += 8) {
        const auto image = read<std::uintptr_t>(object + a);
        if (kindOf(image) == Kind::Image) {
            name = name.empty() ? engineImageName(image) : name;
            total += engineImageBytes(image);
        }
    }
    return total;
}

std::string fieldText(const char* owner, std::size_t offset) {
    char text[32];
    std::snprintf(text, sizeof(text), "%s0x%zX", owner, offset);
    return text;
}

const char* cloneGone(const Clone& clone) {
    const std::uintptr_t image = clone.image;
    __try {
        if (clone.states && !read<std::uintptr_t>(image + kImageStates)) {
            return "its state block is gone";
        }
        if (read<std::int32_t>(image + kImageWidth) != clone.width ||
            read<std::int32_t>(image + kImageHeight) != clone.height) {
            return "its size changed";
        }
        const auto chars = read<const char*>(image + kImageName);
        return chars && std::strcmp(chars, clone.name.c_str()) == 0 ? nullptr : "its name changed";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "its memory is unreadable";
    }
}

std::uintptr_t Builder::mapped(std::uintptr_t engine) const {
    for (const auto& [e, c] : entries) {
        if (e == engine) {
            return c;
        }
    }
    return 0;
}

void Builder::noteShared(const std::string& where, std::uintptr_t object) {
    if (!object) {
        return;
    }
    std::string name;
    const std::uint64_t bytes = objectBytes(object, name);
    shared += (shared.empty() ? "'" : ", '") + name + "' (" + where + ") " + clone_census::megabytes(bytes);
    ++sharedCount;
    sharedBytes += bytes;
    const std::size_t at = leftOut.size();
    Clone record;
    record.image = object;
    record.target = kindOf(object) == Kind::Target;
    record.source = where;
    record.engineName = name;
    record.bytes = bytes;
    leftOut.push_back(std::move(record));
    // A target's images, which view 1's passes may bind or mark on their own.
    for (std::size_t a = kTargetColors; leftOut[at].target && a <= kTargetStencil; a += 8) {
        const auto image = read<std::uintptr_t>(object + a);
        if (kindOf(image) == Kind::Image) {
            Clone part;
            part.image = image;
            part.source = where + fieldText(".", a);
            part.engineName = engineImageName(image);
            part.bytes = engineImageBytes(image);
            leftOut[at].parts.push_back(static_cast<std::uint32_t>(leftOut.size()));
            leftOut.push_back(std::move(part));
        }
    }
}

std::uintptr_t Builder::reuse(const std::string& name, std::uintptr_t image, const std::byte* options) {
    // One VkImage with its memory, without dynamic-resolution level sizes, with the source's options and
    // size.
    const std::uintptr_t found = findImage(name);
    if (!allocated(found) || (std::to_integer<std::uint8_t>(options[kOptionsFlags]) & kOptionsLevelSizes) ||
        read<std::uint8_t>(found + kImageSizeOnly) != 0 ||
        (read<std::uint32_t>(found + kImageFlags) & kImageSet) != 0 ||
        !sameOptionsButSize(reinterpret_cast<const std::byte*>(found + kImageOptions), options) ||
        std::memcmp(reinterpret_cast<const void*>(found + kImageWidth),
                    reinterpret_cast<const void*>(image + kImageWidth), 12) != 0 ||
        read<std::int32_t>(found + kImageLayers) != read<std::int32_t>(image + kImageLayers)) {
        return 0;
    }
    ++kept;
    return found;
}

std::string Builder::freeName(const std::string& name) {
    if (!allocated(findImage(name))) {
        return name;
    }
    // Purging it here could free its state block under a job of the frame before.
    wait(name);
    ++beside;
    std::string other = clone_names::besideName(name);
    if (allocated(findImage(other))) {
        wait(other);
        other = clone_names::spareName(build, images);
    }
    names.push_back(other);
    return other;
}

int queueOrphans(const Builder& b) {
    std::lock_guard lock(g_namesMutex);
    for (const std::string& name : clone_names::dropped(g_lastNames, b.names)) {
        if (std::find(g_waiting.begin(), g_waiting.end(), name) == g_waiting.end()) {
            g_waiting.push_back(name);
        }
    }
    g_lastNames = b.names;
    return static_cast<int>(g_waiting.size());
}

int releaseClones(const Map* map, std::string& waiting) {
    std::lock_guard lock(g_namesMutex);
    int purged = 0;
    auto purge = [&purged](const std::string& name) {
        const std::uintptr_t found = clone_names::ours(name) ? findImage(name) : 0;
        if (!allocated(found)) {
            return false;
        }
        at<PurgeImageFn>(kPurgeImage)(reinterpret_cast<void*>(found));
        ++purged;
        return true;
    };
    for (const std::string& name : g_waiting) {
        if (purge(name)) {
            waiting += (waiting.empty() ? "" : ", ") + name;
        }
    }
    g_waiting.clear();
    for (std::size_t i = 0; map && i < map->clones.size(); ++i) {
        if (!map->clones[i].target) {
            purge(map->clones[i].name);
        }
    }
    return purged;
}

std::uint32_t Builder::indexOf(std::uintptr_t clone) const {
    for (std::size_t i = 0; i < clones.size(); ++i) {
        if (clones[i].image == clone) {
            return static_cast<std::uint32_t>(i);
        }
    }
    return kNoClone;
}

std::uintptr_t Builder::cloneImage(std::uintptr_t image, const std::string& part) {
    if (!image) {
        return 0;
    }
    if (const std::uintptr_t c = mapped(image)) {
        return c;
    }
    if (std::find(sharedImages.begin(), sharedImages.end(), image) != sharedImages.end()) {
        return image;
    }
    alignas(16) std::byte options[kOptionsSize] = {};
    std::memcpy(options, reinterpret_cast<const void*>(image + kImageOptions), kImageOptionsSize);
    if (std::to_integer<std::uint8_t>(options[kOptionsFlags]) & kOptionsLevelSizes) {
        const auto levels = read<std::uintptr_t>(image + kImageLevels);
        if (levels) {
            std::memcpy(options + kImageOptionsSize, reinterpret_cast<const void*>(levels + 4),
                        kOptionsSize - kImageOptionsSize);
        } else {
            options[kOptionsFlags] &= std::byte{static_cast<std::uint8_t>(~kOptionsLevelSizes)};
        }
    }
    std::string name;
    if (stableNames) {
        name = clone_names::stableName(engineImageName(image), source + part, names);
    } else {
        char text[48];
        std::snprintf(text, sizeof(text), "_evrView1_%d_%d", build, images);
        name = text;
    }
    std::uintptr_t c = stableNames ? reuse(name, image, options) : 0;
    if (!c && stableNames && (c = reuse(clone_names::besideName(name), image, options)) != 0) {
        // Made beside by an earlier build: kept; the one of its name waits for releaseClones.
        if (allocated(findImage(name))) {
            wait(name);
        }
        name = clone_names::besideName(name);
        names.push_back(name);
    }
    if (!c) {
        // An image of that name without memory is allocated again (its purge frees nothing).
        name = stableNames ? freeName(name) : name;
        const bool again = stableNames && findImage(name) != 0;
        void* manager = read<void*>(baseAddress() + kImageManager);
        void* clone = manager ? at<ScratchImageFn>(kScratchImage)(manager, name.c_str(), options) : nullptr;
        if (!clone) {
            ++failed;
            return 0;
        }
        const auto w = read<std::int32_t>(image + kImageWidth);
        const auto h = read<std::int32_t>(image + kImageHeight);
        at<ImageResizeFn>(kImageResize)(clone, w, h, read<std::int32_t>(image + kImageDepth),
                                        read<std::int32_t>(image + kImageLayers));
        c = reinterpret_cast<std::uintptr_t>(clone);
        ++(again ? remade : made);
    }
    // The name as the engine keeps it (it lower-cases image names).
    const auto engineKept = read<const char*>(c + kImageName);
    Clone record;
    record.image = c;
    record.name = engineKept ? std::string(engineKept) : name;
    record.width = read<std::int32_t>(c + kImageWidth);
    record.height = read<std::int32_t>(c + kImageHeight);
    record.states = read<std::uintptr_t>(c + kImageStates) != 0;
    record.group = group;
    record.source = source + part;
    describeImage(record, image);
    clones.push_back(std::move(record));
    entries.emplace_back(image, c);
    ++images;
    return c;
}

std::uintptr_t Builder::cloneTarget(std::uintptr_t target) {
    if (const std::uintptr_t c = mapped(target)) {
        return c;
    }
    std::array<std::uintptr_t, kColorCount> colors{};
    for (int k = 0; k < kColorCount; ++k) {
        colors[k] =
            cloneImage(read<std::uintptr_t>(target + kTargetColors + 8 * k), ".c" + std::to_string(k));
    }
    const std::uintptr_t depth = cloneImage(read<std::uintptr_t>(target + kTargetDepth), ".d");
    const std::uintptr_t stencil = cloneImage(read<std::uintptr_t>(target + kTargetStencil), ".s");
    void* memory = at<AllocateFn>(kAllocate)(kTargetSize, kTargetTag);
    if (!memory) {
        ++failed;
        return 0;
    }
    auto* clone = static_cast<std::byte*>(at<TargetCtorFn>(kTargetCtor)(memory));
    at<TargetAttachFn>(kTargetAttach)(clone, reinterpret_cast<void*>(colors[0]),
                                      reinterpret_cast<void*>(depth), reinterpret_cast<void*>(stencil), 0);
    bool more = false;
    for (int k = 1; k < kColorCount; ++k) {
        std::memcpy(clone + kTargetColors + 8 * k, &colors[k], sizeof(colors[k]));
        more = more || colors[k] != 0;
    }
    if (more) {
        setColorViews(clone, colors, depth, stencil);
    }
    Clone record;
    auto addPart = [&](std::uintptr_t image) {
        const std::uint32_t i = image ? indexOf(image) : kNoClone;
        if (i != kNoClone) {
            record.parts.push_back(i);
        }
    };
    for (const std::uintptr_t image : colors) {
        addPart(image);
    }
    addPart(depth);
    addPart(stencil);
    // Images held past the colours (the depth pyramid's target keeps its image at +0x60).
    for (std::size_t a = kTargetColors + 8 * kColorCount; a < kTargetDepth; a += 8) {
        const auto v = read<std::uintptr_t>(target + a);
        if (const std::uintptr_t ci = kindOf(v) == Kind::Image ? cloneImage(v, fieldText(".", a)) : 0) {
            std::memcpy(clone + a, &ci, sizeof(ci));
            addPart(ci);
        }
    }
    std::memcpy(clone, reinterpret_cast<const void*>(target), 16); // sizes and their overrides
    const auto c = reinterpret_cast<std::uintptr_t>(clone);
    entries.emplace_back(target, c);
    ++targets;
    record.image = c;
    record.target = true;
    record.group = group;
    record.source = source;
    record.width = read<std::int32_t>(c);
    record.height = read<std::int32_t>(c + 4);
    record.engineName = record.parts.empty() ? "" : clones[record.parts.front()].engineName;
    clones.push_back(std::move(record));
    return c;
}

} // namespace evr::vkcore::view_clone
