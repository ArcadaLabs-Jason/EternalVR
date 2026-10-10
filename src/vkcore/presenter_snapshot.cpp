// The in-headset capture's presenter part (bug_capture.hpp): when the controllers asked for a capture, the
// next Route S eye pair (or, when none comes, a mono frame such as a menu's) is copied into the eye
// capture's host buffers with the game's GUI target, and a text file describes the frame. A burst
// (ETERNALVR_CAPTURE_BURST) goes on with the next pairs (or mono frames), a line each in the text file.

#include "vkcore/presenter_impl.hpp"

#include "vkcore/bug_capture.hpp"
#include "vkcore/taa_hooks.hpp"
#include "vkcore/view_snapshot.hpp"

#include <algorithm>
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

// A burst's line for the frame just taken into `buffer` (nothing for a capture of one frame): which pair and
// game tick it is, or for a mono frame the newest game frame record.
void appendBurstFrame(
    XrPresenter::Impl& p, VkBuffer buffer, bool mono, std::uint64_t pairIndex, std::uint64_t tick) {
    OneShot* shot = p.capture.once();
    if (!buffer || !shot || shot->frames < 2) {
        return;
    }
    if (!mono) {
        append(shot->sidecar, "burst frame %02u: stereo pair %llu, game tick %llu\n", p.capture.frame(),
               static_cast<unsigned long long>(pairIndex), static_cast<unsigned long long>(tick));
        return;
    }
    ViewRecord view;
    std::uint64_t gap = 0;
    append(shot->sidecar, "burst frame %02u: game frame record %llu, head-tracked game frames so far %llu\n",
           p.capture.frame(), p.latestView(view, gap) ? static_cast<unsigned long long>(view.seq) : 0ull,
           static_cast<unsigned long long>(p.gameTicks.load()));
}

// Arms the eye and UI captures for the in-headset capture (names, the text file); false when it has to wait
// for a later frame. The text file describes game tick `tick`'s view for a Route S pair, else the view
// viewForPresent(`pick`) gives the slot.
bool armCapture(XrPresenter::Impl& p,
                const SwapchainState& sc,
                std::uint64_t pairIndex,
                std::uint64_t tick,
                CaptureKind kind,
                std::uint64_t pick = 0) {
    const bool mono = kind == CaptureKind::Mono;
    if (!p.capture.readyForOnce()) {
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
    // A burst takes no more frames than the session's limit leaves (bug_capture::kMaxFramesPerSession).
    shot.frames = std::min(bug_capture::burstFrames(), std::max(1u, bug_capture::framesLeft()));
    if (shot.frames < bug_capture::burstFrames()) {
        EVR_LOG("capture: %u frame(s) left this session; this burst takes %u", bug_capture::framesLeft(),
                shot.frames);
    }
    shot.requestQpc = bug_capture::requestQpc();

    std::string& t = shot.sidecar;
    append(t, "EternalVR in-headset capture\n");
    append(t, "saved: %04u-%02u-%02u %02u:%02u:%02u.%03u local time\n", now.wYear, now.wMonth, now.wDay,
           now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    append(t, "kind: %s\n",
           mono
               ? "mono frame (a menu, loading screen or a frame without eye views: both eyes show this image)"
           : kind == CaptureKind::Pair ? "stereo pair (eye L and eye R of one game tick)"
                                       : "Parallel Eye Rendering frame (both halves of the ring slot the "
                                         "headset gets: eye L from view 0, "
                                         "eye R from view 1, or the same image in both without eye views)");
    append(t, "stereo pair: %llu, game tick: %llu, head-tracked game frames so far: %llu\n",
           static_cast<unsigned long long>(pairIndex), static_cast<unsigned long long>(tick),
           static_cast<unsigned long long>(p.gameTicks.load()));
    append(t, "render size: %ux%u per eye image (swapchain format %d); headset image %ux%u per eye\n",
           sc.extent.width, sc.extent.height, static_cast<int>(sc.format), p.eyeExtent.width,
           p.eyeExtent.height);
    ViewRecord view;
    std::uint64_t gap = 0;
    const bool hasView =
        kind == CaptureKind::Pair ? p.viewBySeq(tick, view) : p.viewForPresent(pick, view, gap);
    if (hasView) {
        append(t, "game frame record: %llu (%s)\n", static_cast<unsigned long long>(view.seq),
               view.stereo ? "eye views" : "one view");
        appendPose(t, "head pose (OpenXR LOCAL)", view.pose);
        if (view.gameOriginValid) {
            append(t,
                   "game position (the game's view origin, as where and setviewpos use it): %.2f %.2f %.2f, "
                   "yaw %.1f deg\n",
                   view.gameOrigin[0], view.gameOrigin[1], view.gameOrigin[2], view.gameYawDegrees);
        }
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
    append(t, "route S: %s; menu up: %s; UI layer: %s\n", p.seqActive.load() ? "on" : "off",
           p.menuUp.load() ? "yes" : "no", p.settings.ui.enabled ? "on" : "off");
    if (p.settings.ui.enabled) {
        p.uiCapture.armOnce(shot.base + L"-UI.png");
    }
    if (p.motionCapture.enabled() && kind == CaptureKind::Pair) { // it takes Route S's eye R only
        p.motionCapture.armOnce(shot.base);
    }
    p.capture.armOnce(std::move(shot));
    return true;
}

// The armed capture was taken into `buffer`, or disarmed when there is none.
VkBuffer takeArmedCapture(XrPresenter::Impl& p, VkBuffer buffer) {
    if (!buffer) {
        p.capture.disarmOnce(); // the next frame tries again
        p.uiCapture.disarmOnce();
        return buffer;
    }
    OneShot* shot = p.capture.once();
    shot->reserved = shot->frames; // a burst that ends early gives back the rest (EyeCapture::writeOnce)
    shot->number = bug_capture::take(shot->frames);
    return buffer;
}

} // namespace

VkBuffer XrPresenter::Impl::pairCaptureBuffer(const SwapchainState& sc,
                                              std::uint64_t completed,
                                              std::uint64_t pairIndex,
                                              std::uint64_t tick) {
    if (const OneShot* shot = capture.once(); capture.between() && shot && shot->mono) {
        capture.endBurst(); // a burst of mono frames goes on with mono frames only
    }
    const bool goesOn = capture.between(); // a burst's next pair (ETERNALVR_CAPTURE_BURST)
    const bool armed =
        !goesOn && bug_capture::wanted() && armCapture(*this, sc, pairIndex, tick, CaptureKind::Pair);
    VkBuffer buffer = capture.bufferFor(dev, 0, pairIndex, sc.format, sc.extent, completed);
    if (armed) {
        buffer = takeArmedCapture(*this, buffer);
    }
    appendBurstFrame(*this, buffer, false, pairIndex, tick);
    return buffer;
}

VkBuffer XrPresenter::Impl::monoCaptureBuffer(const SwapchainState& sc, std::uint64_t completed) {
    capture.poll(dev, completed); // without Route S nothing else polls it
    if (capture.between()) {      // a burst's next frame (ETERNALVR_CAPTURE_BURST)
        const OneShot* shot = capture.once();
        if (!shot || !shot->mono) {
            capture.endBurst(); // a burst of pairs goes on with pairs only
            return VK_NULL_HANDLE;
        }
        const VkBuffer buffer = capture.bufferFor(dev, 0, 0, sc.format, sc.extent, completed);
        appendBurstFrame(*this, buffer, true, 0, 0);
        return buffer;
    }
    // Under Route S a pair normally comes within a frame or two; a menu or loading screen has none.
    if (!bug_capture::wanted() ||
        (seqActive.load() && bug_capture::secondsWaiting() < bug_capture::kMonoAfterSeconds) ||
        !armCapture(*this, sc, pairing.stats().pairsStarted, gameTicks.load(), CaptureKind::Mono)) {
        return VK_NULL_HANDLE;
    }
    const VkBuffer buffer =
        takeArmedCapture(*this, capture.bufferFor(dev, 0, 0, sc.format, sc.extent, completed));
    appendBurstFrame(*this, buffer, true, 0, 0);
    return buffer;
}

std::array<VkBuffer, 2>
XrPresenter::Impl::ringCaptureBuffers(const SwapchainState& sc, std::uint64_t completed, std::uint64_t pick) {
    capture.poll(dev, completed); // without Route S nothing else polls it
    if (const OneShot* shot = capture.once(); capture.between() && shot && shot->mono) {
        capture.endBurst(); // a burst of mono frames goes on with mono frames only
    }
    const bool goesOn = capture.between(); // a burst's next frame (ETERNALVR_CAPTURE_BURST)
    if (!goesOn && (!bug_capture::wanted() ||
                    !armCapture(*this, sc, framesCopied, gameTicks.load(), CaptureKind::RingEyes, pick))) {
        return {};
    }
    // The ring's format and eye size: the images are taken from the slot, not the game's swapchain.
    const std::uint32_t frame = goesOn ? capture.frame() : 0;
    VkBuffer left = capture.bufferFor(dev, 0, 0, ringFormat, eyeExtent, completed);
    if (!goesOn) {
        left = takeArmedCapture(*this, left);
        if (left) { // the eye snapshots' trace of the last presents, the burst's with them
            view_snapshot::captureFired(capture.once() ? capture.once()->frames : 1);
        }
    }
    OneShot* shot = capture.once();
    if (left && shot && shot->frames > 1) { // which game frame each of the burst's frames shows
        ViewRecord view;
        std::uint64_t gap = 0;
        append(shot->sidecar, "burst frame %02u: game frame record %llu\n", frame,
               viewForPresent(pick, view, gap) ? static_cast<unsigned long long>(view.seq) : 0ull);
    }
    return {left, left ? capture.bufferFor(dev, 1, 0, ringFormat, eyeExtent, completed) : VK_NULL_HANDLE};
}

void XrPresenter::Impl::captureCopied(VkBuffer buffer, std::uint64_t value, bool complete) {
    if (!buffer) {
        return;
    }
    if (value == 0) {
        capture.cancel(); // not submitted: the next frame is taken instead
        return;
    }
    capture.copySubmitted(value);
    // The GUI target is armed with the capture and comes with its first frame only: a burst's later frames
    // leave the first one's result.
    if (OneShot* shot = capture.once(); shot && capture.frame() == 0) {
        shot->ui = settings.ui.enabled && !uiCapture.disarmOnce();
        if (!settings.ui.enabled) {
            shot->uiNote = "the UI layer is off: the HUD is in the eye images";
        } else if (!shot->ui) {
            shot->uiNote = std::string("the GUI target was not copied with this frame, last check: ") +
                           ui_layer::toString(uiLastCheck);
        }
    }
    if (complete) {
        capture.submitted(value, gameTicks.load()); // one present held all of it
    }
}

} // namespace evr::vkcore
