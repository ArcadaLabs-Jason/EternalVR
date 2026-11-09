// The hands rig's per-tick record (arm_ticks.hpp).

#include "vkcore/arm_ticks.hpp"

#include "vkcore/game_frame.hpp"
#include "vkcore/log.hpp"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
#include <share.h>
#include <string>

namespace evr::vkcore::controllers::arm_ticks {

namespace {

constexpr const char* kTag = "arm ticks";

// Type info (build 25216728), as viewmodel_hook.cpp names them (docs/rig-findings/input-aim.md section 3).
constexpr std::size_t kHandsRenderModel = 0x370; // idHands::renderModel (idRenderModelSkinned*)
constexpr std::size_t kModelOrigin = 0x158;      // its g.origin
constexpr std::size_t kModelAxis = 0x164;        // its g.axis (rows forward, left, up)

std::mutex g_mutex;
std::FILE* g_file = nullptr;
bool g_failed = false;
// Off-hand hook thread only: the posed wrist of this tick.
std::optional<LeftWrist> g_wrist;

double seconds(LONGLONG qpc) {
    static const double frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    return static_cast<double>(qpc) / frequency;
}

// The game frame now and the one the hands were last placed in (-1 when unread).
void putFrames(std::string& row, const std::byte* hands) {
    char text[32];
    std::snprintf(text, sizeof(text), ",%d,%d", game_frame::now().value_or(-1),
                  hands ? game_frame::placedIn(hands).value_or(-1) : -1);
    row += text;
}

std::wstring widen(const std::string& text) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

// Under g_mutex.
bool open() {
    if (g_file || g_failed) {
        return g_file != nullptr;
    }
    const std::string& path = settings().testArmTicksPath;
    g_file = _wfsopen(widen(path).c_str(), L"ab", _SH_DENYNO);
    if (!g_file) {
        g_failed = true;
        EVR_LOG("%s: '%s' cannot be opened for writing; no record", kTag, path.c_str());
        return false;
    }
    EVR_LOG("%s: a row per camera hook, viewmodel placement and off-hand tick to '%s' (rig test)", kTag,
            path.c_str());
    return true;
}

void write(const std::string& row) {
    std::lock_guard lock(g_mutex);
    if (!open()) {
        return;
    }
    if (std::fwrite(row.data(), 1, row.size(), g_file) != row.size() || std::fflush(g_file) != 0) {
        EVR_LOG("%s: a write failed; no more rows", kTag);
        std::fclose(g_file);
        g_file = nullptr;
        g_failed = true;
    }
}

void put(std::string& row, float v) {
    char text[32];
    std::snprintf(text, sizeof(text), ",%.5f", static_cast<double>(v));
    row += text;
}

void put(std::string& row, Vec3 v) {
    put(row, v.x);
    put(row, v.y);
    put(row, v.z);
}

void put(std::string& row, const xr_math::IdViewAxis& a) {
    put(row, a.forward);
    put(row, a.left);
    put(row, a.up);
}

std::string head(char kind, LONGLONG stamp) {
    char text[96];
    std::snprintf(text, sizeof(text), "%c,%.6f,%lu,%.6f", kind, seconds(nowQpc()), GetCurrentThreadId(),
                  seconds(stamp));
    return text;
}

} // namespace

bool on() {
    return !settings().testArmTicksPath.empty();
}

void noteWorld(const WorldHand& world, Vec3 eye) {
    if (!on()) {
        return;
    }
    std::string row = head('W', world.qpc);
    putFrames(row, nullptr);
    put(row, eye);
    put(row, world.grip.offset);
    put(row, world.offGrip.offset);
    row += world.offValid ? ",1\n" : ",0\n";
    write(row);
}

void notePlacement(const std::byte* hands,
                   const WorldHand& world,
                   Vec3 eye,
                   const xr_math::EyeRelativePose& placed) {
    if (!on()) {
        return;
    }
    std::string row = head('V', world.qpc);
    putFrames(row, hands);
    put(row, eye);
    put(row, placed.offset);
    put(row, placed.axis);
    row += '\n';
    write(row);
}

void noteLeftWrist(const LeftWrist& wrist) {
    if (on()) {
        g_wrist = wrist;
    }
}

void leftTick(const std::byte* hands, const TickView& view, Vec3 eye, float weight, input::ArmReason reason) {
    if (!on()) {
        return;
    }
    const std::optional<LeftWrist> wrist = g_wrist;
    g_wrist.reset();
    std::string row = head('L', view.world.qpc);
    putFrames(row, hands);
    char text[96];
    std::snprintf(text, sizeof(text), ",%.6f,%.6f", seconds(view.model.qpc), seconds(view.model.worldQpc));
    row += text;
    put(row, eye);
    put(row, view.model.pose.offset);
    put(row, view.model.pose.axis);
    put(row, view.world.offGrip.offset);
    std::snprintf(text, sizeof(text), ",%d,%.4f,%s,%d", view.world.offValid ? 1 : 0,
                  static_cast<double>(weight), input::armReasonName(reason), wrist ? 1 : 0);
    row += text;
    const LeftWrist w = wrist.value_or(LeftWrist{});
    put(row, w.target.position);
    put(row, w.written);
    put(row, w.read);
    put(row, w.animated);
    put(row, w.landed);
    const std::byte* model = nullptr;
    float origin[3] = {};
    float axis[9] = {};
    if (!safeRead(hands + kHandsRenderModel, model) || !model ||
        !safeCopy(origin, model + kModelOrigin, sizeof(origin)) ||
        !safeCopy(axis, model + kModelAxis, sizeof(axis))) {
        origin[0] = origin[1] = origin[2] = 0.0f;
        for (float& a : axis) {
            a = 0.0f;
        }
    }
    for (const float v : origin) {
        put(row, v);
    }
    for (const float v : axis) {
        put(row, v);
    }
    row += '\n';
    write(row);
}

} // namespace evr::vkcore::controllers::arm_ticks
