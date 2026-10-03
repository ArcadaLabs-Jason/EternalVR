#pragma once

// Console commands on a schedule, for the test rig (docs/rig-findings/debug-commands.md; Steam build
// 25216728). ETERNALVR_DEBUG_COMMANDS holds the schedule (debug_script.hpp), e.g.
//   ETERNALVR_DEBUG_COMMANDS=2:ai_Show|3:nextActiveAI;ai_teleportToPlayer trace
// Seconds count from markPlayerInMap(), which the head aim calls once it has verified the player's state
// ("aim: head aim on"): the map and its entities exist by then, however long the launch took. Nothing runs
// before it. Unset (the default), nothing is located or hooked.
//
// The retail console refuses most developer commands ("Not allowed"): idCmdSystemLocal keeps a per-thread
// restriction level (vtable +0x08 reads it, +0x10 sets it) and refuses a command without the allowed flag
// while it is non-zero. The hook sits at the start of ExecuteCommandBuffer (vtable +0x58), which the game's
// main loop calls every frame; there the due commands run through ExecuteCommandText (+0x40) with the
// thread's restriction at 0, restored right after. The command system is idCmdSystemLocal, reached through
// the global the engine reads just after naming "cmdSystem->ExecuteCommandText".
//
// Only while the multiplayer guard is armed; anything missing leaves the game untouched and logs why. Only
// the test commands of the parser's allow-list run (debug_script.hpp); each one left out is logged once.

namespace evr::vkcore {

// Locates and installs the hook once per process when ETERNALVR_DEBUG_COMMANDS is set; later calls return
// the first result.
bool installDebugCommands();

// The player is in a map: the schedule's clock starts at the first call. Cheap; any thread.
void markPlayerInMap();

// Seconds on the schedule's clock (since markPlayerInMap), or a negative value before it. Any thread.
double secondsInMap();

} // namespace evr::vkcore
