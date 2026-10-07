// The XR worker's frame table (eternalvr-frames-<pid>.csv, docs/VR_HEAD_TRACKED.md), the display lead it
// measures (xr_math/display_lead.hpp), the display period's watch (presenter_refresh.hpp), and the 10 s
// lines, the present hook's copies among them.

#include "vkcore/presenter_impl.hpp"

#include "vkcore/frame_pacing.hpp"
#include "vkcore/gpu_timing.hpp"
#include "vkcore/presenter_result.hpp"
#include "vkcore/stall_watch.hpp"
#include "vkcore/vram_watch.hpp"

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
    frame_pacing::noteShown(shownHasView ? shownView.seq : 0,
                            state.predictedDisplayTime - shownView.poseTime);
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

void XrPresenter::Impl::afterFrame(const XrFrameState& state) {
    refresh.onFrame(state.predictedDisplayPeriod);
    if (GetTickCount64() - lastXrStatsTicks < 10000) {
        return;
    }
    lastXrStatsTicks = GetTickCount64();
    EVR_LOG("xr: %llu frame(s), %llu new image(s), %llu repeat(s) (%llu with no finished image to "
            "take); %llu head-tracked, %llu on the screen; pose age average %.1f ms, max %.1f ms; display "
            "period %.2f ms, pose lead %.1f ms; %llu newest image(s), %llu wait(s) for one",
            static_cast<unsigned long long>(xrFrames), static_cast<unsigned long long>(xrCopies),
            static_cast<unsigned long long>(xrRepeats + xrBusyRepeats),
            static_cast<unsigned long long>(xrBusyRepeats),
            static_cast<unsigned long long>(xrProjectionFrames),
            static_cast<unsigned long long>(xrQuadFrames),
            poseAgeCount ? poseAgeSum / static_cast<double>(poseAgeCount) : 0.0, poseAgeMax,
            static_cast<double>(state.predictedDisplayPeriod) / 1e6,
            static_cast<double>(displayLead.leadNs()) / 1e6, static_cast<unsigned long long>(xrNewestTaken),
            static_cast<unsigned long long>(xrNewestWaits));
    poseAgeSum = 0.0;
    poseAgeMax = 0.0;
    poseAgeCount = 0;
    logRates();
    logSizeStats();
    vignette.logStats(settings.ui.vignette);
    if (frameLog) {
        std::fflush(frameLog);
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
    if (lastRates.qpc != 0) {
        const double seconds = qpcSeconds(now - lastRates.qpc);
        const auto rate = [seconds](std::uint64_t now, std::uint64_t then) {
            return seconds > 0.0 ? static_cast<double>(now - then) / seconds : 0.0;
        };
        EVR_LOG(
            "rates: game %.1f present(s)/s, %.1f tick(s)/s, %.1f stereo pair(s)/s shown; XR %.1f frame(s)/s, "
            "%.1f new image(s)/s; copy wait %.2f ms average, %.2f ms longest; newest image wait %.2f ms "
            "average, "
            "%.2f ms longest",
            rate(presents, lastRates.presents), rate(ticks, lastRates.ticks), rate(pairs, lastRates.pairs),
            rate(xrFrames, lastRates.xrFrames), rate(xrCopies, lastRates.xrCopies), copyWait.averageMs(),
            copyWait.maxMs, newestWait.averageMs(), newestWait.maxMs);
    }
    copyWait = {};
    newestWait = {};
    lastRates = {presents, ticks, pairs, xrFrames, xrCopies, now};
    checkGamePresents(*this);   // a line once the game stops presenting (presenter_result.hpp)
    frame_pacing::logSummary(); // the headset's cadence, and ETERNALVR_PACE's waits
    vram::logSummary();
    stall_watch::logSummary();
}

void XrPresenter::Impl::logCopyStats(const SwapchainState& sc, std::uint32_t family) {
    if (!loggedFirstCopy) {
        loggedFirstCopy = true;
        EVR_LOG(
            "presenter: first copy into the ring (%ux%u format %d -> %ux%u format %d, %u eye(s) of %ux%u, "
            "%s, queue family %u)",
            sc.extent.width, sc.extent.height, sc.format, ringExtent.width, ringExtent.height, ringFormat,
            ringEyes, eyeExtent.width, eyeExtent.height,
            (sc.format == ringFormat && sc.extent.width == eyeExtent.width &&
             sc.extent.height == eyeExtent.height)
                ? "copy"
                : "blit",
            family);
    } else if (GetTickCount64() - lastStatsTicks >= 10000) {
        lastStatsTicks = GetTickCount64();
        EVR_LOG(
            "presenter: %llu frame(s) copied, %llu dropped (slot busy or not copyable), %llu of them size or "
            "format mismatches; %llu with a head-tracked view (average %.2f frame(s) behind the newest), "
            "%llu "
            "without",
            static_cast<unsigned long long>(framesCopied), static_cast<unsigned long long>(framesDropped),
            static_cast<unsigned long long>(framesShapeMismatch),
            static_cast<unsigned long long>(presentsWithView),
            presentsWithView ? static_cast<double>(presentSeqGapSum) / static_cast<double>(presentsWithView)
                             : 0.0,
            static_cast<unsigned long long>(presentsWithoutView));
    }
}

} // namespace evr::vkcore
