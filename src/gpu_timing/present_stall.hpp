#pragma once

// Which gaps between the game's presents are stalls worth a log line (docs/VR_STEREO.md, "Stalls").
//
// A present more than kStallGapMs after the previous one is a stall. Loading screens and menus stall all
// the time (a map streams in, a menu opens) and say nothing about stutter in play, so when the layer sees
// the game's ticks (the camera hook runs once per game frame in play, never in menus or on loading
// screens) a stall counts as in play only if a tick came shortly before it began and another during it
// (the frame that ends it). Without ticks every stall is logged. The first kStallLinesLogged stalls in
// play get a line each; later ones are only counted, so a bad session cannot flood the log.

#include <cstdint>
#include <string>

namespace evr::gpu_timing {

inline constexpr double kStallGapMs = 50.0;
inline constexpr std::uint32_t kStallLinesLogged = 30;
// A game tick at most this long before the gap began: the game was in play.
inline constexpr double kInPlayTickMs = 250.0;

struct PresentGap {
    double gapMs = 0.0;
    bool ticksKnown = false;      // the layer sees the game's ticks (the camera hook is installed)
    double tickAgeAtStartMs = -1; // from the last tick to the gap's start; negative: no tick yet
    bool tickDuring = false;      // a tick came during the gap
};

enum class StallVerdict : std::uint8_t {
    None,        // not a stall
    Log,         // a stall in play (or with the phase unknown): one line
    LastLog,     // as Log, and the last line; later stalls are only counted
    Count,       // a stall in play past the line limit
    OutsidePlay, // a stall on a loading screen or in a menu (only counted)
};

class StallGate {
public:
    struct Counters {
        std::uint64_t inPlay = 0;      // stalls in play (or with the phase unknown), logged or not
        std::uint64_t outsidePlay = 0; // stalls on loading screens and in menus
        std::uint32_t logged = 0;      // lines logged
        double longestMs = 0.0;        // the longest stall in play
    };

    StallVerdict onGap(const PresentGap& gap);
    const Counters& counters() const { return counters_; }

private:
    Counters counters_;
};

// The game's own checkpoint saves (stall_watch.hpp): the stall line's closing note for the saves begun in
// its gap ("; the game saved a checkpoint in the gap"), and the 10 s summary's ("; the game saved 2
// checkpoints"). Empty for none.
std::string stallSaveNote(std::uint64_t saves);
std::string summarySaveNote(std::uint64_t saves);

} // namespace evr::gpu_timing
