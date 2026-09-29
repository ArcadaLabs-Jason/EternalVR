#pragma once

// The process's video memory against the budget Windows gives it (docs/VR_STEREO.md, "Stalls"). Memory
// past the budget is paged out and back in by the OS, which the player sees as stutter. The XR worker reads
// the adapter it shares with the game once a second (IDXGIAdapter3::QueryVideoMemoryInfo, local segment
// group: the card's own memory, as Windows counts it for the whole process) and logs it every 10 s; the
// stall line (stall_watch.hpp) shows the last reading.

#include <cstdint>

struct IDXGIAdapter1;

namespace evr::vkcore::vram {

// XR worker, once its D3D12 device exists: the adapter to read (the runtime's, checked to be the game's).
void watch(IDXGIAdapter1* adapter);

// XR worker, every pass of its loop: reads the adapter at most once a second.
void poll();

// XR worker, every 10 s: the `vram:` line (nothing before the first reading).
void logSummary();

struct Reading {
    bool valid = false;
    std::uint64_t usage = 0;  // bytes the process has in local video memory
    std::uint64_t budget = 0; // bytes the OS lets it have there before paging
};

// Any thread: the last reading.
Reading latest();

} // namespace evr::vkcore::vram
