#pragma once

// Route S's start-up log (presenter_seq.cpp): the cvars on the game's command line.

namespace evr::vkcore {

// The v1 cvar set on the game's command line (the launch helper passes it; docs/VR_STEREO.md), and with
// per-eye TAA requested its own cvars. Only logged: S3 turns them back on one at a time.
void logSeqCommandLineCvars();

} // namespace evr::vkcore
