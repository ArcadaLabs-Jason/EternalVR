#pragma once

// The builds the light gather's hooks were read on (light_cull.hpp), told apart by the game module's PE
// timestamp. Game Pass 1.0.56.0 has the same gather (RVA 0x1D04850, Steam 0x1C782C0): its 0xA81 bytes match
// Steam's with call and RIP-relative operands masked, and so do the functions it calls (the visible-light
// set, the occlusion buffer, the sphere and frustum tests). So both rows have the same signature, offsets and
// bytes; only the anchor's RVA differs (docs/rig-findings/umbra-lights.md section 4).

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>

namespace evr::vkcore::light_cull {

// Code checked byte for byte before anything is hooked, by offset from the anchor.
struct ByteCheck {
    std::size_t offset;
    const char* what;
    std::size_t size;
    std::uint8_t bytes[8];
};

// Where the hooks go and resume, by offset from the anchor.
struct GatherOffsets {
    std::size_t gateA;       // test al, al; jne keep (after the visible-light-set lookup)
    std::size_t gateB;       // test eax, eax; je drop (after the occlusion-buffer test)
    std::size_t sphere;      // test al, al; je drop (after the sphere test of a kept light)
    std::size_t gateBDrop;   // gate B's drop: rbx back, the light's done bit cleared
    std::size_t skipCheck;   // cmp byte [rax+2Bh], 0: the frustum path's r_skipLightCPUCulling
    std::size_t frustum;     // the frustum test, past that check
    std::size_t verdict;     // test al, al; jne drop (the frustum test's verdict)
    std::size_t keep;        // mov r10, [rsp+38h]: the light goes on into the view's list
    std::size_t frustumDrop; // the drop the frustum test jumps to (rbx back, the light's done bit left set)
};

struct Build {
    const char* name;
    std::uint32_t timestamp; // PE TimeDateStamp of DOOMEternalx64vk.exe
    std::uint32_t anchorRva; // where the signature is (the gather's Umbra test)
    const char* signature;
    GatherOffsets at;
    std::span<const ByteCheck> checks;
};

// The anchor: mov rax, [rdi+18h]; cmp byte [rax+24h], 0 (useUmbra); je +0x43F (the Umbra-off frustum path);
// mov edx, [rbx+54h] (the light's Umbra object); test edx, edx; je ...
inline constexpr const char* kSignature = "48 8B 47 18 80 78 24 00 0F 84 3F 04 00 00 8B 53 54 85 D2 0F 84";

inline constexpr GatherOffsets kOffsets = {0x23, 0x2C9, 0x432, 0x43C, 0x44D, 0x453, 0x4A5, 0x4AD, 0x2B};

// Read on Steam build 25216728 and compared on Game Pass 1.0.56.0: the same bytes on both.
inline constexpr ByteCheck kChecks[] = {
    {0x23, "gate A", 8, {0x84, 0xC0, 0x0F, 0x85, 0xA6, 0x02, 0x00, 0x00}},
    {0x2C9, "gate B", 8, {0x85, 0xC0, 0x0F, 0x84, 0x6B, 0x01, 0x00, 0x00}},
    {0x432, "sphere test", 8, {0x84, 0xC0, 0x0F, 0x84, 0xF1, 0xFB, 0xFF, 0xFF}},
    {0x43C, "gate B drop", 8, {0x48, 0x8B, 0x5C, 0x24, 0x30, 0x40, 0xF6, 0xD6}},
    {0x44D, "frustum skip check", 4, {0x80, 0x78, 0x2B, 0x00}},
    {0x453, "frustum test", 8, {0x48, 0x8B, 0x4F, 0x20, 0x4C, 0x8D, 0x83, 0xF0}},
    {0x4A5, "frustum verdict", 8, {0x84, 0xC0, 0x0F, 0x85, 0x7E, 0xFB, 0xFF, 0xFF}},
    {0x4AD, "keep", 5, {0x4C, 0x8B, 0x54, 0x24, 0x38}},
    {0x2B, "frustum drop", 5, {0x48, 0x8B, 0x5C, 0x24, 0x30}},
};

inline constexpr Build kBuilds[] = {
    {"Steam build 25216728", 0x6A7B9B8C, 0x1C7851E, kSignature, kOffsets, kChecks},
    {"Game Pass 1.0.56.0", 0x69BC663D, 0x1D04AAE, kSignature, kOffsets, kChecks},
};

// The build with this PE timestamp, or null for one the gather was not read on.
constexpr const Build* findBuild(std::uint32_t timestamp) {
    for (const Build& build : kBuilds) {
        if (build.timestamp == timestamp) {
            return &build;
        }
    }
    return nullptr;
}

// The bytes a hook site's check must cover: a hook writes at least a 5-byte jump there.
inline constexpr std::size_t kHookCheckBytes = 8;

// The first offset `build` hooks or resumes at that no byte check starts at (for a hook site, one of at
// least kHookCheckBytes), or none when every one has one.
constexpr std::optional<std::size_t> uncheckedOffset(const Build& build) {
    const GatherOffsets& o = build.at;
    struct Use {
        std::size_t offset;
        std::size_t minBytes;
    };
    const Use used[] = {{o.gateA, kHookCheckBytes},
                        {o.gateB, kHookCheckBytes},
                        {o.sphere, kHookCheckBytes},
                        {o.gateBDrop, 1},
                        {o.skipCheck, 1},
                        {o.frustum, 1},
                        {o.verdict, kHookCheckBytes},
                        {o.keep, 1},
                        {o.frustumDrop, 1}};
    for (const Use& use : used) {
        bool checked = false;
        for (const ByteCheck& check : build.checks) {
            checked = checked || (check.offset == use.offset && check.size >= use.minBytes);
        }
        if (!checked) {
            return use.offset;
        }
    }
    return std::nullopt;
}

// True when every row has a byte check at every offset it uses.
constexpr bool everyRowChecked() {
    for (const Build& build : kBuilds) {
        if (uncheckedOffset(build).has_value()) {
            return false;
        }
    }
    return true;
}

// True when no two rows share a PE timestamp (findBuild returns the first match).
constexpr bool timestampsUnique() {
    for (std::size_t i = 0; i < std::size(kBuilds); ++i) {
        for (std::size_t j = i + 1; j < std::size(kBuilds); ++j) {
            if (kBuilds[i].timestamp == kBuilds[j].timestamp) {
                return false;
            }
        }
    }
    return true;
}

} // namespace evr::vkcore::light_cull
