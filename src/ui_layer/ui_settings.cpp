#include "ui_layer/ui_settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cwctype>

namespace evr::ui_layer {

namespace {

std::wstring_view trim(std::wstring_view s) {
    while (!s.empty() && std::iswspace(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::iswspace(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

bool equalsNoCase(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) {
            return false;
        }
    }
    return true;
}

std::string narrow(std::wstring_view s) {
    std::string out;
    for (const wchar_t c : s) {
        out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return out;
}

// A switch: "0" or "1".
void readSwitch(const EnvLookup& env, const wchar_t* name, bool& value, std::vector<std::string>& warnings) {
    const std::optional<std::wstring> text = env(name);
    if (!text) {
        return;
    }
    const std::wstring_view t = trim(*text);
    if (t == L"1") {
        value = true;
    } else if (t == L"0") {
        value = false;
    } else {
        warnings.push_back(narrow(name) + " is not 0 or 1; the default is kept");
    }
}

// A finite number in [low, high].
void readNumber(const EnvLookup& env,
                const wchar_t* name,
                float low,
                float high,
                float& value,
                std::vector<std::string>& warnings) {
    const std::optional<std::wstring> text = env(name);
    if (!text) {
        return;
    }
    const std::wstring t(trim(*text));
    wchar_t* end = nullptr;
    const double v = t.empty() ? 0.0 : std::wcstod(t.c_str(), &end);
    if (t.empty() || end != t.c_str() + t.size() || !std::isfinite(v) || v < low || v > high) {
        warnings.push_back(narrow(name) + " is not a number between " + std::to_string(low) + " and " +
                           std::to_string(high) + "; the default is kept");
        return;
    }
    value = static_cast<float>(v);
}

} // namespace

UiSettings readUiSettings(const EnvLookup& env, std::vector<std::string>& warnings) {
    UiSettings s;
    // On by default with Route S (ETERNALVR_MODE=stereo without an experiment), where eye R has no GUI of
    // its own; off otherwise. ETERNALVR_UI_LAYER=0 or 1 decides either way.
    const std::optional<std::wstring> mode = env(L"ETERNALVR_MODE");
    const std::optional<std::wstring> experiment = env(L"ETERNALVR_STEREO_EXPERIMENT");
    s.enabled = mode && equalsNoCase(trim(*mode), L"stereo") && (!experiment || trim(*experiment).empty());
    readSwitch(env, L"ETERNALVR_UI_LAYER", s.enabled, warnings);
    readSwitch(env, L"ETERNALVR_UI_SKIP_COMPOSITE", s.skipComposite, warnings);
    readNumber(env, L"ETERNALVR_UI_DISTANCE", 0.3f, 10.0f, s.distanceMetres, warnings);
    readNumber(env, L"ETERNALVR_UI_WIDTH", 0.1f, 10.0f, s.widthMetres, warnings);
    readNumber(env, L"ETERNALVR_UI_OFFSET_Y", -2.0f, 2.0f, s.offsetYMetres, warnings);
    readSwitch(env, L"ETERNALVR_UI_RETICLE", s.reticle, warnings);
    readNumber(env, L"ETERNALVR_UI_RETICLE_DISTANCE", 0.5f, 100.0f, s.reticleDistanceMetres, warnings);
    readNumber(env, L"ETERNALVR_UI_RETICLE_SIZE", 0.1f, 10.0f, s.reticleDegrees, warnings);
    readSwitch(env, L"ETERNALVR_MENU_POINTER", s.menuPointer, warnings);
    s.menuDistanceMetres = s.distanceMetres;
    s.menuWidthMetres = s.widthMetres;
    readNumber(env, L"ETERNALVR_MENU_DISTANCE", 0.3f, 10.0f, s.menuDistanceMetres, warnings);
    readNumber(env, L"ETERNALVR_MENU_WIDTH", 0.1f, 10.0f, s.menuWidthMetres, warnings);
    readSwitch(env, L"ETERNALVR_MENU_BEAM", s.menuBeam, warnings);
    readSwitch(env, L"ETERNALVR_UI_CROP", s.wideCrop, warnings);
    readSwitch(env, L"ETERNALVR_UI_WASH", s.removeWash, warnings);
    return s;
}

std::optional<PanelSize> panelSize(float widthMetres, std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || !(widthMetres > 0.0f)) {
        return std::nullopt;
    }
    return PanelSize{widthMetres, widthMetres * static_cast<float>(height) / static_cast<float>(width)};
}

PixelRect wideContentRect(std::uint32_t width, std::uint32_t height) {
    // 16:9 is 1.7778; a target at most 0.5 % taller counts as 16:9 (rounding of window sizes).
    const double bandHeight = std::round(static_cast<double>(width) * 9.0 / 16.0);
    if (width == 0 || height == 0 || static_cast<double>(height) <= bandHeight * 1.005) {
        return PixelRect{0, 0, width, height};
    }
    const auto band = static_cast<std::uint32_t>(bandHeight);
    return PixelRect{0, static_cast<std::int32_t>((height - band) / 2), width, band};
}

ImageUv contentToImage(float u, float v, const PixelRect& rect, std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return ImageUv{u, v};
    }
    return ImageUv{
        (static_cast<float>(rect.x) + u * static_cast<float>(rect.width)) / static_cast<float>(width),
        (static_cast<float>(rect.y) + v * static_cast<float>(rect.height)) / static_cast<float>(height)};
}

std::vector<std::uint8_t> reticleImage(std::uint32_t size) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(size) * size * 4, 0);
    const float centre = static_cast<float>(size) / 2.0f;
    const float outer = centre;         // the ring's outer edge
    const float inner = centre * 0.62f; // the ring's inner edge = the dot's edge
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const float dx = static_cast<float>(x) + 0.5f - centre;
            const float dy = static_cast<float>(y) + 0.5f - centre;
            const float r = std::sqrt(dx * dx + dy * dy);
            // One pixel of coverage falloff at each edge.
            const float dot = std::clamp(inner - r + 0.5f, 0.0f, 1.0f);
            const float ring = std::clamp(outer - r + 0.5f, 0.0f, 1.0f) - dot;
            const float alpha = dot + ring * 0.7f;
            const auto a = static_cast<std::uint8_t>(std::lround(alpha * 255.0f));
            const auto c = static_cast<std::uint8_t>(std::lround(dot * 255.0f)); // premultiplied white
            std::uint8_t* p = px.data() + (static_cast<std::size_t>(y) * size + x) * 4;
            p[0] = c;
            p[1] = c;
            p[2] = c;
            p[3] = a;
        }
    }
    return px;
}

std::vector<PixelRect>
copyRegionsWithoutCentre(std::uint32_t width, std::uint32_t height, float maskFraction) {
    const auto side =
        static_cast<std::uint32_t>(std::lround(std::max(0.0f, maskFraction) * static_cast<float>(height)));
    if (side == 0 || side >= width || side >= height) {
        return {PixelRect{0, 0, width, height}};
    }
    const std::uint32_t left = (width - side) / 2;
    const std::uint32_t top = (height - side) / 2;
    const auto x = [](std::uint32_t v) {
        return static_cast<std::int32_t>(v);
    };
    return {
        PixelRect{0, 0, width, top},                                  // above
        PixelRect{0, x(top + side), width, height - top - side},      // below
        PixelRect{0, x(top), left, side},                             // left of the square
        PixelRect{x(left + side), x(top), width - left - side, side}, // right of it
    };
}

float reticleSideMetres(float distance, float degrees) {
    constexpr float kPi = 3.14159265358979f;
    return 2.0f * distance * std::tan(degrees * kPi / 360.0f);
}

} // namespace evr::ui_layer
