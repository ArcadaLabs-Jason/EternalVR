#include "features/render_size/edge_margin.hpp"

#include <cmath>
#include <cstdio>
#include <cwctype>

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

const char* sizeName(float factor) {
    if (factor == kEdgeMarginSmall) {
        return "small";
    }
    if (factor == kEdgeMarginMedium) {
        return "medium";
    }
    if (factor == kEdgeMarginLarge) {
        return "large";
    }
    return nullptr;
}

} // namespace

std::optional<float> parseEdgeMargin(std::wstring_view text) {
    const std::wstring t = lowerTrimmed(text);
    if (t.empty() || t == L"off" || t == L"none" || t == L"0") {
        return 1.0f;
    }
    if (t == L"small") {
        return kEdgeMarginSmall;
    }
    if (t == L"medium") {
        return kEdgeMarginMedium;
    }
    if (t == L"large") {
        return kEdgeMarginLarge;
    }
    if (t.size() > 2) {
        return std::nullopt;
    }
    int percent = 0;
    for (const wchar_t c : t) {
        if (c < L'0' || c > L'9') {
            return std::nullopt;
        }
        percent = percent * 10 + (c - L'0');
    }
    if (percent < 1 || percent > kMaxEdgeMarginPercent) {
        return std::nullopt;
    }
    return 1.0f + static_cast<float>(percent) / 100.0f;
}

std::string describeEdgeMargin(float factor) {
    if (!(factor > 1.0f)) {
        return "off";
    }
    char text[32];
    if (const char* name = sizeName(factor)) {
        std::snprintf(text, sizeof(text), "x%.2f (%s)", static_cast<double>(factor), name);
    } else {
        std::snprintf(text, sizeof(text), "x%.2f", static_cast<double>(factor));
    }
    return text;
}

} // namespace evr::render_size
