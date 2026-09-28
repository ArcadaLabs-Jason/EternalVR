#pragma once

// A player's own controller data (ETERNALVR_CONTROLLER_DATA, docs/VR_CONTROLLERS.md): one edited copy of a
// built-in controller file, or a folder of them. Each file replaces the built-in data of the profile it
// names. In a folder, every "*.toml" directly inside it is read in name order, and when two files name one
// profile the later one wins. There is no file IO here: the layer lists the folder and reads the files.

#include "features/input/controller_bindings.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace evr::input {

// The names of a folder's controller data files, in the order they are read: those ending in ".toml" (any
// case), sorted by name ignoring case. `names` are the folder's file names (no subfolders).
std::vector<std::string> controllerDataFileOrder(std::vector<std::string> names);

// A file name inside `folder`, joined with a backslash unless the folder already ends in a separator.
std::string joinFolderPath(std::string_view folder, std::string_view name);

// Every issue of compiling the control maps of `data` (buildBindingProfile), each message starting with its
// section ("[map.right] ..."). A player's file with any is not used: a map with issues would leave the
// controllers sending nothing, and the built-in data always compiles.
std::vector<BindingIssue> controlMapIssues(const ControllerData& data);

// Where a player's file went.
struct PlayerDataPlacement {
    bool placed = false;  // false: it names no profile of `data`, and nothing changed
    std::string replaced; // the player's file it replaced (both named its profile), empty if none
};

// Puts `player` in place of the entry of `data` whose profile it names. `sources` holds, per entry of
// `data`, the player's file now in it (empty: the built-in data); it is sized to `data` on first use.
PlayerDataPlacement placePlayerData(std::span<ControllerData> data,
                                    std::vector<std::string>& sources,
                                    std::string_view file,
                                    ControllerData player);

} // namespace evr::input
