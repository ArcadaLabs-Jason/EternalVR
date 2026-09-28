#pragma once

// Skipping a cutscene by hand (docs/VR_CONTROLLERS.md): while a cutscene plays, holding the dash button
// (B on the weapon hand by default) holds the game's skip key, which the game turns into a skip after its
// own hold time. The key goes up when the button does or when the cutscene ends.
//
// Only a press that starts during the cutscene counts, so a dash already held when a cutscene begins does
// not skip it. The layer's automatic skip (ETERNALVR_SKIP_CINEMATICS) holds the key itself; this one is for
// players who leave that off. Pure logic, run once per user command.

#include <cstdint>

namespace evr::input {

// The game's cutscene skip key (the Windows virtual-key code of R); held, the game skips after its hold time.
inline constexpr std::uint8_t kCutsceneSkipKey = 'R';

struct CutsceneSkipOutput {
    bool keyDown = false;   // the skip key should be held now
    bool firstHold = false; // the key went down for the first time in this cutscene (log it)
};

class CutsceneSkip {
public:
    // `cutscene`: a cutscene plays; `dash`: the dash action is held.
    CutsceneSkipOutput update(bool cutscene, bool dash);

    [[nodiscard]] bool keyDown() const { return keyDown_; }

private:
    bool wasCutscene_ = false;
    bool armed_ = false; // the dash button was up at some point during this cutscene
    bool keyDown_ = false;
    bool heldThisCutscene_ = false;
};

} // namespace evr::input
