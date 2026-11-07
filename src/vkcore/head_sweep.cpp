// The head sweep through the engine's collision query (head_sweep.hpp, docs/VR_ROOMSCALE.md).
//
// Facts (build 25216728, docs/rig-findings/collision-query.md):
// - idHavokCollision::Translation (RVA 0x4FC0B0) casts a shape from start to end and, given a result
//   pointer, walks the Havok world at once. It takes no lock.
// - The physics step runs on worker threads from the map instance's physics phase until the next game
//   frame syncs it (g_havokSyncOnSameFrame 0): the frame's job list (RVA 0x6DF770) waits for the step and
//   runs the post-step job (RVA 0x502F30, through RVA 0x50C790) before its other jobs. The physics object
//   (map instance + 0x78C58, also *collision world) holds a byte at +0x21089 that the step's start sets and
//   the post-step job clears, and a pending task at +0x21070 that the post-step job waits for.
// - The render-view build, where the camera hook runs, comes after the physics phase: a query there walks
//   the world while the step changes it (the faults players saw at RVA 0x50D326, a collision filter reading
//   a body). A query is therefore made only with the world at rest: the step's byte clear and the pending
//   task, if any, finished. The camera hook sweeps at once when it is (no step this frame: a pause, the
//   step synced in the same frame); otherwise it asks, the sweeps are made at the end of the next post-step
//   job, and it uses the answers a game frame later. The fire hook (inside the frame's own update, where the
//   game makes the same query) sweeps at once.
// - The collision world is the map instance + a displacement that map-instance vslot 0x370 returns
//   (`lea rax, [rcx+disp32]; ret`); the map instance is a global read by the third-person camera's trace
//   (RVA 0x145378B) and the weapon's view trace (RVA 0x135F875).
// - The world keeps ready-made shapes; +0x170 is `clip16sphere`, a 0.16 m sphere. The weapon's view trace
//   casts its own, read from the map instance (+0x99F50 in build 25216728, = collision world + 0xE8): the
//   shot's trace takes that one, so the aim dot stops where a shot would, not on edges a shot clears.
// - trace_t: fraction at +0 (1.0 when nothing was hit), 0x80 bytes.
// - The player's spawn id (the entity the sweep ignores) is idHavokPhysics_Player +0x30.

#include "vkcore/head_sweep.hpp"

#include "features/roomscale/late_trace.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/head_sweep_sites.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/player_aim.hpp"
#include "vkcore/seh_filter.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "room";

constexpr std::size_t kCollisionWorldSlot = 0x370; // map-instance vtable slot returning the world
constexpr std::size_t kSphereShape = 0x170;        // idHavokCollision: clip16sphere (0.16 m)
constexpr std::size_t kPlayerPhysics = 0x8A50;     // idPlayer::physicsObjHavok
constexpr std::size_t kPhysicsSpawnId = 0x30;      // idHavokPhysics_Player: the owner's spawn id
constexpr std::size_t kTraceSize = 0x80;
constexpr std::int32_t kNoSpawnId = 0x1FFFFFE;
constexpr double kStatsSeconds = 10.0;
// Failed looks at a map instance's collision world that are logged; every later sweep looks again.
constexpr std::uint64_t kLoggedLookFailures = 5;
constexpr std::uint64_t kLoggedHeldShots = 5;

using TranslationFn = void(__fastcall*)(void* collision,
                                        std::uint64_t* queryId,
                                        void* result,
                                        const float* start,
                                        const float* end,
                                        void* shape,
                                        const float* axis,
                                        std::int32_t contents,
                                        std::int32_t passSpawnId,
                                        std::uint32_t group,
                                        const char* name,
                                        void* collector,
                                        std::int32_t unused);

GameImage g_image;
TranslationFn g_translation = nullptr;
SweepSites g_sites;
void* const* g_mapInstanceGlobal = nullptr;
PlayerAim g_player;
std::atomic<bool> g_available{false};
std::once_flag g_installOnce;

// Under g_sweepMutex: the camera hook's sweeps and asks, the post-step sweeps (the head, the aim dot's ray, a
// cutscene's fan) and the fire hook's sweep (aim_hooks.cpp), on whichever of the game's threads runs them.
std::mutex g_sweepMutex;
const std::byte* g_cachedMapInstance = nullptr;
std::byte* g_cachedCollision = nullptr;
std::uint64_t g_lookFailures = 0;
std::uint64_t g_calls = 0;
std::uint64_t g_startSolid = 0;
std::uint64_t g_heldShots = 0;
roomscale::LateChannels g_channels;
bool g_loggedPhysics = false;
bool g_loggedPostStep = false;

struct Stats {
    double since = 0.0;
    std::uint64_t now = 0;        // the fire hook's sweeps made
    std::uint64_t heldBack = 0;   // the fire hook's sweeps not made: the world not at rest
    std::uint64_t asks = 0;       // the camera hook's sweeps
    std::uint64_t direct = 0;     // of them, made at once (the world at rest)
    std::uint64_t deferred = 0;   // of them, asked for after the sync (a step in flight)
    std::uint64_t frames = 0;     // post-step jobs with something to sweep
    std::uint64_t swept = 0;      // asks swept there
    std::uint64_t notSwept = 0;   // asks dropped there: another map
    std::uint64_t unfinished = 0; // post-step jobs whose pending task was not finished (nothing swept)
    double frameMicros = 0.0;     // time the post-step hook spent sweeping
    double frameMaxMicros = 0.0;
};
Stats g_stats;

const char kQueryName[] = "EternalVR head";
const float kIdentityAxis[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool copyGuarded(void* destination, const void* source, std::size_t size) {
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

// The call itself, apart from anything with a destructor (__try needs that). False when it faulted.
bool callTranslation(void* collision,
                     void* result,
                     const float* start,
                     const float* end,
                     void* shape,
                     std::int32_t contents,
                     std::int32_t spawnId) {
    __try {
        std::uint64_t queryId = 0;
        g_translation(collision, &queryId, result, start, end, shape, kIdentityAxis, contents, spawnId, 0u,
                      kQueryName, nullptr, 0);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

// The collision world of `mapInstance`, checked through its vtable: the slot must be `lea rax,
// [rcx+disp32]; ret` inside the game's code. A look that cannot read the instance (a map still loading) is
// not kept: the next sweep looks again.
std::byte* collisionWorldOf(const std::byte* mapInstance) {
    if (mapInstance && mapInstance == g_cachedMapInstance) {
        return g_cachedCollision;
    }
    g_cachedMapInstance = nullptr;
    g_cachedCollision = nullptr;
    if (!mapInstance) {
        return nullptr;
    }
    const std::byte* vtable = nullptr;
    const std::byte* slot = nullptr;
    std::uint8_t code[8] = {};
    if (!copyGuarded(&vtable, mapInstance, sizeof(vtable)) || !g_image.contains(vtable) ||
        !copyGuarded(&slot, vtable + kCollisionWorldSlot, sizeof(slot)) ||
        !g_image.inText(slot, sizeof(code)) || !copyGuarded(code, slot, sizeof(code))) {
        if (++g_lookFailures <= kLoggedLookFailures) {
            EVR_LOG(
                "%s: map instance %p: its collision world cannot be read yet; looked for again at the next "
                "sweep (%llu)",
                kTag, static_cast<const void*>(mapInstance), static_cast<unsigned long long>(g_lookFailures));
        }
        return nullptr;
    }
    if (code[0] != 0x48 || code[1] != 0x8D || code[2] != 0x81 || code[7] != 0xC3) {
        EVR_LOG("%s: map-instance vslot 0x%zX is not `lea rax, [rcx+disp]; ret`; head sweep off", kTag,
                kCollisionWorldSlot);
        g_available.store(false);
        return nullptr;
    }
    std::int32_t disp = 0;
    std::memcpy(&disp, code + 3, sizeof(disp));
    g_cachedMapInstance = mapInstance;
    g_cachedCollision = const_cast<std::byte*>(mapInstance) + disp;
    EVR_LOG("%s: collision world at map instance %p + 0x%X", kTag, static_cast<const void*>(mapInstance),
            disp);
    return g_cachedCollision;
}

// Under g_sweepMutex: the current map's collision world, or nullptr.
std::byte* currentCollision() {
    const std::byte* mapInstance = nullptr;
    return copyGuarded(&mapInstance, g_mapInstanceGlobal, sizeof(mapInstance)) ? collisionWorldOf(mapInstance)
                                                                               : nullptr;
}

// The shape a channel casts: the 0.16 m sphere, or for shots the weapon trace's own.
void* shapeOf(const std::byte* collision, bool weaponShape) {
    void* shape = nullptr;
    if (!copyGuarded(&shape, collision + kSphereShape, sizeof(shape)) || !shape) {
        return nullptr;
    }
    if (weaponShape && g_sites.weaponShapeDisp != 0) {
        void* own = nullptr;
        const std::byte* field = g_cachedMapInstance + g_sites.weaponShapeDisp;
        if (copyGuarded(&own, field, sizeof(own)) && own) {
            static std::atomic<bool> logged{false};
            if (!logged.exchange(true)) {
                EVR_LOG("%s: shots are traced with the weapon trace's shape (map instance + 0x%X = collision "
                        "world + 0x%llX)",
                        kTag, static_cast<unsigned>(g_sites.weaponShapeDisp),
                        static_cast<long long>(field - collision));
            }
            return own;
        }
    }
    return shape;
}

// The physics object (*collision world, the object Translation reads the Havok world from), or nullptr.
const std::byte* physicsOf(const std::byte* collision) {
    const std::byte* physics = nullptr;
    return collision && copyGuarded(&physics, collision, sizeof(physics)) ? physics : nullptr;
}

// The physics object's step byte and pending task, as far as they can be read.
roomscale::PhysicsState physicsState(const std::byte* collision) {
    roomscale::PhysicsState state;
    const std::byte* physics = physicsOf(collision);
    std::uint8_t flag = 0;
    if (!physics || !copyGuarded(&flag, physics + g_sites.stepFlagDisp, sizeof(flag))) {
        return state;
    }
    state.stepInFlight = flag;
    const std::byte* task = nullptr;
    if (!copyGuarded(&task, physics + g_sites.taskDisp, sizeof(task))) {
        state.taskPresent = true; // unread: counted as not finished
        return state;
    }
    state.taskPresent = task != nullptr;
    std::uint32_t word = 0;
    if (task && copyGuarded(&word, task + g_sites.taskStateDisp, sizeof(word))) {
        state.taskState = word;
    }
    if (!g_loggedPhysics) {
        g_loggedPhysics = true;
        const char* pending = !task                                                          ? "none"
                              : state.taskState && roomscale::taskFinished(*state.taskState) ? "finished"
                                                                                             : "not finished";
        EVR_LOG("%s: physics object at map instance + 0x%llX; a physics step in flight: %u, pending task %s",
                kTag, static_cast<long long>(physics - g_cachedMapInstance), static_cast<unsigned>(flag),
                pending);
    }
    return state;
}

// Under g_sweepMutex, with the world at rest: the fraction of the way from `from` to `to` to the first
// contact (1: clear), or nullopt when the query faulted (the sweep is then off for the session).
std::optional<float>
sweepWorld(std::byte* collision, Vec3 from, Vec3 to, roomscale::LateQuery query, std::int32_t spawnId) {
    void* world = nullptr;
    if (!copyGuarded(&world, collision, sizeof(world)) || !world) {
        return 1.0f;
    }
    void* shape = shapeOf(collision, query.weaponShape);
    if (!shape) {
        return 1.0f;
    }
    alignas(16) std::byte result[kTraceSize] = {};
    const float start[3] = {from.x, from.y, from.z};
    const float end[3] = {to.x, to.y, to.z};
    if (!callTranslation(collision, result, start, end, shape, query.contents, spawnId)) {
        g_available.store(false);
        EVR_LOG("%s: the collision query faulted; head sweep off for this session", kTag);
        return std::nullopt;
    }
    float fraction = 1.0f;
    std::memcpy(&fraction, result, sizeof(fraction));
    if (++g_calls == 1) {
        EVR_LOG("%s: first head sweep: fraction %.3f, player spawn id 0x%X", kTag, fraction,
                static_cast<unsigned>(spawnId));
    }
    return fraction;
}

// What a fraction of the way to the first contact means for the caller (nullopt: clear).
std::optional<float> contact(float fraction, bool* startedInContact) {
    if (!std::isfinite(fraction) || fraction >= 1.0f) {
        return std::nullopt;
    }
    if (fraction <= 0.0f) {
        if (startedInContact) {
            *startedInContact = true;
            return std::nullopt;
        }
        // The game's eye is itself in contact (a scripted camera, a tight crouch): not a head in a wall.
        if (++g_startSolid <= 5) {
            EVR_LOG("%s: head sweep starts in contact; ignored", kTag);
        }
        return std::nullopt;
    }
    return fraction;
}

unsigned long long ull(std::uint64_t value) {
    return static_cast<unsigned long long>(value);
}

void logStats(double seconds) {
    if (seconds - g_stats.since < kStatsSeconds) {
        return;
    }
    if (g_stats.since > 0.0) {
        EVR_LOG(
            "%s: collision queries last %.0f s: camera hook %llu (%llu at once with the world at rest, %llu "
            "asked with a physics step in flight); after the sync %llu time(s) swept %llu, %llu not (another "
            "map), %llu time(s) the pending task not finished, %.0f us in all, longest %.0f us; fire hook "
            "%llu at once, %llu held back (the world not at rest)",
            kTag, seconds - g_stats.since, ull(g_stats.asks), ull(g_stats.direct), ull(g_stats.deferred),
            ull(g_stats.frames), ull(g_stats.swept), ull(g_stats.notSwept), ull(g_stats.unfinished),
            g_stats.frameMicros, g_stats.frameMaxMicros, ull(g_stats.now), ull(g_stats.heldBack));
    }
    g_stats = Stats{};
    g_stats.since = seconds;
}

bool usable(const std::byte* player) {
    return g_available.load(std::memory_order_acquire) && mp_guard::allowsGameTouch() && player &&
           g_player.isPlayer(player);
}

// The end of the post-step job (RVA 0x502F9C, rbx = the physics object whose step was just synced): the
// camera hook's asks swept, with the world at rest.
void onPostStep(const HookRegisters& registers) {
    if (!g_available.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return;
    }
    const std::lock_guard<std::mutex> lock(g_sweepMutex);
    const double start = nowSeconds();
    std::byte* collision = currentCollision();
    const std::byte* physics = physicsOf(collision);
    if (!roomscale::postStepIsOurs(registers.rbx, reinterpret_cast<std::uintptr_t>(physics))) {
        return; // another physics object's step (another map instance): the asks wait for this one's
    }
    if (!g_loggedPostStep) {
        g_loggedPostStep = true;
        EVR_LOG("%s: the post-step job ran for the physics object at map instance + 0x%llX", kTag,
                static_cast<long long>(physics - g_cachedMapInstance));
    }
    // The post-step job waited for its pending task before this point: one not finished here would be a world
    // still changing, and the asks then wait for the next sync.
    if (!roomscale::syncQueryAllowed(physicsState(collision))) {
        ++g_stats.unfinished;
        return;
    }
    const auto taken = g_channels.takeFor(reinterpret_cast<std::uintptr_t>(collision), g_stats.notSwept);
    bool any = false;
    for (const std::optional<roomscale::LateChannels::Taken>& t : taken) {
        if (!t) {
            continue;
        }
        any = true;
        const std::optional<float> fraction =
            sweepWorld(collision, t->ask.from, t->ask.to, t->query, t->spawnId);
        if (!fraction) {
            return; // faulted: the sweep is off
        }
        g_channels.answer(t->channel, *fraction);
        ++g_stats.swept;
    }
    if (any) {
        const double micros = (nowSeconds() - start) * 1e6;
        ++g_stats.frames;
        g_stats.frameMicros += micros;
        g_stats.frameMaxMicros = std::max(g_stats.frameMaxMicros, micros);
    }
}

void install() {
    if (!locateGameImage(g_image, kTag)) {
        return;
    }
    if (!g_player.init()) {
        EVR_LOG("%s: unknown game build; no head sweep (the lean cap still applies)", kTag);
        return;
    }
    const std::optional<SweepSites> sites = findSweepSites(g_image, kTag);
    if (!sites) {
        return;
    }
    g_sites = *sites;
    g_translation = reinterpret_cast<TranslationFn>(const_cast<std::byte*>(sites->translation));
    g_mapInstanceGlobal = reinterpret_cast<void* const*>(sites->mapInstanceGlobal);
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(sites->postStep), &onPostStep, error)) {
        EVR_LOG("%s: the post-step hook at RVA 0x%X failed (%s); no head sweep (the lean cap still applies)",
                kTag, g_image.rva(sites->postStep), error.c_str());
        return;
    }
    g_available.store(true, std::memory_order_release);
    EVR_LOG(
        "%s: head sweep through the collision query at RVA 0x%X (map instance global RVA 0x%X), only with "
        "the world at rest (the physics object's step byte +0x%zX clear, its pending task +0x%zX "
        "finished): the camera hook sweeps at once then, else asks, made after the physics step's sync "
        "(the post-step job, RVA 0x%X) and used a frame late",
        kTag, g_image.rva(sites->translation), g_image.rva(sites->mapInstanceGlobal), sites->stepFlagDisp,
        sites->taskDisp, g_image.rva(sites->postStep));
}

// The camera hook: swept at once with the world at rest, else asked for after the next sync; the newest
// answer whose sweep started within `fromSlack` of `from`.
std::optional<float> sweepFromCameraHook(std::size_t channel,
                                         const std::byte* player,
                                         Vec3 from,
                                         Vec3 to,
                                         float fromSlack,
                                         bool* startedInContact) {
    if (!usable(player) || channel >= roomscale::kLateChannels) {
        return std::nullopt;
    }
    const std::lock_guard<std::mutex> lock(g_sweepMutex);
    logStats(nowSeconds());
    std::byte* collision = currentCollision();
    std::int32_t spawnId = kNoSpawnId;
    if (!collision || !copyGuarded(&spawnId, player + kPlayerPhysics + kPhysicsSpawnId, sizeof(spawnId))) {
        return std::nullopt;
    }
    roomscale::LateTrace* trace =
        g_channels.use(channel, reinterpret_cast<std::uintptr_t>(collision), spawnId);
    if (!trace) {
        return std::nullopt;
    }
    ++g_stats.asks;
    if (roomscale::syncQueryAllowed(physicsState(collision))) {
        ++g_stats.direct;
        const std::optional<float> now =
            sweepWorld(collision, from, to, roomscale::lateQuery(channel), spawnId);
        if (!now) {
            return std::nullopt; // faulted: the sweep is off
        }
        trace->answerNow(from, to, *now);
    } else {
        ++g_stats.deferred;
        trace->ask(from, to);
    }
    const std::optional<float> fraction = trace->fraction(from, fromSlack);
    return fraction ? contact(*fraction, startedInContact) : std::nullopt;
}

} // namespace

bool installHeadSweep() {
    if (!mp_guard::allowsGameTouch()) {
        return false;
    }
    std::call_once(g_installOnce, &install);
    return g_available.load(std::memory_order_acquire);
}

bool headSweepAvailable() {
    return g_available.load(std::memory_order_acquire);
}

std::optional<float> sweepHead(const std::byte* player, Vec3 from, Vec3 to, float fromSlack) {
    return sweepFromCameraHook(roomscale::kLateHead, player, from, to, fromSlack, nullptr);
}

std::optional<float> sweepShot(const std::byte* player, Vec3 from, Vec3 to, bool& startedInContact) {
    startedInContact = false;
    if (!usable(player)) {
        return std::nullopt;
    }
    const std::lock_guard<std::mutex> lock(g_sweepMutex);
    logStats(nowSeconds());
    std::byte* collision = currentCollision();
    std::int32_t spawnId = kNoSpawnId;
    if (!collision || !copyGuarded(&spawnId, player + kPlayerPhysics + kPhysicsSpawnId, sizeof(spawnId))) {
        return std::nullopt;
    }
    if (!roomscale::syncQueryAllowed(physicsState(collision))) {
        // Not expected inside the frame's own update. The shot then starts at the hand unchecked: logged and
        // counted (the rig QA's head-sweep scenario requires none).
        ++g_stats.heldBack;
        if (++g_heldShots <= kLoggedHeldShots) {
            EVR_LOG(
                "%s: a shot's sweep from the eye to the hand was not made (the world not at rest); the shot "
                "starts at the hand unchecked (%llu)",
                kTag, ull(g_heldShots));
        }
        return std::nullopt;
    }
    ++g_stats.now;
    const std::optional<float> fraction =
        sweepWorld(collision, from, to, roomscale::lateQuery(roomscale::kLateReticle), spawnId);
    return fraction ? contact(*fraction, &startedInContact) : std::nullopt;
}

std::optional<float> sweepShotLate(std::size_t channel,
                                   const std::byte* player,
                                   Vec3 from,
                                   Vec3 to,
                                   float fromSlack,
                                   bool& startedInContact) {
    startedInContact = false;
    return channel != roomscale::kLateHead
               ? sweepFromCameraHook(channel, player, from, to, fromSlack, &startedInContact)
               : std::nullopt;
}

void forgetLateSweeps(std::size_t first, std::size_t count) {
    const std::lock_guard<std::mutex> lock(g_sweepMutex);
    g_channels.forget(first, count);
}

} // namespace evr::vkcore
