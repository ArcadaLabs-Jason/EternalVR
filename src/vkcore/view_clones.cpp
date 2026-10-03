#include "vkcore/view_clones.hpp"

#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/view_clone_map.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace evr::vkcore {

namespace view_clone {

namespace {

constexpr const char* kTag = "view-clones";

// ---- Build 25216728 (RVAs) ----

constexpr std::uint32_t kFinalTarget = 0x66E3208;

// The global region the engine's resize (0x1CD9D20) walks: render target objects and images.
constexpr std::uint32_t kRegionStart = 0x66E2F70;
constexpr std::uint32_t kRegionEnd = 0x66E3410;
// 0x66E3308..0x66E3360: the depth downsample chain (_viewdepthdownres*: 2x..32x max z, min z, reverse 16f)
// the depth pass's compute 0x1C73530 writes; shared, each view culled against the other's depth (tile-shaped
// holes in both eyes in e1m3, rig run c3d1). The shadow atlas (0x66E2E70, 8192x8192) is not cloned: view 1
// shades with view 0's (view_one_passes.cpp).
constexpr std::uint32_t kTargetSlots[] = {
    0x66E31F8, 0x66E3200, 0x66E3208, 0x66E3210, 0x66E3218, 0x66E3220, 0x66E3228, 0x66E3230, 0x66E3238,
    0x66E3240, 0x66E3248, 0x66E3250, 0x66E3258, 0x66E3260, 0x66E3268, 0x66E3270, 0x66E3278, 0x66E3280,
    0x66E3288, 0x66E3290, 0x66E3298, 0x66E32A0, 0x66E32A8, 0x66E32B0, 0x66E32B8, 0x66E32C0, 0x66E32C8,
    0x66E32D0, 0x66E32D8, 0x66E32E0, 0x66E32E8, 0x66E32F0, 0x66E32F8, 0x66E3300, 0x66E3308, 0x66E3310,
    0x66E3318, 0x66E3320, 0x66E3328, 0x66E3330, 0x66E3338, 0x66E3340, 0x66E3348, 0x66E3350, 0x66E3358,
    0x66E3360, 0x66E3368, 0x66E3370, 0x66E3378, 0x66E3400};
// 0x66E30E8 (256x256, 6 mips) and the device context's +0x338..+0x360 (kDcCloneFields) are written by both
// views' opaque passes in e1m3 (shared-writes report, rig run c3d5); not seen in e1m2's opening.
constexpr std::uint32_t kImageSlots[] = {
    0x66E2F78, 0x66E2F80, 0x66E2F88, 0x66E2F90, 0x66E2F98, 0x66E2FA0, 0x66E2FA8, 0x66E2FC8, 0x66E30A0,
    0x66E30A8, 0x66E30B0, 0x66E30D8, 0x66E30E0, 0x66E30F0, 0x66E30F8, 0x66E3100, 0x66E3108, 0x66E3110,
    0x66E3118, 0x66E3180, 0x66E3188, 0x66E3190, 0x66E3198, 0x66E31C0, 0x66E30E8};
// Device context fields: the scene target and its neighbours (targets or images, told apart at run time) and
// the depth image viewDepth.
constexpr std::size_t kContextFields[] = {0x230, 0x510, 0x518, 0x520, 0x528,
                                          0x530, 0x538, 0x540, 0x548, 0x5E8};

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
// (image, width, height, depth, layers): purges and allocates again when the size differs, else returns
// false. 0x1C4B0C0 next to it only compares the size.
constexpr std::uint32_t kImageResize = 0x1C4AE30;
constexpr std::uint32_t kScratchImage = 0x1C3D890; // (manager, name, options)
constexpr std::uint32_t kImageManager = 0x5BF13C8;

using AllocateFn = void* (*)(std::size_t size, int tag);
using TargetCtorFn = void* (*)(void* target);
using TargetAttachFn = void (*)(void* target, void* color, void* depth, void* stencil, int flags);
using ImageResizeFn = bool (*)(void* image, int width, int height, int depth, int layers);
using ScratchImageFn = void* (*)(void* manager, const char* name, const void* options);

// View 1's device context: a copy refreshed every frame, with the images below replaced by clones. The pass
// arguments (a store view_clone_binds.cpp maps) hand it to the render-view passes, e.g. 0x1C2A630 that builds
// the depth pyramid.
constexpr std::size_t kDcDepthPyramid = 0x2F8;
// Cloned for view 1's device context only: light scattering history (both views' opaque passes write them)
// and the depth pyramid.
constexpr std::size_t kDcCloneFields[] = {0x2D0, 0x2D8, 0x2E0, 0x2E8, kDcDepthPyramid, 0x338, 0x340,
                                          0x348, 0x350, 0x358, 0x360};
// Per-view history render targets: the accumulation buffers (+0x60..+0x80), ambient occlusion and depth of
// field accumulation (+0x550..+0x580), auto exposure and its luminance (+0x598..+0x5D8), the screen target.
constexpr std::size_t kDcCloneTargets[] = {0x060, 0x068, 0x070, 0x078, 0x080, 0x550,          0x558, 0x560,
                                           0x568, 0x570, 0x578, 0x580, 0x598, 0x5A0,          0x5A8, 0x5B0,
                                           0x5B8, 0x5C0, 0x5C8, 0x5D0, 0x5D8, kDcScreenTarget};
constexpr std::size_t kDcDepthPyramidViews = 0x300; // one per mip, from 0x1C48E30
constexpr int kDepthPyramidMips = 5;
constexpr std::uint32_t kImageMipView = 0x1C48E30; // (image, mip)
using ImageMipViewFn = void* (*)(void* image, int mip);

const std::byte* g_base = nullptr;
std::byte* g_dc1 = nullptr;             // view 1's device context
std::atomic<const Map*> g_map{nullptr}; // published once built

struct Source {
    std::uintptr_t at;    // where the engine keeps it
    std::uintptr_t value; // the object
    std::int32_t width;   // its size when the clones were made
    std::int32_t height;
};
std::vector<Source> g_sources; // render thread only
int g_builds = 0;
constexpr int kMaxBuilds = 8;
int g_remakes = 0; // builds after the engine freed a clone
constexpr int kMaxRemakes = 64;
std::atomic<bool> g_stopped{false};
std::uint64_t g_nextReport = 0;
bool g_on = false;
std::byte* g_shadow = nullptr; // the global region's shadow

template <typename T>
T at(std::uint32_t rva) {
    return reinterpret_cast<T>(const_cast<std::byte*>(g_base + rva));
}

bool plausibleSize(std::int32_t w, std::int32_t h) {
    return w > 0 && h > 0 && w <= 16384 && h <= 16384;
}

// Told apart under SEH: the pointers are the engine's.
enum class Kind { None, Target, Image };
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

// View 1's device context for this frame: the engine's, with the clones in place.
void refreshDc1(const Map& map) {
    const std::uintptr_t dc = deviceContext();
    if (!g_dc1 || !dc || map.dcFields.empty()) {
        return;
    }
    std::memcpy(g_dc1, reinterpret_cast<const void*>(dc), kDcSize);
    for (const auto& [offset, value] : map.dcFields) {
        std::memcpy(g_dc1 + offset, &value, sizeof(value));
    }
}

// ---- Building ----

struct Builder {
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> entries;
    std::vector<Clone> clones;
    int images = 0;
    int targets = 0;
    int failed = 0;

    std::uintptr_t mapped(std::uintptr_t engine) const {
        for (const auto& [e, c] : entries) {
            if (e == engine) {
                return c;
            }
        }
        return 0;
    }

    std::uintptr_t cloneImage(std::uintptr_t image) {
        if (!image) {
            return 0;
        }
        if (const std::uintptr_t c = mapped(image)) {
            return c;
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
        char name[48];
        std::snprintf(name, sizeof(name), "_evrView1_%d_%d", g_builds, images);
        void* manager = read<void*>(reinterpret_cast<std::uintptr_t>(g_base + kImageManager));
        void* clone = manager ? at<ScratchImageFn>(kScratchImage)(manager, name, options) : nullptr;
        if (!clone) {
            ++failed;
            return 0;
        }
        const auto w = read<std::int32_t>(image + kImageWidth);
        const auto h = read<std::int32_t>(image + kImageHeight);
        at<ImageResizeFn>(kImageResize)(clone, w, h, read<std::int32_t>(image + kImageDepth),
                                        read<std::int32_t>(image + kImageLayers));
        const auto c = reinterpret_cast<std::uintptr_t>(clone);
        // The name as the engine keeps it (it lower-cases image names).
        const auto kept = read<const char*>(c + kImageName);
        clones.push_back({c, kept ? kept : name, read<std::int32_t>(c + kImageWidth),
                          read<std::int32_t>(c + kImageHeight), read<std::uintptr_t>(c + kImageStates) != 0});
        entries.emplace_back(image, c);
        ++images;
        return c;
    }

    std::uintptr_t cloneTarget(std::uintptr_t target) {
        if (const std::uintptr_t c = mapped(target)) {
            return c;
        }
        std::array<std::uintptr_t, kColorCount> colors{};
        for (int k = 0; k < kColorCount; ++k) {
            colors[k] = cloneImage(read<std::uintptr_t>(target + kTargetColors + 8 * k));
        }
        const std::uintptr_t depth = cloneImage(read<std::uintptr_t>(target + kTargetDepth));
        const std::uintptr_t stencil = cloneImage(read<std::uintptr_t>(target + kTargetStencil));
        void* memory = at<AllocateFn>(kAllocate)(kTargetSize, kTargetTag);
        if (!memory) {
            ++failed;
            return 0;
        }
        auto* clone = static_cast<std::byte*>(at<TargetCtorFn>(kTargetCtor)(memory));
        at<TargetAttachFn>(kTargetAttach)(clone, reinterpret_cast<void*>(colors[0]),
                                          reinterpret_cast<void*>(depth), reinterpret_cast<void*>(stencil),
                                          0);
        for (int k = 1; k < kColorCount; ++k) {
            std::memcpy(clone + kTargetColors + 8 * k, &colors[k], sizeof(colors[k]));
        }
        // Images held past the colours (the depth pyramid's target keeps its image at +0x60).
        for (std::size_t a = kTargetColors + 8 * kColorCount; a < kTargetDepth; a += 8) {
            const auto v = read<std::uintptr_t>(target + a);
            if (const std::uintptr_t ci = kindOf(v) == Kind::Image ? cloneImage(v) : 0) {
                std::memcpy(clone + a, &ci, sizeof(ci));
            }
        }
        std::memcpy(clone, reinterpret_cast<const void*>(target), 16); // sizes and their overrides
        const auto c = reinterpret_cast<std::uintptr_t>(clone);
        entries.emplace_back(target, c);
        ++targets;
        return c;
    }

    void add(std::uintptr_t slotAddress, Kind expected) {
        const auto value = read<std::uintptr_t>(slotAddress);
        if (!value) {
            // Made later by the engine (dc+0x548 is null at the first build): watched, so its arrival
            // rebuilds the clones.
            g_sources.push_back({slotAddress, 0, 0, 0});
            return;
        }
        const Kind kind = kindOf(value);
        if (kind == Kind::None || (expected != Kind::None && kind != expected)) {
            if (g_builds == 0) {
                EVR_LOG("%s: slot %p holds %p, not a%s; left shared", kTag,
                        reinterpret_cast<void*>(slotAddress), reinterpret_cast<void*>(value),
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
            cloneTarget(value);
        } else {
            cloneImage(value);
        }
    }
};

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

// Under SEH: the engine may have freed the clone's memory. Null when the clone is still there, else why not.
const char* cloneGone(std::uintptr_t image, const char* name, std::int32_t w, std::int32_t h, bool states) {
    __try {
        if (states && !read<std::uintptr_t>(image + kImageStates)) {
            return "its state block is gone";
        }
        if (read<std::int32_t>(image + kImageWidth) != w || read<std::int32_t>(image + kImageHeight) != h) {
            return "its size changed";
        }
        const auto chars = read<const char*>(image + kImageName);
        return chars && std::strcmp(chars, name) == 0 ? nullptr : "its name changed";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "its memory is unreadable";
    }
}

// The first clone the engine has freed or reused (a map load frees unreferenced scratch images), or null.
const Clone* goneClone(const Map& map, const char*& why) {
    for (const Clone& c : map.clones) {
        if ((why = cloneGone(c.image, c.name.c_str(), c.width, c.height, c.states)) != nullptr) {
            return &c;
        }
    }
    return nullptr;
}

// View 1's device context: its own depth pyramid (with the pyramid's mip views) and light scattering history,
// and every field whose object has a clone.
void buildDc1(Builder& b, std::uintptr_t dc, Map& map) {
    for (const std::size_t f : kDcCloneFields) {
        const auto image = read<std::uintptr_t>(dc + f);
        if (kindOf(image) == Kind::Image) {
            b.cloneImage(image);
        }
    }
    for (const std::size_t f : kDcCloneTargets) {
        const auto target = read<std::uintptr_t>(dc + f);
        if (kindOf(target) == Kind::Target) {
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

bool build() {
    const std::uintptr_t dc = deviceContext();
    if (!dc) {
        return false;
    }
    g_sources.clear();
    Builder b;
    for (const std::uint32_t s : kImageSlots) {
        b.add(reinterpret_cast<std::uintptr_t>(g_base + s), Kind::Image);
    }
    for (const std::uint32_t s : kTargetSlots) {
        b.add(reinterpret_cast<std::uintptr_t>(g_base + s), Kind::Target);
    }
    for (const std::size_t f : kContextFields) {
        b.add(dc + f, Kind::None);
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
        buildDc1(b, dc, *map);
    }
    map->entries.insert(map->entries.end(), b.entries.begin(), b.entries.end());
    std::sort(map->entries.begin(), map->entries.end());
    map->clones = std::move(b.clones);
    const auto finalTarget = read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(g_base + kFinalTarget));
    if (const std::uintptr_t finalClone = finalTarget ? b.mapped(finalTarget) : 0) {
        map->finalImage = read<std::uintptr_t>(finalClone + kTargetColors);
    }
    EVR_LOG("%s: build %d: %d image(s), %d target(s) cloned, %d failed; %zu map entries, %zu device context "
            "field(s); view 1's final image %p, screen image %p",
            kTag, g_builds, b.images, b.targets, b.failed, map->entries.size(), map->dcFields.size(),
            reinterpret_cast<void*>(map->finalImage), reinterpret_cast<void*>(map->screenImage));
    refreshDc1(*map); // before the map hands it out
    g_map.store(map, std::memory_order_release);
    ++g_builds;
    return true;
}

} // namespace

const std::byte* base() {
    return g_base;
}

std::uintptr_t baseAddress() {
    return reinterpret_cast<std::uintptr_t>(g_base);
}

std::uintptr_t deviceContext() {
    return read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(g_base + kDeviceContext));
}

const Map* currentMap() {
    return g_map.load(std::memory_order_acquire);
}

std::uintptr_t lookup(const Map& map, std::uintptr_t engine) {
    const auto it =
        std::lower_bound(map.entries.begin(), map.entries.end(), std::make_pair(engine, std::uintptr_t{0}));
    return it != map.entries.end() && it->first == engine ? it->second : 0;
}

bool isView1Context(std::uintptr_t p, bool blocks) {
    if (!p) {
        return false;
    }
    const auto split = [](int c) {
        return c == 7 || c == 9 || c == 10;
    };
    const auto* table = reinterpret_cast<const std::uintptr_t*>(g_base + kCommandTable);
    for (int c = 0; c < kTableCategories; ++c) {
        for (int s = split(c) ? 2 : 1; s <= (split(c) ? 3 : 1); ++s) {
            const std::uintptr_t context = table[c * kTableSlots + s];
            if (context && (blocks ? read<std::uintptr_t>(context + kContextBlock) : context) == p) {
                return true;
            }
        }
    }
    return false;
}

bool partOn(parallel_eyes::ViewPart part) {
    return (parallelEyesSettings().off & part) == 0;
}

bool hookAt(std::uint32_t rva, MidHookEditCallback callback, const char* what) {
    std::string error;
    if (!installMidHookEdit(const_cast<std::byte*>(g_base + rva), callback, error)) {
        EVR_LOG("%s: %s hook at RVA 0x%X failed: %s", kTag, what, rva, error.c_str());
        return false;
    }
    return true;
}

bool watchAt(std::uint32_t rva, MidHookCallback callback, const char* what) {
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(g_base + rva), callback, error)) {
        EVR_LOG("%s: %s hook at RVA 0x%X failed: %s", kTag, what, rva, error.c_str());
        return false;
    }
    return true;
}

} // namespace view_clone

using namespace view_clone;

bool prepareViewClones(const std::byte* gameBase) {
    g_base = gameBase;
    if (!parallelEyesSettings().clones) {
        return true;
    }
    g_shadow = static_cast<std::byte*>(
        VirtualAlloc(nullptr, kRegionEnd - kRegionStart, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!g_shadow) {
        return false;
    }
    if (partOn(parallel_eyes::kDcCopy)) {
        g_dc1 =
            static_cast<std::byte*>(VirtualAlloc(nullptr, kDcSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!g_dc1) {
            return false;
        }
    }
    return checkBinds() && prepareViewOnePasses();
}

bool installViewClones() {
    if (!parallelEyesSettings().clones) {
        EVR_LOG("%s: off (ETERNALVR_TEST_VIEW_CLONES=0): view 1 shares the engine's targets", kTag);
        return true;
    }
    if (!installBinds() || !installViewOnePasses()) {
        return false;
    }
    g_on = true;
    EVR_LOG("%s: on: %zu image slots, %zu target slots, %zu device context fields%s", kTag,
            std::size(kImageSlots), std::size(kTargetSlots), std::size(kContextFields),
            g_dc1 ? ", view 1's device context" : "");
    return true;
}

bool viewClonesPrepare() {
    if (!g_on || g_stopped.load(std::memory_order_relaxed)) {
        return false;
    }
    const Map* map = g_map.load(std::memory_order_relaxed);
    const char* why = nullptr;
    if (const Clone* gone = map ? goneClone(*map, why) : nullptr) {
        // Not counted against kMaxBuilds: each map load frees them once.
        EVR_LOG("%s: clone '%s' %p was freed or reused by the engine (%s); making the clones again", kTag,
                gone->name.c_str(), reinterpret_cast<void*>(gone->image), why);
        g_map.store(nullptr, std::memory_order_release);
        if (++g_remakes > kMaxRemakes) {
            g_stopped.store(true);
            EVR_LOG("%s: %d remakes; clones off, view 0 alone from now on", kTag, kMaxRemakes);
            return false;
        }
        return build();
    }
    if (map && !sourcesChanged()) {
        refreshDc1(*map);
        const std::uint64_t now = GetTickCount64();
        if (now >= g_nextReport) {
            g_nextReport = now + 60000;
            reportBinds();
            reportPasses();
        }
        return false;
    }
    if (g_builds - g_remakes >= kMaxBuilds) {
        g_stopped.store(true);
        g_map.store(nullptr, std::memory_order_release);
        EVR_LOG("%s: %d builds and the engine's targets still change; clones off, view 0 alone from now on",
                kTag, g_builds);
        return false;
    }
    return build();
}

bool viewClonesStopped() {
    return g_stopped.load(std::memory_order_relaxed);
}

void* viewClonesFinalImage() {
    const Map* map = g_map.load(std::memory_order_acquire);
    return map ? reinterpret_cast<void*>(map->finalImage) : nullptr;
}

void* viewClonesScreenImage() {
    const Map* map = g_map.load(std::memory_order_acquire);
    return map ? reinterpret_cast<void*>(map->screenImage) : nullptr;
}

} // namespace evr::vkcore
