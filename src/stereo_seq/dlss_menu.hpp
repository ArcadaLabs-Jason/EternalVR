#pragma once

// The game's own DLSS entry in its video menu during a Route S session (docs/VR_STEREO.md, "The game's
// video menu"): what it shows and what happens when the menu applies it. No engine code here, so it is
// tested on every platform.
//
// The entry has four choices, index 0 to 3: Off (temporal AA), Performance, Balanced, Quality. The game
// keeps the index in the player's profile (advDlssQualityIndex, saved to profile.bin, which Steam Cloud
// syncs and the launcher never writes back) and shows that index, never the cvars. The layer, though,
// holds r_antialiasing and r_dlssQuality itself when the launcher's Anti-aliasing is DLSS or Off, so the
// menu would show the profile's choice, not what runs. The layer shows what runs instead, and keeps the
// profile's own index when the menu applies an entry the player did not change.

namespace evr::stereo_seq {

// What the layer holds for anti-aliasing.
struct DlssMenuHold {
    bool perEyeTaa = false;  // per-eye TAA requested and not failed closed (else r_antialiasing 0 is held)
    bool dlssOption = false; // the launcher's DLSS (ETERNALVR_STEREO_DLSS): r_antialiasing 2 is held
    bool dlssPerEye = false; // eye R can have its own DLSS feature (else DLSS falls back to TAA)
    int dlssQuality = -1;    // the r_dlssQuality DLSS runs with (-1: unknown)
};

// The menu index of an r_dlssQuality value: 1 to 3 are the same, 0 (Ultra Performance, which the menu
// does not have) is shown as Performance, a higher value as Quality, an unknown one as Quality (the game's
// own mapping sends every index above 2 to r_dlssQuality 3).
int dlssMenuIndexForQuality(int quality);

// True when the layer follows the game's own choice (the launcher's TAA: the profile's DLSS runs per eye
// when it is on), so a change made in the menu takes effect as in the flat game.
bool dlssMenuFollowsGame(const DlssMenuHold& hold);

// The index the menu shows, given the one the profile holds: what runs. Off when no temporal AA or no DLSS
// runs, the held quality when the launcher's DLSS runs, the profile's own when the layer follows the game.
int dlssMenuShown(const DlssMenuHold& hold, int profileIndex);

enum class DlssMenuApply {
    Keep,   // the entry is as shown: the game's setter is skipped, the profile and the cvars stay as they are
    Apply,  // the game's setter runs with the chosen index (the flat game's behaviour)
    Ignore, // the player chose another entry but the launcher's setting decides in VR: skipped, logged
};

// What to do when the menu applies `chosen` after it showed `shown` (-1: the layer showed nothing, which
// keeps the game's behaviour).
DlssMenuApply dlssMenuApply(const DlssMenuHold& hold, int chosen, int shown);

// For the log.
const char* dlssMenuApplyName(DlssMenuApply apply);

} // namespace evr::stereo_seq
