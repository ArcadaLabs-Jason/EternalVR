// Motion controllers, the thumb-rest weapon wheel (controllers.hpp, features/input/rest_wheel.hpp): what
// holds it back in the game, its log lines, and the game's slowdown under it.
//
// The wheel itself runs in the mapper. Here: the game states it starts nothing in, how long it holds the
// game's wheel (the game's own open delay, weaponWheel_HoldTimeForOpeningWheel, read every run: the player
// may change Weapon Wheel Open Delay in the game's menu; 0.30 s when the cvar is not found), and what it did.
// Every arming is logged (capped like the action lines), so a rig run or a player's log shows how often it
// starts and from which rest, and so does the first touch each rest reports: a runtime that never reports the
// thumb rest (a Touch emulation that leaves the sensor out) shows as no such line, and one line says so after
// a minute of play. Every 10 s with any touch, one line per rest counts what its sensor did (RestWheelStats):
// touches too short to register, gaps bridged, quick returns, and landings with the other stick already out,
// so a player's log shows whether touches go missing.
//
// The slowdown (features/input/wheel_slowdown.hpp): with ETERNALVR_THUMBREST_SLOWDOWN=0, weaponWheel_
// slowTimeScale is held at 1 while this wheel holds the game's wheel open, written through the cvar book
// (cvar_book.hpp: the value before the layer's first write goes back on a multiplayer guard trip) from the
// user-command thread, a few writes per wheel use (the risk the book describes). The game's own value, read
// just before each hold, is written back after the wheel closes, when the controllers go stale, when the
// guard trips and when the layer shuts down. With the slowdown on (the default) the cvar is not even looked
// up.

#include "vkcore/controllers_impl.hpp"

#include "features/input/wheel_slowdown.hpp"
#include "platform/mp_policy/saved_cvars.hpp"
#include "vkcore/cvar_book.hpp"
#include "vkcore/demon_view.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/taa_locate.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";
// Play time with the wheel usable and no rest touched before the one line saying so.
constexpr float kNoTouchNoteSeconds = 60.0f;
constexpr float kStatsSeconds = 10.0f;

// Mapper only (mapperMutex).
LogCap g_lines{300};
LogCap g_voided{20};
std::array<bool, 2> g_touchSeen{};
float g_untouchedSeconds = 0.0f;
bool g_noTouchLogged = false;
input::RestWheelStats g_loggedStats;
float g_statsSeconds = 0.0f;
LogCap g_statsLines{360}; // an hour of touch lines, then one a minute

bool due(LogCap& cap, char (&note)[48]) {
    std::uint64_t skipped = 0;
    if (!cap.due(GetTickCount64(), skipped)) {
        return false;
    }
    note[0] = '\0';
    if (skipped != 0) {
        std::snprintf(note, sizeof(note), " (%llu more not logged)",
                      static_cast<unsigned long long>(skipped));
    }
    return true;
}

constexpr std::string_view kSlowdownCvar = "weaponWheel_slowTimeScale";
constexpr std::string_view kOpenDelayCvar = "weaponWheel_HoldTimeForOpeningWheel";
// A delay outside this (milliseconds) is not taken: the hold stays at its minimum.
constexpr int kMaxOpenDelayMilliseconds = 2000;

// Located once by installGameHooks, then read by the mapper.
std::atomic<const std::byte*> g_openDelayCvar{nullptr};
int g_loggedOpenDelay = -1; // mapper only
constexpr const char* kHeldValue = "1";

// The slowdown hold: located once by installGameHooks, then the mapper and the shutdown under g_slowMutex.
std::mutex g_slowMutex;
std::byte* g_slowCvar = nullptr;
cvar_book::SetStringFn g_setCvar = nullptr;
input::WheelSlowdown g_slowdown;
std::optional<std::string> g_gameValue; // the game's value under the current hold
LogCap g_slowLines{12};

// The cvar's value as the setter takes it back (its values block: the integer at +0x08, the float at +0x0C).
std::optional<std::string> slowdownValue() {
    const std::byte* values = nullptr;
    int integer = 0;
    float number = 0.0f;
    if (!safeRead(g_slowCvar, values) || !values || !safeRead(values + 0x08, integer) ||
        !safeRead(values + 0x0C, number)) {
        return std::nullopt;
    }
    return mp_policy::cvarValueText(integer, number);
}

// g_slowMutex held.
void hold() {
    if (!g_slowCvar || g_gameValue) {
        return;
    }
    const auto value = slowdownValue();
    if (!value || !cvar_book::write(kSlowdownCvar, g_slowCvar, g_setCvar, kHeldValue)) {
        return;
    }
    g_gameValue = *value;
    char note[48] = "";
    if (due(g_slowLines, note)) {
        EVR_LOG(
            "%s: thumb-rest wheel: %s %s -> %s while the wheel is open (ETERNALVR_THUMBREST_SLOWDOWN=0)%s",
            kTag, std::string(kSlowdownCvar).c_str(), value->c_str(), kHeldValue, note);
    }
}

// g_slowMutex held. After a guard trip the cvar book has already written the value back.
void restore(const char* why) {
    if (!g_gameValue) {
        return;
    }
    const std::string value = *g_gameValue;
    g_gameValue.reset();
    const bool written = cvar_book::write(kSlowdownCvar, g_slowCvar, g_setCvar, value.c_str());
    char note[48] = "";
    if (!written || due(g_slowLines, note)) {
        EVR_LOG("%s: thumb-rest wheel: %s back to %s, the game's own value (%s)%s%s", kTag,
                std::string(kSlowdownCvar).c_str(), value.c_str(), why,
                written ? "" : "; not written here: the cvar book gives it back on a guard trip", note);
    }
}

void applySlowdown(input::SlowdownAction action, const char* why) {
    if (action == input::SlowdownAction::Hold) {
        hold();
    } else if (action == input::SlowdownAction::Restore) {
        restore(why);
    }
}

const char* side(input::Hand hand) {
    return hand == input::Hand::Left ? "left" : "right";
}

const char* voidReason(input::RestWheelVoid why) {
    switch (why) {
    case input::RestWheelVoid::OwnStick:
        return "straight from its own stick";
    case input::RestWheelVoid::OwnButton:
        return "straight from its own face button";
    case input::RestWheelVoid::StickOut:
    case input::RestWheelVoid::None:
        break;
    }
    return "with a stick out of the centre";
}

// The game's wheel open delay in milliseconds (a string cvar, "180"; its integer at +0x08), or nullopt.
std::optional<int> openDelayMilliseconds() {
    const std::byte* cvar = g_openDelayCvar.load(std::memory_order_acquire);
    const std::byte* values = nullptr;
    int milliseconds = 0;
    if (!cvar || !safeRead(cvar, values) || !values || !safeRead(values + 0x08, milliseconds) ||
        milliseconds <= 0 || milliseconds > kMaxOpenDelayMilliseconds) {
        return std::nullopt;
    }
    return milliseconds;
}

void noteTouches(const input::InputFrame& frame, const input::RestWheel& rest, float dt) {
    if (!rest.usable()) {
        return;
    }
    for (const input::Hand hand : {input::Hand::Left, input::Hand::Right}) {
        const std::size_t i = static_cast<std::size_t>(hand);
        const input::HandState& h = frame.hand(hand);
        if (!g_touchSeen[i] && rest.hands().hasRest[i] &&
            (h.thumbRest || h.primaryTouch || h.secondaryTouch)) {
            g_touchSeen[i] = true;
            EVR_LOG("%s: thumb-rest wheel: the %s controller reports a resting thumb", kTag, side(hand));
        }
    }
    if (g_touchSeen[0] || g_touchSeen[1] || g_noTouchLogged) {
        return;
    }
    g_untouchedSeconds += dt;
    if (g_untouchedSeconds >= kNoTouchNoteSeconds) {
        g_noTouchLogged = true;
        EVR_LOG(
            "%s: thumb-rest wheel: no resting thumb reported in %.0f s of play; the runtime may not report "
            "the thumb rests (the wheel then never starts)",
            kTag, kNoTouchNoteSeconds);
    }
}

// Every kStatsSeconds: what each rest's sensor did since the last line, for the rests that saw a touch.
void noteTouchStats(const input::RestWheel& rest, float dt) {
    if (!rest.usable()) {
        return;
    }
    g_statsSeconds += dt;
    if (g_statsSeconds < kStatsSeconds) {
        return;
    }
    g_statsSeconds = 0.0f;
    const input::RestWheelStats& now = rest.stats();
    for (const input::Hand hand : {input::Hand::Left, input::Hand::Right}) {
        const std::size_t i = static_cast<std::size_t>(hand);
        const auto since = [i](const std::array<std::uint32_t, 2>& a, const std::array<std::uint32_t, 2>& b) {
            return static_cast<unsigned>(a[i] - b[i]);
        };
        const unsigned raw = since(now.rawTouches, g_loggedStats.rawTouches);
        if (raw == 0) {
            continue;
        }
        char note[48] = "";
        if (due(g_statsLines, note)) {
            EVR_LOG(
                "%s: thumb-rest wheel: %s rest, last %.0f s: %u touch(es) from the sensor, %u gone within "
                "%.2f s (not registered), %u gap(s) under %.2f s bridged; %u landing(s), %u back within %.2f "
                "s "
                "of letting go, %u with the %s stick already out (%u of it out %.2f s or less)%s",
                kTag, side(hand), kStatsSeconds, raw, since(now.shortTouches, g_loggedStats.shortTouches),
                input::kRestDebounceSeconds, since(now.bridgedGaps, g_loggedStats.bridgedGaps),
                input::kRestDebounceSeconds, since(now.landings, g_loggedStats.landings),
                since(now.quickReturns, g_loggedStats.quickReturns), input::kQuickReturnSeconds,
                since(now.stickOut, g_loggedStats.stickOut), side(input::otherHand(hand)),
                since(now.stickOutRecent, g_loggedStats.stickOutRecent), input::kRecentStickSeconds, note);
        }
    }
    g_loggedStats = now;
}

} // namespace

bool restWheelBlocked(const State& s) {
    return s.gameSuppressing.load(std::memory_order_relaxed) || forcedView() ||
           s.skippableCutscene.load(std::memory_order_relaxed) || pilotingDemon();
}

void noteRestWheel(const input::InputFrame& frame, const input::GameInput& input, float dt) {
    const State& s = state();
    if (!s.mapper) {
        return;
    }
    noteTouches(frame, s.mapper->restWheel(), dt);
    noteTouchStats(s.mapper->restWheel(), dt);
    const input::RestWheelOutput& w = input.restWheel;
    {
        std::lock_guard lock(g_slowMutex);
        const bool otherWheel = game::contains(input.down, game::GameAction::WeaponWheel) && !w.wheelDown;
        applySlowdown(g_slowdown.update(w.wheelDown, otherWheel, dt), "the wheel closed");
    }
    char note[48] = "";
    switch (w.event) {
    case input::RestWheelEvent::None:
        break;
    case input::RestWheelEvent::Voided:
        if (due(g_voided, note)) {
            EVR_LOG("%s: thumb-rest wheel: the %s thumb landed %s; no window%s", kTag, side(w.restHand),
                    voidReason(w.voided), note);
        }
        break;
    case input::RestWheelEvent::Armed:
        if (due(g_lines, note)) {
            const input::RestWheelSettings& settings = s.mapper->restWheel().settings();
            if (settings.mode == input::RestWheelMode::Extreme) {
                EVR_LOG("%s: thumb-rest wheel: armed: the turn stick picks weapons; the %s thumb rest gives "
                        "turning back (extreme)%s",
                        kTag, side(w.restHand), note);
                break;
            }
            char after[48] = "";
            if (settings.mode == input::RestWheelMode::Edge) {
                std::snprintf(after, sizeof(after), ", %.2f s after the touch", w.sinceTouch);
            }
            EVR_LOG("%s: thumb-rest wheel: armed: the %s stick picks (%s thumb rest, %s%s)%s", kTag,
                    side(w.stickHand), side(w.restHand), input::restWheelModeName(settings.mode), after,
                    note);
        }
        break;
    case input::RestWheelEvent::Opened:
        if (due(g_lines, note)) {
            EVR_LOG("%s: thumb-rest wheel: the game's wheel is held, pointing %s%s", kTag,
                    input::wheelDirectionName(w.direction), note);
        }
        break;
    case input::RestWheelEvent::Picked:
        if (due(g_lines, note)) {
            EVR_LOG("%s: thumb-rest wheel: %s picked by the %s stick pointing %s (out to %.2f)%s", kTag,
                    w.slot ? std::string(game::gameActionName(*w.slot)).c_str() : "nothing",
                    side(w.stickHand), input::wheelDirectionName(w.direction), w.peak, note);
        }
        break;
    case input::RestWheelEvent::Cancelled:
        if (due(g_lines, note)) {
            EVR_LOG(
                "%s: thumb-rest wheel: cancelled, nothing pressed (the stick did not stay in one direction "
                "long enough, or the game held it back; out to %.2f)%s",
                kTag, w.peak, note);
        }
        break;
    case input::RestWheelEvent::Released:
        if (due(g_lines, note)) {
            EVR_LOG(
                "%s: thumb-rest wheel: the game's wheel let go pointing %s: the game picks the highlighted "
                "weapon%s",
                kTag, input::wheelDirectionName(w.direction), note);
        }
        break;
    }
}

float restWheelHoldSeconds() {
    const std::optional<int> delay = openDelayMilliseconds();
    const float hold =
        input::wheelHoldSeconds(delay ? std::optional<float>(static_cast<float>(*delay)) : std::nullopt);
    const int logged = delay.value_or(0);
    if (logged != g_loggedOpenDelay && settings().thumbRest.mode != input::RestWheelMode::Off) {
        g_loggedOpenDelay = logged;
        if (delay) {
            EVR_LOG(
                "%s: thumb-rest wheel: the game opens its wheel %d ms after the press (%s): held at least "
                "%.2f s",
                kTag, *delay, std::string(kOpenDelayCvar).c_str(), hold);
        } else {
            EVR_LOG("%s: thumb-rest wheel: the game's wheel open delay is not known: held at least %.2f s",
                    kTag, hold);
        }
    }
    return hold;
}

void locateWheelCvars() {
    const input::ControllerSettings& cfg = settings();
    if (cfg.thumbRest.mode == input::RestWheelMode::Off) {
        return;
    }
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return;
    }
    const std::vector<std::byte*> objects = findCvarObjects(image, {kSlowdownCvar, kOpenDelayCvar});
    if (objects.size() == 2 && objects[1]) {
        g_openDelayCvar.store(objects[1], std::memory_order_release);
    } else {
        EVR_LOG("%s: thumb-rest wheel: %s not found; the wheel is held at least %.2f s", kTag,
                std::string(kOpenDelayCvar).c_str(), input::kMinWheelHoldSeconds);
    }
    if (cfg.thumbRestSlowdown) {
        return; // the game's own slowdown: nothing to hold
    }
    const auto set = reinterpret_cast<cvar_book::SetStringFn>(const_cast<std::byte*>(findCvarSetter(image)));
    std::lock_guard lock(g_slowMutex);
    if (objects.empty() || !objects[0] || !set) {
        EVR_LOG("%s: thumb-rest wheel: %s not found; the wheel slows time as the game's does", kTag,
                std::string(kSlowdownCvar).c_str());
        return;
    }
    g_slowCvar = objects[0];
    g_setCvar = set;
    g_slowdown = input::WheelSlowdown(false);
    const auto value = slowdownValue();
    EVR_LOG("%s: thumb-rest wheel: no slowdown (ETERNALVR_THUMBREST_SLOWDOWN=0): %s, now %s, is held at %s "
            "while the wheel is open",
            kTag, std::string(kSlowdownCvar).c_str(), value ? value->c_str() : "unreadable", kHeldValue);
}

void restoreWheelSlowdown(const char* why) {
    std::lock_guard lock(g_slowMutex);
    applySlowdown(g_slowdown.reset(), why);
}

} // namespace evr::vkcore::controllers
