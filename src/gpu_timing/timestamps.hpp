#pragma once

// Arithmetic on GPU timestamp values (docs/VR_STEREO.md, "GPU timing").
//
// A queue family writes timestamps with `timestampValidBits` meaningful low bits (36 to 64 on desktop
// GPUs); the counter wraps at 2^validBits. One tick is `timestampPeriod` nanoseconds.

#include <cstdint>

namespace evr::gpu_timing {

// The bits a timestamp of `validBits` carries: 0 for 0 bits (no timestamps), all ones from 64 on.
std::uint64_t validMask(std::uint32_t validBits);

// `t` minus `reference` in ticks for a counter that wraps at 2^validBits. Within half the counter's
// range the result is right across a wrap: a timestamp written before `reference` gives a negative
// value, one written after (even past the wrap) a positive one. 0 for 0 valid bits.
std::int64_t signedTicks(std::uint64_t reference, std::uint64_t t, std::uint32_t validBits);

// Ticks of `periodNs` nanoseconds each, in milliseconds.
double ticksToMs(double ticks, float periodNs);

} // namespace evr::gpu_timing
