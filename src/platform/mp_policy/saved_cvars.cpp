#include "platform/mp_policy/saved_cvars.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace evr::mp_policy {

namespace {

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

struct LeftCvar {
    std::string_view name;
    const char* why;
};

constexpr std::array<LeftCvar, 5> kLeftOnTrip{{
    {"r_windowWidth", "the window size: writing it would resize the game's window mid-game"},
    {"r_windowHeight", "the window size: writing it would resize the game's window mid-game"},
    {"r_fullscreen", "the window mode: writing it would switch the game's window mid-game"},
    {"r_swapInterval", "vertical sync: writing it would change the present mode mid-game"},
    {"r_hdrDisplay", "HDR output: writing it would change the swapchain mid-game"},
}};

} // namespace

bool sameCvarName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return true;
}

std::string cvarValueText(int integer, float number) {
    char text[32];
    if (!std::isfinite(number) || number == static_cast<float>(integer)) {
        std::snprintf(text, sizeof(text), "%d", integer);
        return text;
    }
    // The fewest significant digits that read back as the same float (at most 9 for any float).
    for (int digits = 1; digits <= 9; ++digits) {
        std::snprintf(text, sizeof(text), "%.*g", digits, static_cast<double>(number));
        if (std::strtof(text, nullptr) == number) {
            break;
        }
    }
    return text;
}

const char* cvarLeftOnTrip(std::string_view name) {
    for (const LeftCvar& c : kLeftOnTrip) {
        if (sameCvarName(c.name, name)) {
            return c.why;
        }
    }
    return nullptr;
}

} // namespace evr::mp_policy
