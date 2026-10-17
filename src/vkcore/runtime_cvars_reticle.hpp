#pragma once

// Hand aim's crosshair hold (ETERNALVR_UI_CROSSHAIR=hold, the default; ui_layer/crosshair.hpp): one more
// cvar the layer holds at run time (runtime_cvars.hpp: through the engine's setter and the cvar book, only
// while the multiplayer guard allows touching the game, on the presents where the held set is applied: Route
// S and Parallel Eye Rendering), only while the UI layer is on. hud_reticle_styleOverride is held at 0 (the
// game's "no reticle" style) only while the presenter asks for it, that is while hand aim is on or a scoped
// weapon is zoomed (T-136, controllers::scopeZoomed), on every GUI copy; when it stops asking, or its last
// request is over 0.5 s old and 4 presents have read it (the copies stopped: VR off for the game, a failed
// target check; one long frame is not enough), the value the cvar had before the layer's first write is
// written back, so head aim (outside a scoped zoom), a demon piloted under head aim and a game left without
// VR show the game's crosshair. The log names which of the two ended the hold. Each present also reads the
// cvar back: the presenter takes the square out of the GUI copy (the mask) until kHoldConfirmPresents
// presents in a row read 0, and again as soon as one does not, the guard stops the writes, or no present has
// read it for kHoldStaleMs (a mono session with the UI layer never reads it, so it keeps the mask). An
// ETERNALVR_DEBUG_CVARS value entry for the cvar replaces the hold: never confirmed, the mask stays. Found by
// name through the cvar registrations, as every held cvar: no address of its own. The game never writes the
// cvar (its registration and idHUD_Reticle's update are its only references), and it has no save flag, so it
// never reaches the player's config or profile.

namespace evr::vkcore::runtime_cvars {

// Present hook, each GUI copy: the presenter asks for the hold now (ui_layer::wantsReticleHold), stamped with
// the time; the count of presents that read it starts again (a request older than kHoldStaleMs that
// kHoldStalePresents presents have read counts as none).
void wantReticleHidden(bool want);

// The hold is confirmed now (ui_layer::holdConfirmed), so the copy may keep the centre.
bool reticleHiddenConfirmed();

} // namespace evr::vkcore::runtime_cvars
