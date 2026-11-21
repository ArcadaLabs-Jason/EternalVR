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
#include "vkcore/weapon_mods.hpp"

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
// The Sentinel Hammer's slam (comfort::SlamWatch): idPlayer::hands (an idHands held in the idPlayer, the
// viewmodel hook's hands), whose right item's object (weapon_mods.hpp) is an idHammerWeapon while its decl
// is the hammer's; its slamStartTime (an idTypesafeTime, int64), new one game frame after the hammer comes
// up. On the rig the player physics' groundPounding (+0x46C9) stayed 0 through every slam, and
// idPlayer::lastSlamTime (+0x4D08) and the hammer's slamInProgress (+0x3C0A) changed only as it landed.
constexpr std::size_t kPlayerHands = 0xD2C8;
constexpr std::size_t kHammerSlamStartTime = 0x3BE0;
constexpr std::string_view kHammerDecl = "weapon/player/hammer";
// A slam lasts until the feet are down (under 1 s on the rig); this long at most.
constexpr float kSlamHoldSeconds = 2.0f;
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
int g_launches = 0;                    // the pulses so far
bool g_launchOpen = false;             // the latest launch's hold has not been logged as over
std::string g_launchWhat;              // what launched the player, for the end line
comfort::SlamWatch g_slams;            // the Sentinel Hammer's slams
const std::byte* g_heldDecl = nullptr; // the hands' held decl last named
bool g_heldHammer = false;             // ... and whether it is the hammer's
bool g_wasOnGround = false;            // the feet in the last frame read (the slam's line)
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
    comfort::HammerReading hammer;
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
    float origin[3] = {};
    float velocity[3] = {};
    r.valid = safeRead(current + kStateGroundPlane, ground) &&
              safeRead(current + kStateWorldOrigin, origin) && safeRead(current + kStateVelocity, velocity);
    r.onGround = ground != 0;
    r.origin = {origin[0], origin[1], origin[2]};
    r.velocity = {velocity[0], velocity[1], velocity[2]};
    return r;
}

// Under g_mutex: the hands' held item, and the hammer's slam start time while it is the hammer.
void readHammer(const std::byte* player, Reading& r) {
    if (!r.valid) {
        g_heldDecl = nullptr; // a load: a decl's pointer can name another item after it
        return;
    }
    const std::byte* hands = player + kPlayerHands;
    const std::byte* decl = heldItemDecl(hands);
    r.hammer.valid = true;
    if (decl != g_heldDecl) {
        g_heldDecl = decl;
        g_heldHammer = decl && itemDeclName(decl) == kHammerDecl;
    }
    const std::byte* hammer = g_heldHammer ? heldItemObject(hands, decl) : nullptr;
    if (!hammer) {
        return;
    }
    std::int64_t start = 0;
    if (!safeRead(hammer + kHammerSlamStartTime, start)) {
        r.hammer.valid = false; // the watch's base goes: an unread time is no new slam
        return;
    }
    r.hammer.held = true;
    r.hammer.slamStartTime = start;
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
    EVR_LOG("game motion trace: ground %d hammer %d slam %lld excluded %d riding %d; origin %.2f %.2f %.2f; "
            "velocity %.2f %.2f %.2f (%.2f); carried %.2f",
            r.onGround ? 1 : 0, r.hammer.held ? 1 : 0, static_cast<long long>(r.hammer.slamStartTime),
            excluded ? 1 : 0, g_watch.ride().riding() ? 1 : 0, r.origin.x, r.origin.y, r.origin.z,
            r.velocity.x, r.velocity.y, r.velocity.z, length(r.velocity), carried);
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

std::optional<PlayerPlace> playerPhysicsPlace(const std::byte* player) {
    if (!mp_guard::allowsGameTouch() || !isPlayerSafe(player)) {
        return std::nullopt;
    }
    const std::byte* current = player + kPlayerPhysics + kPhysicsCurrent;
    float origin[3] = {};
    float velocity[3] = {};
    if (!safeRead(current + kStateWorldOrigin, origin) || !safeRead(current + kStateVelocity, velocity)) {
        return std::nullopt;
    }
    return PlayerPlace{{origin[0], origin[1], origin[2]}, {velocity[0], velocity[1], velocity[2]}};
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
    Reading r = readPhysics(player);
    // Not rides: a cutscene (on the screen or around the player), a climb, swimming, a piloted demon, death.
    const bool excluded =
        cutscene || wallClimb || swimFistsHeld() || pilotingDemon() || (r.valid && playerDead(player));
    const double now = nowSeconds();
    std::lock_guard lock(g_mutex);
    if (g_lastFrame >= 0.0 && now - g_lastFrame > kRideStaleSeconds) {
        // Frames stopped (a load, a menu, the flat screen): the slam's base and the feet start again.
        g_slams.reset();
        g_wasOnGround = false;
    }
    g_lastFrame = now;
    readHammer(player, r);
    rideFrame(r, excluded, now);
    if (!vignetteOn()) {
        return;
    }
    if (g_slams.update(r.hammer)) {
        startPulse(now, "the Sentinel Hammer's slam", kSlamHoldSeconds);
        // The feet the frame before: the slam's time is new a frame after the hammer comes up, when a
        // standing slam has already thrown the player up.
        if (g_launches <= kLoggedLaunches) {
            EVR_LOG("vignette: launch %d: the Sentinel Hammer's slam, %s", g_launches,
                    g_wasOnGround ? "from the ground" : "in the air");
        }
    }
    if (r.valid) {
        g_pulse.ground(now, r.onGround);
        g_wasOnGround = r.onGround;
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
