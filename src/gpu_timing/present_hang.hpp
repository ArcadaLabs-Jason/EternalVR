#pragma once

// When the game has stopped presenting long enough to be called hung (docs/VR_STEREO.md, "Hangs").
//
// A stall line (present_stall.hpp) is written when the present that ends the gap arrives, so a game that
// never presents again leaves nothing. The XR worker keeps running while the game hangs (the headset shows
// the last image), so it asks this watch every frame: a gap of kHangFirstNoteMs in play gets a hang note
// (the threads' places: hang_note.hpp), and kHangSecondNoteMs another, so two notes tell a thread stuck in
// one place from one that keeps moving. If the game presents again after a note, that is said once. At most
// kHangsNoted gaps a process get notes, so a game that hangs and recovers over and over cannot flood the log.
//
// In play is judged as for stalls: when the layer sees the game's ticks, the gap counts only if a tick came
// at most kInPlayTickMs before the last present (loading screens and menus do not tick). Without ticks every
// gap counts.

#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::gpu_timing {

inline constexpr double kHangFirstNoteMs = 5000.0;
inline constexpr double kHangSecondNoteMs = 20000.0;
inline constexpr std::uint32_t kHangsNoted = 3;

struct HangCheck {
    std::uint64_t nowMicros = 0;
    std::uint64_t lastPresentMicros = 0; // 0: the game has not presented yet
    std::uint64_t lastTickMicros = 0;    // 0: no game tick yet
    bool ticksKnown = false;             // the layer sees the game's ticks
};

enum class HangAction : std::uint8_t {
    None,
    FirstNote,  // the gap reached kHangFirstNoteMs: note the threads
    SecondNote, // the same gap reached kHangSecondNoteMs: note them again
    Resumed,    // the game presented again after a noted gap
};

struct HangResult {
    HangAction action = HangAction::None;
    double gapMs = 0.0;      // FirstNote, SecondNote: the gap so far; Resumed: the whole gap
    double tickAgeMs = -1.0; // FirstNote, SecondNote: since the last game tick; negative: none
    std::uint32_t hang = 0;  // which noted gap of this process (1 = the first)
};

// ETERNALVR_TEST_HANG (rig only, vkcore/hang_note.hpp): how long the one held present is held, and when.
// "<seconds>" or "<milliseconds>ms", then optionally "@<seconds>": the first present in play that many
// seconds (a fraction allowed) after the player is in the map; without it, the first present in play after
// a couple of thousand. Examples: "30" (30 s), "700ms@20" (700 ms, 20 s into the map). Nothing for an empty,
// zero, malformed or over 10-minute value.
struct TestHang {
    std::uint32_t ms = 0;
    double atSecondsInMap = -1.0; // negative: after a count of presents in play
};
std::optional<TestHang> parseTestHang(std::wstring_view value);

class HangWatch {
public:
    HangResult onCheck(const HangCheck& check);

private:
    std::uint64_t notedPresent_ = 0; // the last present before the gap being noted; 0: none
    std::uint32_t notes_ = 0;        // notes of that gap
    std::uint32_t hangs_ = 0;        // gaps noted in this process
};

} // namespace evr::gpu_timing
