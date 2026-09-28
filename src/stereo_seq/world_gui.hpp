#pragma once

// World GUIs in both eyes (docs/rig-findings/stereo-world-gui.md).
//
// A GUI on a world surface (a door's hologram, a terminal's screen) is an idRenderModelGui hung on a host
// model. The world commits it once per game frame, stamping the commit with the world's render frame
// number, and the backend draws its surfaces only while that stamp equals the world's current frame
// number. The number goes up once per render, so under Route S eye R's render (the second of the tick) is
// one ahead of the commit eye L made for the same game frame: every world GUI is skipped in eye R. Eye R
// draws eye L's surfaces of the same tick instead; their vertices stay valid until the GUI buffer comes
// round again, several ticks later.

#include "stereo_seq/eye_tags.hpp"

#include <cstdint>
#include <string_view>

namespace evr::stereo_seq {

enum class WorldGuiMode {
    Off,   // the engine's check stands (eye R has no world GUIs)
    Count, // the engine's check stands, and the stamps it compares are counted and logged
    On,    // eye R draws the world GUIs eye L committed in the same tick
};

// ETERNALVR_STEREO_WORLD_GUI: "0"/"off"/"false" Off, "count" Count, anything else (or unset) On.
WorldGuiMode worldGuiMode(std::string_view value);

// How a world GUI's commit stamp compares with the frame number the render checks it against.
enum class WorldGuiStamp {
    Current,   // committed in this render's frame: drawn
    OneBehind, // committed in the render before (eye R: eye L's commit of the same tick)
    Older,     // not committed for a while: not drawn
};

WorldGuiStamp classifyWorldGuiStamp(std::uint32_t committed, std::uint32_t current);

// The frame number the engine should compare the stamp with: `committed` for eye R when the GUI was
// committed exactly one render earlier and the mode is On (so the compare passes), else `current`.
std::uint32_t worldGuiFrameFor(WorldGuiMode mode, Eye eye, std::uint32_t committed, std::uint32_t current);

} // namespace evr::stereo_seq
