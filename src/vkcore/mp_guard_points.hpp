#pragma once

// Internal to the multiplayer guard (mp_guard.hpp), shared by mp_guard.cpp (the latch, the hook callbacks,
// the poll and install()) and mp_guard_points.cpp (locating, validating and hooking each detection point,
// docs/rig-findings/mp-guard.md section 2). The points hold no state: install() hands each one the callback
// it hooks, and the main menu point the slot it fills.

#include "vkcore/game_text.hpp"
#include "vkcore/mid_hook.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace evr::vkcore::mp_guard {

constexpr const char* kTag = "mp guard";

// The main-menu screen ids (mainMenuElementID_t) were read from this build's type info.

// idMainMenu::menu and the idMenu screen fields (type info of the same build).
constexpr std::size_t kMainMenuSize = 0xD80;
constexpr std::size_t kMainMenuMenu = 0x70;
constexpr std::int32_t kMenuActiveScreen = 0x90;
constexpr std::int32_t kMenuNextScreen = 0x94;
constexpr std::int32_t kMenuTransition = 0x98;

namespace points {

// Each locates and validates its point in the game and hooks it with the callback(s) given; false (with
// the reason logged) when the point is not found, not what it should be, or not hooked.
bool installMapLoad(const GameImage& image, MidHookCallback onMapLoad);
bool installSteamJoin(const GameImage& image,
                      MidHookCallback onLobbyJoin,
                      MidHookCallback onRichPresenceJoin);
bool installXboxJoin(const GameImage& image, MidHookCallback onInviteEvent, MidHookCallback onInviteDecoded);
bool installInviteConsumed(const GameImage& image, MidHookCallback onInviteConsumed);
bool installLobbySession(const GameImage& image, MidHookCallback onLobbySession);
bool installBattleArenaSession(const GameImage& image, MidHookCallback onBattleArenaSession);
// Also stores the idMainMenu* global's address in `mainMenuSlot` before hooking the screen change.
bool installMainMenu(const GameImage& image,
                     MidHookCallback onMenuTransition,
                     std::atomic<const std::byte* const*>& mainMenuSlot);

} // namespace points

} // namespace evr::vkcore::mp_guard
