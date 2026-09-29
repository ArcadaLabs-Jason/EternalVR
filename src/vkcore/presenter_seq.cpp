// Route S, synchronized sequential stereo (docs/VR_STEREO.md): start-up, the per-eye hook's work for the
// chain's eye, and the present hook's pairing of the two eye presents of a tick into one ring slot (eye L
// in the left half, eye R in the right) with each eye's pose and FOV. The engine side is seq_hooks.cpp.

#include "vkcore/presenter_impl.hpp"

#include "stereo_seq/alternate_eyes.hpp"
#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/dlss_menu_hooks.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/runtime_cvars.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/taa_hooks.hpp"

#include <cstddef>
#include <cstring>
#include <string>

namespace evr::vkcore {

using stereo_seq::Eye;
using stereo_seq::PairAction;

// idRenderView::centeredViewProjectionMatrix (built by the latch, RVA 0x1CE1400).
constexpr std::size_t kCenteredViewProjection = 0x296B0;
// customViewProjectionMatrix, customViewProjectionMatrix2 and their centred variants (the latch builds
// them from weaponFOVX/Y and customFOV2X/Y).
constexpr std::size_t kWeaponViewProjections[] = {0x295B0, 0x29630, 0x29770, 0x297F0};

namespace {

// The v1 cvar set on the game's command line (the launch helper passes it; docs/VR_STEREO.md). Only
// logged: S3 turns them back on one at a time.
void logCommandLineCvars() {
    const wchar_t* wide = GetCommandLineW();
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string line(bytes > 0 ? static_cast<std::size_t>(bytes) : 0, '\0');
    if (bytes > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, line.data(), bytes, nullptr, nullptr);
        line.resize(static_cast<std::size_t>(bytes - 1));
    }
    std::string summary;
    int differing = 0;
    for (const auto& c : stereo_seq::cvarsOnCommandLine(line, stereo_seq::sequentialCvars())) {
        summary += " " + c.name + " " + (c.actual ? *c.actual : std::string("(not set)"));
        if (!c.actual || *c.actual != c.expected) {
            ++differing;
        }
    }
    EVR_LOG("seq: command line cvars:%s; %s", summary.c_str(),
            taaRequested()   ? "per-eye temporal history owns the TAA cvars"
            : differing == 0 ? "the v1 set"
                             : "NOT the v1 set (temporal effects may ghost between the eyes)");
    if (taaRequested()) {
        std::string taa;
        for (const auto* set :
             {&stereo_seq::stereoTaaCommandLineCvars(), &stereo_seq::stereoTaaForcedCvars()}) {
            for (const auto& c : stereo_seq::cvarsOnCommandLine(line, *set)) {
                taa += " " + c.name + " " + (c.actual ? *c.actual : std::string("(not set)"));
            }
        }
        EVR_LOG("seq-taa: per-eye TAA requested (ETERNALVR_STEREO_TAA=1); command line:%s", taa.c_str());
    }
}

// Per-eye TAA (taa_hooks.hpp), written by the per-eye hook into each eye's render view. Render job
// threads; the two chains of a tick never overlap, the mutex only orders them.
std::mutex g_taaMutex;
stereo_seq::TaaResetPlanner g_taaReset;
stereo_seq::AlternateTaaReset g_altTaaReset;            // alternate eyes
stereo_seq::AlternateTaaReset g_adaptiveTaaReset{true}; // auto: Route S ticks continue the run too
std::byte g_leftUpsampler{};
std::atomic<std::uint64_t> g_taaResets{0};
TaaCounters g_lastTaa;

void writeTaaEye(std::byte* renderView, Eye eye, std::uint64_t gameFrame) {
    if (eye == Eye::Left) {
        taaOnStereoTick(); // the first stereo tick switches per-eye TAA on, or fails closed
    }
    if (!taaPerEyeActive()) {
        return;
    }
    std::lock_guard lock(g_taaMutex);
    // One jitter phase per tick for both eyes (with alternate eyes, one per render of each eye; with auto, as
    // the render's tick goes). The upsampler's index (the ray-traced reflections' temporal upscale, off with
    // per-eye TAA) follows eye L's.
    const bool alternate = seqAlternateEyes() && !seqPairInTick();
    renderView[stereo_view_fields::kSubSampleIndex] =
        std::byte{alternate ? stereo_seq::alternateSubSample(gameFrame, taaNumSubSamples())
                            : stereo_seq::taaSubSample(gameFrame, taaNumSubSamples())};
    if (eye == Eye::Left) {
        g_leftUpsampler = renderView[stereo_view_fields::kUpsamplerSubSampleIndex];
    } else {
        renderView[stereo_view_fields::kUpsamplerSubSampleIndex] = g_leftUpsampler;
    }
    const bool reset = seqAdaptiveEyes()    ? g_adaptiveTaaReset.onEye(eye, gameFrame)
                       : seqAlternateEyes() ? g_altTaaReset.onEye(eye, gameFrame)
                       : eye == Eye::Left   ? g_taaReset.onLeft(gameFrame)
                                            : g_taaReset.onRight(gameFrame);
    if (reset) {
        renderView[stereo_view_fields::kDisableTssaaNextFewFrames] = std::byte{1};
        ++g_taaResets;
    }
}

} // namespace

void XrPresenter::Impl::startSequential() {
    const stereo_seq::SeqViewSettings& v = settings.stereo.seqView;
    EVR_LOG(
        "seq: Route S requested: %s, full resolution %s, exposure once per tick %s, discontinuous view %s, "
        "inhibitModelFovScale %d, previous matrices per eye %s",
        v.sameView ? "SAME VIEW in both eyes (S1)" : "per-eye views", v.fullResolution ? "on" : "off",
        v.exposureOnce ? "on" : "off", v.discontinuous ? "on" : "off", v.inhibitModelFov ? 1 : 0,
        settings.stereo.prevMatrices ? "on" : "off");
    logCommandLineCvars();
    if (!hooks.gameView) {
        EVR_LOG("seq: no camera hook; stereo off, mono");
        status::stereo(false, "the camera hook is missing");
        return;
    }
    stereoHooks = installStereoHooks(this, false);
    if (!stereoHooks.eyeView) {
        EVR_LOG("seq: no per-eye hook; stereo off, mono");
        status::stereo(false, "the per-eye hook is missing");
        return;
    }
    SeqHookSettings hookSettings;
    hookSettings.prevMatrices = settings.stereo.prevMatrices;
    hookSettings.alternateEyes = settings.stereo.alternateEyes;
    hookSettings.adaptiveEyes = settings.stereo.adaptiveEyes;
    if (!installSeqHooks(hookSettings)) {
        EVR_LOG("seq: Route S hooks not installed (reason above); stereo off, mono");
        status::stereo(false, "the stereo hooks could not be installed");
        return;
    }
    if (taaRequested()) {
        installTaaHooks(); // a missing piece fails closed on the first stereo tick
    }
    installDlssMenuHooks(); // the game's video menu shows the DLSS state the layer holds
    {
        std::lock_guard lock(mutex);
        ringEyes = 2;
        if (settings.stereo.capture) {
            capture.configure(*settings.stereo.capture);
        }
    }
    std::wstring captureText;
    if (!settings.stereo.capture && readEnv(L"ETERNALVR_CAPTURE_EYES", captureText) && !captureText.empty()) {
        EVR_LOG("seq: ETERNALVR_CAPTURE_EYES is not <dir>[,<every N pairs>]; capture off");
    }
    seqActive.store(true, std::memory_order_release);
    if (seqAdaptiveEyes()) {
        EVR_LOG(
            "stereo: adaptive eyes on (ETERNALVR_ALTERNATE_EYES=auto): both eyes in each game tick while "
            "the processor keeps up with the headset, one eye per tick (alternate eyes) while it does not; "
            "the way changes only at a pair's eye L, and each switch is logged");
    } else if (seqAlternateEyes()) {
        EVR_LOG(
            "stereo: alternate eyes on: each game tick renders one eye (ETERNALVR_ALTERNATE_EYES=1), eye L "
            "then eye R the next tick; each eye updates at half the tick rate and is shown next to the other "
            "eye's newest image, each with the pose it was rendered with");
    }
    EVR_LOG("seq: Route S on: %s; the ring holds both eyes side by side",
            seqAdaptiveEyes()    ? "both eyes per tick or one, as the processor keeps up (adaptive eyes)"
            : seqAlternateEyes() ? "one eye per tick (alternate eyes)"
                                 : "each stereo tick renders eye L, then eye R");
    status::stereo(true, nullptr);
}

// ---- Per-eye hook (render job threads) ---------------------------------------------------------------

namespace {

// The render view carries the axis the camera hook wrote for `record`'s game frame.
bool sameAxis(const std::byte* renderView, const ViewRecord& record) {
    return std::memcmp(renderView + render_view::kViewAxis, record.axis.data(), sizeof(record.axis)) == 0;
}

} // namespace

void XrPresenter::Impl::onSeqEyeView(std::byte* renderView) {
    // With alternate eyes both eyes render in the engine's own chain, each for its own game frame; with auto
    // eye R renders nested in its eye L's tick while the processor keeps up.
    const Eye eye = seqRenderEye();
    const bool nested = seqChainEye() == Eye::Right;
    const int index = stereo_seq::eyeIndex(eye);
    const SeqReadiness readiness = nested ? SeqReadiness::Ready : seqStereoReadiness();
    if (readiness == SeqReadiness::Off) {
        return; // the tick stays mono: the game's own view
    }
    if (nested) {
        seqNoteNestedStack();
    }
    ViewRecord record;
    // Eye R draws the game frame eye L was drawn for (and the game's view must still be that frame's).
    const bool found = nested ? viewBySeq(seqRightTick(), record) && sameAxis(renderView, record)
                              : recordForView(renderView, record);
    if (!found || !record.stereo) {
        // Not a head-tracked game view with located eyes (menus, loading screens): the tick stays mono.
        ++stereoStats.unmatched;
        return;
    }
    if (readiness == SeqReadiness::NeedsBase) {
        // The eye tags take a new base on this frame, which stays the game's own view; the next tick pairs.
        seqMarkWanted();
        return;
    }
    const stereo_seq::EyeViewPlan plan = stereo_seq::planEyeView(eye, settings.stereo.seqView);
    std::optional<xr_math::EngineMatrix> projection;
    if (plan.writePose) {
        // The centred matrix the world-views pass latched from the game's view (no explicit projection).
        stereo_seq::Matrix4 centered{};
        std::memcpy(centered.data(), renderView + kCenteredViewProjection, sizeof(centered));
        centeredDepth[static_cast<std::size_t>(index)] =
            settings.stereo.fixCentered ? stereo_seq::centeredDepthOf(centered) : std::nullopt;
        projection = writeEyePose(renderView, record.eyes[static_cast<std::size_t>(index)]);
        eyePoseWritten[static_cast<std::size_t>(index)] = projection.has_value();
        if (!projection) {
            return; // no usable projection: nothing written, the tick stays mono
        }
        noteWritten(index, *projection);
    }
    const auto setFlag = [renderView](bool on, std::size_t offset) {
        if (on) {
            renderView[offset] = std::byte{1};
        }
    };
    setFlag(plan.forceFullResolution, stereo_view_fields::kForceFullResolution);
    setFlag(plan.skipAutoExposureUpdate, stereo_view_fields::kSkipAutoExposureUpdate);
    setFlag(plan.discontinuousViewPosition, stereo_view_fields::kDiscontinuousViewPosition);
    setFlag(plan.inhibitModelFovScale, stereo_view_fields::kInhibitModelFovScale);
    writeTaaEye(renderView, eye, record.seq);
    seqMarkEyeView(eye, record.seq);
    ++stereoStats.eyeViews[index];
    if (stereoStats.loggedEyes.fetch_add(1) < 6) {
        const auto* origin = reinterpret_cast<const float*>(renderView + render_view::kViewOrigin);
        const auto* axis = reinterpret_cast<const float*>(renderView + render_view::kViewAxis);
        EVR_LOG("seq: eye %s of game frame %llu: origin (%.3f %.3f %.3f) fwd (%.3f %.3f %.3f)%s%s",
                stereo_seq::eyeName(eye), static_cast<unsigned long long>(record.seq), origin[0], origin[1],
                origin[2], axis[0], axis[1], axis[2],
                plan.writePose ? ", explicit projection" : ", game view",
                plan.skipAutoExposureUpdate ? ", exposure held" : "");
    }
}

void XrPresenter::Impl::onSeqEyeLatched(std::byte* renderView, int eyeIndex) {
    const auto eye = static_cast<std::size_t>(eyeIndex);
    auto& depth = centeredDepth[eye];
    const bool written = eyePoseWritten[eye];
    eyePoseWritten[eye] = false;
    if (!written || !mp_guard::allowsGameTouch()) {
        depth.reset();
        return;
    }
    const auto load = [renderView](std::size_t offset) {
        stereo_seq::Matrix4 m{};
        std::memcpy(m.data(), renderView + offset, sizeof(m));
        return m;
    };
    const auto store = [renderView](std::size_t offset, const stereo_seq::Matrix4& m) {
        std::memcpy(renderView + offset, m.data(), sizeof(m));
    };
    if (depth) {
        // The latch built the centred matrix from the explicit projection, depth rows included.
        stereo_seq::Matrix4 centered = load(kCenteredViewProjection);
        stereo_seq::setCenteredDepth(centered, *depth);
        store(kCenteredViewProjection, centered);
        depth.reset();
        ++centeredRepairs;
    }
    if (settings.stereo.inhibitModelFov) {
        // Hands and guns in the eye's frustum (the latch builds them from the symmetric weapon FOV).
        const stereo_seq::Matrix4 eyeProjection = load(render_view_object::kProjection);
        for (const std::size_t offset : kWeaponViewProjections) {
            stereo_seq::Matrix4 m = load(offset);
            if (stereo_seq::retargetViewProjection(m, eyeProjection)) {
                store(offset, m);
                ++weaponRetargets;
            }
        }
    }
}

// ---- Present hook (render thread, under `mutex`) ------------------------------------------------------

bool XrPresenter::Impl::viewBySeq(std::uint64_t seq, ViewRecord& out) {
    std::lock_guard lock(historyMutex);
    const ViewRecord& r = history[seq % kHistorySize];
    if (seq == 0 || r.seq != seq) {
        return false;
    }
    out = r;
    return true;
}

void XrPresenter::Impl::seqPresentNotCopied(const stereo_seq::PresentMatch& match) {
    if (seqAlternateEyes()) {
        altPresentNotCopied(*this, match);
        return;
    }
    const stereo_seq::PairStep step = pairing.onPresent(match);
    if (step.abandoned) {
        releasePendingSlot();
    }
    if (step.action == PairAction::StartPair) {
        pairing.leftNotStored();
    } else if (step.action == PairAction::CompletePair) {
        pairing.rightNotStored();
        releasePendingSlot();
    }
}

void XrPresenter::Impl::releasePendingSlot() {
    if (pendingSlot < kRingSize) {
        ring[pendingSlot].state.store(kSlotFree); // its value keeps it from reuse until the copy is done
        pendingSlot = kRingSize;
    }
    capture.cancel();
}

VkSemaphore XrPresenter::Impl::seqCopyForPresent(VkQueue queue,
                                                 std::uint32_t family,
                                                 SwapchainState& sc,
                                                 std::uint32_t imageIndex,
                                                 const VkPresentInfoKHR* info,
                                                 FamilyCommands& fc,
                                                 std::uint64_t completed,
                                                 const stereo_seq::PresentMatch& match) {
    capture.poll(dev, completed);
    // TAA stays off although the game's settings turn it back on (runtime_cvars.hpp); cheap once located.
    runtime_cvars::apply(true);
    const bool alternate = seqAlternateEyes();
    if (alternate) {
        // One eye per tick (presenter_alt.cpp); a mono frame comes back here.
        if (const std::optional<VkSemaphore> done =
                altCopyForPresent(*this, queue, family, sc, imageIndex, info, fc, completed, match)) {
            return *done;
        }
    }
    const stereo_seq::PairStep step =
        alternate ? stereo_seq::PairStep{PairAction::ShowMono, false} : pairing.onPresent(match);
    if (step.abandoned) {
        releasePendingSlot();
    }
    logSeqStats();
    switch (step.action) {
    case PairAction::Drop:
        return VK_NULL_HANDLE; // the present goes out unchanged, nothing reaches the headset
    case PairAction::StartPair: {
        const std::uint32_t slotIndex = acquireFreeSlot(fc, completed);
        if (slotIndex == kRingSize) {
            pairing.leftNotStored();
            ++framesDropped;
            return VK_NULL_HANDLE;
        }
        const VkBuffer buffer =
            pairCaptureBuffer(sc, completed, pairing.stats().pairsStarted, match.tag.tick);
        const bool toWindow = decideWindow(info, stereo_seq::PresentKind::EyeL);
        const std::uint64_t value =
            submitCopy(queue, family, info, sc, imageIndex, fc, slotIndex,
                       CopyTarget{0, 1, false, settings.ui.enabled, windowMirrorStep(0, toWindow), toWindow,
                                  stereo_seq::PresentKind::EyeL},
                       buffer);
        if (value == 0) {
            ring[slotIndex].state.store(kSlotFree);
            pairing.leftNotStored();
            capture.cancel();
            ++framesDropped;
            return VK_NULL_HANDLE;
        }
        captureCopied(buffer, value, false);
        pendingSlot = slotIndex; // stays kSlotWriting until eye R completes it
        return sc.presentSemaphores[imageIndex];
    }
    case PairAction::CompletePair: {
        const std::uint32_t slotIndex = pendingSlot;
        pendingSlot = kRingSize;
        const VkBuffer buffer = slotIndex < kRingSize
                                    ? capture.bufferFor(dev, 1, 0, sc.format, sc.extent, completed)
                                    : VK_NULL_HANDLE;
        const bool toWindow = slotIndex >= kRingSize || decideWindow(info, stereo_seq::PresentKind::EyeR);
        const std::uint64_t value =
            slotIndex < kRingSize ? submitCopy(queue, family, info, sc, imageIndex, fc, slotIndex,
                                               CopyTarget{1, 1, true, false, windowMirrorStep(1, toWindow),
                                                          toWindow, stereo_seq::PresentKind::EyeR},
                                               buffer)
                                  : 0;
        if (value == 0) {
            if (slotIndex < kRingSize) {
                ring[slotIndex].state.store(kSlotFree);
            }
            pairing.rightNotStored();
            capture.cancel();
            ++framesDropped;
            return VK_NULL_HANDLE;
        }
        if (buffer) {
            capture.copySubmitted(value);
        }
        capture.submitted(value, match.tag.tick);
        RingSlot& slot = ring[slotIndex];
        slot.hasView = viewBySeq(match.tag.tick, slot.view);
        if (!slot.hasView) {
            // Without the tick's record the eyes' poses and FOVs are unknown: shown with another frame's,
            // the pair would be distorted. Not shown (the headset repeats the last pair); the copy was
            // submitted, so the present still waits on it.
            ++pairsWithoutRecord;
            slot.state.store(kSlotFree); // its value keeps it from reuse until the copy is done
            ++framesDropped;
            return sc.presentSemaphores[imageIndex];
        }
        slot.view.showEyes = slot.view.stereo && !settings.stereo.seqView.sameView;
        publishSlot(slotIndex, value);
        pairsPublished.fetch_add(1, std::memory_order_relaxed);
        return sc.presentSemaphores[imageIndex];
    }
    case PairAction::ShowMono:
        break;
    }
    // A mono frame: the same image in both halves, shown with the head pose.
    const std::uint32_t slotIndex = acquireFreeSlot(fc, completed);
    if (slotIndex == kRingSize) {
        ++framesDropped;
        return VK_NULL_HANDLE;
    }
    const bool toWindow = decideWindow(info, stereo_seq::PresentKind::Mono);
    const VkBuffer buffer = monoCaptureBuffer(sc, completed);
    const std::uint64_t value = submitCopy(
        queue, family, info, sc, imageIndex, fc, slotIndex,
        CopyTarget{0, 2, false, settings.ui.enabled, stereo_seq::MirrorStep::None, toWindow}, buffer);
    captureCopied(buffer, value, true);
    if (value == 0) {
        ring[slotIndex].state.store(kSlotFree);
        ++framesDropped;
        return VK_NULL_HANDLE;
    }
    RingSlot& slot = ring[slotIndex];
    std::uint64_t gap = 0;
    slot.hasView = settings.mode == Mode::HeadTracked && latestView(slot.view, gap);
    slot.view.showEyes = false;
    publishSlot(slotIndex, value);
    return sc.presentSemaphores[imageIndex];
}

void XrPresenter::Impl::logSeqStats() {
    const ULONGLONG now = GetTickCount64();
    if (lastSeqStatsTicks == 0) {
        lastSeqStatsTicks = now;
        lastSeqCounters = seqCounters();
        seqTakeDrainMaxMicros(); // the first window starts here
        lastSeqGameTicks = gameTicks.load();
        lastPairStats = pairing.stats();
        return;
    }
    if (now - lastSeqStatsTicks < 10000) {
        return;
    }
    const SeqCounters c = seqCounters();
    const SeqCounters& l = lastSeqCounters;
    const stereo_seq::EyePairing::Stats& p = pairing.stats();
    const stereo_seq::EyePairing::Stats& lp = lastPairStats;
    const std::uint64_t games = gameTicks.load() - lastSeqGameTicks;
    const std::uint32_t renders = c.renderFrames - l.renderFrames;
    const auto d = [](std::uint64_t a, std::uint64_t b) {
        return static_cast<unsigned long long>(a - b);
    };
    EVR_LOG("seq: last %.1f s: %llu game frame(s), %u render frame(s) (%.2f per game frame), %u backend "
            "frame(s), "
            "%llu stereo tick(s), %llu eye R frame end(s)",
            static_cast<double>(now - lastSeqStatsTicks) / 1000.0, static_cast<unsigned long long>(games),
            renders, games ? static_cast<double>(renders) / static_cast<double>(games) : 0.0,
            c.backendFrames - l.backendFrames, d(c.stereoTicks, l.stereoTicks),
            d(c.rightFrameEnds, l.rightFrameEnds));
    EVR_LOG(
        "seq: pairs %llu complete of %llu started, %llu mono; dropped halves %llu left / %llu right (%llu "
        "without a view, %llu not stored); %llu pair(s) without a view record (not shown)",
        d(p.pairsCompleted, lp.pairsCompleted), d(p.pairsStarted, lp.pairsStarted), d(p.mono, lp.mono),
        d(p.leftDropped, lp.leftDropped), d(p.rightDropped, lp.rightDropped),
        d(p.withoutView, lp.withoutView), d(p.notStored, lp.notStored),
        static_cast<unsigned long long>(pairsWithoutRecord));
    logAltStats(*this, c, l);
    // A drain holds the game's frontend: its total and longest wall time are the hitch the player felt.
    EVR_LOG(
        "seq: eye tags %s: %llu matched, %llu untagged; out of sync %llu (missing present) / %llu (untagged "
        "frame) / %llu (overflow) / %llu (rebase); %llu drain(s), %llu failed, %llu on a quiet period only, "
        "%.1f ms in total, longest %.1f ms; frames without a backend frame %llu; previous matrices %llu "
        "rewritten / %llu kept; r_swapInterval %d",
        c.tagsSynced ? "in sync" : "OUT OF SYNC", d(c.tags.matched, l.tags.matched),
        d(c.tags.untagged, l.tags.untagged), d(c.tags.missingPresent, l.tags.missingPresent),
        d(c.tags.untaggedFrame, l.tags.untaggedFrame), d(c.tags.overflow, l.tags.overflow),
        d(c.tags.requested, l.tags.requested), d(c.drains, l.drains), d(c.drainFailures, l.drainFailures),
        d(c.unverifiedBases, l.unverifiedBases), static_cast<double>(c.drainMicros - l.drainMicros) / 1000.0,
        static_cast<double>(seqTakeDrainMaxMicros()) / 1000.0, d(c.noBackendFrames, l.noBackendFrames),
        d(c.prevRewrites, l.prevRewrites), d(c.prevKept, l.prevKept), c.swapInterval);
    EVR_LOG("seq: eye R skipped %llu (render-frame guard busy) / %llu (multiplayer guard) / %llu (stack); "
            "eye R chain at most %zu KiB deep, least stack left at eye R %zu KiB",
            d(c.guardBusy, l.guardBusy), d(c.guardTrips, l.guardTrips), d(c.stackSkips, l.stackSkips),
            c.deepestNested / 1024, c.leastHeadroom / 1024);
    EVR_LOG(
        "seq: eye matrices: %llu centred matrix repair(s), %llu hands-and-guns matrix retarget(s) in total",
        static_cast<unsigned long long>(centeredRepairs.load()),
        static_cast<unsigned long long>(weaponRetargets.load()));
    logWindowStats();
    const DesktopMirror::Counters& m = mirror.counters();
    EVR_LOG(
        "mirror: the window shows %s during stereo pairs (ETERNALVR_MIRROR); %llu kept, %llu replaced, %llu "
        "cleared, %llu shown as rendered, %llu cut to the band (ETERNALVR_MIRROR_CROP), %llu menu panel "
        "image(s) kept, in total",
        settings.mirror == stereo_seq::Mirror::Off ? "black" : stereo_seq::toString(settings.mirror),
        static_cast<unsigned long long>(m.stores), static_cast<unsigned long long>(m.loads),
        static_cast<unsigned long long>(m.clears), static_cast<unsigned long long>(m.skipped),
        static_cast<unsigned long long>(m.crops), static_cast<unsigned long long>(m.panels));
    const TaaCounters t = taaCounters();
    const TaaCounters& lt = g_lastTaa;
    if (taaRequested()) {
        EVR_LOG(
            "seq-taa: per-eye %s; accumulation picks %llu eye L / %llu eye R / %llu engine; resets %llu; "
            "DLSS evaluations %llu eye L / %llu eye R (%llu without a twin), twins %llu made / %llu failed, "
            "%llu twin reset(s); r_antialiasing %d r_TAASafeMode %d r_jitter %d r_TAAAntiGhosting %d",
            t.perEye ? "on" : "off", d(t.picks[0], lt.picks[0]), d(t.picks[1], lt.picks[1]),
            d(t.enginePicks, lt.enginePicks), static_cast<unsigned long long>(g_taaResets.load()),
            d(t.evaluates[0], lt.evaluates[0]), d(t.evaluates[1], lt.evaluates[1]),
            d(t.evaluatesNoTwin, lt.evaluatesNoTwin), d(t.twinCreates, 0), d(t.twinFailures, 0),
            d(t.twinResets, lt.twinResets), t.antialiasing, t.safeMode, t.jitter, t.antiGhosting);
    }
    if (taaRequested()) {
        const auto v = [&](int e, int s) {
            return d(t.tagVsView[e][s], lt.tagVsView[e][s]);
        };
        EVR_LOG("seq-taa: accumulation %dx%d (eye R %dx%d); opaque picks eye R %llu; distortion binds %llu",
                t.sizeA[0], t.sizeA[1], t.sizeB[0], t.sizeB[1], d(t.opaquePicks, lt.opaquePicks),
                d(t.distortionBinds, lt.distortionBinds));
        EVR_LOG(
            "seq-taa: tag eye vs latched projection side (left / centred / right): eye L %llu / %llu / %llu, "
            "eye R %llu / %llu / %llu; exposure renders whose tag in flight names another eye %llu",
            v(0, 0), v(0, 1), v(0, 2), v(1, 0), v(1, 1), v(1, 2),
            d(t.exposureInFlightDiffers, lt.exposureInFlightDiffers));
    }
    g_lastTaa = t;
    lastSeqStatsTicks = now;
    lastSeqCounters = c;
    lastSeqGameTicks = gameTicks.load();
    lastPairStats = p;
}

} // namespace evr::vkcore
