#pragma once

// The arms' surfaces on the hands model (docs/VR_HANDS_HUD.md, "The weapon's mesh kit";
// vkcore/hands_surfaces.cpp).
//
// The first-person hands model (praetor.md6, mesh marine.md6mesh) draws each arm as its own surfaces: the
// right arm, glove included, is `arm_low_rt_base`; the left is `arm_low_lf_base` with the two armour plates
// on its hand. Which of them are drawn is the weapon's choice: each weapon decl names a mesh kit of the
// model's group `Body` (showHideMeshHands.bodyKit), and the game applies it on equip. Most weapons (combat
// shotgun, heavy cannon, plasma rifle, rocket launcher, super shotgun, chaingun) pick "ArmLeft", which
// hides the right arm; the ballista, BFG and unmaykr pick "HideAll". The layer poses those arms, so while it
// does it shows their surfaces, and when it hands the arm back it hides again exactly the ones it showed.
// [static, build 25216728: the decls; SetMeshKit 0x19CF4B0, Show 0x19D0340, Hide 0x19CF620]
//
// The model keeps one visibility bit per surface, 128 of them in four dwords. Pure: the bit reads and the
// decisions; vkcore does the reads and writes.

#include "features/arm/arm_joints.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace evr::arm {

// The surfaces of each arm, by name.
std::span<const std::string_view> armSurfaceNames(ArmSide side);

// The model's kit (group Body) that holds exactly that arm's surfaces: "ArmRight", "ArmLeft".
std::string_view armKitName(ArmSide side);

// The arm's name in a log line: "right arm", "left arm".
const char* armLabel(ArmSide side);

// Whether a surface name stored on the model names `wanted`: the parts before a '$' in either, compared
// ignoring ASCII case (the game's FindSurfaces compare, 0x19AA2B0).
bool surfaceNameIs(std::string_view stored, std::string_view wanted);

// Equal ignoring ASCII case (the kit name compare, 0x3FE520).
bool equalsIgnoringCase(std::string_view a, std::string_view b);

// The visibility bitset: 128 surfaces (16 bytes, cleared and set whole by the model's code).
inline constexpr std::int32_t kMaxSurfaces = 128;

struct SurfaceBit {
    std::size_t word = 0;   // dword index
    std::uint32_t mask = 0; // bit within it
};
// The surface's bit, or nullopt outside 0..kMaxSurfaces-1.
std::optional<SurfaceBit> surfaceBit(std::int32_t surface);

// One surface's step this tick. `posed`: the layer poses the arm (weight above zero, written); `visible`:
// the bit now; `ours`: the layer set the bit and has not cleared it since.
//
// - Posed and hidden: show it, and it is ours.
// - Posed and visible: nothing; ours stays what it was (the game's kit may show it itself: fists,
//   chainsaw, melee).
// - Not posed and ours: hide it again if still visible; either way no longer ours.
// - Not posed and not ours: nothing (the game's).
enum class SurfaceStep : std::uint8_t {
    None,
    Show,
    Hide,
};
struct SurfacePlan {
    SurfaceStep step = SurfaceStep::None;
    bool ours = false; // after the step
};
SurfacePlan planSurface(bool posed, bool visible, bool ours);

// With the arms hidden (ETERNALVR_ARMS=hidden), whatever the weapon's kit or the layer's posing: a visible
// surface is hidden and is then ours; a hidden one is left as it is (ours stays what it was). There is no
// give-back after a multiplayer guard trip: once a surface is hidden the layer cannot tell whether the
// current weapon's kit wants it hidden too, so it stops hiding and leaves the next kit (an equip) or new
// hands (a map load) to show what the game wants.
SurfacePlan planHiddenSurface(bool visible, bool ours);

// The arms hidden for a while (a cutscene shown around the player, ETERNALVR_CUTSCENE_ARMS), on top of the
// plans above. `holding`: the arms are hidden this tick; `held`: the hold hid this surface and has not shown
// it again since.
//
// - Holding and visible: hide it. A surface the layer showed is no longer ours (posing shows it again after);
//   one the game's kit showed is held.
// - Holding and hidden: nothing.
// - Not holding and held: show it again if still hidden; either way no longer held. Only what the hold hid
//   is shown, so the game's kit and ETERNALVR_ARMS=hidden keep what they hide.
struct HeldPlan {
    SurfaceStep step = SurfaceStep::None;
    bool ours = false; // after the step
    bool held = false; // after the step
};
HeldPlan planHeldSurface(bool holding, bool visible, bool ours, bool held);

// What the arm's surfaces look like after a tick, for the trace.
enum class SurfaceState : std::uint8_t {
    Off,     // not found on the model, or the code did not check out: the game's kit decides
    Hidden,  // the game's kit hides them and the layer is not posing the arm
    Shown,   // the layer shows at least one of them
    Games,   // all visible by the game's own kit
    Removed, // the layer hides them all (ETERNALVR_ARMS=hidden)
};
const char* surfaceStateName(SurfaceState state);

} // namespace evr::arm
