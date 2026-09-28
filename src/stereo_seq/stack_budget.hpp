#pragma once

// Stack room for eye R's render (Route S, docs/VR_STEREO.md, "Stack").
//
// Eye R's whole render-frame chain runs nested inside eye L's frame-end job, on the same job thread's
// stack: the engine runs the jobs of a job list it waits on inline, and each job of the chain keeps a
// job list of about 14 KiB on its stack. The engine never nests two chains, so its job threads are sized
// for one. A thread that reached eye L's frame-end job unusually deep would overflow in eye R's chain,
// and a stack overflow ends the process without a dump through the game's crash handler. So eye R is
// only rendered when the rest of the stack holds the deepest eye R chain seen so far plus a margin;
// otherwise the tick stays mono (eye L's half is dropped, the headset repeats the last pair).

#include <cstddef>
#include <cstdint>

namespace evr::stereo_seq {

// Room kept free below the deepest eye R chain seen (the chain goes deeper than the points it is
// measured at, and the stack's guard pages are not usable).
inline constexpr std::size_t kNestedStackMargin = 64 * 1024;
// Assumed depth of eye R's chain until one has been measured.
inline constexpr std::size_t kNestedStackFirstGuess = 64 * 1024;

// The current thread's stack as the stack pointer sees it.
struct StackPosition {
    std::uintptr_t low = 0;  // lowest address of the reserved stack
    std::uintptr_t high = 0; // one past its highest address
    std::uintptr_t sp = 0;   // the current stack pointer
};

// True when `sp` lies in [low, high): the thread runs on the stack the system describes (not on a stack
// of its own that the limits do not cover).
bool stackKnown(const StackPosition& s);

// Bytes left below the stack pointer (0 for an unknown stack).
std::size_t stackHeadroom(const StackPosition& s);

// Whether a nested render fits: the headroom holds the deepest nested chain seen (`deepestNested`, 0 when
// none was measured yet) plus the margin. An unknown stack is not refused (nothing to measure against).
bool nestedRenderFits(const StackPosition& s, std::size_t deepestNested);

// How deep a nested point at `innerSp` runs below the call made at `outerSp` (0 when it is not below).
std::size_t nestedDepth(std::uintptr_t outerSp, std::uintptr_t innerSp);

} // namespace evr::stereo_seq
