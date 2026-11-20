// Motion the game makes, for the comfort vignette (game_motion.hpp).

#include "vkcore/game_motion.hpp"

#include "common/vector.hpp"
#include "features/comfort/ride.hpp"
#include "ui_layer/ui_settings.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/demon_view.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::vkcore::controllers {

namespace {

// idPlayer::physicsObjHavok, an idHavokPhysics_Player held in the idPlayer (body_follow.cpp reads its
// origin).
constexpr std::size_t kPlayerPhysics = 0x8A50;
// idHavokPhysics_Player::current (playerPState_t): groundPlane, the feet on the ground (rig 2026-11-17: 0 in
// a pad's throw and a fall, 1 standing, walking and on the Phobos elevator); worldOrigin, the body's place
// (the feet); velocity, the player physics' own (rig: the run speed while walking, the throw's in a launch,
// 0 standing on the elevator while worldOrigin goes down 6.4 m/s). The type info's groundVelocity
// (+0x4680) read 0 0 -1 everywhere and relativeGroundVelocity, groundPhysics and the ground's spawn ids
// stayed as on the world on the Cultist Base tram (the player carried along by its binding), so neither
// tells a ride; the carried speed (comfort::CarriedSpeed) does on both.
constexpr std::size_t kPhysicsCurrent = 0x3F28;
constexpr std::size_t kStateWorldOrigin = 0x0C;
constexpr std::size_t kStateVelocity = 0x18;
constexpr std::size_t kStateGroundPlane = 0xA0;
// idHavokPhysics_Player::groundPounding: the slam down the Sentinel Hammer makes [inferred: the only ground
// pound of the Slayer; the rig has no save with the hammer].
constexpr std::size_t kPhysicsGroundPounding = 0x46C9;
// A slam lasts until the feet are down; this long at most.
constexpr float kGroundPoundHoldSeconds = 2.0f;
constexpr int kLoggedLaunches = 40;
constexpr int kLoggedRides = 40;
// No game frame for this long (a menu, a load, the flat screen): the ride shows nothing.
constexpr double kRideStaleSeconds = 0.25;
// ETERNALVR_GAME_MOTION_TRACE=1 (rig): the readings every kTraceSeconds while anything moves.
constexpr double kTraceSeconds = 0.25;
constexpr int kTraceLines = 600;

double nowSeconds() {
    return qpcToSeconds(nowQpc());
}

// Under g_mutex.
std::mutex g_mutex;
comfort::LaunchPulse g_pulse;
int g_launches = 0;        // the pulses so far
bool g_launchOpen = false; // the latest launch's hold has not been logged as over
std::string g_launchWhat;  // what launched the player, for the end line
bool g_pounding = false;   // the ground pound seen in the last frame
float g_launchCap = 0.0f;
comfort::RideWatch g_watch;
int g_rides = 0;
double g_lastFrame = -1.0; // the last game frame read
double g_lastTrace = -1.0;
int g_traceLines = 0;

void startPulse(double now, const std::string& what, float cap) {
    if (g_launchOpen && g_launches <= kLoggedLaunches) {
        EVR_LOG("vignette: launch %d (%s) carried on by the next after %.2f s", g_launches,
                g_launchWhat.c_str(), now - g_pulse.start());
    }
    g_pulse.launch(now, cap);
    ++g_launches;
    g_launchOpen = true;
    g_launchWhat = what;
    g_launchCap = cap;
}

// Under g_mutex: the end line of the launch under way once its hold is over.
void logLaunchEnd(double now) {
    if (!g_launchOpen || (!g_pulse.landed() && now < g_pulse.holdEnd())) {
        return;
    }
    g_launchOpen = false;
    if (g_launches > kLoggedLaunches) {
        return;
    }
    if (g_pulse.landed()) {
        EVR_LOG("vignette: launch %d (%s) held %.2f s: on the ground again %.2f s after it", g_launches,
                g_launchWhat.c_str(), g_pulse.holdEnd() - g_pulse.start(),
                g_pulse.landedAt() - g_pulse.start());
    } else {
        EVR_LOG("vignette: launch %d (%s) held %.2f s: its limit (%.2f s), not seen on the ground",
                g_launches, g_launchWhat.c_str(), g_pulse.holdEnd() - g_pulse.start(), g_launchCap);
    }
}

bool readVignetteOn() {
    std::vector<std::string> warnings; // the presenter logs them
    const ui_layer::UiSettings ui = ui_layer::readUiSettings(
        [](std::wstring_view name) -> std::optional<std::wstring> {
            std::wstring text;
            return readEnv(std::wstring(name).c_str(), text) ? std::optional<std::wstring>(text)
                                                             : std::nullopt;
        },
        warnings);
    return ui.vignette != ui_layer::VignetteMode::Off;
}

bool readTrace() {
    std::wstring value;
    return readEnv(L"ETERNALVR_GAME_MOTION_TRACE", value) && value == L"1";
}

bool traceOn() {
    static const bool on = readTrace();
    return on;
}

// One game frame's reading of the player physics.
struct Reading {
    bool valid = false;
    bool onGround = false;
    bool pounding = false;
    Vec3 origin;
    Vec3 velocity;
};

Reading readPhysics(const std::byte* player) {
    Reading r;
    if (!isPlayerSafe(player)) {
        return r;
    }
    const std::byte* physics = player + kPlayerPhysics;
    const std::byte* current = physics + kPhysicsCurrent;
    std::uint8_t ground = 0;
    std::uint8_t pound = 0;
    float origin[3] = {};
    float velocity[3] = {};
    r.valid = safeRead(current + kStateGroundPlane, ground) &&
              safeRead(physics + kPhysicsGroundPounding, pound) &&
              safeRead(current + kStateWorldOrigin, origin) && safeRead(current + kStateVelocity, velocity);
    r.onGround = ground != 0;
    r.pounding = pound != 0;
    r.origin = {origin[0], origin[1], origin[2]};
    r.velocity = {velocity[0], velocity[1], velocity[2]};
    return r;
}

// Under g_mutex.
void trace(const Reading& r, float carried, bool excluded, double now) {
    if (!traceOn() || g_traceLines >= kTraceLines || now - g_lastTrace < kTraceSeconds) {
        return;
    }
    if (length(r.velocity) < 0.3f && carried < 0.3f && !g_watch.ride().riding()) {
        return;
    }
    g_lastTrace = now;
    ++g_traceLines;
    EVR_LOG("game motion trace: ground %d pound %d excluded %d riding %d; origin %.2f %.2f %.2f; velocity "
            "%.2f %.2f %.2f (%.2f); carried %.2f",
            r.onGround ? 1 : 0, r.pounding ? 1 : 0, excluded ? 1 : 0, g_watch.ride().riding() ? 1 : 0,
            r.origin.x, r.origin.y, r.origin.z, r.velocity.x, r.velocity.y, r.velocity.z, length(r.velocity),
            carried);
}

// Under g_mutex.
void rideFrame(const Reading& r, bool excluded, double now) {
    comfort::RideFrame frame;
    frame.valid = r.valid;
    frame.excluded = excluded;
    frame.onGround = r.onGround;
    frame.origin = r.origin;
    frame.ownVelocity = r.velocity;
    const comfort::RideChange change = g_watch.update(frame, now);
    const comfort::RideDetector& ride = g_watch.ride();
    if (change == comfort::RideChange::Started) {
        if (++g_rides <= kLoggedRides) {
            EVR_LOG("game: ride %d starts: the player carried at %.1f m/s or more for %.2f s, more than the "
                    "player's own motion (%.1f m/s now, at %.1f %.1f %.1f)",
                    g_rides, ride.startSpeed(), now - ride.rideStart(), g_watch.carried(), r.origin.x,
                    r.origin.y, r.origin.z);
        }
    } else if (change == comfort::RideChange::Ended && g_rides <= kLoggedRides) {
        EVR_LOG("game: ride %d ends after %.1f s (%s); fastest %.1f m/s, at %.1f %.1f %.1f", g_rides,
                now - ride.rideStart(), comfort::rideEndName(ride.lastEnd()), ride.peakSpeed(), r.origin.x,
                r.origin.y, r.origin.z);
    }
    if (r.valid) {
        trace(r, g_watch.carried(), excluded, now);
    }
}

} // namespace

bool vignetteOn() {
    static const bool on = readVignetteOn();
    return on;
}

void installGameMotionHooks() {
    if (!vignetteOn()) {
        return;
    }
    if (installLaunchHooks()) {
        EVR_LOG("vignette: the launch hooks are in: a jump pad or a booster brings up a pulse");
    } else {
        EVR_LOG("vignette: no launch hooks: jump pads and boosters bring up no pulse");
    }
}

void noteGameLaunch(const char* what, const std::string& name, const comfort::LaunchSize& size) {
    if (!vignetteOn()) {
        return;
    }
    const float cap = comfort::launchHoldSeconds(size);
    std::lock_guard lock(g_mutex);
    startPulse(nowSeconds(), std::string(what) + " '" + name + "'", cap);
}

void noteGameMotionFrame(const std::byte* player, bool cutscene, bool wallClimb) {
    // Nothing at all with the vignette off (bHaptics needs none of it), unless the rig's trace asks.
    if ((!vignetteOn() && !traceOn()) || !mp_guard::allowsGameTouch()) {
        return;
    }
    const Reading r = readPhysics(player);
    // Not rides: a cutscene (on the screen or around the player), a climb, swimming, a piloted demon, death.
    const bool excluded =
        cutscene || wallClimb || swimFistsHeld() || pilotingDemon() || (r.valid && playerDead(player));
    const double now = nowSeconds();
    std::lock_guard lock(g_mutex);
    g_lastFrame = now;
    rideFrame(r, excluded, now);
    if (!vignetteOn()) {
        return;
    }
    if (r.valid) {
        if (r.pounding && !g_pounding) {
            startPulse(now, "the ground pound (the Sentinel Hammer's slam)", kGroundPoundHoldSeconds);
            if (g_launches <= kLoggedLaunches) {
                EVR_LOG("vignette: launch %d: the ground pound (the Sentinel Hammer's slam), %s", g_launches,
                        r.onGround ? "on the ground" : "in the air");
            }
        }
        g_pounding = r.pounding;
        g_pulse.ground(now, r.onGround);
    }
    logLaunchEnd(now);
}

GameMotionVignette gameMotionVignette() {
    GameMotionVignette v;
    const double now = nowSeconds();
    std::lock_guard lock(g_mutex);
    v.launch = g_pulse.amount(now);
    v.ride = g_lastFrame >= 0.0 && now - g_lastFrame <= kRideStaleSeconds ? g_watch.ride().amount() : 0.0f;
    return v;
}

} // namespace evr::vkcore::controllers
