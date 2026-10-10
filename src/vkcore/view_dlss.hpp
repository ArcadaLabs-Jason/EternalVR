#pragma once

// Parallel Eye Rendering with DLSS (ETERNALVR_STEREO_DLSS=1; ETERNALVR_PE_DLSS=0 keeps the standard renderer
// for DLSS instead, parallel_eyes_settings.hpp). Steam build 25216728; the sites are RVAs, checked byte by
// byte before the engine is changed.
//
// The engine keeps its DLSS feature on the command context that records the post-process pass (context
// +0x190, its settings at +0x198..+0x1AC): the pass (0x1C9B5D0) creates it there (0x1CC5760, again when the
// quality or a size changes) and evaluates it (0x1CC5B30). View 1 records its post-process pass into its own
// context (category 11 slot 1, view_contexts.hpp), so the engine makes view 1 a feature and a DLSS history of
// its own; no twin is made (Route S's per-eye twins, taa_ngx.hpp, stay off). What the two views share:
// - one NGX parameter block (0x66E8B28), which create and evaluate fill key by key, and r_dlssForceReset's
//   countdown (0x66E8E30, decremented in evaluate): create, evaluate, the release (0x1CC5F00, from both
//   views' anti-aliasing passes at a switch to TAA) and the render size's query (0x1CC5D40, which fills the
//   same block) take one lock, so one of them runs at a time;
// - the engine's count of failed evaluations (0x66E0D80, written after evaluate returns), which both views
//   reset and raise: each view's results are counted here instead (view_dlss_plan.hpp). Three failures in a
//   row of either view make DLSS fall back to TAA in both eyes (r_antialiasing 1, the engine releases both
//   features), tried again once after a wait (stereo_seq/ngx_twin_retry.hpp; one try a session: each switch
//   makes view 1's clones again), and at once whenever DLSS is chosen in the game's video menu.
// - "Reset": raised for view 1's evaluations while the frames sent lately did not all render view 1, and for
//   the other view's next evaluation after an engine reset reached one (written into the evaluation's
//   parameters at 0x1CC5CE0, read by NGX's helper at 0x1CC7C89).
// View 1's feature is released (0x1CC5F00 on its context) when view 1 stops for good (a multiplayer guard
// trip, the clones off) and before NGX shuts down. A DLSS pass on any other context (the shared async
// compute context: async compute is off under Parallel Eye Rendering) would be one feature for both views:
// TAA in both eyes for the session.

#include <cstddef>
#include <string>
#include <string_view>

namespace evr::vkcore {

// From the install's checks (view_install.cpp), with Parallel Eye Rendering's DLSS asked for: every site's
// bytes. False (logged) when one is not as known: Parallel Eye Rendering stays off and the standard renderer
// runs DLSS per eye, as without ETERNALVR_PE_DLSS. True and nothing else without DLSS.
bool prepareViewDlss(const std::byte* base);

// From the install's changes, after view 1's contexts are built: the hooks. A hook that fails logs and holds
// TAA in both eyes for the session.
void installViewDlss();

// Parallel Eye Rendering's anti-aliasing set (runtime_cvars.cpp), on each present: the value of `name`
// (r_antialiasing: 2 while DLSS runs in both views, 1 during a fallback; r_dlssQuality: held only while DLSS
// runs). False when the cvar is not held now. A try that is due starts here (logged).
bool viewDlssHolds(std::string_view name, std::string& value);

// For the game's video menu (dlss_menu_hooks.cpp): DLSS runs in both views; DLSS fell back to TAA and can be
// tried again; the player chose DLSS in the menu during a fallback (a try at once, the count started over;
// false when there is no fallback).
bool viewDlssRunning();
bool viewDlssFallback();
bool viewDlssRetry();

} // namespace evr::vkcore
