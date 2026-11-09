// The free off hand's 10 s line (offhand_stats.hpp).

#include "vkcore/offhand_stats.hpp"

#include "features/arm/drawn_wrist.hpp"
#include "vkcore/game_frame.hpp"
#include "vkcore/log.hpp"

#include <windows.h>

#include <array>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore::controllers::offhand_stats {

namespace {

constexpr ULONGLONG kLineMs = 10000;

// The viewmodel hook's last placements, newest last.
std::mutex g_placementMutex;
std::array<arm::FramePlacement, 3> g_placements{};
std::size_t g_placementCount = 0;

// Off-hand hook thread only.
input::OffhandWatch g_watch;
ULONGLONG g_lastLine = 0;
std::optional<Posed> g_posed; // this tick's, from notePosed
// The arm posed at the last tick and at the one before, and the last tick's read of the wrist, with their
// frames.
std::optional<arm::PosedWrist> g_lastPosed;
std::optional<arm::PosedWrist> g_posedBefore;
std::optional<arm::ReadWrist> g_lastRead;
float g_lastUpm = 1.0f;

double seconds(LONGLONG qpc) {
    static const double frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    return static_cast<double>(qpc) / frequency;
}

} // namespace

void notePlacement(Vec3 eye, const xr_math::EyeRelativePose& placed) {
    const std::optional<std::int32_t> frame = game_frame::now();
    if (!frame) {
        return;
    }
    std::lock_guard lock(g_placementMutex);
    if (g_placementCount == g_placements.size()) {
        for (std::size_t i = 1; i < g_placements.size(); ++i) {
            g_placements[i - 1] = g_placements[i];
        }
        --g_placementCount;
    }
    g_placements[g_placementCount++] = {*frame, eye, placed};
}

void notePosed(const Posed& posed) {
    g_posed = posed;
}

void tick(const input::OffhandWatchTick& tick, Vec3 eye, std::uint64_t refused) {
    const std::optional<Posed> posed = g_posed;
    g_posed.reset();
    g_watch.noteTick(tick);
    g_watch.noteRefused(refused);
    const std::optional<std::int32_t> frame = game_frame::now();
    // The last frame as drawn: its read of the wrist (the skeleton drawn in it) under its placement, against
    // the arm written in the frame before it (drawn_wrist.hpp).
    std::optional<arm::DrawnWrist> drawn;
    if (frame && g_lastRead && g_posedBefore && g_lastRead->frame + 1 == *frame) {
        std::array<arm::FramePlacement, 3> placements{};
        std::size_t count = 0;
        {
            std::lock_guard lock(g_placementMutex);
            placements = g_placements;
            count = g_placementCount;
        }
        drawn = arm::drawnWrist(*g_posedBefore, std::span(placements.data(), count), *g_lastRead, g_lastUpm);
    }
    if (drawn) {
        g_watch.noteFrame({drawn->drawn, drawn->meant, drawn->snapshotGapMs});
    } else {
        g_watch.noteBreak();
    }
    g_posedBefore = g_lastPosed;
    g_lastPosed.reset();
    g_lastRead.reset();
    if (posed && frame) {
        g_lastPosed = arm::PosedWrist{*frame,
                                      eye,
                                      posed->placement.pose,
                                      posed->writtenWrist,
                                      seconds(posed->worldQpc),
                                      seconds(posed->placement.worldQpc)};
        g_lastRead = arm::ReadWrist{*frame, posed->readWrist};
        g_lastUpm = posed->unitsPerMetre;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_lastLine == 0) {
        g_lastLine = now;
    }
    if (now - g_lastLine >= kLineMs) {
        g_lastLine = now;
        if (!g_watch.empty()) {
            EVR_LOG("offhand: last 10 s: %s", g_watch.line().c_str());
        }
        g_watch.reset();
    }
}

} // namespace evr::vkcore::controllers::offhand_stats
