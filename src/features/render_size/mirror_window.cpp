#include "features/render_size/mirror_window.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cwchar>
#include <cwctype>
#include <string>

namespace evr::render_size {

namespace {

constexpr std::int32_t kMinWindowSide = 64;
constexpr std::int32_t kMaxWindowSide = 8192;

std::wstring_view trim(std::wstring_view text) {
    while (!text.empty() && std::iswspace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::iswspace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

bool equalsIgnoringCase(std::wstring_view text, std::wstring_view word) {
    if (text.size() != word.size()) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (std::towlower(text[i]) != std::towlower(word[i])) {
            return false;
        }
    }
    return true;
}

// A number that fills `text` (decimal, optional sign and fraction), or nullopt.
std::optional<double> parseNumber(std::wstring_view text) {
    text = trim(text);
    if (text.empty() || text.size() > 16) {
        return std::nullopt;
    }
    const std::wstring copy(text);
    wchar_t* end = nullptr;
    const double value = std::wcstod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

// A whole number that fills `text`, or nullopt.
std::optional<std::int32_t> parseInteger(std::wstring_view text) {
    const auto value = parseNumber(text);
    if (!value || *value != std::floor(*value) || std::fabs(*value) > 1.0e7) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(*value);
}

bool contains(const WindowRect& r, std::int32_t x, std::int32_t y) {
    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}

const Monitor* displayAt(const std::vector<Monitor>& monitors, std::int32_t x, std::int32_t y) {
    for (const Monitor& m : monitors) {
        if (contains(m.area, x, y)) {
            return &m;
        }
    }
    return nullptr;
}

// `w` x `h` scaled down uniformly, if needed, to fit `bounds`.
void fitInto(std::int32_t& w, std::int32_t& h, const WindowRect& bounds) {
    if (bounds.width <= 0 || bounds.height <= 0 || (w <= bounds.width && h <= bounds.height)) {
        return;
    }
    // The limiting side takes the bound exactly; the other one follows, rounded down (integers, no drift).
    const auto W = static_cast<std::int64_t>(w);
    const auto H = static_cast<std::int64_t>(h);
    if (W * bounds.height >= H * bounds.width) {
        h = static_cast<std::int32_t>(std::max<std::int64_t>(1, H * bounds.width / W));
        w = bounds.width;
    } else {
        w = static_cast<std::int32_t>(std::max<std::int64_t>(1, W * bounds.height / H));
        h = bounds.height;
    }
}

// The display `display` names; nullptr for `launcher` or a display that does not exist.
const Monitor* chosenDisplay(const MirrorDisplay& display, const std::vector<Monitor>& monitors) {
    switch (display.choice) {
    case DisplayChoice::Launcher:
        return nullptr;
    case DisplayChoice::Primary:
        for (const Monitor& m : monitors) {
            if (m.primary) {
                return &m;
            }
        }
        return nullptr;
    case DisplayChoice::Number:
        if (display.number >= 1 && static_cast<std::size_t>(display.number) <= monitors.size()) {
            return &monitors[static_cast<std::size_t>(display.number - 1)];
        }
        return nullptr;
    case DisplayChoice::Point:
        return displayAt(monitors, display.x, display.y);
    }
    return nullptr;
}

// ETERNALVR_MIRROR_SIZE=fill: the whole area of the chosen display (the launcher's own for `launcher`).
MirrorPlacement
fillDisplay(const WindowRect& launcher, const MirrorDisplay& display, const std::vector<Monitor>& monitors) {
    const Monitor* target = display.choice == DisplayChoice::Launcher
                                ? displayAt(monitors, launcher.x, launcher.y)
                                : chosenDisplay(display, monitors);
    if (!target || target->area.width <= 0 || target->area.height <= 0) {
        return MirrorPlacement{launcher, false, false, 0};
    }
    const auto number = static_cast<std::int32_t>(target - monitors.data()) + 1;
    return MirrorPlacement{target->area, true, true, number};
}

} // namespace

std::optional<double> parseAspect(std::wstring_view text) {
    text = trim(text);
    if (equalsIgnoringCase(text, L"full") || equalsIgnoringCase(text, L"off") || text == L"0") {
        return 0.0;
    }
    const std::size_t colon = text.find(L':');
    if (colon == std::wstring_view::npos) {
        return std::nullopt;
    }
    const auto w = parseNumber(text.substr(0, colon));
    const auto h = parseNumber(text.substr(colon + 1));
    if (!w || !h || !(*w > 0.0) || !(*h > 0.0)) {
        return std::nullopt;
    }
    const double aspect = *w / *h;
    if (aspect < 1.0 || aspect > 4.0) {
        return std::nullopt;
    }
    return aspect;
}

Band centredBand(std::uint32_t width, std::uint32_t height, double aspect) {
    if (width == 0 || height == 0 || !(aspect > 0.0)) {
        return Band{0, height};
    }
    const double bandHeight = std::round(static_cast<double>(width) / aspect);
    if (static_cast<double>(height) <= bandHeight * 1.005 || bandHeight < 1.0) {
        return Band{0, height};
    }
    const auto band = static_cast<std::uint32_t>(bandHeight);
    return Band{(height - band) / 2, band};
}

std::optional<MirrorDisplay> parseMirrorDisplay(std::wstring_view text) {
    text = trim(text);
    if (text.empty() || equalsIgnoringCase(text, L"launcher")) {
        return MirrorDisplay{};
    }
    if (equalsIgnoringCase(text, L"primary")) {
        return MirrorDisplay{DisplayChoice::Primary, 0, 0, 0};
    }
    const std::size_t comma = text.find(L',');
    if (comma == std::wstring_view::npos) {
        const auto number = parseInteger(text);
        if (!number || *number < 1 || *number > 64) {
            return std::nullopt;
        }
        return MirrorDisplay{DisplayChoice::Number, *number, 0, 0};
    }
    const auto x = parseInteger(text.substr(0, comma));
    const auto y = parseInteger(text.substr(comma + 1));
    if (!x || !y) {
        return std::nullopt;
    }
    return MirrorDisplay{DisplayChoice::Point, 0, *x, *y};
}

std::optional<MirrorSize> parseMirrorSize(std::wstring_view text) {
    text = trim(text);
    if (equalsIgnoringCase(text, L"fill")) {
        return MirrorSize{Extent{}, 0.0f, true};
    }
    const std::size_t x = text.find_first_of(L"xX");
    if (x == std::wstring_view::npos) {
        const auto scale = parseNumber(text);
        if (!scale || *scale < 0.25 || *scale > 4.0) {
            return std::nullopt;
        }
        return MirrorSize{Extent{}, static_cast<float>(*scale)};
    }
    const auto w = parseInteger(text.substr(0, x));
    const auto h = parseInteger(text.substr(x + 1));
    if (!w || !h || *w < kMinWindowSide || *h < kMinWindowSide || *w > kMaxWindowSide ||
        *h > kMaxWindowSide) {
        return std::nullopt;
    }
    return MirrorSize{Extent{static_cast<std::uint32_t>(*w), static_cast<std::uint32_t>(*h)}, 0.0f};
}

MirrorPlacement placeMirror(const WindowRect& launcher,
                            const MirrorDisplay& display,
                            const std::optional<MirrorSize>& size,
                            double cropAspect,
                            const std::vector<Monitor>& monitors) {
    const bool crop = cropAspect > 0.0;
    if (!size && !crop && display.choice == DisplayChoice::Launcher) {
        return MirrorPlacement{launcher, true};
    }
    if (size && size->fill) {
        return fillDisplay(launcher, display, monitors);
    }
    std::int32_t w = launcher.width;
    std::int32_t h = launcher.height;
    if (size && size->scale > 0.0f) {
        w = static_cast<std::int32_t>(std::lround(w * static_cast<double>(size->scale)));
        h = static_cast<std::int32_t>(std::lround(h * static_cast<double>(size->scale)));
    } else if (size) {
        w = static_cast<std::int32_t>(size->size.width);
        h = static_cast<std::int32_t>(size->size.height);
    }
    w = std::clamp(w, kMinWindowSide, kMaxWindowSide);
    h = std::clamp(h, kMinWindowSide, kMaxWindowSide);
    if (crop) {
        // The largest rectangle of the crop's aspect inside the size asked for.
        if (static_cast<double>(w) / h > cropAspect) {
            w = static_cast<std::int32_t>(std::lround(h * cropAspect));
        } else {
            h = static_cast<std::int32_t>(std::lround(w / cropAspect));
        }
    }

    const Monitor* target = chosenDisplay(display, monitors);
    MirrorPlacement out;
    out.displayFound = target != nullptr || display.choice == DisplayChoice::Launcher;
    out.centred = target != nullptr;
    if (target) {
        fitInto(w, h, target->work);
        out.rect = WindowRect{target->work.x + (target->work.width - w) / 2,
                              target->work.y + (target->work.height - h) / 2, w, h};
        return out;
    }
    if (const Monitor* own = displayAt(monitors, launcher.x, launcher.y)) {
        fitInto(w, h, own->area);
    }
    out.rect = WindowRect{launcher.x, launcher.y, w, h};
    return out;
}

WindowRect keepFrameOnScreen(const WindowRect& outer, const WindowRect& workArea) {
    WindowRect moved = outer;
    moved.x = outer.x < workArea.x ? workArea.x : outer.x;
    moved.y = outer.y < workArea.y ? workArea.y : outer.y;
    return moved;
}

WindowRect shapeToImage(const WindowRect& rect, Extent image, bool centred) {
    if (image.width == 0 || image.height == 0 || rect.width <= 0 || rect.height <= 0) {
        return rect;
    }
    // Integers, as fitInto: the limiting side keeps the rectangle's, the other one follows, rounded.
    const auto W = static_cast<std::int64_t>(image.width);
    const auto H = static_cast<std::int64_t>(image.height);
    std::int32_t w = rect.width;
    std::int32_t h = rect.height;
    if (W * rect.height >= H * rect.width) {
        h = static_cast<std::int32_t>(std::clamp<std::int64_t>((H * rect.width + W / 2) / W, 1, rect.height));
    } else {
        w = static_cast<std::int32_t>(std::clamp<std::int64_t>((W * rect.height + H / 2) / H, 1, rect.width));
    }
    if (!centred) {
        return WindowRect{rect.x, rect.y, w, h};
    }
    return WindowRect{rect.x + (rect.width - w) / 2, rect.y + (rect.height - h) / 2, w, h};
}

} // namespace evr::render_size
