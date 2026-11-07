#pragma once

// The Chaingun's Energy Shield in front of the gun in the hand (docs/VR_HANDS_HUD.md "The Energy Shield",
// DECISIONS T-217).
//
// Static RE (Steam build 25216728): the shield is an entity of its own (idEnergyShell, the chaingun's mod
// spawns it) whose effect (fx/weapon/chaingun/chaingun_1st_energy_shield) draws the shield the player sees.
// Each think the entity puts itself at the player's eye (idPlayer::firstPersonViewOrigin) with an axis built
// from the view angles, the yaw turned 90 degrees about up, and sends that origin and axis to its effect. The
// effect's first-person shield is a small model (size 0.165, the model 2.47 m wide, 1.23 m tall) 0.25 m along
// the view, drawn with the hands' projection: on a flat screen it fills the middle of the view in front of
// the gun. In VR the gun sits in the hand and reaches about 1.1 m past it (the chaingun), so the shield
// 0.25 m from the eye was inside the gun and the gun drawn over it.
//
// Here the effect's transform is turned onto the ray the shots take and moved along it until the whole
// shield is past the gun's muzzle, and the shield is drawn at the size the game gives it in the world (its
// third-person effect, which other players see: size 1.78, the shield 4.39 m wide and 2.19 m tall, about the
// size of the sphere it blocks damage in, 2.1 m round the player). What the shield blocks is untouched: the
// game measures that from the player, not from the effect.
//
// Conventions as in weapon_pose.hpp: positions relative to the game's eye, id Tech axes (+X forward, +Y
// left, +Z up), axes as rows. Pure: no Windows, OpenXR or game memory.

#include "common/vector.hpp"
#include "xr_math/head_view.hpp"
#include "xr_math/weapon_pose.hpp"

#include <optional>

namespace evr::xr_math {

// The effect's own frame: the game turns the view's axis 90 degrees about up, so the effect's left row is
// the view's back and its forward row the view's left. The first-person shield sits this far along the
// view (the effect's offset, its y = -0.25, which the game does not scale), metres.
inline constexpr float kShieldEffectReach = 0.25f;
// The effect's scale for the shield's size in the world: its third-person size over its first-person one
// (1.78 / 0.165).
inline constexpr float kShieldWorldScale = 1.78f / 0.165f;
// How far past the muzzle the shield's centre goes along the ray, metres: at the world size its model
// reaches 0.18 m back from its centre (its bounds -0.143 to 0.100 along the effect's left row, times 1.78),
// so every part of it is past the muzzle.
inline constexpr float kShieldPastMuzzle = 0.25f;
// The chaingun's muzzle (its fx_muzzle tag, 1.045 m ahead of the weapon's root) in the arms model's frame
// as the game holds the gun at idle, metres: measured on the hands rig (the weapon's root as read, plus the
// tag from the mesh's skeleton), the median over 24 dump lines.
inline constexpr Vec3 kChaingunMuzzleInArms{1.750f, -0.138f, -0.193f};

// The effect's transform: the origin relative to the eye, and the axis.
struct ShieldEffect {
    Vec3 origin;
    IdViewAxis axis;
};

// The direction the effect puts the shield along: the view's forward, the negated left row.
Vec3 shieldEffectForward(const IdViewAxis& effectAxis);
// The effect's axis for a view's axis, as the game builds it: the view turned 90 degrees about its up.
IdViewAxis shieldEffectAxis(const IdViewAxis& view);

// The chaingun's muzzle relative to the eye with the arms model placed at `arms`.
Vec3 chaingunMuzzle(const EyeRelativePose& arms, float unitsPerMetre);

// The effect's transform that puts the shield in front of the gun: the axis the game builds for a view
// along `rayDirection` (no roll, as the game's view has none), the origin moved so the shield's centre sits
// on the ray from `rayStart`, kShieldPastMuzzle past `muzzle` (taken along the ray; a muzzle behind the start
// counts as at it). nullopt when a value is not finite, the game's axis is not orthonormal or a direction is
// unusable (zero, or the ray more than 120 degrees from the game's shield).
std::optional<ShieldEffect> shieldInFrontOfGun(
    const ShieldEffect& game, Vec3 rayStart, Vec3 rayDirection, Vec3 muzzle, float unitsPerMetre);

// The effect's scale is kept by the game from one think to the next. While the layer places the shield it
// sends kShieldWorldScale every think (a shield made again starts at the game's 1). When a think finds no
// placement (a forced view, a lost hand, the controllers detached) the last placement is still drawn for
// kHoldThinks thinks, so a single missed tick does not flick the shield between the two sizes; after that
// the game's 1 goes out once, so the game's own small shield is not drawn at the world size by the eye. A
// scale the hook could not send (the multiplayer guard refused) is made up at the next think that can.
class ShieldScale {
public:
    static constexpr int kHoldThinks = 3;

    struct Step {
        std::optional<float> send; // the scale to send this think, or nothing
        bool draw = false;         // the shield is drawn where the layer put it (placed now, or held)
    };

    Step next(bool placing);
    // The think could not send `step`'s scale.
    void notSent(const Step& step);
    // The think was refused before it asked (the multiplayer guard): a world scale already sent is taken
    // back at the next think that can send.
    void refused();

private:
    bool scaled_ = false;  // the game's effect may hold the world scale
    int missed_ = 0;       // thinks without a placement since the last one
    bool restore_ = false; // send the game's 1 at the next think without a placement, no hold
};

// Whether the answer to "where is this entity" (the entity's effect pose) is moved: only for the shield the
// layer placed, of the shield's class, last placed within kShieldPlacedSeconds (a dead shield's slot reused
// by another entity, or a shield that stopped thinking, is the game's), and only when the effect asks.
inline constexpr double kShieldPlacedSeconds = 0.1;
struct ShieldPoseAsk {
    bool placedShield = false; // the entity is the one the think placed
    bool shieldClass = false;  // its vtable is the shield's
    double sincePlaced = 0.0;  // seconds since that think
    bool fromEffect = false;   // asked by the effect's own pose query (its return address)
};
bool movesShieldPose(const ShieldPoseAsk& ask);

} // namespace evr::xr_math
