#pragma once

// Parallel Eye Rendering: the pure parts of the clone log (view_clone_census.cpp): the engine's texture
// formats (textureFormat_t, named by its table at .data 0x3ADE670 in build 25216728) with their bytes per
// texel, an image's size from its options, and the census summary (which clones view 1 never used, which
// turned up on other command contexts). Unit-tested (tests/vkcore/clone_census_tests.cpp).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace evr::vkcore::clone_census {

struct Format {
    std::string_view name; // the engine's, without FMT_
    std::uint32_t bytes;   // per texel; 0 for the block-compressed ones (not estimated)
};

// textureFormat_t 0..0x37; "?" (0 bytes) past it.
inline Format formatOf(std::uint32_t format) {
    static constexpr std::array<Format, 0x38> kFormats = {{
        {"NONE", 0},
        {"RGBA32F", 16},
        {"RGBA16F", 8},
        {"RGBA8", 4},
        {"ARGB8", 4},
        {"ALPHA", 1},
        {"L8A8_DEPRECATED", 2},
        {"RG8", 2},
        {"LUM8_DEPRECATED", 1},
        {"INT8_DEPRECATED", 1},
        {"BC1", 0},
        {"BC3", 0},
        {"DEPTH", 4},
        {"DEPTH_STENCIL", 8}, // 32-bit depth and 8-bit stencil, padded
        {"X32F", 4},
        {"Y16F_X16F", 4},
        {"X16", 2},
        {"Y16_X16", 4},
        {"RGB565", 2},
        {"R8", 1},
        {"R11FG11FB10F", 4},
        {"X16F", 2},
        {"BC6H_UF16", 0},
        {"BC7", 0},
        {"BC4", 0},
        {"BC5", 0},
        {"RG16F", 4},
        {"R10G10B10A2", 4},
        {"RG32F", 8},
        {"R32_UINT", 4},
        {"R16_UINT", 2},
        {"DEPTH16", 2},
        {"RGBA8_SRGB", 4},
        {"BC1_SRGB", 0},
        {"BC3_SRGB", 0},
        {"BC7_SRGB", 0},
        {"BC6H_SF16", 0},
        {"ASTC_4X4", 0},
        {"ASTC_4X4_SRGB", 0},
        {"ASTC_5X4", 0},
        {"ASTC_5X4_SRGB", 0},
        {"ASTC_5X5", 0},
        {"ASTC_5X5_SRGB", 0},
        {"ASTC_6X5", 0},
        {"ASTC_6X5_SRGB", 0},
        {"ASTC_6X6", 0},
        {"ASTC_6X6_SRGB", 0},
        {"ASTC_8X5", 0},
        {"ASTC_8X5_SRGB", 0},
        {"ASTC_8X6", 0},
        {"ASTC_8X6_SRGB", 0},
        {"ASTC_8X8", 0},
        {"ASTC_8X8_SRGB", 0},
        {"DEPTH32F", 4},
        {"BC1_ZERO_ALPHA", 0},
        {"R8_UINT", 1},
    }};
    return format < kFormats.size() ? kFormats[format] : Format{"?", 0};
}

// width x height x depth x layers x bytes per texel, summed over the mips (each half the one before in
// every dimension, at least 1). Sizes below 1 count as 1.
inline std::uint64_t imageBytes(std::int32_t width,
                                std::int32_t height,
                                std::int32_t depth,
                                std::int32_t layers,
                                std::int32_t mips,
                                std::uint32_t bytesPerTexel) {
    auto atLeast1 = [](std::int64_t v) {
        return v < 1 ? std::int64_t{1} : v;
    };
    std::uint64_t total = 0;
    const std::int32_t count = std::clamp(mips, 1, 16);
    for (std::int32_t m = 0; m < count; ++m) {
        total += static_cast<std::uint64_t>(atLeast1(std::int64_t{width} >> m) *
                                            atLeast1(std::int64_t{height} >> m) *
                                            atLeast1(std::int64_t{depth} >> m));
    }
    return total * static_cast<std::uint64_t>(atLeast1(layers)) * bytesPerTexel;
}

// "18.2 MB": MiB with one decimal, as the vram line counts them.
inline std::string megabytes(std::uint64_t bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

// What the census counts for each clone (the bind hooks, view_clone_binds.cpp, and view 1's screen pass,
// view_one_passes.cpp).
enum Use : std::uint8_t {
    kStored, // a per-view setup stored the engine's object into view 1's render context: the clone instead
    kTargetBinds,   // a pass of view 1 bound the engine's target: the clone instead
    kImageBinds,    // ... the engine's image (the depth pyramid's bind and the screen pass included)
    kMarks,         // ... marked the engine's image written: the clone instead
    kAsClone,       // view 1 stored, bound or marked the clone itself (it had it from an earlier store)
    kOtherContexts, // the clone bound or marked on a command context that is not view 1's
    kUses
};

struct Entry {
    std::string name; // the engine object's name
    std::uint64_t bytes = 0;
    bool target = false;
    std::vector<std::uint32_t> parts; // a target's images (indices of their entries)
    std::array<std::uint64_t, kUses> uses{};
};

// View 1 used it: stored, bound or marked it (the engine's object or the clone); an image also when a target
// that holds it was.
inline std::vector<bool> usedByView1(const std::vector<Entry>& entries) {
    std::vector<bool> used(entries.size(), false);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& u = entries[i].uses;
        used[i] = u[kStored] + u[kTargetBinds] + u[kImageBinds] + u[kMarks] + u[kAsClone] != 0;
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].target && used[i]) {
            for (const std::uint32_t p : entries[i].parts) {
                if (p < used.size()) {
                    used[p] = true;
                }
            }
        }
    }
    return used;
}

struct Summary {
    std::size_t count = 0;
    std::uint64_t bytes = 0;
    std::string names; // comma-separated, at most maxNames, then ", ..."
};

namespace detail {
inline void add(Summary& s, const Entry& e, std::size_t maxNames) {
    if (s.count < maxNames) {
        s.names += (s.count ? ", " : "") + (e.name.empty() ? std::string("?") : e.name);
    } else if (s.count == maxNames) {
        s.names += s.count ? ", ..." : "...";
    }
    ++s.count;
    s.bytes += e.bytes;
}
} // namespace detail

// The image clones view 1 never used (targets: unusedTargets).
inline Summary unusedImages(const std::vector<Entry>& entries, std::size_t maxNames) {
    const std::vector<bool> used = usedByView1(entries);
    Summary s;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].target && !used[i]) {
            detail::add(s, entries[i], maxNames);
        }
    }
    return s;
}

inline std::size_t unusedTargets(const std::vector<Entry>& entries) {
    const std::vector<bool> used = usedByView1(entries);
    std::size_t n = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        n += entries[i].target && !used[i] ? 1 : 0;
    }
    return n;
}

// The engine objects a build left out (ETERNALVR_TEST_VIEW_CLONE_SKIP) that view 1 stored, bound or marked
// all the same: a use the clones then did not separate from view 0's.
inline Summary touched(const std::vector<Entry>& entries, std::size_t maxNames) {
    Summary s;
    for (const Entry& e : entries) {
        if (e.uses[kStored] + e.uses[kTargetBinds] + e.uses[kImageBinds] + e.uses[kMarks] != 0) {
            detail::add(s, e, maxNames);
        }
    }
    return s;
}

// The clones (images and targets) seen on a command context that is not view 1's: cross-talk, view 0's work
// or another pass touching view 1's copy.
inline Summary otherContexts(const std::vector<Entry>& entries, std::size_t maxNames) {
    Summary s;
    for (const Entry& e : entries) {
        if (e.uses[kOtherContexts] != 0) {
            detail::add(s, e, maxNames);
        }
    }
    return s;
}

} // namespace evr::vkcore::clone_census
