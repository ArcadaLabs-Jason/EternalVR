#include "vkcore/presenter_cinema3d.hpp"

#include "vkcore/presenter_impl.hpp"

#include "vkcore/cinema3d_frames.hpp"

#include "features/roomscale/roomscale_settings.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/head_sweep.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/stall_watch.hpp"
#include "xr_math/cinema_eyes.hpp"
#include "xr_math/cinema_run.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace evr::vkcore {

namespace {

// Lines logged per run: its start, its cuts and its lens changes; and one line of traces for each of this
// many game frames (the captures' frame numbers line up with them, ETERNALVR_CAPTURE_EYES), up to
// kLoggedTraceLines.
constexpr int kLoggedLines = 40;
constexpr std::uint64_t kTraceLogEvery = 240;
constexpr int kLoggedTraceLines = 300;

// Camera hook thread only.
struct State {
    int logged = 0;
    std::uint64_t frames = 0;  // pairs recorded in this run
    std::uint64_t cuts = 0;    // camera cuts in this run
    std::uint64_t clamped = 0; // frames whose separation the comfort clamp lowered
    float lastFovX = 0.0f;     // the last frame's FOV, for the change log
    float limit = 0.0f;        // the separation limit, smoothed (cinemaSmoothLimit)
    LONGLONG lastQpc = 0;      // the last frame's time
    int tracesLogged = 0;
    std::uint64_t traceFrames = 0; // frames traced, rays hit in them, microseconds spent
    std::uint64_t traceHits = 0;
    double traceMicros = 0.0;
    double traceMaxMicros = 0.0;
};

// The nearest hit of the fan of rays through the shot, in metres (0: none within reach), and how many rays
// hit.
float traceNearest(const std::byte* player,
                   const xr_math::IdViewAxis& axis,
                   const float* origin,
                   float tanHalfX,
                   float tanHalfLens,
                   float unitsPerMetre,
                   unsigned& hits) {
    hits = 0;
    if (!player || !headSweepAvailable()) {
        return 0.0f;
    }
    const float reachUnits = xr_math::kCinemaTraceReachMetres * unitsPerMetre;
    const Vec3 from{origin[0], origin[1], origin[2]};
    float nearest = 0.0f;
    for (const xr_math::CinemaRay& ray : xr_math::cinemaRayFan(tanHalfX, tanHalfLens)) {
        const Vec3 direction = xr_math::cinemaRayDirection(axis, ray);
        bool inContact = false;
        const std::optional<float> hit = sweepShot(player, from, from + direction * reachUnits, inContact);
        if (inContact || !hit || !std::isfinite(*hit)) {
            continue; // in contact at the start: no use as a distance
        }
        ++hits;
        const float metres = *hit * xr_math::kCinemaTraceReachMetres;
        nearest = nearest > 0.0f ? std::min(nearest, metres) : metres;
    }
    return nearest;
}
State g_state;
xr_math::CinemaNearConfirm g_nearConfirm;       // camera hook thread only
xr_math::CinemaRun g_run;                       // camera hook thread only
std::atomic<std::uint64_t> g_pairQuadFrames{0}; // XR worker: frames shown as a pair
std::atomic<std::uint64_t> g_staleGuiFrames{0}; // of them, without a fresh GUI image (no GUI quad)
std::atomic<std::uint64_t> g_monoPairFrames{0}; // XR worker: pair frames on the mono quad (no GUI image)
std::atomic<bool> g_uiLayerWorks{false};        // XR worker -> camera hook (cinema3dNoteUiLayer)
std::atomic<std::uint64_t> g_runFirstSeq{0};    // the latest run's first game frame (ViewRecord::seq)
std::atomic<std::uint64_t> g_runEndSeq{0};      // the first game frame after it (0: still running)

XrFovf toXr(const xr_math::Fov& f) {
    return {f.angleLeft, f.angleRight, f.angleUp, f.angleDown};
}

void logRunEnd(const char* why, std::uint64_t latestSeq) {
    g_runEndSeq.store(latestSeq + 1, std::memory_order_release);
    EVR_LOG("cinema3D: pair run ended (%s): %llu frame(s), %llu camera cut(s), comfort clamp on %llu "
            "frame(s); %llu stereo frame(s) shown on two quads since the start, %llu of them without a fresh "
            "GUI image; %llu on the mono quad (no GUI image within %.1f s)",
            why, static_cast<unsigned long long>(g_state.frames),
            static_cast<unsigned long long>(g_state.cuts), static_cast<unsigned long long>(g_state.clamped),
            static_cast<unsigned long long>(g_pairQuadFrames.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_staleGuiFrames.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_monoPairFrames.load(std::memory_order_relaxed)),
            xr_math::kCinemaGuiHoldSeconds);
    if (g_state.traceFrames > 0) {
        EVR_LOG("cinema3D: traces: %llu frame(s) of 15 rays, %.1f rays hit per frame, %.0f us per frame on "
                "average, %.0f us at most",
                static_cast<unsigned long long>(g_state.traceFrames),
                static_cast<double>(g_state.traceHits) / static_cast<double>(g_state.traceFrames),
                g_state.traceMicros / static_cast<double>(g_state.traceFrames), g_state.traceMaxMicros);
    }
}

void endRun(const XrPresenter::Impl& p, const char* why) {
    if (g_run.end()) {
        logRunEnd(why, p.latestSeq);
    }
}

// While a glory kill is shown as a fade, the camera hook holds the view black this far ahead, each frame.
constexpr double kGloryBlackRefreshSeconds = 0.1;

} // namespace

bool flatScreenGameView(XrPresenter::Impl& p, std::byte* renderView, std::byte* player) {
    p.trackCutscene(renderView[render_view::kInCutscene] != std::byte{0});
    cutscene_eyes::noteGameView(renderView, p.cutscene);
    // Glory kills (glory_view.hpp): on the flat screen, faded out, or with a steady heading (head aim).
    const double now = qpcSeconds(qpcNow());
    const bool menu = p.menuUp.load(std::memory_order_relaxed);
    if (p.glory.frame(player, controllers::forcedView(), menu, now).started && p.glory.onScreen()) {
        p.replaceScreen.store(true, std::memory_order_release);
    }
    if (p.glory.black()) {
        p.room.holdBlack(now + kGloryBlackRefreshSeconds);
    }
    const bool flatScreen = (p.cutscene && p.settings.cutsceneCinema) || p.glory.onScreen();
    p.cinemaView.onGameView(reinterpret_cast<float*>(renderView + render_view::kFovX), flatScreen,
                            p.settings.cinemaAspect);
    if (!p.trackingReady.load(std::memory_order_acquire)) {
        return true;
    }
    if (flatScreen) {
        // The cutscene (or glory kill) keeps the game's camera and view; its frames go to the flat screen
        // (kViewStaleSeconds, or at once for a glory kill: GloryKills::flat), as a stereo pair if wanted.
        cinema3dGameView(p, renderView, player, p.cutscene && !p.glory.onScreen());
        return true;
    }
    return false;
}

bool cinema3dGameView(XrPresenter::Impl& p,
                      std::byte* renderView,
                      const std::byte* player,
                      bool cutsceneOnScreen) {
    // The GUI goes on its own quad over the pair (eye R's render has none): the UI layer is needed, and
    // working (not off after a failed swapchain or GUI images that could not be made).
    const bool wanted = p.settings.cutscene3D && cutsceneOnScreen && p.settings.cutsceneCinema &&
                        p.settings.ui.enabled && g_uiLayerWorks.load(std::memory_order_relaxed) &&
                        p.settings.stereo.sequential && p.seqActive.load(std::memory_order_acquire) &&
                        !seqAlternateEyes();
    if (!wanted) {
        endRun(p, "not applicable");
        return false;
    }
    const auto* fov = reinterpret_cast<const float*>(renderView + render_view::kFovX);
    const auto* origin = reinterpret_cast<const float*>(renderView + render_view::kViewOrigin);
    const auto* axis = reinterpret_cast<const float*>(renderView + render_view::kViewAxis);
    const xr_math::IdViewAxis gameAxis{
        {axis[0], axis[1], axis[2]}, {axis[3], axis[4], axis[5]}, {axis[6], axis[7], axis[8]}};
    const auto symmetric = xr_math::fovFromGame({fov[0], fov[1]});
    xr_math::CinemaStereoInput in;
    const float ipd = roomScaleSettings().ipdMetres;
    in.interaxialMetres = ipd >= 0.04f && ipd <= 0.09f ? ipd : xr_math::kCinemaDefaultInteraxialMetres;
    in.fovXDegrees = fov[0];
    in.fovYDegrees = fov[1];
    const float lensFovY = xr_math::cinemaLensFovY(fov[0], fov[1], p.settings.cinemaAspect);
    in.unitsPerMetre = p.settings.unitsPerMetre;
    const auto first = xr_math::cinemaStereo(in);
    if (!xr_math::isOrthonormal(gameAxis, 1e-2f) || !symmetric || !first) {
        endRun(p, "the view has no usable axis or FOV");
        return false;
    }
    const xr_math::CinemaPairStep step =
        g_run.pair(renderView[render_view::kCameraCut] != std::byte{0}, qpcSeconds(qpcNow()));
    if (step.afterGap) {
        logRunEnd("no pair frame for a while", p.latestSeq);
    }
    const bool started = step.started;
    const bool cut = step.cut;
    if (started) {
        g_state = State{};
        g_nearConfirm.reset();
        g_runEndSeq.store(0, std::memory_order_release);
        g_runFirstSeq.store(p.latestSeq + 1, std::memory_order_release); // this frame's, assigned below
    }
    g_state.cuts += cut ? 1 : 0;
    ++g_state.frames;
    // The nearest thing the traces find through the shot lowers the limit for this frame; it falls at once
    // and rises slowly (xr_math::cinemaSmoothLimit), and a cut or the first frame starts afresh.
    const LONGLONG traceStart = qpcNow();
    unsigned hits = 0;
    const float tanX = std::tan(fov[0] * 0.5f * 0.017453292f);
    const float tanLens = std::tan(lensFovY * 0.5f * 0.017453292f);
    in.traceNearestMetres = g_nearConfirm.step(
        traceNearest(player, gameAxis, origin, tanX, tanLens, in.unitsPerMetre, hits), started || cut);
    if ((started || cut) && g_state.logged < kLoggedLines &&
        renderView[render_view::kUsesViewOriginOffset] != std::byte{0}) {
        // The traces start at vieworg; a view that renders from viewOriginOffset + localViewOrigin is
        // logged with both, to check they agree.
        const auto* local = reinterpret_cast<const float*>(renderView + render_view::kLocalViewOrigin);
        const auto* offset = reinterpret_cast<const float*>(renderView + render_view::kViewOriginOffset);
        EVR_LOG("cinema3D: view renders from viewOriginOffset (%.1f %.1f %.1f) + local (%.1f %.1f %.1f); "
                "vieworg (%.1f %.1f %.1f), where the traces start",
                offset[0], offset[1], offset[2], local[0], local[1], local[2], origin[0], origin[1],
                origin[2]);
    }
    const double traceMicros = qpcSeconds(qpcNow() - traceStart) * 1.0e6;
    ++g_state.traceFrames;
    g_state.traceHits += hits;
    g_state.traceMicros += traceMicros;
    g_state.traceMaxMicros = std::max(g_state.traceMaxMicros, traceMicros);
    const auto target = xr_math::cinemaStereo(in);
    const double seconds = g_state.lastQpc != 0 ? qpcSeconds(traceStart - g_state.lastQpc) : 0.0;
    g_state.lastQpc = traceStart;
    g_state.limit = target ? xr_math::cinemaSmoothLimit(g_state.limit, target->comfortMetres,
                                                        static_cast<float>(seconds), started || cut)
                           : g_state.limit;
    in.limitMetres = g_state.limit;
    const auto stereo = xr_math::cinemaStereo(in);
    if (!stereo) {
        endRun(p, "the view has no usable FOV");
        return false;
    }
    g_state.clamped += stereo->clamped ? 1 : 0;
    const bool lensChanged = std::fabs(fov[0] - g_state.lastFovX) > 0.5f;
    if ((started || cut || lensChanged) && g_state.logged < kLoggedLines) {
        ++g_state.logged;
        EVR_LOG(
            "cinema3D: %s: fov %.2f x %.2f (lens %.2f), camera separation %.4f m (wanted %.4f%s), zero "
            "parallax at %.2f m, nearest assumed %.2f m (traced %.2f m, %u of 15 rays) at %.4f of the screen "
            "width (infinity %.4f)%s",
            started ? "pair run started"
            : cut   ? "camera cut"
                    : "lens changed",
            fov[0], fov[1], lensFovY, stereo->interaxialMetres, in.interaxialMetres,
            stereo->clamped ? ", comfort clamp" : "", stereo->zeroParallaxMetres, stereo->nearestMetres,
            in.traceNearestMetres, hits, stereo->nearestDisparity, xr_math::kCinemaInfinityShift,
            g_state.logged == kLoggedLines ? " (later lines are not logged)" : "");
    }
    g_state.lastFovX = fov[0];

    ViewRecord record;
    record.gameOriginValid = true;
    record.gameOrigin = {origin[0], origin[1], origin[2]};
    record.gameYawDegrees = std::atan2(gameAxis.forward.y, gameAxis.forward.x) * 57.29578f;
    record.poseTime = p.nextDisplayTime.load();
    record.locatedQpc = qpcNow();
    record.axis = {axis[0], axis[1], axis[2], axis[3], axis[4], axis[5], axis[6], axis[7], axis[8]};
    record.fov = toXr(*symmetric);
    record.stereo = true;
    record.cinema3D = true;
    record.cutscene = true;
    record.cutsceneArms = true; // the game's own weapon FOV and matrices stay (no hands in these frames)
    record.historyReset = step.historyReset;
    for (std::size_t i = 0; i < 2; ++i) {
        EyeRecord& eye = record.eyes[i];
        eye.fov = toXr(stereo->eyes[i].fov);
        const float units = stereo->eyes[i].leftUnits;
        eye.offset = {gameAxis.left.x * units, gameAxis.left.y * units, gameAxis.left.z * units};
        eye.axis = record.axis;
    }
    {
        std::lock_guard lock(p.historyMutex);
        record.seq = ++p.latestSeq;
        p.history[record.seq % kHistorySize] = record;
    }
    if (record.seq % kTraceLogEvery == 1 && g_state.tracesLogged < kLoggedTraceLines) {
        ++g_state.tracesLogged;
        EVR_LOG(
            "cinema3D: frame %llu: fov %.2f, traced nearest %.2f m (%u of 15 rays hit), assumed nearest %.2f "
            "m, comfort limit %.4f m now %.4f m, separation %.4f m, nearest disparity %.4f of the screen "
            "width",
            static_cast<unsigned long long>(record.seq), fov[0], in.traceNearestMetres, hits,
            stereo->nearestMetres, target ? target->comfortMetres : 0.0f, g_state.limit,
            stereo->interaxialMetres, stereo->nearestDisparity);
    }
    ++p.gameViews;
    p.gameTicks.fetch_add(1, std::memory_order_relaxed);
    stall_watch::onGameTick();
    return true;
}

bool cinema3dResetAfter(const XrPresenter::Impl& p, bool inCutscene) {
    bool ended = false;
    const bool reset = g_run.headTracked(inCutscene, ended);
    if (ended) {
        logRunEnd("the player's view is back", p.latestSeq);
    }
    return reset;
}

bool cinema3dFrame(std::uint64_t seq) {
    const std::uint64_t first = g_runFirstSeq.load(std::memory_order_acquire);
    const std::uint64_t end = g_runEndSeq.load(std::memory_order_acquire);
    return first != 0 && seq >= first && (end == 0 || seq < end);
}

void fillScreenQuad(const XrPresenter::Impl& p, XrCompositionLayerQuad& quad) {
    quad.layerFlags = 0;
    quad.space = p.localSpace;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = p.xrSwapchain;
    // The left half of a Route S ring (a mono frame fills both halves).
    quad.subImage.imageRect = {
        {0, 0},
        {static_cast<std::int32_t>(p.eyeExtent.width), static_cast<std::int32_t>(p.eyeExtent.height)}};
    quad.subImage.imageArrayIndex = 0;
    quad.pose = p.quadPose;
    quad.size = p.quadSize;
}

std::uint32_t screenLayers(XrPresenter::Impl& p,
                           XrCompositionLayerQuad& left,
                           XrCompositionLayerQuad& right,
                           XrCompositionLayerQuad& ui,
                           const XrCompositionLayerBaseHeader** layers,
                           bool& uiShown) {
    if (const auto band = p.cinemaView.band(p.eyeExtent.width, p.eyeExtent.height)) {
        // A cutscene drawn for a flat display's shape: only its band, on a screen of that shape.
        left.subImage.imageRect.offset.y = static_cast<std::int32_t>(band->y);
        left.subImage.imageRect.extent.height = static_cast<std::int32_t>(band->height);
        left.size.height =
            p.quadSize.width * static_cast<float>(band->height) / static_cast<float>(p.eyeExtent.width);
    }
    return cinemaEyeLayers(p, left, right, ui, layers, uiShown);
}

std::uint32_t cinemaEyeLayers(XrPresenter::Impl& p,
                              XrCompositionLayerQuad& left,
                              XrCompositionLayerQuad& right,
                              XrCompositionLayerQuad& ui,
                              const XrCompositionLayerBaseHeader** layers,
                              bool& uiShown) {
    // Eye R's render has no GUI (docs/rig-findings/ui-layer.md, section 3): the GUI is left out of both eye
    // images and shown on a quad over the screen instead, the same in both eyes, at the screen. The quad is
    // the HUD's own image (the 16:9 band the game lays its GUI out in) at the screen's place and width.
    // Without a fresh GUI image the pair stays on its two quads (the depth does not come and go with it),
    // and the GUI stays out of the eye images, for kCinemaGuiHoldSeconds; past that (or with no GUI image
    // at all) the game composites its GUI into eye L only, so the frame goes on the mono quad
    // (xr_math::cinemaLayerPlan).
    const bool pair = p.shownView.cinema3D && p.shownView.showEyes;
    const bool fresh = pair && p.fillUiQuad(ui);
    const double sinceGui = p.uiHasImage ? qpcSeconds(qpcNow() - p.uiShownQpc) : -1.0;
    const xr_math::CinemaLayerPlan plan = xr_math::cinemaLayerPlan(pair, fresh, sinceGui);
    if (!plan.eyes) {
        g_monoPairFrames.fetch_add(pair ? 1 : 0, std::memory_order_relaxed);
        return 0;
    }
    right = left;
    left.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
    right.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
    right.subImage.imageRect.offset.x += static_cast<std::int32_t>(p.eyeExtent.width);
    layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&right);
    std::uint32_t count = 1;
    if (plan.gui) {
        ui.space = p.localSpace;
        ui.pose = p.quadPose;
        ui.size = {p.quadSize.width, p.quadSize.width * ui.size.height / ui.size.width};
        layers[count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&ui);
    } else {
        g_staleGuiFrames.fetch_add(1, std::memory_order_relaxed);
    }
    uiShown = plan.skipComposite;
    if (g_pairQuadFrames.fetch_add(1, std::memory_order_relaxed) == 0) {
        EVR_LOG(
            "cinema3D: the first stereo cutscene frame is on two screen quads (eye L, eye R), the GUI on a "
            "third over them");
    }
    return count;
}

void cinema3dNoteUiLayer(bool working) {
    g_uiLayerWorks.store(working, std::memory_order_relaxed);
}

} // namespace evr::vkcore
