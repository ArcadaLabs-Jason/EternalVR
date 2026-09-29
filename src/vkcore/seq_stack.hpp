#pragma once

// Eye R's nested chain on the frame-end wrapper's stack (docs/VR_STEREO.md, "Stack"): where the wrapper
// started it, how deep it went and the least stack left at a start. seqNoteNestedStack (seq_hooks.hpp) is
// defined with these. Thread-safe.

#include <cstddef>
#include <cstdint>

namespace evr::vkcore::seq_stack {

// Eye R's chain starts on this thread with the wrapper's stack pointer at `sp`; it ended.
void enter(std::uintptr_t sp);
void leave();

// The stack left at an eye R start.
void noteHeadroom(std::size_t headroom);

std::size_t deepestNested(); // bytes: deepest eye R chain point seen below the wrapper's call
std::size_t leastHeadroom(); // bytes: least stack left at an eye R start (0: none or unknown)

} // namespace evr::vkcore::seq_stack
