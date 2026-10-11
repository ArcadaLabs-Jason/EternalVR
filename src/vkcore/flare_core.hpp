#pragma once

// The part of lens flares per eye that Route S (flare_views.hpp) and Parallel Eye Rendering (pe_flares.hpp)
// share: the flare job's calls recorded in the flare book, the query slot hook, and the update run again
// over one half of a flare's vertex block from an eye's latched render view (flare_core.cpp).

#include "features/flares/flare_book.hpp"

#include <cstddef>
#include <cstdint>

namespace evr::vkcore::flare_core {

// Locates and installs the record and slot hooks once per process; later calls return the first result. The
// first caller's tag prefixes the log lines; `notInstalled` ends a line that says why nothing was installed,
// and `what` the line with the hooks.
bool install(const char* tag, const char* notInstalled, const char* what);

// Installed, and no rebuild faulted since.
bool live();

// Parallel Eye Rendering: the prepare takes room for both views' quads, so records carry two halves. Set
// once before the first frame; cleared for good (stopTwoHalves) when the flares go off.
void startTwoHalves();
void stopTwoHalves();
bool twoHalves();

// The render a render view's flares belong to: the view and its world's frame number.
bool renderKeyOf(std::uintptr_t view, flares::RenderKey& key);

// One half's rebuild, for the caller's counts.
struct HalfResult {
    std::size_t taken = 0;    // records of this render for this half
    std::size_t done = 0;     // rebuilt from the view
    std::size_t skipped = 0;  // returned before writing (r_skipFlares set since)
    std::size_t changed = 0;  // the write pointer moved after the engine's update, or another quad count
    std::size_t odd = 0;      // wrote another number of quads
    std::size_t noRoom = 0;   // half 1 of a record without room for it
    bool otherRender = false; // nothing for this render, but another render's records were never taken
    bool fault = false;       // a rebuild faulted: live() is false from now on
};

// Called for each flare rebuilt, with its record and the half rebuilt.
using RebuildDone = void (*)(const flares::FlareRecord& record, int half);

// After an eye's latch, with that eye's pose written into `view`: half `half` of the render `key`'s flares
// built again from the view's latched matrices, their vertices carrying the queries `slotView` reads
// (flares::viewQuery: 0 the engine's, stored in the render entity as the engine's update does; 1 view 1's,
// never stored there). Render job thread.
HalfResult rebuild(std::byte* view, const flares::RenderKey& key, int half, int slotView, RebuildDone done);

// The record hooks' counts since the last call, for the caller's periodic line.
struct RecordCounts {
    std::uint64_t recorded = 0;
    std::uint64_t duplicate = 0;
    std::uint64_t full = 0;
    std::uint64_t late = 0;
    std::uint64_t unwritten = 0; // the engine's own call wrote nothing
};
RecordCounts takeRecordCounts();

// Renders with flares no eye took, since the start.
std::uint64_t rendersNotTaken();

} // namespace evr::vkcore::flare_core
