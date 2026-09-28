#pragma once

// Reading a number from hand-edited text (settings, data files).
//
// std::from_chars for floating point is missing from the C++ libraries of some supported compilers, and
// strtof follows the process's C locale, which inside the game is not ours to rely on (a comma-decimal
// locale would read "0.25" as 0). So plain decimal numbers are read here by hand: an optional sign,
// digits with at most one '.', and an optional exponent. Anything else is refused.

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string_view>

namespace evr {

// The finite float that is the whole of `text`, or nullopt.
inline std::optional<float> parseFloat(std::string_view text) {
    std::size_t i = 0;
    const auto at = [&text](std::size_t k) {
        return k < text.size() ? text[k] : '\0';
    };
    const auto digit = [](char c) {
        return c >= '0' && c <= '9';
    };
    bool negative = false;
    if (at(i) == '+' || at(i) == '-') {
        negative = at(i) == '-';
        ++i;
    }
    double mantissa = 0.0;
    int scale = 0;
    int digits = 0;
    while (digit(at(i))) {
        mantissa = mantissa * 10.0 + (at(i) - '0');
        ++digits;
        ++i;
    }
    if (at(i) == '.') {
        ++i;
        while (digit(at(i))) {
            mantissa = mantissa * 10.0 + (at(i) - '0');
            --scale;
            ++digits;
            ++i;
        }
    }
    if (digits == 0 || digits > 30) {
        return std::nullopt;
    }
    if (at(i) == 'e' || at(i) == 'E') {
        ++i;
        bool negativeExponent = false;
        if (at(i) == '+' || at(i) == '-') {
            negativeExponent = at(i) == '-';
            ++i;
        }
        int exponent = 0;
        int exponentDigits = 0;
        while (digit(at(i)) && exponentDigits < 4) {
            exponent = exponent * 10 + (at(i) - '0');
            ++exponentDigits;
            ++i;
        }
        if (exponentDigits == 0) {
            return std::nullopt;
        }
        scale += negativeExponent ? -exponent : exponent;
    }
    if (i != text.size()) {
        return std::nullopt;
    }
    const double value = mantissa * std::pow(10.0, scale);
    if (!std::isfinite(value) || value > static_cast<double>(std::numeric_limits<float>::max())) {
        return std::nullopt;
    }
    return static_cast<float>(negative ? -value : value);
}

} // namespace evr
