#include "stereo_seq/desktop_window.hpp"

#include <algorithm>
#include <cwctype>
#include <string>

namespace evr::stereo_seq {

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

} // namespace

std::uint32_t stereoSwapchainImages(std::uint32_t gameMin,
                                    std::uint32_t wanted,
                                    std::uint32_t surfaceMin,
                                    std::uint32_t surfaceMax) {
    std::uint32_t count = std::max(gameMin, wanted);
    count = std::max(count, surfaceMin);
    if (surfaceMax != 0) {
        count = std::min(count, surfaceMax);
    }
    return count;
}

std::optional<std::uint32_t> parseSwapImages(std::wstring_view text) {
    const std::wstring t = lowerTrimmed(text);
    if (t.size() != 1 || t[0] < L'0' || t[0] > L'8') {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(t[0] - L'0');
}

std::optional<Mirror> parseMirror(std::wstring_view text) {
    const std::wstring t = lowerTrimmed(text);
    if (t == L"left" || t == L"l") {
        return Mirror::Left;
    }
    if (t == L"right" || t == L"r") {
        return Mirror::Right;
    }
    if (t == L"off" || t == L"0" || t == L"none") {
        return Mirror::Off;
    }
    return std::nullopt;
}

WindowPresents parseWindowPresents(std::wstring_view text) {
    const std::wstring t = lowerTrimmed(text);
    if (t == L"all") {
        return WindowPresents::All;
    }
    if (t == L"gated") {
        return WindowPresents::Gated;
    }
    return WindowPresents::Default;
}

bool windowGateWanted(WindowPresents setting, std::uint32_t vendorId) {
    constexpr std::uint32_t kNvidia = 0x10DE;
    switch (setting) {
    case WindowPresents::All:
        return false;
    case WindowPresents::Gated:
        return true;
    case WindowPresents::Default:
        break;
    }
    return vendorId == kNvidia;
}

const char* toString(Mirror mirror) {
    switch (mirror) {
    case Mirror::Left:
        return "left";
    case Mirror::Right:
        return "right";
    case Mirror::Off:
        return "off";
    }
    return "?";
}

MirrorStep mirrorStep(Mirror mirror, std::uint32_t eye) {
    switch (mirror) {
    case Mirror::Left:
        return eye == 0 ? MirrorStep::Store : MirrorStep::Load;
    case Mirror::Right:
        return eye == 0 ? MirrorStep::Load : MirrorStep::Store;
    case Mirror::Off:
        return MirrorStep::Clear;
    }
    return MirrorStep::None;
}

PanelMirror panelMirror(Mirror mirror, bool gated, PresentKind kind, bool toWindow) {
    if (mirror == Mirror::Off) {
        return PanelMirror{false, toWindow ? MirrorStep::Clear : MirrorStep::None};
    }
    const MirrorStep shown = toWindow ? MirrorStep::Load : MirrorStep::None;
    switch (kind) {
    case PresentKind::EyeL:
        // Eye R has no GUI (the engine draws it once per tick): when eye R's present may reach the window
        // (ungated, or gated with Right), eye L keeps the pair's GUI for it.
        return PanelMirror{toWindow || !gated || mirror == Mirror::Right, shown};
    case PresentKind::EyeR:
        return PanelMirror{false, shown};
    case PresentKind::Mono:
        break;
    }
    return PanelMirror{toWindow, shown};
}

bool WindowPresentGate::present(double seconds, double refreshHz, PresentKind kind, Mirror mirror) {
    const bool shownEye = kind == PresentKind::Mono ||
                          (kind == PresentKind::EyeL && mirror != Mirror::Right) ||
                          (kind == PresentKind::EyeR && mirror == Mirror::Right);
    if (!shownEye) {
        ++counters_.otherEye;
        return false;
    }
    const double hz = refreshHz > 0.0 ? refreshHz : 60.0;
    if (seconds - last_ < 2.0 / hz) {
        ++counters_.tooSoon;
        return false;
    }
    last_ = seconds;
    ++counters_.presented;
    return true;
}

} // namespace evr::stereo_seq
