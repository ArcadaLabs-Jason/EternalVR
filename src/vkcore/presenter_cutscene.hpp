#pragma once

// Cutscenes in the camera hook (docs/VR_HEAD_TRACKED.md): their changes and the skip key's hold, the cuts'
// re-base in a cutscene shown around the player (xr_math/cutscene_cuts.hpp; the presenter's part is in
// presenter_cutscene.cpp), and the logs for the eyes rendered from one point inside cutscenes (public issue
// #24: both eyes showed no depth there).

#include "xr_math/cutscene_cuts.hpp"

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace evr::vkcore {

// Camera hook thread only.
struct CutsceneTrack {
    ULONGLONG since = 0;
    std::uint64_t changes = 0;
    bool skipHolding = false;
    ULONGLONG skipHoldStart = 0;
    ULONGLONG skipReleased = 0;
    std::uint64_t skipHolds = 0;
    xr_math::CutRebaseState cuts;
    bool endRebase = false; // a cutscene shown around the player ended: head aim's yaw is re-based next
    std::uint64_t rebaseLogs = 0;
};

namespace cutscene_eyes {

// Camera hook, every game frame before the view is written: the game's renderView_t fields that could build
// a cutscene's view origin apart from vieworg (engine-facts.md section 2: usesViewOriginOffset +0xC4,
// localViewOrigin +0xC8, viewOriginOffset +0xD4, viewBypass.allowBypass +0xE8, forceIdentityViewMatrix
// +0x130, cameraCut +0x16), logged at each cutscene's start, every 10 s in a cutscene and once in play
// (`cutscene-eyes: ...`). Logging only.
void noteGameView(const std::byte* renderView, bool cutscene);

// Render job thread, after an eye's latch built its matrices (onSeqEyeLatched): the latched view's position
// for `eye` (0 L, 1 R) of game frame `seq` from r.vieworg (+0x28A64), the inverse view matrix (+0x29400) and
// the view matrix (+0x293C0); at eye R its distance from eye L of the same game frame, in metres, logged
// every 10 s for cutscene and play frames each, with a budget of its own for each (`cutscene-eyes: eye L-R
// distance ...`; about 0.065 in play). The eyes are read only while a line is due. Logging only.
void noteLatchedEye(
    const std::byte* renderView, int eye, std::uint64_t seq, bool cutscene, float unitsPerMetre);

} // namespace cutscene_eyes

} // namespace evr::vkcore
