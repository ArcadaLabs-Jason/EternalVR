#pragma once

// Particles in eye R's ray-traced reflections under the particle sync (fx_sync_hooks.hpp,
// docs/rig-findings/stereo-fx-lag.md section 8; Steam build 25216728).
//
// With ray tracing on, the particle bind (0x1955670, RVA 0x1955B40) and the effect bind (0x19511E0, RVA
// 0x195164D) set the model's bit in a mask of the world's ray tracing scene (`[[world + 0x71DAE0] + 0x298]`),
// which the blended (transparent and particle) acceleration structure reads (0x1C58D30). A job the world job
// queues every render (0x18DEAF0) clears the mask (memset at RVA 0x18DEB6D) before the binds. Eye R skips
// the binds of what eye L generated in the tick, so its mask held only its own binds and eye L's particles
// were missing from eye R's reflections. In eye R the clear is now skipped: the mask keeps the bits of eye
// L's binds, which are the models eye R draws from eye L's slot, and eye R's own binds add theirs.

#include "stereo_seq/fx_sync.hpp"
#include "vkcore/game_text.hpp"

#include <cstddef>
#include <string>

namespace evr::vkcore::fx_rt {

// The render's answer (fx_sync_hooks.cpp's: the particle sync's rule for the render running now).
using RenderAction = stereo_seq::FxAction (*)();

// Checks the clear's job and both binds' mask writes (`particleBind` 0x1955670, `effectUpdate` 0x19511E0) and
// hooks the clear's call; false (logged under `tag`) when anything does not check out or the hook fails:
// then nothing changes. Call once, after the particle sync's hooks are in.
bool install(const GameImage& image,
             const std::byte* particleBind,
             const std::byte* effectUpdate,
             RenderAction action,
             const char* tag);

// The RVA of the hooked call (0x18DEB6D on Steam), 0 until install() found it.
unsigned foundRva();

// "; ray-traced particle mask kept by eye R K, cleared eye L l / eye R r", the counts since the last call;
// empty when not installed.
std::string takeReadout();

} // namespace evr::vkcore::fx_rt
