// Two-handed aim in the layer (two_hand.hpp).

#include "vkcore/two_hand.hpp"

#include "features/input/aim_smoothing.hpp"
#include "features/input/input_mapper.hpp"
#include "features/input/two_hand.hpp"
#include "vkcore/demon_view.hpp"
#include "vkcore/log.hpp"
#include "vkcore/menu_input.hpp"
#include "vkcore/mp_guard.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <numbers>
#include <string>

namespace evr::vkcore::controllers::two_hand {

namespace {

constexpr const char* kTag = "two-handed aim";
constexpr float kDegreesPerRadian = 180.0f / std::numbers::pi_v<float>;
// The off-hand hook's note of the gun is used for this long (a few of its ticks), and the camera hook's state
// by the mapper and the off-hand hook.
constexpr double kHandsNoteSeconds = 0.25;
// The planted fault's late release, in frames: the halved turn kept with no ease, well past the ease's end.
constexpr int kLateFrames = 20;
// With ETERNALVR_OFFHAND_TRACE: the frames logged after each start and end of a hold and its ease (the frame
// before is logged with it).
constexpr int kTraceFrames = 3;

// The off-hand hook's note, under its lock.
std::mutex g_handsMutex;
bool g_gunUp = false;
const std::byte* g_armsItem = nullptr;
LONGLONG g_handsQpc = 0;

// For the mapper, the off-hand hook and the dump.
std::atomic<bool> g_supports{false};
std::atomic<bool> g_engaged{false};
std::atomic<LONGLONG> g_aimQpc{0}; // when the camera hook last ran aim()
// While held: how far the gun's support point is from the weapon hand's grip (metres), for the off glove's
// place along the gun (input::gloveSlide).
std::atomic<float> g_supportMetres{0.0f};
std::mutex g_rowMutex;
std::optional<std::array<float, 3>> g_rowInUse;

// Camera hook only.
// The grip pressed as the mapper's default thresholds have it (input::kGripThresholds); the held aim smoothed
// as the one-handed aim is (the player's ETERNALVR_AIM_SMOOTHING), made on the first frame.
std::optional<input::TwoHandHold> g_hold;
const std::byte* g_rowDecl = nullptr;
std::uint32_t g_rowLoads = 0;
std::optional<input::SupportRow> g_row;
std::string g_rowName;
input::TwoHandBlock g_lastBlock = input::TwoHandBlock::Off;
int g_lateLeft = 0;
Quat g_lastTurn;
LONGLONG g_frameQpc = 0; // the last aim()'s, for the ease's clock
std::uint32_t g_aimLoads = 0;
bool g_aimLoadsKnown = false;
std::string g_traceLast;
int g_traceLeft = 0;
std::uint32_t g_eventLines = 0;
std::uint32_t g_noRowLines = 0;

// The turn from `b` to `a` (degrees); exactly 0 for the same orientation.
float degreesBetween(Quat a, Quat b) {
    const Quat d = a * conjugate(b);
    return 2.0f * std::atan2(length(Vec3{d.x, d.y, d.z}), std::fabs(d.w)) * kDegreesPerRadian;
}

// Half of a turn (the planted fault).
Quat halfTurn(Quat q) {
    if (q.w < 0.0f) {
        q = {-q.x, -q.y, -q.z, -q.w};
    }
    return normalize(Quat{q.x, q.y, q.z, q.w + 1.0f});
}

// The held item's row, looked up once per decl and map (a decl's pointer can name another item after a load).
const std::optional<input::SupportRow>& rowFor(State& s, const std::byte* decl) {
    const std::uint32_t loads = mp_guard::mapLoads();
    if (decl != g_rowDecl || loads != g_rowLoads) {
        g_rowDecl = decl;
        g_rowLoads = loads;
        g_rowName = decl ? itemDeclName(decl) : std::string{};
        const auto row = g_rowName.empty() ? std::nullopt : s.offsets.lookupTwoHand(g_rowName);
        g_row = row ? std::optional(input::SupportRow{(*row)[0], (*row)[1], (*row)[2]}) : std::nullopt;
        // The weapon in the left hand: the chaingun's support is the right glove on the controller anyway
        // (DECISIONS T-141), and it has no row.
        if (!g_row && !g_rowName.empty() && g_noRowLines < 40) {
            ++g_noRowLines;
            EVR_LOG("%s: '%s' takes no support grip (no [two_hand] row)", kTag, g_rowName.c_str());
        }
    }
    return g_row;
}

void trace(const char* line) {
    if (g_traceLeft > 0) {
        EVR_LOG("%s", line);
        --g_traceLeft;
    }
}

// The hold, made with the session's settings: the one-handed aim's smoothing, none under the planted line
// fault (round 1's unsmoothed line, the jitter check's positive control).
input::TwoHandHold& hold(const input::ControllerSettings& cfg) {
    if (!g_hold) {
        const bool lineFault = cfg.testArmFault == input::ArmFault::TwoHandLine;
        const std::optional<input::OneEuroParams> smoothing =
            lineFault ? std::nullopt : input::aimSmoothingParams(cfg.aimSmoothing);
        g_hold.emplace(input::kGripThresholds, smoothing);
        EVR_LOG("%s: on; the held aim smoothed as the one-handed aim is (%.2f)%s", kTag,
                static_cast<double>(smoothing ? cfg.aimSmoothing : 0.0f),
                lineFault ? ", the planted line fault ON (ETERNALVR_TEST_ARM_FAULT=two_hand_line): unsmoothed"
                          : "");
    }
    return *g_hold;
}

// The camera hook's state, if it ran aim() recently.
bool recent() {
    const LONGLONG at = g_aimQpc.load(std::memory_order_relaxed);
    return at != 0 && secondsSince(at) < kHandsNoteSeconds;
}

} // namespace

void reset(const char* why) {
    if (!settings().twoHandAim) {
        return;
    }
    if (!g_hold) {
        return;
    }
    if (g_hold->engaged() && g_eventLines < 200) {
        ++g_eventLines;
        EVR_LOG("%s: the hold reset (%s): the weapon hand aims again", kTag, why);
    }
    g_hold->reset();
    g_supports.store(false, std::memory_order_relaxed);
    g_engaged.store(false, std::memory_order_relaxed);
    g_lateLeft = 0;
    g_frameQpc = 0;
}

void noteHands(bool gunUp, const std::byte* armsItem) {
    if (!settings().twoHandAim) {
        return;
    }
    std::lock_guard lock(g_handsMutex);
    g_gunUp = gunUp;
    g_armsItem = armsItem;
    g_handsQpc = nowQpc();
}

void aim(State& s, const std::byte* player, GameViewPoses& poses, std::optional<WeaponAim>& weapon) {
    const input::ControllerSettings& cfg = settings();
    if (!cfg.twoHandAim) {
        return;
    }
    if (const std::uint32_t loads = mp_guard::mapLoads(); !g_aimLoadsKnown || loads != g_aimLoads) {
        if (g_aimLoadsKnown) {
            reset("a level load");
        }
        g_aimLoads = loads;
        g_aimLoadsKnown = true;
    }
    const LONGLONG now = nowQpc();
    const double dt = g_frameQpc != 0 ? secondsSince(g_frameQpc) : 0.0;
    g_frameQpc = now;
    const input::Hand hand = weaponHand();
    const input::Hand off = input::otherHand(hand);
    const auto w = static_cast<std::size_t>(hand);
    const auto o = static_cast<std::size_t>(off);
    Quat roomFromLocal;
    {
        std::lock_guard lock(s.roomMutex);
        roomFromLocal = s.roomFromLocal.orientation;
    }

    input::TwoHandGates g;
    g.enabled = true;
    g.handAim =
        cfg.aim == input::AimSource::Hand && activeAim() == input::AimSource::Hand && weapon.has_value();
    g.dead = playerDead(player);
    g.demon = pilotingDemon();
    g.forcedView = s.yielding.load();
    g.weaponWheel = wheelView();
    g.menu = menu_input::suppressGameplay();
    g.swimming = swimFistsHeld();
    const std::byte* decl = nullptr;
    {
        std::lock_guard lock(g_handsMutex);
        g.gunUp = g_gunUp && g_handsQpc != 0 && secondsSince(g_handsQpc) < kHandsNoteSeconds;
        decl = g_armsItem;
    }
    const std::optional<input::SupportRow>& row = rowFor(s, decl);
    g.support = row.has_value();

    input::TwoHandFrame f;
    f.block = input::twoHandBlock(g);
    const std::optional<input::InputFrame> frame = latestFrame();
    f.offGrip = frame ? frame->hand(off).grip : 0.0f;
    f.weaponTracked = poses.aimValid[w] && poses.gripValid[w];
    f.offTracked = poses.gripValid[o];
    f.weaponGrip = poses.grip[w].position;
    f.aim = poses.aim[w].orientation;
    // The one-handed aim's input, before its smoothing (`weapon->tracked`, LOCAL).
    f.trackedAim =
        weapon ? std::optional(normalize(roomFromLocal * weapon->tracked.orientation)) : std::nullopt;
    f.offGripPoint = poses.grip[o].position;
    f.support = row ? input::supportRowFor(*row, hand) : input::SupportRow{};
    f.dt = static_cast<float>(std::min(dt, static_cast<double>(input::kMaxFrameSeconds)));
    const input::TwoHandResult r = hold(cfg).update(f);
    const bool fault = cfg.testArmFault == input::ArmFault::TwoHand;

    Quat used = r.aim;
    if (r.engaged) {
        g_lastTurn = normalize(r.target * conjugate(f.aim));
        if (fault) {
            used = normalize(halfTurn(g_lastTurn) * f.aim);
        }
        g_lateLeft = fault ? kLateFrames : 0;
    } else if (g_lateLeft > 0) {
        --g_lateLeft; // the planted fault: the last frames' turn kept after the release, with no ease
        used = normalize(halfTurn(g_lastTurn) * f.aim);
    }
    if (used != f.aim) {
        poses.aim[w].orientation = used;
        if (weapon) {
            weapon->used.orientation = normalize(conjugate(roomFromLocal) * used);
        }
    }
    if (weapon) {
        weapon->twoHanded = r.engaged || r.easing || g_lateLeft > 0;
    }
    g_supports.store(r.engaged || r.capturable, std::memory_order_relaxed);
    g_engaged.store(r.engaged, std::memory_order_relaxed);
    g_supportMetres.store(length(input::supportInAim(f.support)), std::memory_order_relaxed);
    g_aimQpc.store(now, std::memory_order_relaxed);
    {
        std::lock_guard lock(g_rowMutex);
        g_rowInUse =
            row ? std::optional(std::array<float, 3>{f.support.forward, f.support.left, f.support.up})
                : std::nullopt;
    }

    if (r.started || r.ended != input::TwoHandEnd::None) {
        if (g_eventLines < 200 || g_eventLines % 100 == 0) {
            if (r.started) {
                EVR_LOG("%s: held on '%s' (the off hand %.1f cm along the gun from the grip, %.1f cm off its "
                        "line; the support point at %.1f cm; the gun turned %.1f deg)",
                        kTag, g_rowName.c_str(), static_cast<double>(r.along * 100.0f),
                        static_cast<double>(r.distance * 100.0f),
                        static_cast<double>(length(input::supportInAim(f.support)) * 100.0f),
                        static_cast<double>(degreesBetween(r.target, f.aim)));
            } else {
                EVR_LOG("%s: let go (%s): the weapon hand aims again", kTag, input::twoHandEndName(r.ended));
            }
        }
        ++g_eventLines;
        if (cfg.offhandTrace) {
            g_traceLeft = kTraceFrames + 1;
            trace(g_traceLast.c_str());
        }
    }
    if (f.block != g_lastBlock) {
        if (cfg.offhandTrace && g_eventLines < 200) {
            EVR_LOG("%s: %s", kTag,
                    f.block == input::TwoHandBlock::None
                        ? "can hold"
                        : (std::string("blocked: ") + input::twoHandBlockName(f.block)).c_str());
        }
        g_lastBlock = f.block;
    }
    if (cfg.offhandTrace) {
        // One frame: when (the dump's clock), the off hand's grip, the hold, the aim used against the weapon
        // hand's own and the hold's full turn (degrees). Traced through each ease.
        char line[220];
        std::snprintf(
            line, sizeof(line),
            "%s: frame t=%.5f grip %.3f %s aim %.3f deg from the weapon hand's, the hold's %.3f deg", kTag,
            qpcToSeconds(now), static_cast<double>(f.offGrip), r.engaged ? "held" : "free",
            static_cast<double>(degreesBetween(used, f.aim)),
            static_cast<double>(r.engaged ? degreesBetween(r.target, f.aim) : 0.0f));
        g_traceLast = line;
        if (r.easing || g_lateLeft > 0) {
            g_traceLeft = std::max(g_traceLeft, kTraceFrames + 1);
        }
        trace(line);
    }
}

std::optional<input::Hand> supportsWeapon() {
    if (!settings().twoHandAim || !g_supports.load(std::memory_order_relaxed) || !recent()) {
        return std::nullopt;
    }
    return input::otherHand(weaponHand());
}

bool gloveOnSupport() {
    const input::ControllerSettings& cfg = settings();
    return cfg.twoHandAim && g_engaged.load(std::memory_order_relaxed) &&
           cfg.testArmFault != input::ArmFault::TwoHand && recent();
}

std::optional<float> gloveSupportMetres() {
    if (!gloveOnSupport()) {
        return std::nullopt;
    }
    const float metres = g_supportMetres.load(std::memory_order_relaxed);
    return metres > 0.0f ? std::optional(metres) : std::nullopt;
}

std::optional<DumpState> dumpState() {
    if (!settings().twoHandAim) {
        return std::nullopt;
    }
    DumpState d;
    d.engaged = g_engaged.load(std::memory_order_relaxed);
    std::lock_guard lock(g_rowMutex);
    d.row = g_rowInUse;
    return d;
}

} // namespace evr::vkcore::controllers::two_hand
