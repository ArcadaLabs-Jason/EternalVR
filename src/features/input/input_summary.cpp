#include "features/input/input_summary.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace evr::input {

namespace {

float clean(float value) {
    return std::isfinite(value) && value > 0.0f ? value : 0.0f;
}

// Appends printf text to `out` at `length`, never past `size - 1`.
template <typename... Args>
void append(char* out, std::size_t size, std::size_t& length, const char* format, Args... args) {
    if (length + 1 >= size) {
        return;
    }
    const int n = std::snprintf(out + length, size - length, format, args...);
    if (n > 0) {
        length = std::min(size - 1, length + static_cast<std::size_t>(n));
    }
}

} // namespace

bool windowActive(const InputSummaryWindow& window) {
    for (std::size_t i = 0; i < game::kGameActionCount; ++i) {
        if (window.mappedPresses[i] != 0 || window.sentPresses[i] != 0) {
            return true;
        }
    }
    return window.stickOffSeconds[0] > 0.0f || window.stickOffSeconds[1] > 0.0f ||
           window.menuSeconds > 0.0f || window.suppressedSeconds > 0.0f || window.forcedViewSeconds > 0.0f;
}

void InputSummary::add(const InputSummarySample& sample) {
    const float dt = std::min(clean(sample.dtSeconds), kMaxFrameSeconds);
    window_.seconds += dt;
    const game::GameActionSet sentActions = sample.reachedGame ? sample.sent : game::GameActionSet{};
    const std::array<float, 2> sticks{clean(sample.leftStick), clean(sample.rightStick)};
    for (std::size_t h = 0; h < 2; ++h) {
        window_.stickMax[h] = std::max(window_.stickMax[h], std::min(sticks[h], 1.0f));
        if (sticks[h] > kSummaryStickCentre) {
            window_.stickOffSeconds[h] += dt;
        }
    }
    for (std::size_t i = 0; i < game::kGameActionCount; ++i) {
        if (sample.mapped.test(i) && !lastMapped_.test(i)) {
            ++window_.mappedPresses[i];
        }
        if (sentActions.test(i) && !lastSent_.test(i)) {
            ++window_.sentPresses[i];
        }
    }
    lastMapped_ = sample.mapped;
    lastSent_ = sentActions;
    window_.moveMappedMax = std::max(window_.moveMappedMax, std::min(clean(sample.moveMapped), 1.0f));
    window_.turnMappedDegrees += clean(sample.turnMapped);
    if (sample.reachedGame) {
        window_.moveSentMax = std::max(window_.moveSentMax, std::min(clean(sample.moveSent), 1.0f));
        window_.turnSentDegrees += clean(sample.turnSent);
    }
    if (sample.menuHold) {
        window_.menuSeconds += dt;
    }
    if (sample.gameSuppressing) {
        window_.suppressedSeconds += dt;
    }
    if (sample.forcedView) {
        window_.forcedViewSeconds += dt;
    }
}

bool InputSummary::take(InputSummaryWindow& out) {
    if (window_.seconds < kSummaryWindowSeconds) {
        return false;
    }
    quietSeconds_ += window_.seconds;
    const bool log = windowActive(window_) || quietSeconds_ >= kSummaryIdleSeconds;
    if (log) {
        out = window_;
        quietSeconds_ = 0.0f;
    }
    window_ = {};
    return log;
}

void InputSummary::gap() {
    lastMapped_.reset();
    lastSent_.reset();
}

void InputSummary::restart() {
    lastMapped_.reset();
    lastSent_.reset();
    window_ = {};
    quietSeconds_ = 0.0f;
}

std::size_t formatInputSummary(const InputSummaryWindow& w, char* out, std::size_t size) {
    if (size == 0) {
        return 0;
    }
    out[0] = '\0';
    std::size_t length = 0;
    append(out, size, length,
           "input, last %.0f s: left stick max %.2f, off centre %.1f s; right stick max %.2f, off centre "
           "%.1f s; move max %.2f (sent %.2f); turn %.0f deg (sent %.0f); presses mapped/sent:",
           w.seconds, w.stickMax[0], w.stickOffSeconds[0], w.stickMax[1], w.stickOffSeconds[1],
           w.moveMappedMax, w.moveSentMax, w.turnMappedDegrees, w.turnSentDegrees);
    // The held-back part is worth more than the last presses: it is formatted first and its room kept, and a
    // list that does not fit ends in "...".
    char tail[160];
    std::size_t tailLength = 0;
    if (w.menuSeconds > 0.0f || w.suppressedSeconds > 0.0f || w.forcedViewSeconds > 0.0f) {
        append(tail, sizeof(tail), tailLength,
               "; held back: menu %.1f s, game suppressing buttons %.1f s, forced view %.1f s", w.menuSeconds,
               w.suppressedSeconds, w.forcedViewSeconds);
    } else {
        append(tail, sizeof(tail), tailLength, "; held back: none");
    }
    constexpr std::size_t kEllipsis = 3;
    std::size_t last = game::kGameActionCount; // the last action with a press
    for (std::size_t i = 0; i < game::kGameActionCount; ++i) {
        if (w.mappedPresses[i] != 0 || w.sentPresses[i] != 0) {
            last = i;
        }
    }
    bool any = false;
    bool cut = false;
    for (std::size_t i = 0; i < game::kGameActionCount && !cut; ++i) {
        if (w.mappedPresses[i] == 0 && w.sentPresses[i] == 0) {
            continue;
        }
        const std::string_view name = game::gameActionName(static_cast<game::GameAction>(i));
        char item[96];
        const int n = std::snprintf(
            item, sizeof(item), "%s %.*s %u/%u", any ? "," : "", static_cast<int>(name.size()), name.data(),
            static_cast<unsigned>(w.mappedPresses[i]), static_cast<unsigned>(w.sentPresses[i]));
        const std::size_t itemLength = n > 0 ? std::min(static_cast<std::size_t>(n), sizeof(item) - 1) : 0;
        const std::size_t room = (i == last ? 0 : kEllipsis) + tailLength + 1; // what must stay free
        if (length + itemLength + room > size) {
            cut = true;
            break;
        }
        append(out, size, length, "%s", item);
        any = true;
    }
    if (cut) {
        append(out, size, length, "...");
    } else if (!any) {
        append(out, size, length, " none");
    }
    append(out, size, length, "%s", tail);
    return length;
}

} // namespace evr::input
