// Stereo through the engine's own two-view path (docs/VR_STEREO.md): each game frame's eye data (camera
// hook), the per-eye hook that turns each of the engine's two views into one eye, the post-latch check,
// and the projection layer's per-eye views.

#include "vkcore/presenter_impl.hpp"

#include "features/roomscale/eye_separation.hpp"
#include "features/tracking/fov_watch.hpp"

#include "stereo_seq/adaptive_eyes.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/presenter_eyes.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/vrs_nv.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <string>
#include <utility>

namespace evr::vkcore {

namespace {

bool stereoFlag(const wchar_t* name, bool fallback) {
    std::wstring value;
    if (!readEnv(name, value) || value.empty()) {
        return fallback;
    }
    return !(value == L"0" || _wcsicmp(value.c_str(), L"false") == 0 || _wcsicmp(value.c_str(), L"off") == 0);
}

XrFovf toXr(const xr_math::Fov& f) {
    return {f.angleLeft, f.angleRight, f.angleUp, f.angleDown};
}

xr_math::Fov fromXr(const XrFovf& f) {
    return {f.angleLeft, f.angleRight, f.angleUp, f.angleDown};
}

bool finitePose(const XrPosef& p) {
    return std::isfinite(p.orientation.x) && std::isfinite(p.orientation.y) &&
           std::isfinite(p.orientation.z) && std::isfinite(p.orientation.w) && std::isfinite(p.position.x) &&
           std::isfinite(p.position.y) && std::isfinite(p.position.z);
}

constexpr float kDegrees = 57.29578f;

// Frames whose eye FOVs foveation refused, logged (camera hook thread only); later ones are not.
constexpr int kRefusedShapeLines = 3;
int g_refusedShapeLines = 0;

} // namespace

StereoSettings readStereoSettings() {
    StereoSettings s;
    std::wstring value;
    s.requested = readEnv(L"ETERNALVR_MODE", value) && _wcsicmp(value.c_str(), L"stereo") == 0;
    const StereoExperiment experiment = stereoExperimentFromEnv();
    // Not on an engine Parallel Eye Rendering changed and then failed to finish (view_slots.hpp): that one
    // renders view 0 alone, shown as mono.
    s.sequential = s.requested && experiment == StereoExperiment::None && !parallelEyesChangedEngine();
    s.enabled = s.sequential || experiment != StereoExperiment::None;
    s.views = experiment == StereoExperiment::TwoViews ? 2 : 1;
    s.eyePoses = stereoFlag(L"ETERNALVR_STEREO_EYE_POSES", true);
    s.inhibitModelFov = stereoFlag(L"ETERNALVR_STEREO_INHIBIT_MODEL_FOV", true);
    s.copyJitter = stereoFlag(L"ETERNALVR_STEREO_JITTER_COPY", true);
    if (readEnv(L"ETERNALVR_TEST_WEAPON_FOV", value) && !value.empty()) {
        const float fov = std::wcstof(value.c_str(), nullptr);
        if (std::isfinite(fov) && fov > 1.0f && fov < 170.0f) {
            s.testWeaponFov = fov;
        }
    }
    s.seqView.sameView = stereoFlag(L"ETERNALVR_STEREO_SAME_VIEW", false);
    s.seqView.fullResolution = stereoFlag(L"ETERNALVR_STEREO_FULL_RES", true);
    s.seqView.exposureOnce = stereoFlag(L"ETERNALVR_STEREO_EXPOSURE_ONCE", true);
    s.seqView.discontinuous = stereoFlag(L"ETERNALVR_STEREO_DISCONTINUOUS", false);
    s.seqView.inhibitModelFov = s.inhibitModelFov;
    s.prevMatrices = stereoFlag(L"ETERNALVR_STEREO_PREV_MATRICES", true);
    std::string alternate;
    if (readEnv(L"ETERNALVR_ALTERNATE_EYES", value)) {
        for (const wchar_t c : value) {
            alternate.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    }
    const stereo_seq::AlternateMode alternateMode = stereo_seq::alternateMode(alternate);
    s.alternateEyes = alternateMode != stereo_seq::AlternateMode::Off;
    s.adaptiveEyes = alternateMode == stereo_seq::AlternateMode::Auto;
    s.fixCentered = stereoFlag(L"ETERNALVR_STEREO_FIX_CENTERED", true);
    if (readEnv(L"ETERNALVR_CAPTURE_EYES", value) && !value.empty()) {
        s.capture = stereo_seq::parseCaptureSetting(value);
    }
    return s;
}

// ---- Camera hook (game-frame thread) -----------------------------------------------------------------

void XrPresenter::Impl::prepareEyes(ViewRecord& record, const xr_math::IdViewAxis& body, Quat headOpenXr) {
    std::array<XrView, 2> views{};
    for (XrView& v : views) {
        v.type = XR_TYPE_VIEW;
    }
    XrSpaceLocation headLocation{XR_TYPE_SPACE_LOCATION};
    {
        // The head and both eyes located in LOCAL at the frame's pose time; each eye is then taken relative
        // to the head, which does not rely on the runtime's VIEW space.
        std::shared_lock lock(spaceMutex);
        if (!trackingReady.load(std::memory_order_acquire)) {
            return;
        }
        XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};
        info.viewConfigurationType = viewConfig;
        info.displayTime = record.poseTime;
        info.space = localSpace;
        XrViewState state{XR_TYPE_VIEW_STATE};
        std::uint32_t count = 0;
        const XrResult r = xr.xrLocateViews(session, &info, &state, static_cast<std::uint32_t>(views.size()),
                                            &count, views.data());
        const XrResult h = xr.xrLocateSpace(viewSpace, localSpace, record.poseTime, &headLocation);
        constexpr XrViewStateFlags neededViews =
            XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
        constexpr XrSpaceLocationFlags neededHead =
            XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
        if (XR_FAILED(r) || XR_FAILED(h) || count != views.size() ||
            (state.viewStateFlags & neededViews) != neededViews ||
            (headLocation.locationFlags & neededHead) != neededHead || !finitePose(headLocation.pose)) {
            ++eyesMissing;
            return;
        }
    }
    const Pose rawHead{
        normalize(Quat{headLocation.pose.orientation.x, headLocation.pose.orientation.y,
                       headLocation.pose.orientation.z, headLocation.pose.orientation.w}),
        Vec3{headLocation.pose.position.x, headLocation.pose.position.y, headLocation.pose.position.z}};
    // The head the frame was rendered for (with the test sway, if any).
    const Pose head{Quat{record.pose.orientation.x, record.pose.orientation.y, record.pose.orientation.z,
                         record.pose.orientation.w},
                    Vec3{record.pose.position.x, record.pose.position.y, record.pose.position.z}};
    std::array<xr_math::EyeInHead, 2> eyes{};
    for (std::size_t i = 0; i < 2; ++i) {
        const XrView& v = views[i];
        if (!finitePose(v.pose)) {
            ++eyesMissing;
            return;
        }
        const Pose eyeInSpace{normalize(Quat{v.pose.orientation.x, v.pose.orientation.y, v.pose.orientation.z,
                                             v.pose.orientation.w}),
                              Vec3{v.pose.position.x, v.pose.position.y, v.pose.position.z}};
        eyes[i].pose = xr_math::eyeInHeadFromSpace(rawHead, eyeInSpace);
        eyes[i].fov = fromXr(v.fov);
        if (!xr_math::plausibleEyeInHead(eyes[i].pose)) {
            ++eyesMissing;
            if (eyesMissing <= 3) {
                EVR_LOG("stereo: eye %zu is %.3f m from the head centre; frame rendered mono", i,
                        length(eyes[i].pose.position));
            }
            return;
        }
    }
    // Foveation's eye shapes (vrs_nv.hpp): only from plausible eyes with FOVs a headset has
    // (ETERNALVR_FOV_CHECK).
    const tracking::FovProblem fovProblem =
        settings.checkFov ? tracking::fovProblem({eyes[0].fov, eyes[1].fov}) : tracking::FovProblem::None;
    if (fovProblem == tracking::FovProblem::None) {
        for (std::size_t i = 0; i < 2; ++i) {
            vrs_nv::noteEye(static_cast<int>(i), eyes[i].fov, eyes[i].pose.orientation);
        }
    } else if (vrs_nv::wanted() && g_refusedShapeLines < kRefusedShapeLines) {
        ++g_refusedShapeLines;
        const xr_math::Fov& l = eyes[0].fov;
        const xr_math::Fov& r = eyes[1].fov;
        EVR_LOG("stereo: the eye FOVs (eye L %.2f/%.2f/%.2f/%.2f, eye R %.2f/%.2f/%.2f/%.2f deg) are not a "
                "headset's: %s; foveation takes no eye shape from them%s",
                l.angleLeft * kDegrees, l.angleRight * kDegrees, l.angleUp * kDegrees, l.angleDown * kDegrees,
                r.angleLeft * kDegrees, r.angleRight * kDegrees, r.angleUp * kDegrees, r.angleDown * kDegrees,
                tracking::fovProblemText(fovProblem),
                g_refusedShapeLines == kRefusedShapeLines ? " (later frames are not logged)" : "");
    }
    // ETERNALVR_IPD: the game renders with the player's eye separation; the compositor keeps the runtime's.
    const std::array<Vec3, 2> separated = roomscale::withSeparation(
        {eyes[0].pose.position, eyes[1].pose.position}, roomScaleSettings().ipdMetres);
    for (std::size_t i = 0; i < 2; ++i) {
        xr_math::EyeInHead eye = eyes[i];
        xr_math::EyeInHead gameEye = eye;
        gameEye.pose.position = separated[i];
        if (!settings.stereo.eyePoses) {
            // Experiment E4: both views from the head centre with the head-centred FOV, so the two halves
            // of the image must match.
            eye.pose = Pose::identity();
            eye.fov = fromXr(record.fov);
            gameEye = eye;
        }
        const xr_math::IdEyeView idEye =
            xr_math::eyeViewInWorld(body, headOpenXr, gameEye.pose, settings.unitsPerMetre);
        const Pose inSpace = xr_math::eyePoseInSpace(head, eye.pose);
        EyeRecord& out = record.eyes[i];
        out.pose.orientation = {inSpace.orientation.x, inSpace.orientation.y, inSpace.orientation.z,
                                inSpace.orientation.w};
        out.pose.position = {inSpace.position.x, inSpace.position.y, inSpace.position.z};
        out.fov = toXr(eye.fov);
        out.offset = {idEye.offset.x, idEye.offset.y, idEye.offset.z};
        out.axis = {idEye.axis.forward.x, idEye.axis.forward.y, idEye.axis.forward.z,
                    idEye.axis.left.x,    idEye.axis.left.y,    idEye.axis.left.z,
                    idEye.axis.up.x,      idEye.axis.up.y,      idEye.axis.up.z};
    }
    record.stereo = true;
    if (!loggedFirstEyes) {
        loggedFirstEyes = true;
        for (std::size_t i = 0; i < 2; ++i) {
            const EyeRecord& e = record.eyes[i];
            EVR_LOG("stereo: eye %zu in head (%.4f %.4f %.4f) fov %.2f/%.2f/%.2f/%.2f deg; world offset "
                    "(%.4f %.4f "
                    "%.4f), fwd (%.3f %.3f %.3f)",
                    i, eyes[i].pose.position.x, eyes[i].pose.position.y, eyes[i].pose.position.z,
                    e.fov.angleLeft * kDegrees, e.fov.angleRight * kDegrees, e.fov.angleUp * kDegrees,
                    e.fov.angleDown * kDegrees, e.offset[0], e.offset[1], e.offset[2], e.axis[0], e.axis[1],
                    e.axis[2]);
        }
    }
}

// ---- Per-eye hook and post-latch check (render job threads) ------------------------------------------

bool XrPresenter::Impl::recordForView(const std::byte* renderView, ViewRecord& out) {
    const auto* axis = reinterpret_cast<const float*>(renderView + render_view::kViewAxis);
    std::lock_guard lock(historyMutex);
    for (std::size_t i = 0; i < kHistorySize && i < latestSeq; ++i) {
        const ViewRecord& r = history[(latestSeq - i) % kHistorySize];
        if (std::memcmp(r.axis.data(), axis, sizeof(r.axis)) == 0) {
            out = r;
            return true;
        }
    }
    return false;
}

std::optional<xr_math::EngineMatrix> XrPresenter::Impl::writeEyePose(std::byte* renderView,
                                                                     const EyeRecord& eye) {
    // The depth rows come from the matrix the engine latched for this view earlier in the frame.
    xr_math::EngineMatrix depth{};
    std::memcpy(depth.data(), renderView + render_view_object::kProjection, sizeof(depth));
    const auto projection = xr_math::engineProjection(fromXr(eye.fov), depth);
    if (!projection) {
        ++stereoStats.noProjection;
        return std::nullopt;
    }
    render_view::moveViewOrigin(renderView, eye.offset[0], eye.offset[1], eye.offset[2]);
    std::memcpy(renderView + render_view::kViewAxis, eye.axis.data(), sizeof(eye.axis));
    std::memcpy(renderView + stereo_view_fields::kExplicitProjection, projection->data(),
                sizeof(*projection));
    renderView[stereo_view_fields::kUseExplicitProjection] = std::byte{1};
    return projection;
}

void XrPresenter::Impl::noteWritten(int slot, const xr_math::EngineMatrix& projection) {
    std::lock_guard lock(stereoStats.writtenMutex);
    stereoStats.written[static_cast<std::size_t>(slot)] = projection;
    stereoStats.writtenValid[static_cast<std::size_t>(slot)] = true;
}

void XrPresenter::Impl::onEyeView(std::byte* renderView, int viewIndex, const std::byte* firstViewG) {
    if (viewIndex < 0 || viewIndex > 1 || !mp_guard::allowsGameTouch()) {
        return;
    }
    if (seqActive.load(std::memory_order_acquire)) {
        if (viewIndex == 0) {
            onSeqEyeView(renderView);
        }
        return;
    }
    ViewRecord record;
    if (!recordForView(renderView, record) || !record.stereo) {
        ++stereoStats.unmatched;
        return;
    }
    const EyeRecord& eye = record.eyes[static_cast<std::size_t>(viewIndex)];
    // Parallel Eye Rendering, as Route S: under the explicit projection the latch builds the centred matrix
    // with the world's depth row, which loses the room's geometry and the weapon; the row read here goes back
    // after the latch (onSeqEyeLatched). The stereo experiments keep the latch's.
    const auto v = static_cast<std::size_t>(viewIndex);
    const bool repair = viewSlotsActive();
    if (repair) {
        stereo_seq::Matrix4 centered{};
        std::memcpy(centered.data(), renderView + render_view_object::kCenteredViewProjection,
                    sizeof(centered));
        centeredDepth[v] = settings.stereo.fixCentered ? stereo_seq::centeredDepthOf(centered) : std::nullopt;
    }
    const auto projection = writeEyePose(renderView, eye);
    if (repair) {
        eyePoseWritten[v] = projection.has_value();
        eyeInCutscene[v] = record.cutscene;
        eyeArmsHidden[v] = record.cutsceneArms;
        eyeSeq[v] = record.seq;
    }
    // Test only: ETERNALVR_TEST_VIEW_LIFT=<view>,<metres> raises one view's camera, to tell which view an eye
    // shows (two-view renderers).
    static const std::pair<int, float> lift = [] {
        std::wstring v;
        if (!readEnv(L"ETERNALVR_TEST_VIEW_LIFT", v) || v.size() < 3) {
            return std::pair<int, float>{-1, 0.0f};
        }
        return std::pair<int, float>{v[0] - L'0', std::wcstof(v.c_str() + 2, nullptr)};
    }();
    if (viewIndex == lift.first) {
        reinterpret_cast<float*>(renderView + render_view::kViewOrigin)[2] += lift.second;
    }
    if (!projection) {
        return;
    }
    // A cutscene with its arms hidden keeps the game's own model FOV scale (controllers::cutsceneArmsHidden).
    renderView[stereo_view_fields::kInhibitModelFovScale] =
        std::byte{settings.stereo.inhibitModelFov && !record.cutsceneArms ? 1u : 0u};
    if (settings.stereo.copyJitter && xr_math::copiesFirstViewJitter(viewIndex) && firstViewG != renderView) {
        const std::byte first = firstViewG[stereo_view_fields::kSubSampleIndex];
        if (renderView[stereo_view_fields::kSubSampleIndex] != first) {
            ++stereoStats.jitterDiffered;
        }
        renderView[stereo_view_fields::kSubSampleIndex] = first;
        renderView[stereo_view_fields::kUpsamplerSubSampleIndex] =
            firstViewG[stereo_view_fields::kUpsamplerSubSampleIndex];
        ++stereoStats.jitterCopies;
    }
    noteWritten(viewIndex, *projection);
    if (viewIndex == 0) {
        latchedSeq.store(record.seq, std::memory_order_release);
    }
    ++stereoStats.eyeViews[viewIndex];
    if (stereoStats.loggedEyes.fetch_add(1) < 6) {
        const auto& m = *projection;
        const auto* origin = reinterpret_cast<const float*>(renderView + render_view::kViewOrigin);
        EVR_LOG("stereo: view %d <- eye %d of game frame %llu: origin (%.3f %.3f %.3f) fwd (%.3f %.3f %.3f), "
                "projection "
                "[0] %.4f [2] %.4f [5] %.4f [6] %.4f [10] %.6f [11] %.6f, jitter %u/%u (first view %u/%u)",
                viewIndex, viewIndex, static_cast<unsigned long long>(record.seq), origin[0], origin[1],
                origin[2], eye.axis[0], eye.axis[1], eye.axis[2], m[0], m[2], m[5], m[6], m[10], m[11],
                std::to_integer<unsigned>(renderView[stereo_view_fields::kSubSampleIndex]),
                std::to_integer<unsigned>(renderView[stereo_view_fields::kUpsamplerSubSampleIndex]),
                std::to_integer<unsigned>(firstViewG[stereo_view_fields::kSubSampleIndex]),
                std::to_integer<unsigned>(firstViewG[stereo_view_fields::kUpsamplerSubSampleIndex]));
    }
}

void XrPresenter::Impl::onEyeLatched(std::byte* renderView, int screenView) {
    if (screenView < 0 || screenView > 1 || (seqActive.load(std::memory_order_acquire) && screenView != 0)) {
        return;
    }
    // Route S renders one screen view per frame; the eye is the render's (the chain's, or with alternate eyes
    // the render frame's).
    const int viewIndex =
        seqActive.load(std::memory_order_acquire) ? stereo_seq::eyeIndex(seqRenderEye()) : screenView;
    if (seqActive.load(std::memory_order_acquire) || viewSlotsActive()) {
        onSeqEyeLatched(renderView, viewIndex); // the centred depth row and the weapon matrices
    }
    const int latchedIndex =
        *reinterpret_cast<const std::int32_t*>(renderView + render_view_object::kViewIndex);
    xr_math::EngineMatrix latched{};
    std::memcpy(latched.data(), renderView + render_view_object::kProjection, sizeof(latched));
    xr_math::EngineMatrix written{};
    bool valid = false;
    {
        std::lock_guard lock(stereoStats.writtenMutex);
        written = stereoStats.written[static_cast<std::size_t>(viewIndex)];
        valid = stereoStats.writtenValid[static_cast<std::size_t>(viewIndex)];
    }
    bool same = valid;
    for (std::size_t i = 0; valid && i < 16; ++i) {
        same = same && std::fabs(latched[i] - written[i]) <= 1e-6f * (1.0f + std::fabs(written[i]));
    }
    if (valid && !same) {
        ++stereoStats.latchMismatch;
    }
    ++stereoStats.latched[viewIndex];
    if (stereoStats.loggedLatches.fetch_add(1) < 6) {
        EVR_LOG("stereo: latched view %d (render view %p, viewIndex field %d): projection [0] %.4f [2] %.4f "
                "[5] %.4f "
                "[6] %.4f [10] %.6f [11] %.6f [14] %.1f; %s the matrix written",
                viewIndex, static_cast<const void*>(renderView), latchedIndex, latched[0], latched[2],
                latched[5], latched[6], latched[10], latched[11], latched[14],
                !valid ? "nothing written yet for"
                : same ? "equals"
                       : "DIFFERS from");
    }
    const unsigned long long ticks = GetTickCount64();
    unsigned long long last = stereoStats.lastStatsTicks.load();
    if (ticks - last >= 10000 && stereoStats.lastStatsTicks.compare_exchange_strong(last, ticks)) {
        const EngineFrameCounters counters = readEngineFrameCounters();
        const std::uint64_t latches = stereoStats.latched[0].load() + stereoStats.latched[1].load();
        const std::uint32_t renderDelta =
            counters.renderFrames - stereoStats.lastRenderFrames.exchange(counters.renderFrames);
        const std::uint32_t backendDelta =
            counters.backendFrames - stereoStats.lastBackendFrames.exchange(counters.backendFrames);
        const std::uint64_t latchDelta = latches - stereoStats.lastLatchTotal.exchange(latches);
        EVR_LOG("stereo: eye views %llu / %llu, latched %llu / %llu (%llu differ from what was written), "
                "%llu unmatched, "
                "%llu without projection, jitter copied %llu (%llu differed); last 10 s: %u render frame(s), "
                "%u backend "
                "frame(s), %llu eye latch(es)",
                static_cast<unsigned long long>(stereoStats.eyeViews[0].load()),
                static_cast<unsigned long long>(stereoStats.eyeViews[1].load()),
                static_cast<unsigned long long>(stereoStats.latched[0].load()),
                static_cast<unsigned long long>(stereoStats.latched[1].load()),
                static_cast<unsigned long long>(stereoStats.latchMismatch.load()),
                static_cast<unsigned long long>(stereoStats.unmatched.load()),
                static_cast<unsigned long long>(stereoStats.noProjection.load()),
                static_cast<unsigned long long>(stereoStats.jitterCopies.load()),
                static_cast<unsigned long long>(stereoStats.jitterDiffered.load()), renderDelta, backendDelta,
                static_cast<unsigned long long>(latchDelta));
    }
}

// ---- Worker ------------------------------------------------------------------------------------------

void XrPresenter::Impl::startStereo() {
    if (!settings.stereo.enabled) {
        return;
    }
    if (settings.stereo.sequential) {
        startSequential(); // Route S (presenter_seq.cpp)
        return;
    }
    const bool wantTwo = settings.stereo.views == 2;
    stereoHooks = installStereoHooks(this, wantTwo);
    requestTwoViews(wantTwo && stereoHooks.twoViews);
    if (wantTwo && stereoHooks.twoViews && eyeCopyRequested()) {
        // Parallel Eye Rendering's eye copy (presenter_eyes.hpp): each eye takes its own view's full image,
        // so the ring holds two.
        std::lock_guard lock(mutex);
        ringEyes = 2;
        EVR_LOG("stereo: Parallel Eye Rendering's eye copy on: each eye from its own view's image");
    }
    EVR_LOG("stereo: %s; eye poses %s, inhibitModelFovScale %d, jitter copy %s%s",
            stereoHooks.twoViews && wantTwo ? "two views side by side" : "one view (left eye)",
            settings.stereo.eyePoses ? "per eye" : "head centre (E4)",
            settings.stereo.inhibitModelFov ? 1 : 0, settings.stereo.copyJitter ? "on" : "off",
            settings.stereo.testWeaponFov > 0.0f ? ", weapon FOV test on" : "");
}

bool XrPresenter::Impl::fillProjectionViews(std::array<XrCompositionLayerProjectionView, 2>& views) {
    const bool twoViews = shownView.stereo && stereoHooks.twoViews && settings.stereo.views == 2;
    const bool leftEyeExperiment =
        shownView.stereo && !settings.stereo.sequential && settings.stereo.views == 1;
    const int width = static_cast<int>(ringExtent.width);
    const int height = static_cast<int>(ringExtent.height);
    for (std::size_t i = 0; i < views.size(); ++i) {
        XrCompositionLayerProjectionView& v = views[i];
        v.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        v.next = nullptr;
        v.subImage.swapchain = xrSwapchain;
        v.subImage.imageArrayIndex = 0;
        if (ringEyes == 2) {
            // Route S: each eye's half of the ring image. A pair shows each eye with the pose and FOV it was
            // rendered with; a mono frame (in both halves) and the same-view test show the head's.
            const xr_math::PixelRect rect = xr_math::sideBySideRect(static_cast<int>(i), width, height);
            v.pose = shownView.showEyes ? shownView.eyes[i].pose : shownView.pose;
            v.fov = shownView.showEyes ? shownView.eyes[i].fov : shownView.fov;
            v.subImage.imageRect = {{rect.x, rect.y}, {rect.width, rect.height}};
        } else if (twoViews) {
            // Each eye's half of the side-by-side image, with the pose and FOV it was rendered with.
            const xr_math::PixelRect rect = xr_math::sideBySideRect(static_cast<int>(i), width, height);
            v.pose = shownView.eyes[i].pose;
            v.fov = shownView.eyes[i].fov;
            v.subImage.imageRect = {{rect.x, rect.y}, {rect.width, rect.height}};
        } else if (leftEyeExperiment) {
            // One view rendered as the left eye (E2): both eyes see it.
            v.pose = shownView.eyes[0].pose;
            v.fov = shownView.eyes[0].fov;
            v.subImage.imageRect = {{0, 0}, {width, height}};
        } else {
            // Mono: the head pose and the FOV the frame was rendered with.
            v.pose = shownView.pose;
            v.fov = shownView.fov;
            v.subImage.imageRect = {{0, 0}, {width, height}};
        }
    }
    return true;
}

} // namespace evr::vkcore
