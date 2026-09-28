// The XR worker's frame table (eternalvr-frames-<pid>.csv, docs/VR_HEAD_TRACKED.md), the display lead it
// measures (xr_math/display_lead.hpp), and the rates in the 10 s line.

#include "vkcore/presenter_impl.hpp"

#include "vkcore/gpu_timing.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>

namespace evr::vkcore {

void XrPresenter::Impl::openFrameLog() {
    const std::wstring dir = logDirectory();
    if (dir.empty()) {
        return;
    }
    wchar_t name[96];
    swprintf_s(name, L"\\eternalvr-frames-%lu.csv", GetCurrentProcessId());
    frameLog = _wfsopen((dir + name).c_str(), L"w", _SH_DENYWR);
    if (frameLog) {
        // The shown view's head (LOCAL) and, under hand aim, the weapon hand's aim ray as used and as
        // tracked (all zero without one), for measuring aim jitter (docs/rig-findings/aim-jitter.md).
        std::fputs("xr_frame,display_time_ns,period_ms,view,pose_age_ms,display_minus_pose_time_ms,lead_ms,"
                   "head_qx,head_qy,head_qz,head_qw,aim_qx,aim_qy,aim_qz,aim_qw,"
                   "aim_tracked_qx,aim_tracked_qy,aim_tracked_qz,aim_tracked_qw\n",
                   frameLog);
    }
}

void XrPresenter::Impl::noteShownView(const XrFrameState& state) {
    if (shownHasView && settings.poseLead) {
        displayLead.noteShown(shownView.seq, state.predictedDisplayTime - shownView.poseTime,
                              state.predictedDisplayPeriod);
    }
}

void XrPresenter::Impl::logFrame(const XrFrameState& state) {
    if (!frameLog) {
        return;
    }
    // Pose age (T-111): from the xrLocateSpace return that latched the shown frame's pose to this
    // xrEndFrame; the prediction horizon is how far ahead of now the runtime asked us to render.
    const double age = shownHasView ? qpcSeconds(qpcNow() - shownView.locatedQpc) * 1000.0 : -1.0;
    const double poseAhead =
        shownHasView ? static_cast<double>(state.predictedDisplayTime - shownView.poseTime) / 1e6 : 0.0;
    const bool aim = shownHasView && shownView.weaponAimValid;
    const XrQuaternionf none{0.0f, 0.0f, 0.0f, 0.0f};
    const XrQuaternionf head = shownHasView ? shownView.pose.orientation : none;
    const XrQuaternionf used = aim ? shownView.weaponAim.orientation : none;
    const XrQuaternionf tracked = aim ? shownView.weaponAimTracked : none;
    std::fprintf(
        frameLog,
        "%llu,%lld,%.3f,%llu,%.3f,%.3f,%.3f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
        static_cast<unsigned long long>(xrFrames), static_cast<long long>(state.predictedDisplayTime),
        static_cast<double>(state.predictedDisplayPeriod) / 1e6,
        static_cast<unsigned long long>(shownHasView ? shownView.seq : 0), age, poseAhead,
        static_cast<double>(displayLead.leadNs()) / 1e6, head.x, head.y, head.z, head.w, used.x, used.y,
        used.z, used.w, tracked.x, tracked.y, tracked.z, tracked.w);
    if (shownHasView) {
        gpu_timing::notePoseAge(age);
        poseAgeSum += age;
        poseAgeMax = std::max(poseAgeMax, age);
        ++poseAgeCount;
    }
}

void XrPresenter::Impl::logRates() {
    logD3dHealth();
    // The game's own rates beside the runtime's: a headset's frame counter shows the XR rate, which a game
    // rendering slower (or faster) than the display does not change.
    const LONGLONG now = qpcNow();
    const std::uint64_t presents = gamePresents.load(std::memory_order_relaxed);
    const std::uint64_t ticks = gameTicks.load(std::memory_order_relaxed);
    const std::uint64_t pairs = pairsPublished.load(std::memory_order_relaxed);
    if (lastRateQpc != 0) {
        const double seconds = qpcSeconds(now - lastRateQpc);
        const auto rate = [seconds](std::uint64_t now, std::uint64_t then) {
            return seconds > 0.0 ? static_cast<double>(now - then) / seconds : 0.0;
        };
        EVR_LOG(
            "rates: game %.1f present(s)/s, %.1f tick(s)/s, %.1f stereo pair(s)/s shown; XR %.1f frame(s)/s, "
            "%.1f new image(s)/s",
            rate(presents, lastRatePresents), rate(ticks, lastRateTicks), rate(pairs, lastRatePairs),
            rate(xrFrames, lastRateXrFrames), rate(xrCopies, lastRateXrCopies));
    }
    lastRateQpc = now;
    lastRatePresents = presents;
    lastRateTicks = ticks;
    lastRatePairs = pairs;
    lastRateXrFrames = xrFrames;
    lastRateXrCopies = xrCopies;
}

} // namespace evr::vkcore
