#pragma once

// Moving objects' motion vectors in eye R (docs/rig-findings/stereo-moved-flag.md).
//
// When the world commits an entity whose transform or bounds changed, it sets the entity's "moved this
// frame" flag and a status byte of 1. The next world frame's list builder sees status 1, clears the moved
// flag and sets status 0; the frame's end then rebuilds the entity's draw surfaces, and only a moved entity
// gets the depth pre-pass that writes object motion vectors. The game updates a moving demon once per game
// frame, so in mono every frame commits it again (moved). Under Route S eye R's render is a second world
// frame with no game update: it finds status 1 and clears the flag, so eye R draws the demon without object
// motion and its TAA smears it. Eye R leaves status 1 alone; the next eye L world frame clears it (or
// commits the entity again), as a mono frame would.

#include "stereo_seq/eye_tags.hpp"

#include <cstdint>
#include <string_view>

namespace evr::stereo_seq {

enum class MovedFlagMode {
    Off,   // the engine clears the flag in eye R (moving objects smear in eye R)
    Count, // as Off, and the list builder's status bytes and the surfaces' moved flags are counted
    On,    // eye R keeps the moved flag eye L's commit set
};

// ETERNALVR_STEREO_MOVED: "0"/"off"/"false"/"no" Off, "count" Count, anything else (or unset) On.
MovedFlagMode movedFlagMode(std::string_view value);

// The status byte the list builder should act on: 0 (skip the entity) for eye R when the status is 1 (the
// cleanup after a commit) and the mode is On, else `status`.
std::uint8_t movedFlagStatusFor(MovedFlagMode mode, Eye eye, std::uint8_t status);

} // namespace evr::stereo_seq
