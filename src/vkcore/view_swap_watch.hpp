#pragma once

// Parallel Eye Rendering's swapchain guard (view_swap_guard.hpp), the part that only watches (RVAs in Steam
// build 25216728): each swapchain destroy is logged with its thread and with the frame begin that recreated
// it, if one did (the begin as the worker job 0x1CD6F80 or called directly, and the frame's view count), and
// the destroyed swapchain's images are kept for the guard's first check after it. Its hooks:
// - the frame begin's entry 0x1CD9750 (rcx = the render thread, its view count at +0xA8; [rsp] is 0x1CD6F89
//   when it runs as the job), noted for its own thread;
// - around the begin's recreate call (0x1CDD3C0 -> `call 0x1D090E0` at 0x1CDD5BF; hooks at 0x1CDD5B4 and
//   0x1CDD5C4): the recreate is running (it sends the work to the window thread and waits);
// - the destroy 0x1D09540 (rcx = the swapchain, its images at +0x38, their count at +0x80), which also holds
//   the eye copy's presents of new images no view 0 pass drew yet (view_snapshot::holdNewSwapchainImages).

#include <cstddef>
#include <cstdint>

namespace evr::vkcore::swap_watch {

// A read of game memory that may fault (an access violation is caught); false then, or for an address in the
// first 64 KiB.
bool guardedCopy(void* to, std::uintptr_t from, std::size_t size);

template <typename T>
bool readAt(std::uintptr_t at, T& out) {
    return guardedCopy(&out, at, sizeof(T));
}

// Checks the hooks' sites byte by byte before anything of the game changes; a miss logs.
void prepare(const std::byte* base);

// Installs the hooks when every site was as known (logs one line either way); independent of the guard.
void install(const std::byte* base);

// The begin's recreate is running now (false when its hooks are not in).
bool recreateRunning();

// The number of the last destroy once, for the guard's first check after it; 0 when that was logged already.
std::uint64_t takeFirstCheck();

// `image` was one of the last destroyed swapchain's images (an address the new swapchain may reuse).
bool destroyedImage(std::uintptr_t image);

// Swapchain destroys seen.
std::uint64_t destroys();

} // namespace evr::vkcore::swap_watch
