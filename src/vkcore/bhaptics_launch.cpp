// bHaptics suits and sleeves: launched by a jump pad or a booster (bhaptics_game.cpp, docs/BHAPTICS.md).
//
// Two mid hooks on the game's own paths, Steam build 25216728 (docs/rig-findings/bhaptics-launch.md):
// - Jump pads: idPlayer's handler of the "touchedBouncePad" event (RVA 0x13E5650). The event ("Player
//   touched a bounce pad", arguments bouncePadEntity and bouncePadDestinationEntity) is posted only by
//   idTrigger_BouncePad::TriggerStuff_Impl (RVA 0xDA9300, vtable slot 601), right after it gave the player
//   the launch velocity. The handler casts its first argument to idTrigger_BouncePad and then sets
//   idPlayer::bouncePadIsInTransit (+0x2F4F3), which the player's think clears on landing. The hook sits just
//   after the cast succeeded (+0x36): rbx = the player, rsi = the pad. The flag as found there tells a touch
//   during a pad's flight (a chain of pads, or the same pad on the next frame).
// - Boosters: idTrigger_SonicBoom::TriggerStuff_Impl (RVA 0xDAF3B0, slot 601), the triggers that blast the
//   player to a destination (e1m2's capitol_trigger_boost_1). The hook sits after the activator was found to
//   be a player and its sonic blast mechanic (idPlayer::playerMechanicSonicBlast, +0x36F78) took the request
//   (+0x54): rsi = the trigger, rdi = the player. A trigger with isLandingPad (+0xD28) ends a blast instead.
// A trigger may run again on the next frames while the player is still in its volume, so LaunchFilter takes
// touches less than kLaunchRepeatSeconds apart for one launch. Field offsets from the type-info tables of
// that build; the hooks go in only with bHaptics on, which needs the build PlayerAim confirmed. Each only
// notes.

#include "vkcore/controllers_impl.hpp"

#include "features/bhaptics/launch.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "bhaptics";

// The touchedBouncePad handler: its prologue, the cast's call, then cmp byte [rbx+0x2F4F3], 0.
constexpr const char* kJumpPadSignature =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 48 8B D9 4D 8B F1 49 8B "
    "C8 "
    "49 8B E8 48 8B FA E8 ?? ?? ?? ?? 48 8B F0 48 85 C0 74 78 80 BB F3 F4 02 00 00";
constexpr std::size_t kJumpPadHook = 0x36; // the cmp: the cast succeeded
// idTrigger_SonicBoom::TriggerStuff_Impl up to mov [rsp+0x38], rbp, where the mechanic took the request.
constexpr const char* kBoosterSignature = "48 89 74 24 20 57 48 83 EC 20 48 8B F1 48 8B FA 48 8B CA E8 ?? ?? "
                                          "?? ?? 84 C0 0F 84 ?? ?? ?? ?? 48 89 5C "
                                          "24 30 48 8D 9F 78 6F 03 00 48 8B 03 48 8B CB FF 50 28 84 C0 75 1B "
                                          "48 8D 0D ?? ?? ?? ?? 48 8B 5C 24 30 48 "
                                          "8B 74 24 48 48 83 C4 20 5F E9 ?? ?? ?? ?? 48 89 6C 24 38";
constexpr std::size_t kBoosterHook = 0x54;

constexpr std::size_t kEntityName = 0x40;
constexpr std::size_t kPlayerPadInTransit = 0x2F4F3;
constexpr std::size_t kPadLaunchSpeed = 0xCB4;
constexpr std::size_t kPadUseFlightTime = 0xCB8;
constexpr std::size_t kPadFlightTime = 0xCBC;
constexpr std::size_t kBoosterSpeed = 0xCDC;
constexpr std::size_t kBoosterLandingPad = 0xD28;
constexpr int kLoggedLaunches = 100;
constexpr int kLoggedRepeats = 20;

GameImage g_image;
std::mutex g_mutex;
bhaptics::LaunchFilter g_filter; // under g_mutex
double g_lastTouch = 0.0;        // under g_mutex
int g_loggedLaunches = 0;        // under g_mutex
int g_loggedRepeats = 0;         // under g_mutex
int g_loggedLandingPads = 0;     // under g_mutex

double seconds() {
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    return static_cast<double>(nowQpc()) / frequency;
}

// One touch of a pad or a booster by the player; `detail` is for the log.
void touched(bhaptics::LaunchSource source, const std::byte* trigger, const std::string& detail) {
    const double now = seconds();
    const std::string name = itemDeclName(trigger + kEntityName);
    std::lock_guard lock(g_mutex);
    const double since = now - g_lastTouch;
    g_lastTouch = now;
    if (!g_filter.note(now)) {
        if (g_loggedRepeats++ < kLoggedRepeats) {
            EVR_LOG("%s: same launch: %s '%s' again %.3f s after the last touch (%s)", kTag,
                    bhaptics::launchSourceName(source), name.c_str(), since, detail.c_str());
        }
        return;
    }
    noteBhapticsLaunch();
    if (g_loggedLaunches++ < kLoggedLaunches) {
        EVR_LOG("%s: launch: %s '%s' (%s)", kTag, bhaptics::launchSourceName(source), name.c_str(),
                detail.c_str());
    }
}

void onJumpPad(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const auto* player = reinterpret_cast<const std::byte*>(regs.rbx);
    const auto* pad = reinterpret_cast<const std::byte*>(regs.rsi);
    if (!isPlayerSafe(player)) {
        return;
    }
    std::uint8_t inFlight = 0;
    std::uint8_t useFlightTime = 0;
    float speed = 0.0f;
    float flightTime = 0.0f;
    safeRead(player + kPlayerPadInTransit, inFlight);
    safeRead(pad + kPadLaunchSpeed, speed);
    safeRead(pad + kPadUseFlightTime, useFlightTime);
    safeRead(pad + kPadFlightTime, flightTime);
    char detail[128];
    if (useFlightTime != 0) {
        std::snprintf(detail, sizeof(detail), "flight time %.2f s, during a pad's flight %d", flightTime,
                      inFlight != 0 ? 1 : 0);
    } else {
        std::snprintf(detail, sizeof(detail), "launch speed %.1f, during a pad's flight %d", speed,
                      inFlight != 0 ? 1 : 0);
    }
    touched(bhaptics::LaunchSource::JumpPad, pad, detail);
}

void onBooster(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const auto* trigger = reinterpret_cast<const std::byte*>(regs.rsi);
    if (!isPlayerSafe(reinterpret_cast<const std::byte*>(regs.rdi))) {
        return;
    }
    std::uint8_t landingPad = 0;
    float speed = 0.0f;
    safeRead(trigger + kBoosterLandingPad, landingPad);
    safeRead(trigger + kBoosterSpeed, speed);
    if (landingPad != 0) {
        std::lock_guard lock(g_mutex);
        if (g_loggedLandingPads++ < kLoggedRepeats) {
            EVR_LOG("%s: booster '%s' is a landing pad: not a launch", kTag,
                    itemDeclName(trigger + kEntityName).c_str());
        }
        return;
    }
    char detail[64];
    std::snprintf(detail, sizeof(detail), "speed %.1f", speed);
    touched(bhaptics::LaunchSource::Booster, trigger, detail);
}

bool install(const char* what, const char* signature, std::size_t offset, MidHookCallback callback) {
    const std::byte* site = findUnique(g_image, kTag, what, signature);
    if (!site) {
        return false;
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(site + offset), callback, error)) {
        EVR_LOG("%s: %s hook at RVA 0x%X failed: %s", kTag, what, g_image.rva(site + offset), error.c_str());
        return false;
    }
    EVR_LOG("%s: %s hook at RVA 0x%X", kTag, what, g_image.rva(site + offset));
    return true;
}

} // namespace

bool installBhapticsLaunchHooks() {
    if (!locateGameImage(g_image, kTag)) {
        return false;
    }
    const bool jumpPad = install("jump pad", kJumpPadSignature, kJumpPadHook, &onJumpPad);
    const bool booster = install("booster", kBoosterSignature, kBoosterHook, &onBooster);
    return jumpPad || booster;
}

} // namespace evr::vkcore::controllers
