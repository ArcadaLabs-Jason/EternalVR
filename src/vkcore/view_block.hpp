#pragma once

// Room for more than one render view in the renderer's static per-view state block (view_block.cpp).

#include "vkcore/code_ranges.hpp"
#include "vkcore/game_code.hpp"

#include <vector>

namespace evr::vkcore {

// The renderer's per-view state block (one static entry in build 25216728) moves to new memory with `views`
// constructed entries, and every instruction that refers to it is repointed. Before the renderer starts
// (vkCreateInstance), in two steps so nothing changes until every step of the install has been checked:
// - preparePerViewBlock checks the block and its references and takes the new memory near the game module;
//   it adds the code ranges the move writes to `writes` (view_install.cpp makes them writable). False, with
//   nothing changed, when the block or its references are not exactly as known.
// - movePerViewBlock constructs the entries and repoints the references (the ranges writable); it cannot
//   fail. abandonPerViewBlock frees the memory of a prepared move that is not made.
bool preparePerViewBlock(const GameText& text, int views, std::vector<CodeRange>& writes);
void movePerViewBlock();
void abandonPerViewBlock();

} // namespace evr::vkcore
