#pragma once

// The DOOM Eternal builds EternalVR knows, told apart by the game module's PE timestamp, and the few
// addresses the layer cannot find by signature (vtables, one call site). Everything else is found by
// signature and works on either build.
//
// Steam: build 25216728. Game Pass (Microsoft Store): package 1.0.56.0, the same game code relinked, so its
// addresses differ (analysis/gamepass).

#include <cstdint>

namespace evr::vkcore {

enum class GameBuildKind : std::uint8_t { Steam, GamePass };

struct GameBuild {
    GameBuildKind kind;
    const char* name;
    std::uint32_t timestamp;          // PE TimeDateStamp of DOOMEternalx64vk.exe
    std::uint32_t playerVtable;       // idPlayer's vtable (RVA)
    std::uint32_t guiModelVtable;     // idRenderModelGui's vtable (RVA)
    std::uint32_t worldGuiCallReturn; // the world-surface callback's return from the GUI stamp check (RVA)
};

// The build with this PE timestamp, or null for one EternalVR does not know.
[[nodiscard]] const GameBuild* findGameBuild(std::uint32_t timestamp);

// The running game's build (the process's main module), or null. Read once.
[[nodiscard]] const GameBuild* currentGameBuild();

} // namespace evr::vkcore
