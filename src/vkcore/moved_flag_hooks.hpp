#pragma once

// Moving objects keep their motion vectors in eye R (stereo_seq/moved_flag.hpp,
// docs/rig-findings/stereo-moved-flag.md; Steam build 25216728). On by default; ETERNALVR_STEREO_MOVED=0
// turns it off, =count only counts. Only with per-eye TAA or DLSS (anti-aliasing on): the moved flag it keeps
// costs GPU time (docs/rig-findings/stereo-moved-flag.md, section 4).
//
// The world's list builder (0x18DEDB0) loads each entity's status byte (`movzx ecx, byte [r14 + rsi + 8]`,
// RVA 0x18DF0AA) and on status 1 clears the entity's moved flag (0x18DF0FE) and sets status 0. A hook on the
// `test cl, cl` after the load (RVA 0x18DF0B0) makes eye R's render skip that cleanup. A probe on the surface
// rebuild's moved check (RVA 0x1C8D14D) counts surfaces rebuilt moved or not, per eye.
//
// Located by signature and cross-checked; anything missing leaves the game untouched and logs why.

namespace evr::vkcore {

// Locates and installs the hooks once per process; later calls return the first result.
bool installMovedFlagHooks();

} // namespace evr::vkcore
