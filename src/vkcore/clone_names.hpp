#pragma once

// Parallel Eye Rendering: the names of view 1's image clones (view_clone_make.cpp). A clone keeps its name
// from build to build, so a rebuild finds it by name and resizes it in place or allocates it again, instead
// of making a new image beside it (the old one stayed until the next map load freed it). Pure logic,
// unit-tested (tests/vkcore/clone_names_tests.cpp).

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace evr::vkcore::clone_names {

inline constexpr std::string_view kPrefix = "_evrview1_";
// A clone made beside the one of its name, which is still allocated at another size (purged at the next
// resize), and a last resort when that one is too: prefixes stableName never gives (it puts _ after
// "_evrview1").
inline constexpr std::string_view kBesidePrefix = "_evrview1b_";
inline constexpr std::string_view kSparePrefix = "_evrview1x_";
// ScratchImage stops the game at a fatal error for a name of 256 characters or more (0x1C3D8DB); the suffix
// of a repeated name fits within the margin.
inline constexpr std::size_t kMaxLength = 200;
inline constexpr std::size_t kSuffixRoom = 8;

// Lower case, every character but a-z, 0-9 and _ made _ (the engine keeps image names lower-cased).
inline std::string plain(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 'A' && u <= 'Z') {
            c = static_cast<char>(u - 'A' + 'a');
        } else if (!((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '_')) {
            c = '_';
        }
    }
    return out;
}

// The stable name of the clone of the engine image `engineName`, found at `source` ("dc1+0x2F8"): the prefix
// and the plain engine name, or the plain source when the engine name is empty or too long; the second clone
// of the same name in one build gets "_2", the third "_3"... `used` holds the names given in this build so
// far and gets this one.
inline std::string
stableName(std::string_view engineName, std::string_view source, std::vector<std::string>& used) {
    std::string base = std::string(kPrefix) + plain(engineName);
    if (engineName.empty() || base.size() > kMaxLength - kSuffixRoom) {
        base = std::string(kPrefix) + plain(source);
    }
    if (base.size() > kMaxLength - kSuffixRoom) {
        base.resize(kMaxLength - kSuffixRoom);
    }
    std::string name = base;
    for (int n = 2; std::find(used.begin(), used.end(), name) != used.end(); ++n) {
        name = base + "_" + std::to_string(n);
    }
    used.push_back(name);
    return name;
}

// The name of the clone made beside the one named `stable` (a stableName).
inline std::string besideName(std::string_view stable) {
    const std::string_view rest =
        stable.substr(0, kPrefix.size()) == kPrefix ? stable.substr(kPrefix.size()) : stable;
    return std::string(kBesidePrefix) + std::string(rest);
}

// A name for the `n`th image clone of build `build` that no other name takes.
inline std::string spareName(int build, int n) {
    return std::string(kSparePrefix) + std::to_string(build) + "_" + std::to_string(n);
}

// One of the clones' names (any of the three kinds, and the per-build names of
// ETERNALVR_TEST_VIEW_CLONE_NAMES=build once lower-cased): the only images the clones may purge.
inline bool ours(std::string_view name) {
    constexpr std::string_view kStem = "_evrview1";
    return name.size() > kStem.size() + 1 && name.substr(0, kStem.size()) == kStem;
}

// The names of `before` that are not in `now`, in before's order: clones whose engine image is gone.
inline std::vector<std::string> dropped(const std::vector<std::string>& before,
                                        const std::vector<std::string>& now) {
    std::vector<std::string> out;
    for (const std::string& name : before) {
        if (std::find(now.begin(), now.end(), name) == now.end()) {
            out.push_back(name);
        }
    }
    return out;
}

} // namespace evr::vkcore::clone_names
