#pragma once

// GPU particle stages of CPU particle systems under the particle sync (fx_sync_hooks.hpp,
// docs/rig-findings/stereo-fx-lag.md section 7; Steam build 25216728).
//
// A particle system's stages can run on the GPU. The world job resets the GPU particle manager's draw lists
// and emitter records every render, eye R's included (0x1C28320); only the particle update fills them: its
// bind (0x1955670) calls 0x1C2B670 for every enabled GPU stage (RVA 0x1955949: draw list 0 and the light
// atlas list), its generation (0x1953D90) calls 0x1C2B6C0 per stage (an emitter record each). With eye L's
// binding kept and its generation skipped, eye R's render had none of them for what eye L generated. For
// such a particle system eye R now runs the GPU stages' part of the bind (here) and the engine's generation.
//
// Every address and offset is checked against the bind's code and the functions it calls; anything that does
// not match leaves locate() false (logged).

#include "vkcore/game_text.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace evr::vkcore::fx_gpu {

// Checks the bind's GPU stage loop (`bind`, 0x1955670), 0x1C2B670 and what it calls, and the GPU instance
// layout through the generation's call of 0x1C2B6C0 (`generate`, 0x1953D90). false (logged under `tag`) when
// anything does not check out; then nothing below may be called but the step readout.
bool locate(const GameImage& image, const std::byte* bind, const std::byte* generate, const char* tag);

// The particle system has a GPU particle instance: its GPU runtime (`[model + 0x658]`) has an entry whose
// instance (+4) is not -1.
bool hasInstance(std::uintptr_t model);

// The GPU stages' part of the bind for the particle system, as the bind does it from the stage records the
// last generation wrote: 0x1C2B670(runtime, stage, index, index) for every GPU stage whose runtime entry is
// enabled. With `call` false only counted. Returns the stages.
// Not redone: the bind first zeroes every stage's render data `[stage]` (0x1955700), then for each stage in
// its records sets the distance LOD bit `[stage + 0x44]` 0x20 (0x19558F2..0x1955918) and `[stage] =
// [[[[model + 0x4E8] + 0x110] + index * 0x20] + 0x690]` (0x195591B). Eye L's bind did that for the records of
// the generation before its own, so a GPU stage that first appears in eye L's generation of this tick gets
// its draw list entry in eye R with `[stage]` still 0: at worst it is missing from eye R for that tick
// (0.1.34 had no entry for it either); the next tick's binds set it.
unsigned bindStages(std::uintptr_t model, bool call);

// A read-only hook on the GPU particle step (0x1C28DD0, once per world for every render, called or queued by
// its frame end) that sums the manager's emitter records, draw list 0 and light atlas list per eye, the eye
// of the frame end that called or queued it (seqFrameEndEye). false (logged) when its code or the reset's
// does not check out. Installed only after the particle sync's hooks are in.
bool installStepReadout(const GameImage& image, const char* tag);

// "; GPU particle manager per render (emitter records / draw list / light atlas): eye L r / d / a, eye R
// r / d / a (n / m GPU steps)", the averages since the last call; empty when the readout is not installed.
std::string takeStepReadout();

} // namespace evr::vkcore::fx_gpu
