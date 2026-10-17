#pragma once

// The game's crosshair under hand aim (docs/rig-findings/ui-layer.md, "Stereo-correct crosshair"): on the
// head-locked quad it marks the head's ray, not the gun's, so it must not show; the layer's dot marks the
// gun's ray instead. ETERNALVR_UI_CROSSHAIR chooses how it is taken out:
//
// - `hold` (the default): the layer holds hud_reticle_styleOverride at 0 while hand aim is on
//   (vkcore/runtime_cvars_reticle.hpp). Style 0 draws no reticle: it is what idHUD_Reticle's update gives
//   g_reticleMode 2 and the player's hideReticle flag, and the override is read after both, every update. The
//   override is in no profile, menu or config (no save flag, not among idGameSettings' fields), so the
//   player's flat game never sees it; g_reticleMode and the HUD settings are profile fields and are never
//   held. Everything else the game draws in the centre stays: objective markers, prompts, the low ammo and
//   chainsaw "no target" texts, the ability indicators.
// - `mask`: the centred square of the GUI copy is cleared (copyRegionsWithoutCentre with kCrosshairMask),
//   which takes everything drawn there with the crosshair. Under `hold` the mask is the fallback: it stays on
//   until the hold is confirmed (holdConfirmed), so a hold that never lands (no setter, the multiplayer
//   guard, an ETERNALVR_DEBUG_CVARS value entry for the cvar, a mono session without run-time holds) shows
//   what `mask` shows.
//
// Neither applies while a menu is up or the target holds a full-screen backdrop (centreMasked), nor under
// head aim, where the game's crosshair is on the right ray.

#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::ui_layer {

enum class CrosshairMode : std::uint8_t { Hold, Mask };

const char* crosshairModeName(CrosshairMode mode);

// ETERNALVR_UI_CROSSHAIR: "hold" or "mask" (any case, spaces around ignored); nullopt for anything else.
std::optional<CrosshairMode> parseCrosshairMode(std::wstring_view text);

// The centred square left out of the GUI copy by the mask, as a fraction of the target's height: the game's
// crosshair and the ability indicators around it.
inline constexpr float kCrosshairMask = 0.12f;

// The cvar the hold writes and its value: the game's own "no reticle" style.
inline constexpr const char* kReticleStyleCvar = "hud_reticle_styleOverride";
inline constexpr int kReticleStyleHidden = 0;

// Presents in a row the cvar must read kReticleStyleHidden, already before the layer's write of that
// present, before the mask comes off: the game's HUD update reads it before the GUI pass that the copy then
// takes, so a reading at one present proves only the next frames.
inline constexpr std::uint32_t kHoldConfirmPresents = 8;
// A confirmation older than this (no present has checked the cvar since) no longer counts, and neither does a
// request of the presenter's that kHoldStalePresents presents have also read (holdRequested). For the
// confirmation it is a safety net that does not fire with today's call order: every present that copies the
// GUI has just checked the cvar or started the run again (runtime_cvars::apply runs first), and a session
// without run-time holds never builds a run.
inline constexpr std::uint64_t kHoldStaleMs = 500;
// Presents (runtime_cvars::apply, each present before its copy) that must also have read a request since it
// was made before it lapses (holdRequested): copies that stopped leave every later present without one, while
// a single frame longer than kHoldStaleMs (a hitch) is one or two presents (Route S copies on every second).
inline constexpr std::uint32_t kHoldStalePresents = 4;

// The run of presents that confirm the hold after one more present: `holding` the layer holds the cvar at
// kReticleStyleHidden now (hand aim, the hold on), `hidden` it read kReticleStyleHidden before this
// present's write. Any other present starts the run again.
std::uint32_t nextHoldStreak(std::uint32_t streak, bool holding, bool hidden);

// The hold is in effect at `nowMs`: a run of at least kHoldConfirmPresents, the last checked at `lastMs`
// (the same clock), no more than kHoldStaleMs ago (a check stamped after `nowMs` was read counts).
bool holdConfirmed(std::uint32_t streak, std::uint64_t lastMs, std::uint64_t nowMs);

// The presenter's request for the hold, made at `requestMs` and read by `presents` presents before this one,
// still stands at `nowMs`: `want`, unless it is both over kHoldStaleMs old (a request stamped after `nowMs`
// was read counts as new) and read by at least kHoldStalePresents presents. The presenter asks on every GUI
// copy; when the copies stop (VR off for the game, a failed target check, copies failed) the request lapses
// and the hold gives the cvar back instead of hiding the crosshair for the rest of the process. One long
// frame does not lapse it.
bool holdRequested(bool want, std::uint64_t requestMs, std::uint64_t nowMs, std::uint32_t presents);

// The count of presents that read a request after one more present has read it: one more, up to
// kHoldStalePresents (holdRequested needs no more).
std::uint32_t nextRequestPresents(std::uint32_t presents);

// What the hold keeps across presents: the cvar's value before the layer's first write of a hold, written
// back when that hold ends. `kept` is cleared whenever no hold is requested (reticleWrite), so a value set
// while no hold runs (a console style under head aim, 0 included) is the player's: never written back, and
// kept by the next hold.
struct ReticleKeep {
    bool kept = false;
    int before = -1;
};

// One present's write for the hold. `Hide`: write kReticleStyleHidden; `Restore`: write keep.before back.
enum class ReticleWrite : std::uint8_t { None, Hide, Restore };

// The hold's write for a present on which the cvar reads `reads`: `requested` (holdRequested) and not hidden
// yet -> Hide, keeping the value it read on the first such present (a later reset by the game, a cheat reset
// to -1, is written over again and does not replace it); not requested, a value kept and the cvar still
// hidden -> Restore, clearing `kept` (keep.before stays: the value to write). Nothing when it already reads
// hidden while requested, or nothing was kept (the cvar already read hidden before the hold: it is not the
// layer's to give back). Not requested with a value kept but the cvar no longer hidden (a cheat reset or a
// checkpoint reload put a value back before the hold ended): nothing, and `kept` is cleared too, so that
// value and any set later are the player's.
ReticleWrite reticleWrite(bool requested, int reads, ReticleKeep& keep);

// The layer asks for the hold: the mode is `hold` and hand aim is on, or (any aim) a scoped weapon is zoomed
// (`scopeZoomed`, vkcore/zoom_look.cpp): the scope's reticle would draw its ring over the view (T-136).
bool wantsReticleHold(CrosshairMode mode, bool handAim, bool scopeZoomed = false);

struct CrosshairState {
    CrosshairMode mode = CrosshairMode::Hold;
    bool handAim = false;       // the weapon hand aims (controllers::weaponAimSpace)
    bool menu = false;          // a menu is up or its panel is held (docs/VR_MENUS.md)
    bool backdrop = false;      // the backdrop test sees a full-screen layer (ui_layer/backdrop.hpp)
    bool holdConfirmed = false; // holdConfirmed() now
};

// The GUI copy leaves the centred square out: hand aim, no menu or backdrop, and the mode is `mask` or the
// hold is not confirmed.
bool centreMasked(const CrosshairState& state);

// The presenter's tally of the hold, for its log: the hand-aim copies under `hold` that kept their centre
// because the hold was confirmed (not those a menu or a backdrop kept whole anyway, which the presenter
// counts apart), and how often the confirmation changed.
struct HoldTally {
    bool confirmed = false; // at the last hand-aim copy under `hold`
    std::uint64_t kept = 0; // copies the confirmed hold kept whole
    std::uint64_t changes = 0;

    // One GUI copy: `holding` hand aim under `hold`, `nowConfirmed` the hold is confirmed, `maskable` no menu
    // or backdrop keeps the copy whole anyway. True when the confirmation changed with this copy (changes
    // counts it). A copy without `holding` changes nothing.
    bool note(bool holding, bool nowConfirmed, bool maskable);
};
// Changes of the confirmation the presenter logs; later ones are only counted.
inline constexpr std::uint64_t kLoggedHoldChanges = 12;

} // namespace evr::ui_layer
