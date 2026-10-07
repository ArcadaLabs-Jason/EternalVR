#pragma once

// The per-eye fixes Route S installs once its hooks are active (seq_hooks.cpp): each locates, checks and
// installs its own hooks and logs the result; a missing piece leaves that fix out and the others in.

namespace evr::vkcore {

void installSeqEyeFixes();

} // namespace evr::vkcore
