#pragma once

// Eye R's per-view slot against the engine's after a device context resize (part of taa_hooks.hpp).
//
// A per-view slot (device context + 0x8, 0xA8 bytes) holds five render targets that each start with their
// width and height: the accumulation pair (+0x58, +0x60), the opaque accumulation (+0x68), the view colour
// (+0x70) and the distortion target (+0x78). Eye R only reads its pair and its opaque accumulation (the
// selectors); the view colour and distortion targets of slot 0 serve both eyes.
//
// With DLSS the device context is resized to the render size while the output keeps the display size. The
// engine resizes its slot in place and keeps the pair, the opaque accumulation and the distortion target at
// the output size (only the view colour follows the render size), while the slot builder makes the opaque
// and distortion targets at the render size. So eye R's slot is only rebuilt when a target it uses differs.

#include <cstddef>

namespace evr::vkcore {

inline constexpr std::size_t kSlotTargetCount = 5;
// Bit i stands for target i (in the order above).
inline constexpr unsigned kEyeRTargets = 0b00111;

// The targets whose size differs between the engine's slot and eye R's, as a bit mask (a null target on
// either side counts as equal).
unsigned slotSizeMismatches(const std::byte* engineSlot, const std::byte* ourSlot);

// Logs each target's size in both slots with the resize's render and upscaled sizes.
void logSlotSizes(const char* tag,
                  const std::byte* engineSlot,
                  const std::byte* ourSlot,
                  const int* size,
                  const int* upscaledSize);

} // namespace evr::vkcore
