#pragma once

// Ammo picked up (a tester's idea, public issue #12: "a wave trailing down your back like putting ammo in a
// backpack"). Health and armor show in the player's own values (pickups.hpp); ammo does not, so the layer's
// hooks on the game's pickup path (bhaptics_ammo.cpp) look at each prop the player picks up, and
// isAmmoPickup tells the ammo ones by what the prop gives. A weapon switch, an upgrade, a respawn or a load
// picks nothing up, so none of them can start the wave.
//
// AmmoWave is the effect: a band down the back of the vest, row by row from the top, lighter and quicker than
// the armor's wave, since ammo is picked up all the time. A pickup less than kAmmoMergeSeconds after the
// wave started is part of it (a chainsaw's shower of drops, a row of boxes run over): one wave, not one each.
// Pure.

#include <optional>
#include <string_view>

namespace evr::bhaptics {

// A prop that gives ammo, from the MSVC RTTI names (".?AVidDeclAmmo@@") of its use component's decl
// (idProp2::useableComponentDecl) and, for an item pickup (idDeclProp_ItemPickupComponent), of the inventory
// decl it gives (`itemClass`, empty otherwise): an item pickup of an idDeclAmmo (the shells, bullets, cells,
// rockets and the rest lying about), or a universal ammo pickup (idDeclProp_UniversalAmmoComponent).
bool isAmmoPickup(std::string_view useComponentClass, std::string_view itemClass);

// Pickups closer than this to the wave's start are one wave.
inline constexpr double kAmmoMergeSeconds = 0.3;
// The band's rows, each for kAmmoRowSeconds (about 0.2 s for the vest's 5), at kAmmoIntensity, with the row
// just left at kAmmoTrailShare of it; each row's frame kAmmoOverlapMillis longer than its step.
inline constexpr double kAmmoRowSeconds = 0.04;
inline constexpr float kAmmoIntensity = 45.0f;
inline constexpr float kAmmoTrailShare = 0.4f;
inline constexpr int kAmmoOverlapMillis = 20;

struct AmmoStep {
    int row = 0;            // the band, row 0 at the top
    int trailRow = -1;      // the row it just left, or -1
    float intensity = 0.0f; // the band's, 0..100 before the strength
    int millis = 0;
};

class AmmoWave {
public:
    explicit AmmoWave(int rows);

    // A pickup at `seconds`: starts the wave and returns true, or returns false when it comes less than
    // kAmmoMergeSeconds after the wave's start (merged into it; the wave runs on unchanged).
    bool pickUp(double seconds);
    // The step to play when the band reached a new row by `seconds` (a row passed between two calls is left
    // out); nullopt otherwise and once the wave is over.
    std::optional<AmmoStep> step(double seconds);
    // Ends the wave and forgets its start, so the next pickup starts one.
    void stop();
    [[nodiscard]] bool active() const { return active_; }

private:
    int rows_;
    bool active_ = false;
    bool started_ = false; // `start_` holds a wave's start
    double start_ = 0.0;
    int done_ = -1; // the last step played
};

} // namespace evr::bhaptics
