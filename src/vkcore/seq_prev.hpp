#pragma once

// Route S's per-eye previous-frame matrices (stereo_seq/prev_matrices.hpp): one book for the hook after the
// engine's store (seq_hooks.cpp) and the per-eye hook (presenter_seq.cpp), which puts a rewrite back when
// an eye R render frame stays mono. Thread-safe.

#include "stereo_seq/eye_tags.hpp"
#include "stereo_seq/prev_matrices.hpp"

#include <cstddef>
#include <cstdint>

namespace evr::vkcore::seq_prev {

// The engine just stored `view`'s previous matrices in render frame `renderFrame`, which draws `eye`.
void afterStore(std::byte* view, stereo_seq::Eye eye, std::uint32_t renderFrame);

// The per-eye hook wrote no view in this eye R render frame: the engine's store goes back into `view`.
bool undoRewrite(std::byte* view);

stereo_seq::PrevMatrixBook::Stats stats();

} // namespace evr::vkcore::seq_prev
