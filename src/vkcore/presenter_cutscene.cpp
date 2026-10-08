// Cutscenes in the camera hook (presenter_cutscene.hpp): changes and the skip key, the cuts' re-base, and the
// eye logs.

#include "vkcore/presenter_impl.hpp"

#include "vkcore/keep_active.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/seh_filter.hpp"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace evr::vkcore {

// ---- Cutscene changes and the skip key (camera hook) --------------------------------------------------

void XrPresenter::Impl::trackCutscene(bool inCutscene) {
    CutsceneTrack& t = cutsceneTrack;
    const ULONGLONG now = GetTickCount64();
    const bool around = !settings.cutsceneCinema;
    if (inCutscene != cutscene) {
        cutscene = inCutscene;
        t.since = now;
        ++t.changes;
        if (t.changes <= 20) {
            EVR_LOG("game: %s%s", inCutscene ? "cutscene starts" : "cutscene ends; the player's view is back",
                    !inCutscene ? ""
                    : !around   ? " (on the flat screen)"
                    : settings.cutsceneCutRebase
                        ? " (around the player; each cut re-based to where the head looks)"
                        : " (around the player)");
        }
        if (inCutscene && settings.cutsceneCinema) {
            replaceScreen.store(true, std::memory_order_release);
        }
        // Head aim's yaw is re-based on the next frame that reaches it, before it aims.
        t.endRebase = !inCutscene && around && settings.cutsceneCutRebase;
    }
    // The arms and the weapon FOV in a cutscene shown around the player (controllers.hpp).
    controllers::noteImmersiveCutscene(inCutscene && around);
    // Without the automatic skip, holding the dash button skips by hand (controllers.hpp).
    controllers::noteSkippableCutscene(inCutscene && !settings.skipCinematics);
    if (!settings.skipCinematics) {
        return;
    }
    // Holds R (the game's skip key) while a cutscene plays: 2.5 s holds with short gaps, released as
    // soon as the cutscene ends.
    HWND window = gameWindow();
    if (t.skipHolding) {
        if (!inCutscene || now - t.skipHoldStart >= 2500) {
            injectKey(kSkipKey, false, window);
            t.skipHolding = false;
            t.skipReleased = now;
        }
    } else if (inCutscene && now - t.since >= 300 && now - t.skipReleased >= 400) {
        if (injectKey(kSkipKey, true, window)) {
            t.skipHolding = true;
            t.skipHoldStart = now;
            ++t.skipHolds;
            if (t.skipHolds <= 10) {
                EVR_LOG("game: holding the skip key (hold %llu)",
                        static_cast<unsigned long long>(t.skipHolds));
            }
        }
    }
}

// ---- The cuts' re-base (camera hook) -----------------------------------------------------------------

float XrPresenter::Impl::cutsceneBodyYaw(float bodyYaw,
                                         const xr_math::IdViewAxis& camera,
                                         Quat head,
                                         bool cameraCut) {
    CutsceneTrack& t = cutsceneTrack;
    if (!settings.cutsceneCutRebase) {
        return bodyYaw;
    }
    const xr_math::IdAngles angles = xr_math::anglesFromAxis(camera);
    xr_math::CutFrame f;
    f.cutscene = cutscene && !settings.cutsceneCinema;
    f.cameraYaw = angles.yaw;
    f.cameraPitch = angles.pitch;
    f.bodyYaw = bodyYaw;
    f.headYaw = xr_math::headAngles(head).yaw;
    f.seconds = qpcSeconds(qpcNow());
    // A menu over the cutscene: the offset stays and nothing is detected. Once head aim has written, the
    // body under a menu is the one it holds for the menu (aimWithHead), which takes no offset.
    const bool menu = menuUp.load(std::memory_order_relaxed);
    const bool menuBody = menu && settings.headAim && aimPhase == AimPhase::Active && aimMenuBody;
    f.menu = menu;
    const xr_math::CutStep step = xr_math::cutRebaseStep(t.cuts, f);
    if (step.event != xr_math::CutEvent::None && ++t.rebaseLogs <= 200) {
        if (step.event == xr_math::CutEvent::End) {
            EVR_LOG("cutscene: end, the cuts' re-base of %.1f deg dropped", -step.turn);
        } else {
            EVR_LOG("cutscene: %s, yaw re-based by %.1f deg (camera turned %.1f deg in one frame, %.2f s "
                    "since the "
                    "last frame, cameraCut %d; head yaw %.1f, body %.1f from the game's)",
                    xr_math::cutEventName(step.event), step.turn, step.cameraTurn, step.gapSeconds,
                    cameraCut ? 1 : 0, f.headYaw, t.cuts.offset);
        }
    }
    return menuBody ? bodyYaw : step.bodyYaw;
}

void XrPresenter::Impl::rebaseAfterCutscene(Quat head) {
    CutsceneTrack& t = cutsceneTrack;
    if (!t.endRebase) {
        return;
    }
    t.endRebase = false;
    if (cutscene) {
        return; // another cutscene started first
    }
    if (aimPhase != AimPhase::Active) {
        if (++t.rebaseLogs <= 200) {
            EVR_LOG("cutscene: end; head aim is not on, so the view keeps the game's heading");
        }
        return;
    }
    const float headYaw = xr_math::headAngles(head).yaw;
    const float turn = xr_math::rebaseHeadYaw(aimState, headYaw);
    if (++t.rebaseLogs <= 200) {
        EVR_LOG(
            "cutscene: end, yaw re-based by %.1f deg: the player's view faces the game's heading where the "
            "head looks (head yaw %.1f)",
            turn, headYaw);
    }
}

// ---- The eye logs ------------------------------------------------------------------------------------

namespace cutscene_eyes {

namespace {

// renderView_t (engine-facts.md section 2).
constexpr std::size_t kCameraCut = 0x16;
constexpr std::size_t kUsesViewOriginOffset = 0xC4;
constexpr std::size_t kLocalViewOrigin = 0xC8;
constexpr std::size_t kViewOriginOffset = 0xD4;
constexpr std::size_t kAllowBypass = 0xE8;
constexpr std::size_t kForceIdentityViewMatrix = 0x130;
constexpr std::size_t kFieldsSize = 0x134;
// idRenderView.
constexpr std::size_t kViewMatrix = 0x293C0;
constexpr std::size_t kInverseViewMatrix = 0x29400;
constexpr ULONGLONG kEveryMs = 10000;
constexpr std::uint64_t kMaxLines = 200;

bool copyGuarded(void* destination, const void* source, std::size_t size) {
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

struct Point {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

float distance(Point a, Point b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

// Camera hook only.
bool g_wasCutscene = false;
bool g_loggedPlay = false;
ULONGLONG g_lastFields = 0;
std::uint64_t g_frames = 0;    // cutscene frames since the last line
std::uint64_t g_cutFrames = 0; // ... with cameraCut set
std::uint64_t g_fieldLines = 0;

// Latch threads (eye L and eye R can latch at once under Parallel Eye Rendering).
struct EyeSample {
    bool valid = false;
    std::uint64_t seq = 0; // the game frame
    Point origin;          // r.vieworg
    Point inverseCol;      // inverseViewMatrix's translation, row-major (elements 3, 7, 11)
    Point inverseRow;      // the same, column-major (12, 13, 14)
    Point fromView;        // -R^T t of the view matrix, row-major
};
std::mutex g_eyeMutex;
EyeSample g_left;
// Per kind (play, cutscene): when the last line went out, and how many did.
std::atomic<ULONGLONG> g_lastEyes[2] = {};
std::atomic<std::uint64_t> g_eyeLines[2] = {};

} // namespace

void noteGameView(const std::byte* renderView, bool cutscene) {
    if (g_fieldLines >= kMaxLines) {
        return;
    }
    // The renderView_t the hook was given: its own bytes, read as the camera hook reads the others.
    const bool cut = renderView[kCameraCut] != std::byte{0};
    if (cutscene) {
        ++g_frames;
        g_cutFrames += cut ? 1 : 0;
    }
    const bool start = cutscene && !g_wasCutscene;
    g_wasCutscene = cutscene;
    const ULONGLONG now = GetTickCount64();
    const bool due = start || (cutscene && now - g_lastFields >= kEveryMs) || (!cutscene && !g_loggedPlay);
    std::byte fields[kFieldsSize];
    if (!due || !copyGuarded(fields, renderView, sizeof(fields))) {
        return;
    }
    ++g_fieldLines;
    g_lastFields = now;
    g_loggedPlay = g_loggedPlay || !cutscene;
    float origin[3];
    float local[3];
    float offset[3];
    std::memcpy(origin, fields + render_view::kViewOrigin, sizeof(origin));
    std::memcpy(local, fields + kLocalViewOrigin, sizeof(local));
    std::memcpy(offset, fields + kViewOriginOffset, sizeof(offset));
    const auto byte = [&fields](std::size_t at) {
        return std::to_integer<unsigned>(fields[at]);
    };
    EVR_LOG(
        "cutscene-eyes: %s: vieworg (%.2f %.2f %.2f), usesViewOriginOffset %u, localViewOrigin (%.3f %.3f "
        "%.3f), viewOriginOffset (%.3f %.3f %.3f), viewBypass.allowBypass %u, forceIdentityViewMatrix %u, "
        "cameraCut %u (%llu of %llu cutscene frame(s) since the last line)",
        start      ? "cutscene start"
        : cutscene ? "in the cutscene"
                   : "play",
        origin[0], origin[1], origin[2], byte(kUsesViewOriginOffset), local[0], local[1], local[2], offset[0],
        offset[1], offset[2], byte(kAllowBypass), byte(kForceIdentityViewMatrix), byte(kCameraCut),
        static_cast<unsigned long long>(g_cutFrames), static_cast<unsigned long long>(g_frames));
    g_frames = 0;
    g_cutFrames = 0;
}

void noteLatchedEye(
    const std::byte* renderView, int eye, std::uint64_t seq, bool cutscene, float unitsPerMetre) {
    const int kind = cutscene ? 1 : 0;
    const ULONGLONG now = GetTickCount64();
    // Nothing is read unless a line is due and the budget has room.
    if (eye < 0 || eye > 1 || seq == 0 || !(unitsPerMetre > 0.0f) || g_eyeLines[kind].load() >= kMaxLines ||
        now - g_lastEyes[kind].load() < kEveryMs) {
        return;
    }
    float view[16];
    float inverse[16];
    float origin[3];
    std::byte latched[kFieldsSize];
    if (!copyGuarded(view, renderView + kViewMatrix, sizeof(view)) ||
        !copyGuarded(inverse, renderView + kInverseViewMatrix, sizeof(inverse)) ||
        !copyGuarded(latched, renderView + render_view_object::kLatched, sizeof(latched))) {
        return;
    }
    std::memcpy(origin, latched + render_view::kViewOrigin, sizeof(origin));
    EyeSample s;
    s.valid = true;
    s.seq = seq;
    s.origin = {origin[0], origin[1], origin[2]};
    s.inverseCol = {inverse[3], inverse[7], inverse[11]};
    s.inverseRow = {inverse[12], inverse[13], inverse[14]};
    const float t[3] = {view[3], view[7], view[11]};
    s.fromView = {-(view[0] * t[0] + view[4] * t[1] + view[8] * t[2]),
                  -(view[1] * t[0] + view[5] * t[1] + view[9] * t[2]),
                  -(view[2] * t[0] + view[6] * t[1] + view[10] * t[2])};
    std::lock_guard lock(g_eyeMutex);
    if (eye == 0) {
        g_left = s;
        return;
    }
    const EyeSample left = g_left;
    if (!left.valid || left.seq != seq || now - g_lastEyes[kind].load() < kEveryMs) {
        return; // eye L of another game frame (alternate eyes): no pair
    }
    g_left.valid = false;
    g_lastEyes[kind].store(now);
    g_eyeLines[kind].fetch_add(1);
    float offset[3];
    std::memcpy(offset, latched + kViewOriginOffset, sizeof(offset));
    EVR_LOG("cutscene-eyes: eye L-R distance of game frame %llu (both eyes of it) %.4f m (r.vieworg), %.4f m "
            "(inverse view matrix, row-major), %.4f m (its column-major reading), %.4f m (view matrix); "
            "cutscene %s; r.usesViewOriginOffset %u, r.viewOriginOffset (%.3f %.3f %.3f)",
            static_cast<unsigned long long>(seq), distance(left.origin, s.origin) / unitsPerMetre,
            distance(left.inverseCol, s.inverseCol) / unitsPerMetre,
            distance(left.inverseRow, s.inverseRow) / unitsPerMetre,
            distance(left.fromView, s.fromView) / unitsPerMetre, cutscene ? "yes" : "no",
            std::to_integer<unsigned>(latched[kUsesViewOriginOffset]), offset[0], offset[1], offset[2]);
}

} // namespace cutscene_eyes

} // namespace evr::vkcore
