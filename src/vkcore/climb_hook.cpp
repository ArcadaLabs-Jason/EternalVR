// Motion controllers, climbable walls (controllers.hpp).
//
// The game's wall-climb mechanic (idPlayerMechanicWallClimb, Steam build 25216728) owns the view while the
// player clings to a climbable wall when wallclimb_takeoverViewAngles is 1, its default: on the jump to the
// wall it sets the player's inhibit bits to 0xF (with the view bit), and every tick it turns its own copy of
// the view by the user command's angle changes only (the mouse and the turn stick), clamps it to the wall,
// and sets the player's view angles from it (idPlayer::SetViewAngles, returning to RVA 0x13B87CE). Its jump
// off the wall goes along the player's first-person view axis, which it copies at the start of each tick. In
// VR the head could look anywhere while the view, and so the jump, only followed the stick.
//
// With the cvar at 0 the mechanic sets the inhibit bits to 7 (no view bit) and leaves the view angles to the
// player's own update, so head aim keeps the view on the head there as everywhere else, and the jump goes
// where the player looks. The mechanic's dead zone (wallclimb_deadZone_enable), which with the cvar at 0
// would set the view angles itself whenever the view looks down at the wall, is turned off with it. The
// layer holds both at 0 (input::kClimbLookCvars) while it drives the view (head or hand aim).
//
// Under hand aim the view on the wall follows the head, not the weapon hand: a mid hook at the entry of the
// mechanic's dead-zone step (RVA 0x13B88F0), which its per-tick update calls only while the player is on a
// wall, tells the forced-view gate the player is climbing (input::ForcedReason::WallClimb). The viewmodel,
// the shots and the off hand leave the game alone as they did, and hand aim aims with the head. The game's
// own SetViewAngles calls on the wall do not end that: every tick the player's update applies the climb
// animation's deltas to the player (cvar pmec_ApplyAnimDeltasToPlayer; returning to RVA 0x138F33B), setting
// the view to the angles it already has turned by the animation's root rotation, and DisconnectFromWall
// (returning to 0x13B4677) sets it once to the angles the mechanic copied from the player at the start of the
// tick. The first runs after the player's view update in the same think, so both keep the head's aim (the
// gate's foreign calls do not count on the wall, ForcedAngleGate). The climb stats line counts these calls on
// the wall and how many of them turned the view (takeForeignViewWrites, aim_hooks.cpp), to show whether a
// climb animation turns the player.
//
// The game's own values are saved before the first write and written back once when the multiplayer guard
// stops game touches (a player who goes on to play online flat gets the game's wall climbing) and when the
// presenter shuts down (restoreClimbCvars). Neither cvar is saved to the game's config.
//
// ETERNALVR_CLIMB_LOOK=0 leaves the cvars and the gate as the game has them; the hook then only counts.

#include "vkcore/controllers_impl.hpp"

#include "features/input/forced_angles.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/taa_locate.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "climb";

// The wall-climb mechanic's dead-zone step (RVA 0x13B88F0; 0x13BCED0 in the Game Pass build): mov r11, rsp;
// push rbp; push rdi; lea rbp, [rsp-0x78]; sub rsp, 0x178; mov rax, [rip + wallclimb_deadZone_enable];
// mov rdi, rcx; cmp dword [rax+8], 0; je; mov rax, [rip + wallclimb_useMoveTable]; cmp dword [rax+8], 0;
// jne. Its only caller is the mechanic's per-tick update (vtable slot 11, RVA 0x13B7190), and only while
// the player is on a wall. Unique in the Steam and Game Pass builds; rcx is the mechanic.
constexpr const char* kDeadZoneStepSignature =
    "4C 8B DC 55 57 48 8D 6C 24 88 48 81 EC 78 01 00 00 48 8B 05 ?? ?? ?? ?? "
    "48 8B F9 83 78 08 00 0F 84 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 83 78 08 00 "
    "0F 85";
constexpr std::size_t kDeadZoneCvarLoad = 17;  // mov rax, [rip + wallclimb_deadZone_enable]
constexpr std::size_t kMoveTableCvarLoad = 37; // mov rax, [rip + wallclimb_useMoveTable]
constexpr std::size_t kMechanicOwner = 0x18;   // idPlayerMechanic: the idPlayer it belongs to

constexpr ULONGLONG kStatsMilliseconds = 10000;

using SetStringFn = void (*)(void* cvar, const char* value, bool force);

// Set by installClimbHook, then read-only.
bool g_installed = false;
SetStringFn g_setString = nullptr;
std::array<std::byte*, input::kClimbLookCvars.size()> g_cvars{};

// The dead-zone hook (the game's tick) counts, the camera hook reads.
std::atomic<std::uint32_t> g_stepsSinceFrame{0};
std::atomic<std::uint64_t> g_steps{0};

// The cvar hold: the camera hook every frame, the presenter's shutdown once.
std::mutex g_holdMutex;
std::array<input::SavedCvarValue, input::kClimbLookCvars.size()> g_saved{};
std::array<bool, input::kClimbLookCvars.size()> g_written{};
std::uint64_t g_writes = 0;

// Camera hook only.
input::ClimbFrames g_frames;
bool g_loggedEngaged = false;
bool g_loggedNotHeld = false;
std::uint64_t g_headFrames = 0;
// The game's own SetViewAngles calls on the wall, and the view turns among them (takeForeignViewWrites).
std::uint64_t g_wallViewWrites = 0;
std::uint64_t g_wallViewTurns = 0;
float g_wallLargestTurn = 0.0f;
std::uintptr_t g_wallLargestTurnCaller = 0;
std::uint64_t g_loggedFrames = 0;
ULONGLONG g_lastStats = 0;

bool switchOn() {
    static const bool on = [] {
        std::wstring value;
        return !readEnv(L"ETERNALVR_CLIMB_LOOK", value) || input::climbLookSwitch(value);
    }();
    return on;
}

void onDeadZoneStep(const HookRegisters& regs) {
    const auto* mechanic = reinterpret_cast<const std::byte*>(regs.rcx);
    const std::byte* player = nullptr;
    if (!safeRead(mechanic + kMechanicOwner, player) || !isPlayerSafe(player)) {
        return;
    }
    g_stepsSinceFrame.fetch_add(1, std::memory_order_relaxed);
    g_steps.fetch_add(1, std::memory_order_relaxed);
}

// Writes each climb cvar that differs from the value held, saving the game's value first; true when all read
// the held value. g_holdMutex held.
//
// idCVar::SetString runs here on the camera hook's thread (the risk and the assumption: cvar_book.hpp); the
// game reads these two as integers, and a write happens about once a session.
bool holdCvars() {
    bool held = g_setString != nullptr;
    for (std::size_t i = 0; i < g_cvars.size() && held; ++i) {
        const std::string value(input::kClimbLookCvars[i].value);
        const int want = std::atoi(value.c_str());
        const int before = cvarInt(g_cvars[i]);
        if (before != want && mp_guard::allowsGameTouch()) {
            g_saved[i].beforeWrite(before);
            g_setString(g_cvars[i], value.c_str(), true);
            ++g_writes;
            if (!g_written[i] || g_writes <= 12) {
                g_written[i] = true;
                const std::string name(input::kClimbLookCvars[i].name);
                EVR_LOG("%s: %s %d -> %s (reads %d)", kTag, name.c_str(), before, value.c_str(),
                        cvarInt(g_cvars[i]));
            }
        }
        held = cvarInt(g_cvars[i]) == want;
    }
    return held;
}

bool anySaved() {
    for (const input::SavedCvarValue& s : g_saved) {
        if (s.saved()) {
            return true;
        }
    }
    return false;
}

// Writes the game's own values saved before the first write back, once. g_holdMutex held. This is the one
// write made after the multiplayer guard stopped game touches: it undoes the layer's own change, so a player
// who goes on to play online flat has the game's wall climbing.
void restoreCvars(const char* why) {
    for (std::size_t i = 0; i < g_cvars.size(); ++i) {
        const std::optional<int> value = g_saved[i].take();
        if (!value || !g_setString) {
            continue;
        }
        const int before = cvarInt(g_cvars[i]);
        const std::string text = std::to_string(*value);
        g_setString(g_cvars[i], text.c_str(), true);
        const std::string name(input::kClimbLookCvars[i].name);
        EVR_LOG("%s: %s %d -> %s, the game's own value (%s; reads %d)", kTag, name.c_str(), before,
                text.c_str(), why, cvarInt(g_cvars[i]));
    }
}

const char* stateText(bool wanted, bool held) {
    if (!switchOn()) {
        return "ETERNALVR_CLIMB_LOOK=0: the wall-climb mechanic keeps the view";
    }
    if (!wanted) {
        return "aim view: the game keeps its own view";
    }
    return held ? "the view there is the player's own"
                : "the cvars are not held: the mechanic keeps the view";
}

void logStats(bool wanted, bool held) {
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastStats < kStatsMilliseconds || g_frames.frames() == g_loggedFrames) {
        return;
    }
    g_lastStats = now;
    g_loggedFrames = g_frames.frames();
    char turns[96] = "";
    if (g_wallViewTurns > 0) {
        std::snprintf(turns, sizeof(turns), " (largest %.2f deg, from RVA 0x%llX)", g_wallLargestTurn,
                      static_cast<unsigned long long>(g_wallLargestTurnCaller));
    }
    EVR_LOG(
        "%s: %llu frame(s) on a climbable wall in %llu climb(s) (%llu wall-climb step(s)), hand aim on the "
        "head in %llu; the game set the view %llu time(s) on the wall, %llu of them turned it%s; %s",
        kTag, static_cast<unsigned long long>(g_frames.frames()),
        static_cast<unsigned long long>(g_frames.climbs()),
        static_cast<unsigned long long>(g_steps.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_headFrames), static_cast<unsigned long long>(g_wallViewWrites),
        static_cast<unsigned long long>(g_wallViewTurns), turns, stateText(wanted, held));
}

// The foreign SetViewAngles calls since the last frame: kept while on the wall, dropped otherwise.
void noteWallViewWrites(bool onWall) {
    const ForeignViewWrites w = takeForeignViewWrites();
    if (!onWall) {
        return;
    }
    g_wallViewWrites += w.writes;
    g_wallViewTurns += w.turns;
    if (w.turns > 0 && w.largestTurnDegrees > g_wallLargestTurn) {
        g_wallLargestTurn = w.largestTurnDegrees;
        g_wallLargestTurnCaller = w.largestTurnCaller;
    }
}

} // namespace

bool installClimbHook() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    std::vector<std::string_view> names;
    for (const input::ClimbCvar& c : input::kClimbLookCvars) {
        names.push_back(c.name);
    }
    names.push_back("wallclimb_useMoveTable");
    const std::vector<std::byte*> objects = findCvarObjects(image, names);
    const std::byte* step = findUnique(image, kTag, "wall-climb dead-zone step", kDeadZoneStepSignature);
    if (!step) {
        return false;
    }
    // The step's two cvar loads name the cvars found by registration: the right function in this build.
    const std::byte* deadZone = ripTarget(image, step + kDeadZoneCvarLoad + 3, step + kDeadZoneCvarLoad + 7);
    const std::byte* moveTable =
        ripTarget(image, step + kMoveTableCvarLoad + 3, step + kMoveTableCvarLoad + 7);
    for (const std::byte* object : objects) {
        if (!object) {
            EVR_LOG("%s: a wall-climb cvar is missing; the mechanic keeps the view on climbable walls", kTag);
            return false;
        }
    }
    if (deadZone != objects[1] || moveTable != objects[2]) {
        EVR_LOG(
            "%s: the step at RVA 0x%X does not read wallclimb_deadZone_enable and wallclimb_useMoveTable; "
            "the mechanic keeps the view on climbable walls",
            kTag, image.rva(step));
        return false;
    }
    g_setString = reinterpret_cast<SetStringFn>(const_cast<std::byte*>(findCvarSetter(image)));
    if (!g_setString) {
        return false;
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(step), &onDeadZoneStep, error)) {
        EVR_LOG("%s: wall-climb step hook at RVA 0x%X failed: %s", kTag, image.rva(step), error.c_str());
        return false;
    }
    for (std::size_t i = 0; i < g_cvars.size(); ++i) {
        g_cvars[i] = objects[i];
    }
    g_installed = true;
    EVR_LOG(
        "%s: wall-climb step hook at RVA 0x%X; wallclimb_takeoverViewAngles %d, wallclimb_deadZone_enable "
        "%d; %s",
        kTag, image.rva(step), cvarInt(g_cvars[0]), cvarInt(g_cvars[1]),
        switchOn() ? "on a climbable wall the view will follow the head (ETERNALVR_CLIMB_LOOK=0 turns it off)"
                   : "ETERNALVR_CLIMB_LOOK=0: the wall-climb mechanic keeps the view");
    return true;
}

bool climbFrame() {
    if (!g_installed) {
        return false;
    }
    const bool onWall = g_frames.update(g_stepsSinceFrame.exchange(0, std::memory_order_relaxed));
    noteWallViewWrites(onWall);
    const bool wanted = switchOn() && settings().aim != input::AimSource::View;
    const bool allowed = mp_guard::allowsGameTouch();
    bool held = false;
    input::ClimbCvarAction action = input::ClimbCvarAction::None;
    {
        std::lock_guard lock(g_holdMutex);
        action = input::climbCvarAction(wanted, allowed, anySaved());
        if (action == input::ClimbCvarAction::Hold) {
            held = holdCvars();
        } else if (action == input::ClimbCvarAction::Restore) {
            restoreCvars(allowed ? "no longer wanted" : "the multiplayer guard stopped game touches");
        }
    }
    if (action == input::ClimbCvarAction::Hold && !held && !g_loggedNotHeld) {
        g_loggedNotHeld = true;
        EVR_LOG("%s: the wall-climb cvars could not be held; the mechanic keeps the view", kTag);
    }
    if (onWall && !g_loggedEngaged) {
        g_loggedEngaged = true;
        EVR_LOG(
            "%s: on a climbable wall (wallclimb_takeoverViewAngles %d, wallclimb_deadZone_enable %d): %s%s",
            kTag, cvarInt(g_cvars[0]), cvarInt(g_cvars[1]), stateText(wanted, held),
            held ? ", so the jump goes where the player looks" : "");
    }
    logStats(wanted, held);
    return onWall && held;
}

void restoreClimbCvars(const char* why) {
    if (!g_installed) {
        return;
    }
    std::lock_guard lock(g_holdMutex);
    restoreCvars(why);
}

void noteClimbAim(bool headAims) {
    if (headAims) {
        ++g_headFrames;
    }
}

} // namespace evr::vkcore::controllers
