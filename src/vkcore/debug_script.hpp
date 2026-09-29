#pragma once

// The schedule of ETERNALVR_DEBUG_COMMANDS (debug_commands.hpp), parsed without the game so it is
// unit-tested.
//
// Format: steps separated by '|', each "<seconds>:<command>[;<command>...]". Seconds count from the moment
// the player is in the map (head aim on) and may have a fraction. Commands are trimmed; empty ones and empty
// steps are dropped. A command the multiplayer policy refuses on a command line (mp_policy.hpp: connect,
// join, lobby, BATTLEMODE) or a map / devmap to a map that is not single-player makes the whole script
// invalid. Example:
//   "2:ai_Show|3:nextActiveAI;ai_teleportToPlayer trace"

#include <string>
#include <string_view>
#include <vector>

namespace evr::vkcore {

struct DebugStep {
    double seconds = 0.0;
    std::vector<std::string> commands;
};

struct DebugScript {
    std::vector<DebugStep> steps; // in the order given, which must not go back in time
    std::string error;            // empty when the whole text parsed
};

// A step without ':', with a time that is not a finite non-negative number, earlier than the step before it,
// without a command or with a refused command makes the whole script invalid: `error` says which step, and
// `steps` is empty.
DebugScript parseDebugScript(std::string_view text);

} // namespace evr::vkcore
