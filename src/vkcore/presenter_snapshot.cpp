// The in-headset capture's presenter part (bug_capture.hpp): when the controllers asked for a capture, the
// next Route S eye pair (or, when none comes, a mono frame such as a menu's) is copied into the eye
// capture's host buffers with the game's GUI target, and a text file describes the frame.

#include "vkcore/presenter_impl.hpp"

#include "vkcore/bug_capture.hpp"
#include "vkcore/taa_hooks.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <utility>

namespace evr::vkcore {

namespace {

void append(std::string& out, const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (n > 0) {
        out += line;
    }
}

double degrees(float radians) {
    return static_cast<double>(radians) * 180.0 / 3.14159265358979323846;
}

void appendPose(std::string& out, const char* what, const XrPosef& p) {
    append(out, "%s: position %.4f %.4f %.4f m, orientation (x y z w) %.5f %.5f %.5f %.5f\n", what,
           p.position.x, p.position.y, p.position.z, p.orientation.x, p.orientation.y, p.orientation.z,
           p.orientation.w);
}

void appendFov(std::string& out, const char* what, const XrFovf& f, const char* end = " degrees\n") {
    append(out, "%s: left %.2f right %.2f up %.2f down %.2f%s", what, degrees(f.angleLeft),
           degrees(f.angleRight), degrees(f.angleUp), degrees(f.angleDown), end);
}

} // namespace

bool XrPresenter::Impl::armCapture(const SwapchainState& sc,
                                   std::uint64_t pairIndex,
                                   std::uint64_t tick,
                                   bool mono) {
    if (!capture.readyForOnce()) {
        return false; // a pair in flight or the writer busy: the next frame
    }
    const std::wstring dir = bug_capture::directory();
    if (dir.empty()) {
        bug_capture::take(); // logged; nothing to save into
        return false;
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t stem[128];
    swprintf_s(stem, L"\\capture-%04u%02u%02u-%02u%02u%02u-p%06llu-t%llu", now.wYear, now.wMonth, now.wDay,
               now.wHour, now.wMinute, now.wSecond, static_cast<unsigned long long>(pairIndex),
               static_cast<unsigned long long>(tick));
    OneShot shot;
    shot.base = dir + stem;
    shot.mono = mono;
    shot.requestQpc = bug_capture::requestQpc();

    std::string& t = shot.sidecar;
    append(t, "EternalVR in-headset capture\n");
    append(t, "saved: %04u-%02u-%02u %02u:%02u:%02u.%03u local time\n", now.wYear, now.wMonth, now.wDay,
           now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    append(t, "kind: %s\n",
           mono
               ? "mono frame (a menu, loading screen or a frame without eye views: both eyes show this image)"
               : "stereo pair (eye L and eye R of one game tick)");
    append(t, "stereo pair: %llu, game tick: %llu, head-tracked game frames so far: %llu\n",
           static_cast<unsigned long long>(pairIndex), static_cast<unsigned long long>(tick),
           static_cast<unsigned long long>(gameTicks.load()));
    append(t, "render size: %ux%u per eye image (swapchain format %d); headset image %ux%u per eye\n",
           sc.extent.width, sc.extent.height, static_cast<int>(sc.format), eyeExtent.width, eyeExtent.height);
    ViewRecord view;
    std::uint64_t gap = 0;
    const bool hasView = mono ? latestView(view, gap) : viewBySeq(tick, view);
    if (hasView) {
        appendPose(t, "head pose (OpenXR LOCAL)", view.pose);
        // The game's FOV setting is one symmetric FOV covering both eyes; each eye renders its own.
        const char* setting = "game FOV setting (the game's own, symmetric, both eyes)";
        if (!view.stereo) {
            appendFov(t, setting, view.fov);
        } else {
            appendFov(t, setting, view.fov, " degrees; ");
            appendFov(t, "rendered FOV eye L", view.eyes[0].fov, "; ");
            appendFov(t, "eye R", view.eyes[1].fov);
            appendPose(t, "eye L pose", view.eyes[0].pose);
            appendPose(t, "eye R pose", view.eyes[1].pose);
            const XrVector3f& l = view.eyes[0].pose.position;
            const XrVector3f& r = view.eyes[1].pose.position;
            append(t, "eye separation: %.1f mm\n",
                   1000.0 *
                       std::sqrt(static_cast<double>((l.x - r.x) * (l.x - r.x) + (l.y - r.y) * (l.y - r.y) +
                                                     (l.z - r.z) * (l.z - r.z))));
        }
        if (view.weaponAimValid) {
            appendPose(t, "weapon hand aim (OpenXR LOCAL)", view.weaponAim);
        }
    } else {
        append(t, "head pose: none (the frame has no head-tracked view record)\n");
    }
    const TaaCounters taa = taaCounters();
    append(t,
           "taa: per-eye history %s (ETERNALVR_STEREO_TAA %s); r_antialiasing %d, r_TAASafeMode %d, r_jitter "
           "%d, r_TAAAntiGhosting %d (-1: not located)\n",
           taa.perEye ? "on" : "off", taaRequested() ? "on" : "off", taa.antialiasing, taa.safeMode,
           taa.jitter, taa.antiGhosting);
    append(t,
           "dlss: evaluations so far eye L %llu / eye R %llu (%llu of eye R without its own feature); eye R "
           "features %llu made / %llu failed; accumulation %dx%d, eye R %dx%d\n",
           static_cast<unsigned long long>(taa.ngx.evaluates[0]),
           static_cast<unsigned long long>(taa.ngx.evaluates[1]),
           static_cast<unsigned long long>(taa.ngx.evaluatesNoTwin),
           static_cast<unsigned long long>(taa.ngx.twinCreates),
           static_cast<unsigned long long>(taa.ngx.twinFailures), taa.sizeA[0], taa.sizeA[1], taa.sizeB[0],
           taa.sizeB[1]);
    append(t, "route S: %s; menu up: %s; UI layer: %s\n", seqActive.load() ? "on" : "off",
           menuUp.load() ? "yes" : "no", settings.ui.enabled ? "on" : "off");
    if (settings.ui.enabled) {
        uiCapture.armOnce(shot.base + L"-UI.png");
    }
    if (motionCapture.enabled() && !mono) {
        motionCapture.armOnce(shot.base);
    }
    capture.armOnce(std::move(shot));
    return true;
}

VkBuffer XrPresenter::Impl::takeArmedCapture(VkBuffer buffer) {
    if (!buffer) {
        capture.disarmOnce(); // the next frame tries again
        uiCapture.disarmOnce();
        return buffer;
    }
    capture.once()->number = bug_capture::take();
    return buffer;
}

VkBuffer XrPresenter::Impl::pairCaptureBuffer(const SwapchainState& sc,
                                              std::uint64_t completed,
                                              std::uint64_t pairIndex,
                                              std::uint64_t tick) {
    const bool armed = bug_capture::wanted() && armCapture(sc, pairIndex, tick, false);
    const VkBuffer buffer = capture.bufferFor(dev, 0, pairIndex, sc.format, sc.extent, completed);
    return armed ? takeArmedCapture(buffer) : buffer;
}

VkBuffer XrPresenter::Impl::monoCaptureBuffer(const SwapchainState& sc, std::uint64_t completed) {
    capture.poll(dev, completed); // without Route S nothing else polls it
    // Under Route S a pair normally comes within a frame or two; a menu or loading screen has none.
    if (!bug_capture::wanted() ||
        (seqActive.load() && bug_capture::secondsWaiting() < bug_capture::kMonoAfterSeconds) ||
        !armCapture(sc, pairing.stats().pairsStarted, gameTicks.load(), true)) {
        return VK_NULL_HANDLE;
    }
    return takeArmedCapture(capture.bufferFor(dev, 0, 0, sc.format, sc.extent, completed));
}

void XrPresenter::Impl::captureCopied(VkBuffer buffer, std::uint64_t value, bool mono) {
    if (!buffer) {
        return;
    }
    if (value == 0) {
        capture.cancel(); // not submitted: the next frame is taken instead
        return;
    }
    capture.copySubmitted(value);
    if (OneShot* shot = capture.once()) {
        shot->ui = settings.ui.enabled && !uiCapture.disarmOnce();
        if (!settings.ui.enabled) {
            shot->uiNote = "the UI layer is off: the HUD is in the eye images";
        } else if (!shot->ui) {
            shot->uiNote = std::string("the GUI target was not copied with this frame, last check: ") +
                           ui_layer::toString(uiLastCheck);
        }
    }
    if (mono) {
        capture.submitted(value, gameTicks.load()); // one image: complete with eye L's copy
    }
}

} // namespace evr::vkcore
