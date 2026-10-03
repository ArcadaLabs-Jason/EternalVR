#pragma once

// The game's own video settings in the layer log (game_settings.hpp): which cvars the line holds, how it is
// written and when it is read. Kept apart from the layer so it is unit-tested without the game.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace evr::vkcore::game_settings {

// The type a cvar is registered with (kex-cvarlist-2024.tsv); its value is read from the matching field.
enum class CvarKind { Bool, Int, Float };

struct SettingCvar {
    const char* name;
    CvarKind kind;
};

// The cvars behind the game's Video menu, in its order (reference/content/eternal-settings-menu.md,
// docs/rig-findings/perf-cpu-cvars.md section 2). The Overall Quality preset has no cvar of its own (the
// profile keeps it): each Advanced setting is shown by the cvars that tell its levels apart.
std::span<const SettingCvar> settingCvars();

struct SettingValue {
    enum class State { Read, NotFound, Unreadable };

    std::string_view name;
    CvarKind kind = CvarKind::Int;
    State state = State::Read;
    int integer = 0;
    float number = 0.0f;
};

// "1" / "0" for a bool, the integer for an int, the shortest of up to 6 significant digits for a float
// ("2.5", "0.25", "16").
std::string formatValue(CvarKind kind, int integer, float number);

// "r_enableRayTracing 1, r_lodScale 3.5, ...": every cvar that was found, "?" for one that could not be read.
std::string formatLine(const std::vector<SettingValue>& values);

// "a, b": the cvars that were not found, or empty.
std::string notFoundList(const std::vector<SettingValue>& values);

// When the values are read and the line is logged: kSettleMs after each map load (the game has applied the
// player's profile by then), and every kCheckMs after that while the player stays in the map. A map load
// always logs the line; a check logs it only when it differs from the last one. Without any map load seen
// for kFallbackMs (the map-load hook missing), the first read happens anyway.
class LineSchedule {
public:
    static constexpr std::uint64_t kSettleMs = 5000;
    static constexpr std::uint64_t kCheckMs = 10000;
    static constexpr std::uint64_t kFallbackMs = 60000;

    // True when the values are to be read now. `mapLoads`: the map loads seen so far; `nowMs`: a monotonic
    // clock in milliseconds.
    bool due(std::uint32_t mapLoads, std::uint64_t nowMs);

    // After a read that due() asked for: whether `line` is to be logged.
    bool shouldLog(const std::string& line);

private:
    bool started_ = false;
    std::uint64_t startMs_ = 0;
    std::uint32_t loads_ = 0;
    bool pending_ = false;
    std::uint64_t loadMs_ = 0;
    bool afterLoad_ = false;
    bool logged_ = false;
    std::uint64_t readMs_ = 0;
    std::string last_;
};

} // namespace evr::vkcore::game_settings
