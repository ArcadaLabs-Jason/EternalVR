// Motion controllers, the viewmodel (controllers.hpp, docs/rig-findings/input-aim.md section 3, T-054).
//
// - endGameView (camera hook, viewmodel_view.cpp): the weapon hand's aim and grip poses relative to the
//   game's eye, for this file's hook and the shot hook; the view yaw the mapper rotates movement into; and
//   the weapon FOV: renderView_t's weaponFOVX/Y and customFOV2X/Y get the headset FOV the camera hook wrote
//   into fov_x/y, so the arms and weapon are drawn with the world's projection.
// - A mid-hook in idHands::UpdatePosition just before the render model's stores (RVA 0x13807EA): the final
//   origin [rsp+0x48] and axis [rbp-0x30] are replaced with the controller's grip, turned to its aim ray,
//   plus the held weapon's offset from the table (data/weapons/viewmodel_offsets.toml). A weapon with a
//   [sights] row is then turned about the grip so its sight line runs parallel to the aim ray (on by default,
//   T-135; ETERNALVR_WEAPON_SIGHTS=0 turns it off on the rig); the shot ray and the aim dot do not change.
//   The weapon's active mod is read each update (weapon_mods.hpp): a mod's own row (the Precision Bolt's
//   scope) replaces the weapon's from the next update on, turning the gun about the glove's wrist.
//   Last the model is moved, not turned, so the glove's wrist keeps the grip's own frame
//   (xr_math::placeViewmodel). The game's own stores then carry it into both the deferred and the current
//   render-model pose. Under head aim the game's own placement stays, moved by the rendered head's offset
//   from the game's eye (input::gunPlacement, head_gun.hpp, T-143): the gun in front of the face, along the
//   aim.

#include "vkcore/controllers_impl.hpp"

#include "features/arm/left_hand.hpp"
#include "features/input/gun_placement.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/demon_view.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/hands_surfaces.hpp"
#include "vkcore/head_gun.hpp"
#include "vkcore/left_hand_arms.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/offhand_mods.hpp"
#include "vkcore/room_scale.hpp"
#include "vkcore/weapon_mods.hpp"
#include "xr_math/hand_aim.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

// input-aim.md section 3, extended to the origin load that follows (movsd xmm0,[rsp+0x48]).
constexpr const char* kViewmodelSignature =
    "0F B6 86 ?? ?? ?? ?? 0F 10 45 D0 24 0C 3C 0C 74 54 0F 11 86 ?? ?? ?? ?? 0F 10 4D E0 0F 11 8E ?? ?? ?? "
    "?? 8B "
    "45 F0 89 86 ?? ?? ?? ?? 0F 10 45 D0 0F 11 86 ?? ?? ?? ?? 0F 10 4D E0 0F 11 8E ?? ?? ?? ?? 8B 45 F0 89 "
    "86 "
    "?? ?? ?? ?? F2 0F 10 44 24 48 F2 0F 11 86 ?? ?? ?? ?? 8B 44 24 50";
// The render model's fields the stores name: flags, g.axis (three parts), deferredAxis (three parts),
// g.origin.
struct Disp {
    std::size_t at;
    std::int32_t value;
};
constexpr Disp kViewmodelDisps[] = {{3, 0xB0},     {0x14, 0x164}, {0x1F, 0x174}, {0x28, 0x184},
                                    {0x33, 0x104}, {0x3E, 0x114}, {0x47, 0x124}, {0x55, 0x158}};
constexpr std::size_t kOriginFromRsp = 0x48;
constexpr std::intptr_t kAxisFromRbp = -0x30;

// Type info (build 25216728).
constexpr std::size_t kHandsOwner = 0x358;                // idHands::owner
constexpr std::size_t kHandsRightItemDecl = 0x29B0 + 0x8; // idHands::rightItem.itemDecl
constexpr std::size_t kDeclName = 0x8;                    // idResource::name (idAtomicString, a char*)
constexpr std::size_t kPlayerFirstPersonOrigin = 0x16580; // idPlayer::firstPersonViewOrigin

constexpr float kRadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

// A melee or a Blood Punch puts the fists in the hands for the punch (weapon/player/fists_doom5melee), and
// the punch's hit follows the fists on the hands model, not the view angles. Under Melee aim with Head or Off
// hand the view is turned onto that source before the press reaches the game (action_aim_hook.cpp), so the
// hands are left where the game puts them from its view while the fists are held: the punch goes where the
// view looks (issue #15: with the arms kept on the weapon hand the punch went along the controller).
constexpr std::string_view kFistsPrefix = "weapon/player/fists";
// While the player swims the game puts these fists in the hands, or the arms hold their pose with a gun in
// the slot (ArmsTick); the swim and its dash follow the game's view.
constexpr std::string_view kSwimFists = "weapon/player/fists_swim";

// The held item's decl, its offset and its sight row (viewmodel hook thread only).
const std::byte* g_cachedDecl = nullptr;
bool g_cachedSeated = false;
game::WeaponOffset g_cachedOffset;
std::optional<game::WeaponSight> g_cachedSight;    // not with ETERNALVR_WEAPON_SIGHTS=0
std::optional<game::WeaponSight> g_cachedOwnSight; // the weapon's own row, the mods not read
ModDecls g_cachedMods{};                           // the held weapon's active mods (weapon_mods.hpp)
bool g_cachedFists = false;                        // the held item is the fists of a melee or a Blood Punch
// The held item's decl the swim check last saw (viewmodel hook thread only), and its answer (read by the
// camera hook on any thread).
const std::byte* g_swimDecl = nullptr;
std::atomic<bool> g_swimFists{false};
std::atomic<std::uint64_t> g_loggedFists{0};
std::atomic<std::uint64_t> g_loggedWeapons{0};
std::atomic<std::uint64_t> g_loggedMods{0};
std::atomic<std::uint64_t> g_loggedPlacements{0};

// How far a sight row turns the model: the sight line's angle from the model's forward.
float sightTurnDegrees(const game::WeaponSight& sight) {
    const float c = std::cos(sight.pitch * kRadiansPerDegree) * std::cos(sight.yaw * kRadiansPerDegree);
    return std::acos(std::fmin(1.0f, std::fmax(-1.0f, c))) / kRadiansPerDegree;
}

std::optional<xr_math::IdAngles> sightAngles(const std::optional<game::WeaponSight>& sight) {
    return sight ? std::optional<xr_math::IdAngles>({sight->pitch, sight->yaw, 0.0f}) : std::nullopt;
}

// The decl name of the item in the right hand, or empty.
std::string declName(const std::byte* decl) {
    const char* name = nullptr;
    if (!safeRead(decl + kDeclName, name) || !name) {
        return {};
    }
    char buffer[128] = {};
    if (!safeCopy(buffer, name, sizeof(buffer) - 1)) {
        // The string may end near the end of its page; read it a byte at a time.
        for (std::size_t i = 0; i + 1 < sizeof(buffer); ++i) {
            if (!safeCopy(buffer + i, name + i, 1) || buffer[i] == '\0') {
                break;
            }
        }
    }
    buffer[sizeof(buffer) - 1] = '\0';
    const std::string_view text(buffer);
    for (const char c : text) {
        if (c < 0x20 || c > 0x7E) {
            return {};
        }
    }
    return std::string(text);
}

// The held item's decl, and the one the arms are placed and held by (the tick's ArmsTick).
const std::byte* g_loggedArmsHeld = nullptr;
std::atomic<int> g_armsLines{0};

game::WeaponOffset offsetFor(const ArmsTick& arms) {
    const input::ControllerSettings& cfg = settings();
    const std::byte* held = arms.held;
    const std::byte* decl = arms.arms;
    if (decl != held && held != g_loggedArmsHeld && g_armsLines.fetch_add(1) < 40) {
        EVR_LOG("%s: the hands swim with '%s' held: the arms are placed and held as the swim fists'", kTag,
                held ? declName(held).c_str() : "");
    }
    g_loggedArmsHeld = decl != held ? held : nullptr;
    if (decl != g_swimDecl) {
        g_swimDecl = decl;
        g_swimFists.store(decl && declName(decl).starts_with(kSwimFists), std::memory_order_relaxed);
    }
    // Seated offsets when ETERNALVR_SEATED says so or the room's posture is seated (T-074).
    const bool seated = cfg.seated || roomPosture() == posture::Posture::Seated;
    // The held weapon's active mods, each tick: a mod switch turns the model on the next update.
    const ModDecls mods = weaponModDecls(heldItemObject(arms.hands, decl));
    const bool itemChanged = decl != g_cachedDecl || seated != g_cachedSeated;
    if (itemChanged || mods != g_cachedMods) {
        g_cachedDecl = decl;
        g_cachedSeated = seated;
        g_cachedMods = mods;
        const std::string name = decl ? declName(decl) : std::string{};
        g_cachedFists = name.starts_with(kFistsPrefix);
        // ETERNALVR_VIEWMODEL_OFFSET replaces the table's offset; a sight row still applies on top of it.
        g_cachedOffset = cfg.viewmodelOffset
                             ? *cfg.viewmodelOffset
                             : state().offsets.lookup(name, seated ? game::OffsetPosture::Seated
                                                                   : game::OffsetPosture::Standing);
        const std::array<std::string, 2> modNames{mods[0] ? declName(mods[0]) : std::string{},
                                                  mods[1] ? declName(mods[1]) : std::string{}};
        const SightRows rows = sightRowsFor(name, modNames);
        const std::optional<game::SightRow>& sightRow = rows.row;
        g_cachedSight = sightRow ? std::optional<game::WeaponSight>(sightRow->sight) : std::nullopt;
        g_cachedOwnSight = rows.own ? std::optional<game::WeaponSight>(rows.own->sight) : std::nullopt;
        if (itemChanged ? g_loggedWeapons.fetch_add(1) < 40 : g_loggedMods.fetch_add(1) < 40) {
            char sight[160] = "off";
            if (cfg.weaponSights) {
                if (g_cachedSight) {
                    std::snprintf(sight, sizeof(sight),
                                  "row '%.*s' (%.2f %.2f) deg: the model turned %.2f deg",
                                  static_cast<int>(sightRow->key.size()), sightRow->key.data(),
                                  g_cachedSight->pitch, g_cachedSight->yaw, sightTurnDegrees(*g_cachedSight));
                } else {
                    std::snprintf(sight, sizeof(sight), "no row: not turned");
                }
            }
            EVR_LOG("%s: %s '%s' (mods '%s', '%s'): viewmodel offset (%.3f %.3f %.3f) m, (%.1f %.1f %.1f) "
                    "deg%s; sights %s",
                    kTag, itemChanged ? "held item" : "mods changed on held", name.c_str(),
                    modNames[0].c_str(), modNames[1].c_str(), g_cachedOffset.forward, g_cachedOffset.left,
                    g_cachedOffset.up, g_cachedOffset.pitch, g_cachedOffset.yaw, g_cachedOffset.roll,
                    cfg.viewmodelOffset ? " (ETERNALVR_VIEWMODEL_OFFSET)" : "", sight);
        }
    }
    return g_cachedOffset;
}

void onViewmodel(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    State& s = state();
    const input::ControllerSettings& cfg = settings();
    const auto* hands = reinterpret_cast<const std::byte*>(regs.rdi);
    const std::byte* owner = nullptr;
    if (!safeRead(hands + kHandsOwner, owner) || !isPlayerSafe(owner)) {
        return;
    }
    // Every tick of the player's hands counts, the game's own included: the views' poses age by ticks
    // (view_pose_clock.hpp).
    const TickView view = handsTick();
    const ArmsTick arms = noteArmsTick(hands); // the tick's arms' item, for every hook of the tick
    if (!cfg.viewmodel || !s.attached.load(std::memory_order_acquire) || s.yielding.load()) {
        return; // forced views (glory kills, cutscenes) keep the game's own hands animation
    }
    noteZoomLookHands(hands, owner); // the gun stays in the hand while zoomed (zoom_look.cpp)
    float eye[3];
    const bool eyeRead = safeCopy(eye, owner + kPlayerFirstPersonOrigin, sizeof(eye)) &&
                         std::isfinite(eye[0]) && std::isfinite(eye[1]) && std::isfinite(eye[2]);
    // g_cachedFists is the last tick's until offsetFor() below reads this tick's item; under head aim the
    // placement does not depend on it, and under hand aim the GameView test below reads it again after.
    if (input::gunPlacement(cfg.aim, g_cachedFists, cfg.actionAim.melee) == input::GunPlacement::Head) {
        // Head aim: the game's placement moved to the rendered head. The weapon hand is not needed, and with
        // the arms model away from the hand both arms are the game's (gun_placement.hpp).
        offsetFor(arms); // the held item's log line and the swim's fists
        if (eyeRead) {
            head_gun::moveToHead(hands, reinterpret_cast<std::byte*>(regs.rsp + kOriginFromRsp),
                                 {eye[0], eye[1], eye[2]});
        }
        return;
    }
    const WorldHand& world = view.world;
    if (!view.worldCurrent || !eyeRead) {
        return;
    }
    const game::WeaponOffset offset = offsetFor(arms);
    if (input::gunPlacement(cfg.aim, g_cachedFists, cfg.actionAim.melee) == input::GunPlacement::GameView) {
        if (g_loggedFists.fetch_add(1) == 0) {
            EVR_LOG("%s: viewmodel: the fists follow the game's view, on the melee aim's %s", kTag,
                    input::actionAimSourceName(cfg.actionAim.melee));
        }
        return;
    }
    // The weapon in the left hand: once the left glove's wrist on the held item is known, the model moved
    // until it sits in the hand, also while the game has the arms (left_hand_arms.hpp); the rows are measured
    // for the right glove.
    left_hand_arms::Placement lefty =
        offhand_mods::leftGlove() ? left_hand_arms::placement() : left_hand_arms::Placement{};
    lefty.weight = lefty.decl == g_cachedDecl ? lefty.weight : 0.0f; // for the held item only
    const std::optional<xr_math::IdAngles> sight = sightAngles(g_cachedSight);
    const game::WeaponOffset& w = game::kRowsWrist;
    // A mod's sight row turns the gun about the glove's wrist under the turn applied here; an
    // ETERNALVR_VIEWMODEL_OFFSET is used as given.
    const arm::RowPlacement row = arm::rowPlacement(
        {offset.forward, offset.left, offset.up}, {offset.pitch, offset.yaw, offset.roll}, lefty.weight,
        sightAngles(g_cachedOwnSight), sight, {w.forward, w.left, w.up}, !cfg.viewmodelOffset);
    xr_math::EyeRelativePose placed =
        xr_math::placeViewmodel(world.grip, world.gripAxis, row.translation, row.rotation, sight,
                                {w.forward, w.left, w.up}, world.unitsPerMetre);
    if (lefty.weight > 0.0f) {
        placed.offset =
            placed.offset + arm::leftGloveShift(placed, lefty.leftWrist, world.grip.offset, world.gripAxis,
                                                {w.forward, -w.left, w.up}, world.unitsPerMetre) *
                                lefty.weight;
    }
    const Vec3 origin = xr_math::atEye({eye[0], eye[1], eye[2]}, placed);
    const float newOrigin[3] = {origin.x, origin.y, origin.z};
    const float newAxis[9] = {placed.axis.forward.x, placed.axis.forward.y, placed.axis.forward.z,
                              placed.axis.left.x,    placed.axis.left.y,    placed.axis.left.z,
                              placed.axis.up.x,      placed.axis.up.y,      placed.axis.up.z};
    auto* originAt = reinterpret_cast<std::byte*>(regs.rsp + kOriginFromRsp);
    auto* axisAt = reinterpret_cast<std::byte*>(static_cast<std::intptr_t>(regs.rbp) + kAxisFromRbp);
    float gameOrigin[3] = {};
    safeCopy(gameOrigin, originAt, sizeof(gameOrigin));
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    safeCopy(originAt, newOrigin, sizeof(newOrigin));
    safeCopy(axisAt, newAxis, sizeof(newAxis));
    s.viewmodelWrites.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard lock(s.viewMutex);
        s.model = {true, nowQpc(), placed}; // the off hand's joint is placed relative to this
    }
    if (g_loggedPlacements.fetch_add(1) < 5) {
        EVR_LOG("%s: viewmodel: game origin (%.2f %.2f %.2f) -> hand (%.2f %.2f %.2f); eye (%.2f %.2f %.2f)",
                kTag, gameOrigin[0], gameOrigin[1], gameOrigin[2], newOrigin[0], newOrigin[1], newOrigin[2],
                eye[0], eye[1], eye[2]);
    }
}

bool viewmodelSiteChecks(const std::byte* site) {
    for (const Disp& d : kViewmodelDisps) {
        if (readI32(site + d.at) != d.value) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string itemDeclName(const std::byte* decl) {
    return declName(decl);
}

bool swimFistsHeld() {
    return g_swimFists.load(std::memory_order_relaxed);
}

const std::byte* heldItemDecl(const std::byte* hands) {
    const std::byte* decl = nullptr;
    return hands && safeRead(hands + kHandsRightItemDecl, decl) ? decl : nullptr;
}

bool installViewmodelHook() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    const std::byte* site = findUnique(image, kTag, "viewmodel store", kViewmodelSignature);
    if (!site) {
        return false;
    }
    if (!viewmodelSiteChecks(site)) {
        EVR_LOG("%s: the viewmodel stores do not name the render model's pose fields; viewmodel hook off",
                kTag);
        return false;
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(site), &onViewmodel, error)) {
        EVR_LOG("%s: viewmodel hook at RVA 0x%X failed: %s", kTag, image.rva(site), error.c_str());
        return false;
    }
    EVR_LOG("%s: viewmodel hook at RVA 0x%X", kTag, image.rva(site));
    return true;
}

} // namespace evr::vkcore::controllers
