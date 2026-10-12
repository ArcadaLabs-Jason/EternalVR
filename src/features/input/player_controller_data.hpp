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

// Takes out of `data` the keys a later version may write and this one does not know (BindingIssue::skipped,
// T-127): a control map's entry whose key has the later-version shape (isLaterBindingKey; erased from the
// map), and a [profile] input or [labels] name it does not know (already left out by parseControllerData;
// its issue is taken). Returns one issue per entry, its message naming the section and the entry. What is
// left is checked as before (ok(), controlMapIssues): a file with no other issue is used without these
// entries, one with any other issue is not used. A file with only known keys is unchanged. A map of
// nothing but such keys is left as it is: compiling it reports that no binding is left, so the file is not
// used.
std::vector<BindingIssue> takeSkippedEntries(ControllerData& data);

// A player's controller data file read the way the layer reads it (vkcore readPlayerData).
struct UsablePlayerData {
    // Without the skipped entries.
    ControllerData data;
    // The entries left out (takeSkippedEntries), worth a line each when the file is used.
    std::vector<BindingIssue> skipped;
    // What stops the file being used: the file's own issues, or, when it has none, its control maps'
    // (controlMapIssues, line 0). Empty: the file is used.
    std::vector<BindingIssue> problems;

    [[nodiscard]] bool usable() const { return problems.empty(); }
};

// Parses `text`, takes out the entries a later version may write (takeSkippedEntries), and checks the rest.
UsablePlayerData usablePlayerData(std::string_view text);

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
