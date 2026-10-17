#include "game/eternal/zoom_look.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>

namespace evr::game {

namespace {

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

bool sameNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<ZoomLook> parseZoomLook(std::string_view text) {
    const std::string_view t = trimmed(text);
    if (sameNoCase(t, "all")) {
        return ZoomLook{};
    }
    if (sameNoCase(t, "off") || sameNoCase(t, "game") || sameNoCase(t, "none")) {
        return ZoomLook{false, false};
    }
    ZoomLook look{false, false};
    std::string_view rest = t;
    while (true) {
        const std::size_t comma = rest.find(',');
        const std::string_view part = trimmed(rest.substr(0, comma));
        if (sameNoCase(part, "hands")) {
            look.keepHands = true;
        } else if (sameNoCase(part, "scope")) {
            look.noScopeGui = true;
        } else {
            return std::nullopt;
        }
        if (comma == std::string_view::npos) {
            return look;
        }
        rest = rest.substr(comma + 1);
    }
}

bool plausibleZoomFields(const ZoomDeclFields& fields) {
    // The gameTime is the milliseconds in the game's time units (the Bolt's 74 ms read 73997); a generous
    // bound, and 0 with 0 milliseconds.
    const bool delayOk = fields.hideHandsDelayMs >= 0 && fields.hideHandsDelayMs <= kHideDelayMsMax &&
                         fields.hideHandsDelay >= 0 &&
                         fields.hideHandsDelay <= std::int64_t{kHideDelayMsMax} * 10000 &&
                         (fields.hideHandsDelayMs > 0 || fields.hideHandsDelay == 0);
    return fields.reticle != 0 && fields.hideHandsOnZoom <= 1 && delayOk &&
           fields.scopeGuiNum >= kScopeGuiNone && fields.scopeGuiNum <= kScopeGuiLast;
}

bool zoomLookNeeded(const ZoomDeclFields& game) {
    return game.hideHandsOnZoom != 0 || game.scopeGuiNum != kScopeGuiNone;
}

ZoomDeclFields zoomLookFields(const ZoomDeclFields& game, const ZoomLook& look) {
    ZoomDeclFields out = game;
    if (!zoomLookNeeded(game)) {
        return out;
    }
    if (look.keepHands) {
        out.hideHandsOnZoom = 0;
        out.hideHandsDelay = 0;
        out.hideHandsDelayMs = 0;
    }
    if (look.noScopeGui && game.scopeGuiNum != kScopeGuiNone) {
        out.scopeGuiNum = kScopeGuiNone;
        if (game.reticle != 0 && game.reticleWhenZoomed != 0) {
            out.reticleWhenZoomed = game.reticle;
        }
    }
    return out;
}

bool scopeReticleHold(bool zoomed, bool scopedWeapon, const ZoomLook& look) {
    return zoomed && scopedWeapon && look.noScopeGui;
}

} // namespace evr::game
