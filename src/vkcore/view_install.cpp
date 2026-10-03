// Parallel Eye Rendering's install from the game's vkCreateInstance (view_slots.hpp): every check first, then
// the inert hooks, then the changes to the engine with the hooks that follow them.

#include "vkcore/bin_tile_hooks.hpp"
#include "vkcore/code_ranges.hpp"
#include "vkcore/game_code.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/runtime_cvars.hpp"
#include "vkcore/ui_vulkan.hpp"
#include "vkcore/view_async.hpp"
#include "vkcore/view_block.hpp"
#include "vkcore/view_clones.hpp"
#include "vkcore/view_contexts.hpp"
#include "vkcore/view_redirects.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/view_slots_storage.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "parallel eyes";
constexpr std::uint32_t kKnownTimestamp = 0x6A7B9B8C; // Steam build 25216728

std::once_flag g_once;

// Every check of the install and the memory it takes, before anything of the game is written:
// r_maxRenderViews, the view slot sites, the async compute read, the per-view block and its references, the
// context tables, the code bytes and the clones' sites. Adds the ranges the changes write to `writes`.
bool checkAndPrepare(const GameText& text, std::vector<CodeRange>& writes) {
    if (!view_slots::prepareStorage(text.base) || !checkAsyncComputeRead(text.base) ||
        !preparePerViewBlock(text, 2, writes) || !prepareViewContexts(text.base, writes) ||
        !prepareViewRedirectPatches(text.base, writes) || !prepareViewClones(text.base)) {
        return false;
    }
    if (parallelEyesSettings().testFail == parallel_eyes::TestFail::Check) {
        EVR_LOG("%s: a check fails here (ETERNALVR_TEST_INSTALL_FAIL=check)", kTag);
        return false;
    }
    return true;
}

// The engine changes and the hooks that follow them, in the order the rig tested: the block move and view 1's
// contexts, then the redirects with their code bytes, then the clones (a hook installed after the bytes or
// references it covers were changed carries the changed ones). Every check ran before (checkAndPrepare) and
// the ranges are writable, so the changes themselves cannot fail; a hook can, which leaves the changes made
// before it (view_slots.hpp: view 0 alone for the session).
bool installChanges(const std::byte* base) {
    movePerViewBlock();
    installViewContexts();
    if (!installViewRedirects(base)) {
        return false;
    }
    applyViewRedirectPatches(base);
    if (parallelEyesSettings().testFail == parallel_eyes::TestFail::Hook) {
        EVR_LOG("%s: a hook fails here, after the code bytes (ETERNALVR_TEST_INSTALL_FAIL=hook)", kTag);
        return false;
    }
    if (!installViewClones()) {
        return false;
    }
    installBinTileHook(); // each view's lights binned in its own (asymmetric) frustum; a missing piece logs
    return true;
}

std::optional<std::wstring> envValue(std::wstring_view name) {
    std::wstring value;
    if (!readEnv(std::wstring(name).c_str(), value)) {
        return std::nullopt;
    }
    return value;
}

} // namespace

const parallel_eyes::Settings& parallelEyesSettings() {
    static const parallel_eyes::Settings settings = [] {
        parallel_eyes::Settings s = parallel_eyes::readSettings(&envValue, &ui_vulkan::enabled);
        if (!s.why.empty()) {
            EVR_LOG("%s: off: %s; the standard renderer", kTag, s.why.c_str());
        }
        for (const std::string& w : s.warnings) {
            EVR_LOG("%s: %s", kTag, w.c_str());
        }
        return s;
    }();
    return settings;
}

bool parallelEyesRequested() {
    static const bool requested = [] {
        if (!parallelEyesSettings().requested) {
            return false;
        }
        GameText text;
        if (!findGameText(text) || text.timestamp != kKnownTimestamp) {
            EVR_LOG("%s: not available for this game version (timestamp 0x%X); the standard renderer", kTag,
                    text.timestamp);
            return false;
        }
        return true;
    }();
    return requested;
}

void installViewSlotsEarly() {
    std::call_once(g_once, [] {
        if (!parallelEyesRequested()) {
            return;
        }
        mp_guard::install(); // before vkCreateInstance's present policy, which follows the result
                             // (layer_entry)
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: off: the multiplayer guard is not armed; the standard renderer", kTag);
            return;
        }
        GameText text;
        findGameText(text);
        std::vector<CodeRange> writes;
        WritableRanges writable;
        if (!checkAndPrepare(text, writes) || !writable.make(writes)) {
            abandonPerViewBlock();
            EVR_LOG("%s: off: a check failed (above); the game is unchanged, the standard renderer", kTag);
            return;
        }
        if (!installAsyncComputeOff(text.base) || !view_slots::installStorageHooks()) {
            writable.restore();
            abandonPerViewBlock();
            EVR_LOG("%s: off: a hook failed (above); the hooks installed stay inert and the game works as it "
                    "was; the standard renderer",
                    kTag);
            return;
        }
        view_slots::markChanged();
        runtime_cvars::setParallelEyes(parallelEyesSettings().antiAliasingOff,
                                       parallelEyesSettings().antiAliasingHeld);
        if (parallelEyesSettings().alternateEyes) {
            EVR_LOG("%s: ETERNALVR_ALTERNATE_EYES is ignored (alternate eyes are a mode of the standard "
                    "renderer); "
                    "frame pacing is not turned off for it",
                    kTag);
        }
        const bool installed = installChanges(text.base);
        writable.restore();
        if (!installed) {
            EVR_LOG("%s: FAILED after the engine was changed (above): not on; both eyes show the same "
                    "image (view 0's) for this session, and the standard renderer does not run on the "
                    "changed engine",
                    kTag);
            return;
        }
        // Last: with r_maxRenderViews 2 the engine builds view index 1's storage, which the hooks redirect
        // from here on.
        view_slots::activate();
        const parallel_eyes::Settings& s = parallelEyesSettings();
        EVR_LOG("%s: on: both eyes as two views of one render (%zu slot sites, %zu occlusion sites); async "
                "compute off; view 1's clones %s; eye copy %s; parts off (ETERNALVR_TEST_VIEW_OFF): %s",
                kTag, view_slots::slotSites(), view_slots::occlusionSites(), s.clones ? "on" : "off",
                s.eyeCopy == parallel_eyes::EyeCopy::Screen  ? "each view's screen pass"
                : s.eyeCopy == parallel_eyes::EyeCopy::Final ? "each view's final image (test)"
                                                             : "off (test)",
                parallel_eyes::partsText(s.off).c_str());
    });
}

} // namespace evr::vkcore
