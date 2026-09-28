#pragma once

// The player character's body, as the game defines it.

namespace evr::game {

// The DOOM Slayer's eye height in metres, the height the view is anchored to (ARCHITECTURE section
// 6). This assumes 1 game unit = 1 m, which is still to be confirmed on the rig (ARCHITECTURE
// section 5).
inline constexpr float kSlayerEyeHeightMetres = 1.657f;

} // namespace evr::game
