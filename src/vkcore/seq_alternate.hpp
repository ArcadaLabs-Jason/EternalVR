#pragma once

// Alternate eyes (ETERNALVR_ALTERNATE_EYES=1 or auto, docs/rig-findings/alternate-eye.md): the eye each
// render of the engine's own chain draws, shared by the frame-end wrapper and the hooks inside the render
// (seq_hooks.cpp), and with auto the choice for each pair between both eyes in its tick (Route S) and one
// eye per tick (stereo_seq/adaptive_eyes.hpp). Thread-safe: the render's hooks run on the engine's job
// threads.

#include "stereo_seq/adaptive_eyes.hpp"
#include "stereo_seq/alternate_eyes.hpp"

#include <cstdint>

namespace evr::vkcore::seq_alternate {

// Once at install, before any render asks: On or Auto (Off does not use this module).
void configure(stereo_seq::AlternateMode mode);
bool adaptive();

// The eye render frame `renderFrame` (renderSystem + 0x10) draws (stereo_seq::EyeAlternator::eyeFor). With
// auto, the first ask of an eye L render also decides whether its tick renders eye R nested (pairInTick).
stereo_seq::Eye eyeFor(std::uint32_t renderFrame);
// Render frame `renderFrame` is eye L of a tick that renders both eyes (auto only; decided with eyeFor).
bool pairInTick(std::uint32_t renderFrame);
// The frame end of `renderFrame`: handed over as `eye` with its view written (`stereo`), or mono;
// `pairDone`: eye L of a tick that renders eye R nested. With auto it also ends the tick before for the
// decision (the wall time between these calls is a tick; a switch is logged, and every 10 s a summary).
void rendered(std::uint32_t renderFrame, stereo_seq::Eye eye, bool stereo, bool pairDone = false);
// Auto: the nested eye R of the tick `rendered` just noted with pairDone: rendered or not, its wall time.
void rightRendered(bool done, std::uint64_t micros);
stereo_seq::EyeAlternator::Stats stats();

// Every render frame's frame end, eye R's nested one included (the wrapper counts them all, from install on):
// the renders' running number. A render's own work before its frame end (its world commits) sees the
// number of the renders that ended before it, one more than the render before saw
// (stereo_seq/alternate_prev.hpp).
void countRender();
std::uint64_t renderIndex();

// The headset's display period (XrFrameState::predictedDisplayPeriod, nanoseconds); 0 or less is ignored.
void noteDisplayPeriodNs(std::int64_t nanoseconds);

} // namespace evr::vkcore::seq_alternate
