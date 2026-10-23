// The arms with the weapon in the left hand (left_hand_arms.hpp).

#include "vkcore/left_hand_arms.hpp"

#include "features/arm/arm_frames.hpp"
#include "features/arm/grip_book.hpp"
#include "features/arm/left_hand.hpp"
#include "vkcore/log.hpp"
#include "vkcore/weapon_arm.hpp"
#include "xr_math/offhand_pose.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace evr::vkcore::controllers::left_hand_arms {

namespace {

constexpr const char* kTag = "left-handed arms";
// The two attach joints are one pose to 0.0 mm wherever the game does not hold them apart (hands-rig.md, 15).
constexpr float kSameRootMetres = 0.01f;

// The placement for the viewmodel hook.
std::mutex g_placementMutex;
Placement g_placement;
std::uint32_t g_rootLines = 0;
// Hook thread only: each arm's grips of the game's, one per held item, taken from reads that held still
// (arm::SteadyPose) while the game had the arms for nothing of its own, and kept by the item's name across
// new hands (arm::GripBook: a level's first raise of an item no longer waits for a grip); a melee's fists in
// between do not lose the gun's.
constexpr float kSteadyMetres = 0.0005f;
// A held item whose reads have not held still for this many ticks is logged once (arm::SteadyPose).
constexpr int kUnsettledTicks = 90;
struct Grips {
    arm::SteadyPose steady;
    arm::GripBook<weapon_arm::Capture> book;
    std::optional<xr_math::ModelPose> lastRoot; // the last capture's root, for the log
    std::uint32_t lastSerial = 0;
    float lastMove = 0.0f; // its move from the capture before (game units)
    const std::byte* unsettledDecl = nullptr;
    int unsettledTicks = 0;
    std::vector<const std::byte*> logged; // held items logged as unsettled, or not stored (full)
    // The grip last logged as kept: its item and the gun's root it was read under.
    const std::byte* keptDecl = nullptr;
    Vec3 keptRoot;
};
// A kept grip is logged when its item changes or its root moves this far from the last line's (metres).
constexpr float kKeptLogMetres = 0.01f;
int g_readLines = 0;
std::array<Grips, 2> g_grips;
const std::byte* g_gripHands = nullptr;
// Hook thread only: whether the held item's [left_handed_off_hand] row puts the right glove on the right
// controller with the game's off hand (the chaingun: its left grip, reflected, closes on nothing), for the
// decl it was looked up for on these hands (new hands may give that pointer to another item).
std::optional<const std::byte*> g_offRowDecl;
bool g_offRowController = false;
int g_offRowLines = 0;
int g_keptLines = 0;
const std::byte* g_keptDecl = nullptr; // the held item last logged as on kept grips
// Hook thread only: the held item's [left_hand_wrist] row (the left glove's wrist on it at idle, model
// space), which places the model until the game's grip is read, for the decl it was looked up for on these
// hands.
std::optional<const std::byte*> g_wristRowDecl;
std::optional<Vec3> g_wristRow;
int g_wristRowLines = 0;
// Hook thread only: the left glove's wrist on the held item the model is placed by.
arm::LeftWristOnGun g_leftWrist;

const char* sideName(arm::ArmSide side) {
    return side == arm::ArmSide::Right ? "right" : "left";
}

// True the first time for `decl` on this arm.
bool firstLog(std::vector<const std::byte*>& logged, const std::byte* decl) {
    if (std::find(logged.begin(), logged.end(), decl) != logged.end() || logged.size() >= 64) {
        return false;
    }
    logged.push_back(decl);
    return true;
}

Grips& gripsOf(arm::ArmSide side) {
    return g_grips[side == arm::ArmSide::Right ? 1 : 0];
}

// Whether the arm's book knows `decl` on these hands: its name read from the game once per pointer and hands
// (a name not read, empty, is not learned and is read again next time).
bool learned(Grips& g, const std::byte* decl) {
    return decl && (g.book.nameOf(decl) || g.book.learn(decl, itemDeclName(decl)));
}

void forgetGrips() {
    for (Grips& g : g_grips) {
        g.steady.reset();
        g.book.newHands(); // the grips by name stay
        g.lastRoot.reset();
        g.unsettledTicks = 0;
    }
    g_offRowDecl.reset();
    g_wristRowDecl.reset();
    g_leftWrist.forget();
    // A capture read before counts for nothing now.
    weapon_arm::forgetCapture(arm::ArmSide::Right);
    weapon_arm::forgetCapture(arm::ArmSide::Left);
}

// The arm's latest capture kept for its held item once its read has held still.
void keepGrip(arm::ArmSide side, float unitsPerMetre) {
    const std::optional<weapon_arm::Capture> c = weapon_arm::captured(side);
    Grips& g = gripsOf(side);
    if (c && c->serial != g.lastSerial) {
        g.lastMove = g.lastRoot ? length(c->root.position - g.lastRoot->position) : 0.0f;
        g.lastRoot = c->root;
        g.lastSerial = c->serial;
    }
    if (!c || !g.steady.update(c->root, c->serial, kSteadyMetres * unitsPerMetre)) {
        // Read but never still (the gun swaying more than the rule allows, or the reads not one a tick): the
        // arms stay the game's for that item, said once.
        const std::byte* decl = c ? c->decl : nullptr;
        g.unsettledTicks = decl && decl == g.unsettledDecl ? g.unsettledTicks + 1 : 0;
        g.unsettledDecl = decl;
        if (g.unsettledTicks == kUnsettledTicks && firstLog(g.logged, decl)) {
            EVR_LOG("%s: the %s arm's grip of '%s' not taken: %d ticks of reads without %d still in a row "
                    "(the last moved %.4f, the rule %.4f game units, %.1f deg)",
                    kTag, sideName(side), itemDeclName(decl).c_str(), kUnsettledTicks,
                    arm::SteadyPose::kSteadyReads, static_cast<double>(g.lastMove),
                    static_cast<double>(kSteadyMetres * unitsPerMetre),
                    static_cast<double>(arm::SteadyPose::kSteadyDegrees));
        }
        return;
    }
    g.unsettledTicks = 0;
    if (!learned(g, c->decl) || !g.book.keep(c->decl, *c)) {
        if (firstLog(g.logged, c->decl)) {
            EVR_LOG("%s: the %s arm's grips are full (%zu held items): '%s' is not kept, its arms stay the "
                    "game's",
                    kTag, sideName(side), arm::GripBook<weapon_arm::Capture>::kMaxItems,
                    itemDeclName(c->decl).c_str());
        }
        return;
    }
    if ((c->decl != g.keptDecl || length(c->root.position - g.keptRoot) > kKeptLogMetres * unitsPerMetre) &&
        g_readLines < 80) {
        ++g_readLines;
        const Vec3 r = c->root.position * (1.0f / unitsPerMetre);
        EVR_LOG("%s: the %s arm's grip of '%s' kept, read with the gun's root at (%.3f %.3f %.3f) m (model)",
                kTag, sideName(side), itemDeclName(c->decl).c_str(), static_cast<double>(r.x),
                static_cast<double>(r.y), static_cast<double>(r.z));
    }
    g.keptDecl = c->decl;
    g.keptRoot = c->root.position;
}

// The arm's grip for the held item `decl`: read on these hands, else kept from a level before while the gun's
// root `gunNow` (as read last) sits where the kept grip was read (a weapon mod can hold the gun elsewhere in
// the model), else nullptr.
const weapon_arm::Capture* gripFor(arm::ArmSide side,
                                   const std::byte* decl,
                                   const std::optional<xr_math::ModelPose>& gunNow,
                                   float maxApart) {
    Grips& g = gripsOf(side);
    if (!learned(g, decl)) {
        return nullptr;
    }
    return g.book.gripOf(decl, [&](const weapon_arm::Capture& kept) {
        return gunNow && arm::sameRootPose(kept.root, *gunNow, maxApart);
    });
}

bool offHandOnController(const std::byte* decl) {
    if (decl != g_offRowDecl) {
        g_offRowDecl = decl;
        const std::string name = decl ? itemDeclName(decl) : std::string{};
        g_offRowController = state().offsets.lookupLeftOffHand(name) == game::LeftOffHand::Controller;
        if (g_offRowController && g_offRowLines < 20) {
            ++g_offRowLines;
            EVR_LOG("%s: '%s': the game's left grip, reflected, holds nothing on this gun: the right glove "
                    "follows the right controller",
                    kTag, name.c_str());
        }
    }
    return g_offRowController;
}

// Metres, model space.
std::optional<Vec3> wristRow(const std::byte* decl) {
    if (decl != g_wristRowDecl) {
        g_wristRowDecl = decl;
        const std::string name = decl ? itemDeclName(decl) : std::string{};
        const auto row = state().offsets.lookupLeftHandWrist(name);
        g_wristRow = row ? std::optional(Vec3{(*row)[0], (*row)[1], (*row)[2]}) : std::nullopt;
        if (g_wristRow && g_wristRowLines < 20) {
            ++g_wristRowLines;
            EVR_LOG("%s: '%s': placed in the left hand by its [left_hand_wrist] row until the game's grip is "
                    "read",
                    kTag, name.c_str());
        }
    }
    return g_wristRow;
}

// Hook thread only: the readiness last logged.
std::optional<bool> g_loggedReady;
std::uint32_t g_readyLines = 0;

void logReady(bool ready, bool right, bool left, bool free) {
    if (g_loggedReady == ready || g_readyLines >= 40) {
        return;
    }
    g_loggedReady = ready;
    ++g_readyLines;
    if (ready) {
        EVR_LOG(
            "%s: the left glove holds the gun (the right glove's grip reflected), the right arm is the off "
            "hand (%s)",
            kTag, free ? "on the right controller" : "the game's left glove reflected");
    } else {
        EVR_LOG("%s: no grip of the game's for the held item yet (right %s, left %s): both arms the game's "
                "until one is read",
                kTag, right ? "read" : "not read", left ? "read" : (free ? "not needed" : "not read"));
    }
}

} // namespace

void tick(const std::byte* hands,
          const ArmsTick& arms,
          const input::ArmSignals& weaponSignals,
          const input::ArmSignals& offSignals,
          const WorldHand& world,
          const ModelPlacement& model,
          float dt) {
    const input::ControllerSettings& cfg = settings();
    const std::byte* decl = arms.arms;
    if (hands != g_gripHands) {
        g_gripHands = hands;
        forgetGrips();
    }
    // A grip is the held item's idle hold: not taken while the game has the arms for an action of its own (a
    // raise, a melee's fists, a switch), whose pose would move the gun off the hand, nor while the hands swim
    // or have just stopped (input::gripTakingAllowed): the swim's pose and the hands' signals change a few
    // ticks apart, with no switch handing the arms back.
    if (input::gripTakingAllowed(input::readsGameGrip(weaponSignals, cfg.weaponArm), decl == arms.held,
                                 arms.swim, arms.sinceSwim)) {
        keepGrip(arm::ArmSide::Right, world.unitsPerMetre);
        keepGrip(arm::ArmSide::Left, world.unitsPerMetre);
    } else {
        for (Grips& g : g_grips) {
            g.steady.reset(); // still again after the action, read by read
            g.unsettledTicks = 0;
        }
    }
    // The free off hand, or the game's except where its grip reflected holds nothing.
    const input::OffhandMode offMode = input::leftHandOffMode(cfg.offhand, offHandOnController(decl));
    const bool free = offMode == input::OffhandMode::Free;
    const std::optional<xr_math::ModelPose> gunNow = weapon_arm::attachRead(arm::ArmSide::Right);
    const float maxApart = kSameRootMetres * world.unitsPerMetre;
    const weapon_arm::Capture* rightOk = gripFor(arm::ArmSide::Right, decl, gunNow, maxApart);
    const weapon_arm::Capture* leftOk = gripFor(arm::ArmSide::Left, decl, gunNow, maxApart);
    // The game's left grip is read under the left attach joint and written under the gun's root (the off
    // hand's right glove, reflected): taken only where the two roots were read as one pose.
    std::optional<xr_math::ModelPose> leftOnGun;
    if (rightOk && leftOk) {
        leftOnGun = arm::onGunRoot(leftOk->root, leftOk->grip.wrist, rightOk->root, maxApart);
        if (!leftOnGun && g_rootLines < 20) {
            ++g_rootLines;
            EVR_LOG(
                "%s: the left grip was read with the left attach joint %.3f from the gun's root; read again",
                kTag, static_cast<double>(length(leftOk->root.position - rightOk->root.position)));
        }
        leftOk = leftOnGun ? leftOk : nullptr;
    }
    const bool ready = rightOk && (free || leftOk);
    logReady(ready, rightOk != nullptr, leftOk != nullptr, free);
    if (ready && !gripsOf(arm::ArmSide::Right).book.readHere(decl) && decl != g_keptDecl &&
        g_keptLines < 40) {
        ++g_keptLines;
        EVR_LOG("%s: '%s': the grips kept from earlier hands (a level before) until one is read on these",
                kTag, itemDeclName(decl).c_str());
    }
    g_keptDecl = ready && !gripsOf(arm::ArmSide::Right).book.readHere(decl) ? decl : nullptr;

    // The left glove: the held item's [right_grip] row's grip, else the game's right grip, reflected.
    const std::optional<arm::GripPose> row = weapon_arm::rowGrip(decl);
    std::optional<arm::GripPose> leftGrip;
    if (row) {
        leftGrip = arm::mirroredGrip(*row);
    } else if (rightOk) {
        leftGrip = arm::mirroredGrip(rightOk->grip);
    }
    // The right glove: on the right controller (its wrist placed each tick, its fingers the game's right
    // hand's), or the game's left grip reflected.
    std::optional<arm::GripPose> rightGrip;
    std::optional<xr_math::ModelPose> rightWrist;
    if (free && rightOk && world.offValid) {
        const game::WeaponOffset o = cfg.offhandOffset;
        const arm::HandOffset offset{Vec3{o.forward, o.left, o.up} * world.unitsPerMetre,
                                     {o.pitch, o.yaw, o.roll}};
        rightWrist = arm::rightWristFromGrip(xr_math::inModelSpace(model.pose, world.offGrip), offset);
        rightGrip = rightOk->grip;
    } else if (!free && leftOk && leftOnGun) {
        rightGrip = arm::mirroredGrip(arm::GripPose{*leftOnGun, leftOk->grip.hand});
    }

    const input::ArmDecision noGrip{false, input::ArmReason::NoGrip};
    const input::ArmDecision weaponDecision =
        ready ? input::decideWeaponArm(weaponSignals, cfg.weaponArm) : noGrip;
    const input::ArmDecision offDecision = !ready ? noGrip
                                           : free ? input::decideArm(offSignals, offMode)
                                                  : weaponDecision;
    weapon_arm::Hold rightHold{offDecision, rightGrip ? &*rightGrip : nullptr, free, world.rightShoulder,
                               world.rightElbow};
    rightHold.wrist = rightWrist;
    rightHold.item = decl;
    weapon_arm::holding(arm::ArmSide::Right, hands, rightHold, world, model, dt);
    // The left glove's grip is in the gun's root frame; it is written in the left attach joint's, which the
    // game poses with the gun's root except in a few animations (the chaingun's mod: up to 24 cm apart), so
    // it is placed on the gun's root as read this tick.
    weapon_arm::Hold leftHold{weaponDecision, leftGrip ? &*leftGrip : nullptr, false, world.leftShoulder,
                              world.leftElbow};
    leftHold.item = decl;
    const std::optional<xr_math::ModelPose> gun = weapon_arm::attachRead(arm::ArmSide::Right);
    if (gun && leftGrip) {
        leftHold.wrist = arm::compose(*gun, leftGrip->wrist);
    }
    weapon_arm::holding(arm::ArmSide::Left, hands, leftHold, world, model, dt);
    // The gun stays placed for the left glove whenever its wrist on the held item is known, also while the
    // game has the arms (a switch, a melee), so the gun stays on the left hand; once taken from the game's
    // grip it stays for the held item if the grip lapses (a grip kept from a level before whose root no
    // longer holds); before the game's grip is read, where the item's row has it.
    const bool gripNow = rightOk && leftGrip;
    const std::optional<Vec3> leftWrist = g_leftWrist.update(
        decl, rightOk ? std::optional(rightOk->root) : std::nullopt, leftGrip,
        gripNow || g_leftWrist.fromGrip(decl) ? std::nullopt : wristRow(decl), world.unitsPerMetre);
    std::lock_guard lock(g_placementMutex);
    if (leftWrist) {
        g_placement.leftWrist = *leftWrist;
    }
    g_placement.decl = decl;
    g_placement.weight = leftWrist ? 1.0f : 0.0f;
}

void release(const std::byte* hands) {
    weapon_arm::release(arm::ArmSide::Right, hands);
    weapon_arm::release(arm::ArmSide::Left, hands);
    forgetGrips();
    std::lock_guard lock(g_placementMutex);
    g_placement.weight = 0.0f;
}

Placement placement() {
    std::lock_guard lock(g_placementMutex);
    return g_placement;
}

} // namespace evr::vkcore::controllers::left_hand_arms
