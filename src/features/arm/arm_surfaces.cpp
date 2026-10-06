// The arms' surfaces on the hands model (arm_surfaces.hpp).

#include "features/arm/arm_surfaces.hpp"

#include <array>

namespace evr::arm {

namespace {

constexpr std::array<std::string_view, 1> kRightSurfaces{"arm_low_rt_base"};
constexpr std::array<std::string_view, 3> kLeftSurfaces{"arm_low_lf_base", "armor_hand_01_low_lf_base",
                                                        "armor_hand_02_low_lf_base"};

constexpr char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}

std::string_view beforeDollar(std::string_view s) {
    const std::size_t at = s.find('$');
    return at == std::string_view::npos ? s : s.substr(0, at);
}

} // namespace

std::span<const std::string_view> armSurfaceNames(ArmSide side) {
    if (side == ArmSide::Right) {
        return kRightSurfaces;
    }
    return kLeftSurfaces;
}

std::string_view armKitName(ArmSide side) {
    return side == ArmSide::Right ? "ArmRight" : "ArmLeft";
}

const char* armLabel(ArmSide side) {
    return side == ArmSide::Right ? "right arm" : "left arm";
}

bool equalsIgnoringCase(std::string_view a, std::string_view b) {
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

bool surfaceNameIs(std::string_view stored, std::string_view wanted) {
    const std::string_view name = beforeDollar(stored);
    return !name.empty() && equalsIgnoringCase(name, beforeDollar(wanted));
}

std::optional<SurfaceBit> surfaceBit(std::int32_t surface) {
    if (surface < 0 || surface >= kMaxSurfaces) {
        return std::nullopt;
    }
    return SurfaceBit{static_cast<std::size_t>(surface) >> 5,
                      1u << (static_cast<std::uint32_t>(surface) & 31u)};
}

SurfacePlan planSurface(bool posed, bool visible, bool ours) {
    if (posed) {
        return visible ? SurfacePlan{SurfaceStep::None, ours} : SurfacePlan{SurfaceStep::Show, true};
    }
    if (ours && visible) {
        return {SurfaceStep::Hide, false};
    }
    return {SurfaceStep::None, false};
}

SurfacePlan planHiddenSurface(bool visible, bool ours) {
    return visible ? SurfacePlan{SurfaceStep::Hide, true} : SurfacePlan{SurfaceStep::None, ours};
}

const char* surfaceStateName(SurfaceState state) {
    switch (state) {
    case SurfaceState::Off:
        return "off";
    case SurfaceState::Hidden:
        return "hidden by the weapon's kit";
    case SurfaceState::Shown:
        return "shown";
    case SurfaceState::Games:
        return "the game's";
    case SurfaceState::Removed:
        return "hidden (arms hidden)";
    }
    return "?";
}

} // namespace evr::arm
