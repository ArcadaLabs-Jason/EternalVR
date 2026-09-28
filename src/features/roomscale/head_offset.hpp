#pragma once

// The rendered head's offset from the game's eye (room-scale v1, T-062; eye height, T-029).
//
// The game places its eye on the player's body; the layer adds the tracked head's offset from the room
// anchor on top. Horizontally that is a lean, capped at `leanCapMetres` from the anchor: a real body
// cannot walk the camera out of the character, it can only lean. Vertically the anchored head sits at
// the game's eye (Slayer height), or at the player's own measured height (Real height, when the runtime
// has a floor), and the head may then crouch down to `minEyeMetres` above the feet or rise
// `maxRiseMetres` above the anchored height.
//
// Input and output are room space (room_anchor.hpp): OpenXR axes, metres before world scale. The caller
// scales by the world scale and turns the result into the game's axes with the body frame.

#include "common/vector.hpp"

#include <cstdint>
#include <optional>

namespace evr::roomscale {

enum class HeightMode : std::uint8_t {
    Slayer, // the anchored head is at the game's eye height, whatever the player's height (default)
    Real,   // the anchored head is at the player's own height above the floor, scaled by world scale
};

struct HeadOffsetLimits {
    float leanCapMetres = 0.60f;
    float maxRiseMetres = 0.25f;
    float minEyeMetres = 0.30f;
};

struct HeadOffsetInput {
    Vec3 roomHead;                         // the head in room space
    std::optional<float> anchorAboveFloor; // the anchored head's height above the floor, if known
    HeightMode height = HeightMode::Slayer;
    float gameEyeUnits = 1.657f; // the game's eye height above the player's origin
    float unitsPerMetre = 1.0f;  // world scale
};

struct HeadOffset {
    Vec3 offset;                // room axes, metres (scale by unitsPerMetre for game units)
    float requestedLean = 0.0f; // the horizontal distance asked for
    float lean = 0.0f;          // the horizontal distance given
    bool leanClamped = false;
    bool heightClamped = false;
    bool realHeight = false; // Real height was applied (it needs the floor)
};

// Limits that are not finite, positive and sane (lean 0.05 to 2 m, rise 0 to 1 m, eye 0.1 to 1.5 m) fall
// back to the defaults one by one.
HeadOffsetLimits sanitized(HeadOffsetLimits limits);

HeadOffset headOffset(const HeadOffsetInput& input, const HeadOffsetLimits& limits);

} // namespace evr::roomscale
