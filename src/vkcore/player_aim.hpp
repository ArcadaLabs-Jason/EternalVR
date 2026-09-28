#pragma once

// Read and nudge the local player's view angles (head aim, xr_math/head_aim.hpp).
//
// Layout facts are from the type-info tables of Steam build 25216728 (docs/rig-findings/engine-facts.md):
// idPlayer::physicsObjHavok (idHavokPhysics_Player) at +0x8A50; inside it the user command at +0x3DE0
// (idUserCmd::angles, three shorts, at +0x1C), viewAngles at +0x3F10, deltaViewAngles at +0x3F1C and
// current (playerPState_t, whose deltaViewAngles is at +0x80) at +0x3F28. They are used only when the
// running exe is that build and the object's vtable is idPlayer's; anything else leaves head aim off.

#include "xr_math/head_aim.hpp"

#include <cstddef>
#include <cstdint>

namespace evr::vkcore {

class PlayerAim {
public:
    // Decides once, from the loaded exe, whether the layout above applies. Logs the reason when not.
    bool init();
    [[nodiscard]] bool available() const { return available_; }

    // True when `object` is an idPlayer (its vtable is idPlayer's).
    [[nodiscard]] bool isPlayer(const std::byte* object) const;

    struct Sample {
        xr_math::IdAngles view;       // idHavokPhysics_Player::viewAngles
        xr_math::IdAngles delta;      // idHavokPhysics_Player::deltaViewAngles
        xr_math::IdAngles stateDelta; // idHavokPhysics_Player::current.deltaViewAngles
        xr_math::IdAngles command;    // idUserCmd::angles, in degrees
    };
    [[nodiscard]] Sample read(const std::byte* player) const;

    enum class DeltaField { Physics, State };
    // Adds to the chosen deltaViewAngles (yaw and pitch) and returns its new value.
    xr_math::IdAngles addDelta(std::byte* player, DeltaField field, float pitch, float yaw) const;

private:
    bool available_ = false;
    const std::byte* playerVtable_ = nullptr;
};

} // namespace evr::vkcore
