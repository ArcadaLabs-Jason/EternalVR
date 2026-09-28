#include "features/render_size/render_size.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <string>

namespace evr::render_size {

namespace {

std::wstring lowerTrimmed(std::wstring_view text) {
    while (!text.empty() && std::iswspace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::iswspace(text.back())) {
        text.remove_suffix(1);
    }
    std::wstring out(text);
    for (wchar_t& c : out) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return out;
}

// A decimal integer that fills `text` (digits only), or nullopt.
std::optional<std::uint32_t> parseUnsigned(std::wstring_view text) {
    if (text.empty() || text.size() > 6) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9') {
            return std::nullopt;
        }
        value = value * 10 + static_cast<std::uint32_t>(c - L'0');
    }
    return value;
}

std::optional<std::int32_t> parseSigned(std::wstring_view text) {
    bool negative = false;
    if (!text.empty() && (text.front() == L'-' || text.front() == L'+')) {
        negative = text.front() == L'-';
        text.remove_prefix(1);
    }
    const auto value = parseUnsigned(text);
    if (!value) {
        return std::nullopt;
    }
    const auto signedValue = static_cast<std::int32_t>(*value);
    return negative ? -signedValue : signedValue;
}

std::wstring_view trimmed(std::wstring_view text) {
    while (!text.empty() && std::iswspace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::iswspace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

// The side rounded to the nearest multiple of kAlign, within kMinSide..limit (limit itself rounded down).
std::uint32_t alignSide(double side, std::uint32_t limit) {
    const std::uint32_t top = std::max(kMinSide, limit / kAlign * kAlign);
    const double units = std::round(side / static_cast<double>(kAlign));
    const double aligned = units * static_cast<double>(kAlign);
    return static_cast<std::uint32_t>(
        std::clamp(aligned, static_cast<double>(kMinSide), static_cast<double>(top)));
}

std::uint32_t limitOf(std::uint32_t value) {
    return value == 0 ? kMaxSide : std::min(value, kMaxSide);
}

} // namespace

std::optional<Request> parseRenderSize(std::wstring_view text) {
    const std::wstring t = lowerTrimmed(text);
    if (t.empty() || t == L"off" || t == L"0") {
        return Request{};
    }
    if (t == L"auto") {
        Request r;
        r.mode = Mode::Auto;
        return r;
    }
    const std::size_t x = t.find(L'x');
    if (x == std::wstring::npos) {
        return std::nullopt;
    }
    const auto w = parseUnsigned(std::wstring_view(t).substr(0, x));
    const auto h = parseUnsigned(std::wstring_view(t).substr(x + 1));
    if (!w || !h || *w < kMinSide || *h < kMinSide || *w > kMaxFixedSide || *h > kMaxFixedSide) {
        return std::nullopt;
    }
    Request r;
    r.mode = Mode::Fixed;
    r.fixed = {*w, *h};
    return r;
}

std::optional<float> parseRenderScale(std::wstring_view text) {
    const std::wstring t = lowerTrimmed(text);
    if (t.empty() || t == L"auto") {
        return 1.0f;
    }
    // Digits and one decimal point only (no exponent, sign or hex).
    int points = 0;
    for (const wchar_t c : t) {
        if (c == L'.') {
            ++points;
        } else if (c < L'0' || c > L'9') {
            return std::nullopt;
        }
    }
    if (points > 1 || t == L".") {
        return std::nullopt;
    }
    const float value = std::wcstof(t.c_str(), nullptr);
    if (!std::isfinite(value) || value < kMinScale || value > kMaxScale) {
        return std::nullopt;
    }
    return value;
}

Extent fitToLimits(Extent size, const ViewLimits& limits) {
    if (size.width == 0 || size.height == 0) {
        return {};
    }
    const std::uint32_t eyes = std::max<std::uint32_t>(1, limits.eyesSideBySide);
    std::uint32_t maxWidth =
        std::min(limitOf(limits.maxImageRect.width), limitOf(limits.maxSwapchain.width) / eyes);
    std::uint32_t maxHeight =
        std::min(limitOf(limits.maxImageRect.height), limitOf(limits.maxSwapchain.height));
    maxWidth = std::max(maxWidth, kMinSide);
    maxHeight = std::max(maxHeight, kMinSide);
    const double w = size.width;
    const double h = size.height;
    const double down =
        std::min({1.0, static_cast<double>(maxWidth) / w, static_cast<double>(maxHeight) / h});
    return {alignSide(w * down, maxWidth), alignSide(h * down, maxHeight)};
}

std::optional<Extent> autoSize(const ViewLimits& limits, float scale, std::uint64_t budget) {
    if (limits.recommended.width == 0 || limits.recommended.height == 0 || !std::isfinite(scale) ||
        scale <= 0.0f) {
        return std::nullopt;
    }
    const double w = limits.recommended.width;
    const double h = limits.recommended.height;
    const double pixels = w * h;
    const double fit = budget > 0 && pixels > static_cast<double>(budget)
                           ? std::sqrt(static_cast<double>(budget) / pixels)
                           : 1.0;
    const double factor = fit * static_cast<double>(scale);
    const Extent wanted{static_cast<std::uint32_t>(std::lround(w * factor)),
                        static_cast<std::uint32_t>(std::lround(h * factor))};
    return fitToLimits(wanted, limits);
}

std::optional<Extent> selectSize(const Request& request, const std::optional<ViewLimits>& limits) {
    switch (request.mode) {
    case Mode::Off:
        return std::nullopt;
    case Mode::Fixed:
        return fitToLimits(request.fixed, limits.value_or(ViewLimits{}));
    case Mode::Auto:
        if (!limits) {
            return std::nullopt;
        }
        return autoSize(*limits, request.scale);
    }
    return std::nullopt;
}

std::optional<WindowRect> parseWindowRect(std::wstring_view text) {
    std::int32_t values[4] = {};
    std::size_t index = 0;
    std::wstring_view rest = trimmed(text);
    while (index < 4) {
        const std::size_t comma = rest.find(L',');
        const std::wstring_view part = trimmed(rest.substr(0, comma));
        const auto value = parseSigned(part);
        if (!value) {
            return std::nullopt;
        }
        values[index++] = *value;
        if (comma == std::wstring_view::npos) {
            break;
        }
        rest = rest.substr(comma + 1);
        if (index == 4) {
            return std::nullopt; // a fifth value
        }
    }
    if (index != 4 || values[2] <= 0 || values[3] <= 0) {
        return std::nullopt;
    }
    return WindowRect{values[0], values[1], values[2], values[3]};
}

std::string describe(const Request& request) {
    char text[48];
    switch (request.mode) {
    case Mode::Off:
        return "off";
    case Mode::Auto:
        std::snprintf(text, sizeof(text), "auto x%.2f", static_cast<double>(request.scale));
        return text;
    case Mode::Fixed:
        std::snprintf(text, sizeof(text), "%ux%u", request.fixed.width, request.fixed.height);
        return text;
    }
    return "?";
}

} // namespace evr::render_size
