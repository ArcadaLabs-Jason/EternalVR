// The DOOM Eternal builds EternalVR knows (game_build.hpp).

#include "vkcore/game_build.hpp"

#include <windows.h>

#include <array>
#include <cstddef>

namespace evr::vkcore {

namespace {

// Game Pass values: the Steam functions located in the Game Pass image by masked byte signatures, and the
// vtables found as the tables holding those functions in the same order (analysis/gamepass).
constexpr std::array<GameBuild, 2> kBuilds = {{
    {GameBuildKind::Steam, "Steam build 25216728", 0x6A7B9B8C, 0x2DB5698, 0x2E73048, 0x1C74E08},
    {GameBuildKind::GamePass, "Game Pass 1.0.56.0", 0x69BC663D, 0x2E54218, 0x2F116C8, 0x1D01398},
}};

} // namespace

const GameBuild* findGameBuild(std::uint32_t timestamp) {
    for (const GameBuild& build : kBuilds) {
        if (build.timestamp == timestamp) {
            return &build;
        }
    }
    return nullptr;
}

const GameBuild* currentGameBuild() {
    static const GameBuild* const build = [] {
        const auto* module = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
        return findGameBuild(nt->FileHeader.TimeDateStamp);
    }();
    return build;
}

} // namespace evr::vkcore
