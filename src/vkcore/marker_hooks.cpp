#include "vkcore/marker_hooks.hpp"

#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "xr_math/marker_projection.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "hud-markers";

using Matrix = xr_math::EngineMatrix; // idRenderMatrix, row-major

// The player's GUI build (RVA 0x6CF730): `lea rcx, [rax + 0x289D0]` (the render view's latched view), the
// projection builder's flags, `lea rdx, [rbp + 0x20]`, `call` (+0x1B); `mov rcx, [rsp + 0x30]` (the player's
// view), `lea rdx, [rbp - 0x20]`, `call` (+0x2C); `lea rdx, [rbp - 0x20]` (+0x31, the hook), `lea rcx,
// [rbp + 0x20]`, `call` the thunk (+0x39) that multiplies them into the HUD's matrix.
constexpr const char* kMatrixSignature =
    "48 8D 88 D0 89 02 00 C6 44 24 28 00 45 33 C9 C6 44 24 20 00 45 33 C0 48 8D 55 20 E8 ?? ?? ?? ?? "
    "48 8B 4C 24 30 48 8D 55 E0 45 33 C0 E8 ?? ?? ?? ?? 48 8D 55 E0 48 8D 4D 20 E8 ?? ?? ?? ??";
constexpr std::size_t kProjectionCall = 0x1B;
constexpr std::size_t kViewCall = 0x2C;
constexpr std::size_t kMatrixHook = 0x31;
constexpr std::size_t kThunkCall = 0x39;
constexpr std::ptrdiff_t kProjectionFromRbp = 0x20;
constexpr std::ptrdiff_t kViewFromRbp = -0x20;
// The builders: `sub rsp, 0x48`, `cmp byte [rcx + 0x90], 0` (useExplicitProjectionMatrix), `je`; and `sub
// rsp, 0x48`, `cmp byte [rcx + 0x130], 0` (forceIdentityViewMatrix). The thunk: `lea r8, [rip + matrix]`,
// `jmp`.
constexpr const char* kProjectionBuilder = "48 83 EC 48 80 B9 90 00 00 00 00 74";
constexpr const char* kViewBuilder = "48 83 EC 48 80 B9 30 01 00 00 00";
constexpr const char* kThunk = "4C 8D 05 ?? ?? ?? ?? E9 ?? ?? ?? ??";

// The POI update (RVA 0xEEEB1B, right after its `call` of the projector with r14 the marker's
// idHUD_POIInfo): the on-screen test against [rsp + 0x48] x [rsp + 0x4C] (the frame's width and height) into
// dil (1: off the frame), the aspect stretch of the position at r14 + 0x18, then `test dil, dil` (+0xEA, the
// hook) and `je`.
constexpr const char* kPoiSignature =
    "45 0F 57 C0 84 C0 74 2B F3 41 0F 10 46 18 44 0F 2F C0 77 1F 0F 2F 44 24 48 77 18 F3 41 0F 10 46 1C "
    "44 0F 2F C0 77 0C 0F 2F 44 24 4C 77 05 40 32 FF EB 03 40 B7 01 48 8B 05 ?? ?? ?? ?? 0F 57 F6 0F 57 C0 "
    "49 8B CD F3 0F 2A B0 F8 E7 0A 00 F3 0F 2A 80 FC E7 0A 00 F3 0F 5E F0 E8 ?? ?? ?? ?? 49 8B CD 0F 28 F8 "
    "E8 ?? ?? ?? ?? F3 44 0F 10 2D ?? ?? ?? ?? 0F 28 D8 F3 44 0F 10 15 ?? ?? ?? ?? 0F 28 E7 F3 41 0F 59 FD "
    "41 0F 28 CA 44 0F 28 F1 0F 57 C9 41 0F 28 D6 F3 0F 5E E3 F3 41 0F 59 DD 0F 28 C4 F3 0F 5E C6 F3 0F 5E "
    "F4 F3 0F 10 C8 F3 41 0F 10 46 1C F3 0F 5F D1 41 0F 28 CE F3 0F 5C C3 F3 0F 5F CE F3 0F 59 D0 F3 41 0F "
    "10 46 18 F3 0F 5C C7 F3 0F 58 D3 F3 0F 59 C8 F3 41 0F 11 56 1C F3 0F 58 CF F3 41 0F 11 4E 18 40 84 FF "
    "74 57";
constexpr std::size_t kPoiHook = 0xEA;
// The projector the POI update called just before the signature: `mulss xmm8, [rip + matrix]` (+0x59, the
// matrix's first element).
constexpr std::ptrdiff_t kProjectorCallBefore = -5;
constexpr std::size_t kProjectorRead = 0x59;
constexpr const char* kProjectorReadSignature = "F3 44 0F 59 05";
// idHUD_POIInfo (type info): worldPosition +0x0, screenPosition +0x18, poiUid +0x80; idDeclPOI.worldOffset
// +0x1A4 (the POI update adds it to the world position before it projects; r15 holds the decl).
constexpr std::size_t kPoiScreen = 0x18;
constexpr std::size_t kPoiUid = 0x80;
constexpr std::size_t kDeclWorldOffset = 0x1A4;
constexpr std::size_t kFrameWidthFromRsp = 0x48;
constexpr std::size_t kFrameHeightFromRsp = 0x4C;

// The XR worker renews the quad every frame it shows; the HUD keeps the game's projection once it stops.
constexpr std::uint64_t kLeaseMs = 250;
constexpr std::uint64_t kReportMs = 10000;
constexpr std::uint64_t kTestLogEveryMs = 1000;
constexpr int kMaxTestLines = 4000;

std::once_flag g_once;
bool g_installed = false;
bool g_testLog = false;

// The XR worker's lease, read lock-free by the game's job threads. The UI settings do not change in a
// session: they are kept from the first note on (`settingsKept` published with release); the GUI target's
// size (which a resize changes) and the lease's end are atomics of their own.
struct Lease {
    ui_layer::UiSettings settings;
    std::atomic<bool> settingsKept{false};
    std::atomic<std::uint64_t> target{0}; // width << 32 | height
    std::atomic<std::uint64_t> until{0};  // GetTickCount64
} g_lease;

// The current game frame, set by the matrix hook for the POI updates after it (both on the game thread).
struct Frame {
    std::atomic<bool> quad{false};
    std::atomic<bool> clamp{false};
    std::atomic<bool> testLogged{false};
} g_frame;

struct Counters {
    std::atomic<std::uint64_t> quadFrames{0}; // the HUD's projection set for the quad
    std::atomic<std::uint64_t> gameFrames{0}; // no head-locked HUD quad: the game's projection
    std::atomic<std::uint64_t> notSet{0};     // the game's projection not one the quad's can take rows from
    std::atomic<std::uint64_t> movedOff{0};   // markers drawn beyond the frame that the game took for on it
    std::atomic<std::uint64_t> markers{0};    // marker updates seen
    std::atomic<std::uint64_t> hookTicks{0};  // QueryPerformanceCounter ticks inside both hooks
    std::atomic<std::uint64_t> maxTicks{0};
    std::atomic<std::uint64_t> lastReport{0};
    std::atomic<std::uint64_t> lastTestLog{0};
    std::atomic<int> testLines{0};
} g_counters;

std::array<float, 4> g_loggedFov{}; // the frustum last logged (degrees), on the game thread

template <typename T>
T readAt(std::uintptr_t at) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof(value));
    return value;
}

std::uint64_t ticksNow() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<std::uint64_t>(t.QuadPart);
}

void addHookTime(std::uint64_t start) {
    const std::uint64_t spent = ticksNow() - start;
    g_counters.hookTicks.fetch_add(spent, std::memory_order_relaxed);
    std::uint64_t seen = g_counters.maxTicks.load(std::memory_order_relaxed);
    while (spent > seen &&
           !g_counters.maxTicks.compare_exchange_weak(seen, spent, std::memory_order_relaxed)) {
    }
}

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < kReportMs || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    const std::uint64_t quad = g_counters.quadFrames.exchange(0);
    const std::uint64_t game = g_counters.gameFrames.exchange(0);
    const std::uint64_t notSet = g_counters.notSet.exchange(0);
    const std::uint64_t movedOff = g_counters.movedOff.exchange(0);
    const std::uint64_t markers = g_counters.markers.exchange(0);
    const std::uint64_t ticks = g_counters.hookTicks.exchange(0);
    const std::uint64_t maxTicks = g_counters.maxTicks.exchange(0);
    if (quad + game + notSet == 0) {
        return;
    }
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    const double us = 1e6 / static_cast<double>(f.QuadPart);
    const std::uint64_t frames = quad + game + notSet;
    EVR_LOG("%s: the HUD's projection for the head-locked quad in %llu game frame(s), the game's in %llu (no "
            "head-locked HUD quad), %llu not set (not a perspective of the engine's); %llu marker update(s), "
            "%llu moved off the frame; hooks %.2f us per game frame (longest call %.2f us)",
            kTag, static_cast<unsigned long long>(quad), static_cast<unsigned long long>(game),
            static_cast<unsigned long long>(notSet), static_cast<unsigned long long>(markers),
            static_cast<unsigned long long>(movedOff),
            static_cast<double>(ticks) * us / static_cast<double>(frames),
            static_cast<double>(maxTicks) * us);
}

struct LeasedQuad {
    xr_math::MarkerQuad quad;
    bool clampToShown = false;
};

std::optional<LeasedQuad> currentLease() {
    if (GetTickCount64() >= g_lease.until.load(std::memory_order_acquire) ||
        !g_lease.settingsKept.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    const std::uint64_t target = g_lease.target.load(std::memory_order_acquire);
    const auto width = static_cast<std::uint32_t>(target >> 32);
    const auto height = static_cast<std::uint32_t>(target & 0xFFFFFFFFu);
    const ui_layer::UiSettings& ui = g_lease.settings;
    const ui_layer::PixelRect shown =
        ui.wideCrop ? ui_layer::wideContentRect(width, height) : ui_layer::PixelRect{0, 0, width, height};
    LeasedQuad out;
    out.quad.widthMetres = ui.widthMetres;
    out.quad.distanceMetres = ui.distanceMetres;
    out.quad.offsetYMetres = ui.offsetYMetres;
    out.quad.targetWidth = static_cast<int>(width);
    out.quad.targetHeight = static_cast<int>(height);
    out.quad.shownX = shown.x;
    out.quad.shownY = shown.y;
    out.quad.shownWidth = static_cast<int>(shown.width);
    out.quad.shownHeight = static_cast<int>(shown.height);
    out.clampToShown = ui.wideCrop;
    return out;
}

void logFrustum(const xr_math::MarkerQuad& quad, const Matrix& engine, const Matrix& set) {
    const auto fov = xr_math::markerTargetFov(quad);
    if (!fov) {
        return;
    }
    constexpr float kDegrees = 57.2957795f;
    const std::array<float, 4> degrees{fov->angleLeft * kDegrees, fov->angleRight * kDegrees,
                                       fov->angleUp * kDegrees, fov->angleDown * kDegrees};
    bool same = true;
    for (std::size_t i = 0; i < degrees.size(); ++i) {
        same = same && std::fabs(degrees[i] - g_loggedFov[i]) < 0.01f;
    }
    if (same) {
        return;
    }
    g_loggedFov = degrees;
    EVR_LOG(
        "%s: the HUD's markers follow the head-locked quad (%.2f m wide at %.2f m, %.2f m up, showing (%d, "
        "%d) "
        "%d x %d of the %d x %d GUI target): its projection spans %.2f to %.2f deg across, %.2f to %.2f deg "
        "up (the game's [0] %.4f [2] %.4f [5] %.4f [6] %.4f, now %.4f %.4f %.4f %.4f)",
        kTag, quad.widthMetres, quad.distanceMetres, quad.offsetYMetres, quad.shownX, quad.shownY,
        quad.shownWidth, quad.shownHeight, quad.targetWidth, quad.targetHeight, degrees[0], degrees[1],
        degrees[3], degrees[2], engine[0], engine[2], engine[5], engine[6], set[0], set[2], set[5], set[6]);
}

bool takeTestLine() {
    return g_counters.testLines.fetch_add(1, std::memory_order_relaxed) < kMaxTestLines;
}

Matrix g_testView{}; // the test-logged frame's view matrix (game thread)

void testLogMatrix(const Matrix& projection, const Matrix& view, bool quad) {
    const std::uint64_t now = GetTickCount64();
    g_frame.testLogged.store(false, std::memory_order_relaxed);
    if (now - g_counters.lastTestLog.load(std::memory_order_relaxed) < kTestLogEveryMs || !takeTestLine()) {
        return;
    }
    g_counters.lastTestLog.store(now, std::memory_order_relaxed);
    g_testView = view;
    const Matrix& v = view;
    // The view matrix's rows are the view's right, up and back axes; its origin is -R^T t.
    const float origin[3] = {-(v[0] * v[3] + v[4] * v[7] + v[8] * v[11]),
                             -(v[1] * v[3] + v[5] * v[7] + v[9] * v[11]),
                             -(v[2] * v[3] + v[6] * v[7] + v[10] * v[11])};
    EVR_LOG(
        "%s: test log: HUD projection (%s) [0] %.5f [2] %.5f [5] %.5f [6] %.5f [10] %.5f [11] %.5f [14] "
        "%.2f; view origin (%.3f %.3f %.3f), right (%.5f %.5f %.5f), up (%.5f %.5f %.5f), back (%.5f %.5f "
        "%.5f)",
        kTag, quad ? "the quad's" : "the game's", projection[0], projection[2], projection[5], projection[6],
        projection[10], projection[11], projection[14], origin[0], origin[1], origin[2], v[0], v[1], v[2],
        v[4], v[5], v[6], v[8], v[9], v[10]);
    g_frame.testLogged.store(true, std::memory_order_relaxed);
}

void testLogMarker(const HookRegisters& r, bool gameOff, bool off) {
    if (!g_frame.testLogged.load(std::memory_order_relaxed) || !takeTestLine()) {
        return;
    }
    const auto world = readAt<std::array<float, 3>>(r.r14);
    const auto offset = readAt<std::array<float, 3>>(r.r15 + kDeclWorldOffset);
    const auto drawn = readAt<std::array<float, 2>>(r.r14 + kPoiScreen);
    const float width = readAt<float>(r.rsp + kFrameWidthFromRsp);
    const float height = readAt<float>(r.rsp + kFrameHeightFromRsp);
    const float p[3] = {world[0] + offset[0], world[1] + offset[1], world[2] + offset[2]};
    const Matrix& v = g_testView;
    const float q[3] = {v[0] * p[0] + v[1] * p[1] + v[2] * p[2] + v[3],
                        v[4] * p[0] + v[5] * p[1] + v[6] * p[2] + v[7],
                        v[8] * p[0] + v[9] * p[1] + v[10] * p[2] + v[11]};
    EVR_LOG(
        "%s: test log: marker %d at world (%.3f %.3f %.3f), view (%.4f %.4f %.4f); drawn at (%.2f, %.2f) of "
        "%.0f x %.0f, %s%s",
        kTag, readAt<int>(r.r14 + kPoiUid), p[0], p[1], p[2], q[0], q[1], q[2], drawn[0], drawn[1], width,
        height, off ? "off the frame" : "on the frame", off && !gameOff ? " (moved off)" : "");
}

// The hooks' own work is timed for the 10 s line; the log lines they write (the summary, the frustum on a
// change, the test log) are not.
void onMatrix(const HookRegisters& r) {
    report();
    const std::uint64_t start = ticksNow();
    g_frame.quad.store(false, std::memory_order_relaxed);
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    auto* projectionAt = reinterpret_cast<void*>(r.rbp + kProjectionFromRbp);
    const auto engine = readAt<Matrix>(r.rbp + kProjectionFromRbp);
    const auto lease = currentLease();
    const std::optional<Matrix> set =
        lease ? xr_math::markerProjection(lease->quad, engine) : std::optional<Matrix>{};
    if (!lease) {
        g_counters.gameFrames.fetch_add(1, std::memory_order_relaxed);
    } else if (set) {
        std::memcpy(projectionAt, set->data(), sizeof(Matrix));
        g_frame.clamp.store(lease->clampToShown, std::memory_order_relaxed);
        g_counters.quadFrames.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_counters.notSet.fetch_add(1, std::memory_order_relaxed);
    }
    g_frame.quad.store(set.has_value(), std::memory_order_relaxed);
    addHookTime(start);
    if (lease && set) {
        logFrustum(lease->quad, engine, *set);
    }
    if (g_testLog) {
        testLogMatrix(set ? *set : engine, readAt<Matrix>(r.rbp + kViewFromRbp), set.has_value());
    }
}

void onPoi(HookRegisters& r) {
    const std::uint64_t start = ticksNow();
    g_counters.markers.fetch_add(1, std::memory_order_relaxed);
    const bool gameOff = (r.rdi & 0xFF) != 0;
    bool off = gameOff;
    if (g_frame.quad.load(std::memory_order_relaxed) && g_frame.clamp.load(std::memory_order_relaxed) &&
        mp_guard::allowsGameTouch()) {
        const auto drawn = readAt<std::array<float, 2>>(r.r14 + kPoiScreen);
        const float width = readAt<float>(r.rsp + kFrameWidthFromRsp);
        const float height = readAt<float>(r.rsp + kFrameHeightFromRsp);
        off = xr_math::markerOffFrame(gameOff, {drawn[0], drawn[1]}, width, height);
        if (off && !gameOff) {
            r.rdi = (r.rdi & ~std::uintptr_t{0xFF}) | 1u; // dil = 1: the game's off-frame path clamps it
            g_counters.movedOff.fetch_add(1, std::memory_order_relaxed);
        }
    }
    addHookTime(start);
    if (g_testLog) {
        testLogMarker(r, gameOff, off);
    }
}

bool locate(const GameImage& image, const std::byte*& matrixSite, const std::byte*& poiSite) {
    matrixSite = findUnique(image, kTag, "the HUD matrix build", kMatrixSignature);
    poiSite = findUnique(image, kTag, "the POI update's off-frame test", kPoiSignature);
    if (!matrixSite || !poiSite) {
        return false;
    }
    const std::byte* projection = branchTarget(image, matrixSite + kProjectionCall);
    const std::byte* view = branchTarget(image, matrixSite + kViewCall);
    const std::byte* thunk = branchTarget(image, matrixSite + kThunkCall);
    if (!matchesAt(image, projection, kProjectionBuilder) || !matchesAt(image, view, kViewBuilder) ||
        !matchesAt(image, thunk, kThunk)) {
        EVR_LOG("%s: the HUD matrix build's calls are not the projection and view builders and the multiply",
                kTag);
        return false;
    }
    const std::byte* matrix = ripTarget(image, thunk + 3, thunk + 7);
    const std::byte* projector = branchTarget(image, poiSite + kProjectorCallBefore);
    const std::byte* read = projector ? projector + kProjectorRead : nullptr;
    if (!matrix || !matchesAt(image, read, kProjectorReadSignature) ||
        ripTarget(image, read + 5, read + 9) != matrix) {
        EVR_LOG("%s: the POI update's projector does not read the HUD matrix (RVA 0x%X)", kTag,
                matrix ? image.rva(matrix) : 0u);
        return false;
    }
    return true;
}

} // namespace

bool installMarkerHooks() {
    std::call_once(g_once, [] {
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: the multiplayer guard is not armed; not installed", kTag);
            return;
        }
        std::wstring value;
        g_testLog = readEnv(L"ETERNALVR_TEST_MARKER_LOG", value) && value == L"1";
        GameImage image;
        const std::byte* matrixSite = nullptr;
        const std::byte* poiSite = nullptr;
        if (!locateGameImage(image, kTag) || !locate(image, matrixSite, poiSite)) {
            EVR_LOG("%s: not installed; the HUD's markers keep the game's projection", kTag);
            return;
        }
        std::string error;
        if (!installMidHook(const_cast<std::byte*>(matrixSite + kMatrixHook), &onMatrix, error)) {
            EVR_LOG("%s: hook failed: %s; the HUD's markers keep the game's projection", kTag, error.c_str());
            return;
        }
        // The off-frame test only matters with the quad's projection; without it the markers keep the
        // projection and the game's test as before.
        if (!installMidHookEdit(const_cast<std::byte*>(poiSite + kPoiHook), &onPoi, error)) {
            EVR_LOG("%s: the off-frame test hook failed: %s; markers above and below the 16:9 band are not "
                    "clamped to it",
                    kTag, error.c_str());
        }
        g_installed = true;
        EVR_LOG(
            "%s: the HUD matrix build hooked at RVA 0x%X, the POI update's off-frame test at RVA 0x%X: the "
            "HUD's world-attached markers follow the head-locked quad%s",
            kTag, image.rva(matrixSite + kMatrixHook), image.rva(poiSite + kPoiHook),
            g_testLog ? "; ETERNALVR_TEST_MARKER_LOG=1: the HUD's projection, view and markers logged about "
                        "once a second"
                      : "");
    });
    return g_installed;
}

void noteMarkerQuad(bool headLocked,
                    const ui_layer::UiSettings& ui,
                    std::uint32_t width,
                    std::uint32_t height) {
    if (!g_installed) {
        return;
    }
    if (!headLocked || width == 0 || height == 0) {
        g_lease.until.store(0, std::memory_order_release);
        return;
    }
    if (!g_lease.settingsKept.load(std::memory_order_relaxed)) {
        g_lease.settings = ui; // only the XR worker writes, once, before publishing it
        g_lease.settingsKept.store(true, std::memory_order_release);
    }
    g_lease.target.store(static_cast<std::uint64_t>(width) << 32 | height, std::memory_order_release);
    g_lease.until.store(GetTickCount64() + kLeaseMs, std::memory_order_release);
}

} // namespace evr::vkcore
