#pragma once

// The screen-space reflections' noise index under Route S (stereo_seq/ssr_noise.hpp,
// docs/rig-findings/stereo-temporal.md section 3.2a; Steam build 25216728, the same code in the Game Pass
// build).
//
// SSR's parameter setup (RVA 0x1C72480, called from 0x1C71630 with the render-view job's block at render
// context plus 0x6A7E20) reads the low byte of the backend counter the render-view job read for its render
// and stored in its block (+0xB0, stored at 0x1C56A19; the field the SSDO hook reads too), reverses its low
// four bits and hands the result to SSRParms.w. A hook after the read (RVA 0x1C7250A) finds the render's eye
// tag by that stored counter and, for an eye's own view while per-eye TAA or DLSS runs, puts the tick's
// counter in its place (stereo_seq::ssrCounterFor): both eyes of a tick get one index, and over 16 ticks each
// eye gets all 16. Mono and untagged renders keep the engine's. The renders given the tick's counter and
// those left with the engine's are counted per eye and logged once per session, after about 60 s of stereo
// play.
//
// Located by signature and checked by the parameter's name ("SSRParms", the declaration the setup's `mov rdx`
// reads); anything missing leaves the game's index and logs why, once.

namespace evr::vkcore {

// Locates and installs the hook once per process (Route S start, after its hooks are active); later calls
// return the first result.
bool installSsrNoiseHooks();

} // namespace evr::vkcore
