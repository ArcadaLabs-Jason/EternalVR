#pragma once

// When the layer may ask the game's collision query, and the sweeps the camera hook asks for after the next
// physics sync (docs/rig-findings/collision-query.md, "When the world may be queried").
//
// The game steps its physics on worker threads across the end of a game frame: the map instance's physics
// phase starts the step, and the next game frame syncs it before its other work (g_havokSyncOnSameFrame 0,
// the default). The render-view build, where the camera hook runs, comes after the physics phase, so a
// query there walks the world while the step changes it. The camera hook therefore sweeps at once only when
// no step is in flight (paused, the step synced in the same frame); otherwise it asks, the sweep is made
// right after the next sync with the world at rest, and the camera hook uses the answer one game frame
// later.

#include "common/vector.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::roomscale {

// The physics object's state as the layer reads it before a query.
struct PhysicsState {
    // The byte the step's start sets and its post-step job clears; nullopt: not readable.
    std::optional<std::uint8_t> stepInFlight;
    // The physics object's pending task (+0x21070, which the post-step job waits for), when there is one:
    // its state word (+0x24), nullopt when not readable.
    bool taskPresent = false;
    std::optional<std::uint32_t> taskState;
};

// A task whose state word says it finished: the low three bits are 4 (the game's wait at RVA 0x50C910 adds
// no waiter then).
[[nodiscard]] bool taskFinished(std::uint32_t state);

// Whether the world may be queried synchronously now: the step's byte read and clear, and the pending task,
// if any, read and finished. Anything unread counts as a step in flight.
[[nodiscard]] bool syncQueryAllowed(const PhysicsState& physics);

// The post-step hook's `rbx` is the physics object of the collision world the asks were made for.
[[nodiscard]] bool postStepIsOurs(std::uintptr_t rbx, std::uintptr_t ourPhysics);

// How many of a channel's own asks an answer may lag behind when it is used. A deferred answer is one ask
// old when the camera hook reads it (the hook asks again first); an answer is never more than a few asks
// old unless the frames went by without a sync, and then the world it was swept in has moved on.
inline constexpr std::uint64_t kLateMaxAsksBehind = 3;

// How far (metres) a sweep's start may have moved since the answer's sweep started: past that the answer is
// for another place (a pit reset, a scripted move). The head's sweep allows its offset's length on top (the
// body walks under the head between frames); a ray's start (the hand, a cutscene camera) moves less in a
// frame.
inline constexpr float kLateHeadSlackMetres = 0.05f;
inline constexpr float kLateRaySlackMetres = 0.5f;

// A sweep asked for: from, to (world, game units), and the channel's ask number.
struct LateAsk {
    Vec3 from;
    Vec3 to;
    std::uint64_t number = 0;
};

// One channel of sweeps (the head, the aim dot's ray, one ray of a cutscene's fan).
class LateTrace {
public:
    // The camera hook: the sweep to make after the next sync (replaces one not taken yet).
    void ask(Vec3 from, Vec3 to);
    // The camera hook with no step in flight: the sweep made at once, asked and answered.
    void answerNow(Vec3 from, Vec3 to, float fraction);
    // After the sync: the ask to sweep now, once; nullopt when there is none.
    [[nodiscard]] std::optional<LateAsk> take();
    // After the sync: the taken ask's fraction of the way to the first contact (1: clear).
    void answer(float fraction);
    // Nothing asked or answered so far applies any more (a camera cut, another map).
    void forget();
    // The camera hook, after its ask: the newest answer, when it lags at most kLateMaxAsksBehind asks and its
    // sweep started within `fromSlack` (game units) of `from` (not after a teleport); nullopt for none, an
    // older or a moved one, or a value that is not a fraction.
    [[nodiscard]] std::optional<float> fraction(Vec3 from, float fromSlack) const;

private:
    std::uint64_t asks_ = 0;
    std::optional<LateAsk> asked_;
    std::optional<LateAsk> taken_;
    std::optional<LateAsk> answeredAsk_;
    float answer_ = 1.0f;
};

// The channels: the aim dot's ray, a cutscene's fan of rays (xr_math::cinemaRayFan), then the head.
inline constexpr std::size_t kLateReticle = 0;
inline constexpr std::size_t kLateCinemaFirst = 1;
inline constexpr std::size_t kLateCinemaRays = 15;
inline constexpr std::size_t kLateHead = kLateCinemaFirst + kLateCinemaRays;
inline constexpr std::size_t kLateChannels = kLateHead + 1;

// What a channel's sweep casts: the head the world's 0.16 m sphere against the world and player clip
// (MASK_PLAYERDEADSOLID, no monsters), the rays the weapon trace's own shape against what a shot hits
// (the weapon trace's contents, monsters in, player clip out).
struct LateQuery {
    std::int32_t contents = 0;
    bool weaponShape = false;
};
inline constexpr std::int32_t kHeadContents = 0x100009;
inline constexpr std::int32_t kShotContents = 0x140001;
[[nodiscard]] LateQuery lateQuery(std::size_t channel);

// Every channel with the collision world and the ignored spawn id its asks were made for.
class LateChannels {
public:
    // The camera hook: `channel`'s ask goes to `world` (an opaque collision world, 0: none) ignoring
    // `spawnId`; another world than before forgets what the channel had. nullptr for a channel out of range.
    LateTrace* use(std::size_t channel, std::uintptr_t world, std::int32_t spawnId);
    // Drops what `count` channels from `first` asked or were answered, within range.
    void forget(std::size_t first, std::size_t count);

    struct Taken {
        std::size_t channel = 0;
        LateAsk ask;
        std::int32_t spawnId = 0;
        LateQuery query;
    };
    // After the sync for `world`: every channel's ask, taken; asks made for another world are taken and
    // dropped (counted in `dropped`).
    std::array<std::optional<Taken>, kLateChannels> takeFor(std::uintptr_t world, std::uint64_t& dropped);
    // After the sync: the answer to `channel`'s taken ask.
    void answer(std::size_t channel, float fraction);

private:
    struct Channel {
        LateTrace trace;
        std::uintptr_t world = 0;
        std::int32_t spawnId = 0;
    };
    std::array<Channel, kLateChannels> channels_{};
};

} // namespace evr::roomscale
