#include "vkcore/game_settings_line.hpp"

#include <array>
#include <charconv>

namespace evr::vkcore::game_settings {

namespace {

using K = CvarKind;

constexpr SettingCvar kCvars[] = {
    // Ray tracing (Update 6): the switch, its reflections and their resolution (3 at Low to 1 from Ultra).
    {"r_enableRayTracing", K::Bool},
    {"r_raytracedReflections", K::Bool},
    {"r_raytracedReflectionsTemporalUpscaleQuality", K::Int},
    // Anti-aliasing and DLSS: r_antialiasing 2 is DLSS, r_dlssQuality its mode (0 Ultra Performance to 3
    // Quality).
    {"r_antialiasing", K::Int},
    {"r_dlssQuality", K::Int},
    {"r_dlssSharpness", K::Float},
    // Resolution scaling: mode, static scale and the dynamic target.
    {"r_enableResolutionScale", K::Bool},
    {"rs_enable", K::Int},
    {"rs_forceResolution", K::Float},
    {"rs_dropMilliseconds", K::Float},
    // Advanced: texture pool, then each quality setting by the cvars that tell its levels apart.
    {"is_poolSize", K::Int},
    {"r_shadowAtlasHeight", K::Int},
    {"r_shadowsDistanceFadeMultiplier", K::Float},
    {"r_lightDistanceFadeMultiplier", K::Float},
    {"r_particlesLightAtlasQuality", K::Int},
    {"r_particleFadeQualityMultiplier", K::Float},
    {"r_decalDistanceFadeMultiplier", K::Float},
    {"r_lightScatteringQuality", K::Int},
    {"r_SSDO", K::Int},
    {"r_SSDOQuality", K::Int},
    {"r_SSR", K::Int},
    {"r_SSRQuality", K::Int},
    {"r_lodScale", K::Float},
    {"r_materialAniso", K::Float},
    {"r_waterGridResolution", K::Int},
    // Post-processing.
    {"r_motionblur", K::Int},
    {"r_motionBlurQuality", K::Int},
    {"r_dof", K::Int},
    {"r_chromaticAberration", K::Bool},
    {"r_filmGrainRatio", K::Float},
    {"r_sharpening", K::Float},
    // Display and view.
    {"r_hdrDisplay", K::Bool},
    {"r_swapInterval", K::Int},
    {"r_fullscreen", K::Int},
    {"g_fov", K::Int},
};

} // namespace

std::span<const SettingCvar> settingCvars() {
    return kCvars;
}

std::string formatValue(CvarKind kind, int integer, float number) {
    if (kind == CvarKind::Bool) {
        return integer != 0 ? "1" : "0";
    }
    if (kind == CvarKind::Int) {
        return std::to_string(integer);
    }
    std::array<char, 32> text{};
    const auto result =
        std::to_chars(text.data(), text.data() + text.size(), number, std::chars_format::general, 6);
    if (result.ec != std::errc{}) {
        return "?";
    }
    return std::string(text.data(), result.ptr);
}

std::string formatLine(const std::vector<SettingValue>& values) {
    std::string line;
    for (const SettingValue& v : values) {
        if (v.state == SettingValue::State::NotFound) {
            continue;
        }
        line += line.empty() ? "" : ", ";
        line += v.name;
        line += ' ';
        line += v.state == SettingValue::State::Read ? formatValue(v.kind, v.integer, v.number) : "?";
    }
    return line;
}

std::string notFoundList(const std::vector<SettingValue>& values) {
    std::string list;
    for (const SettingValue& v : values) {
        if (v.state == SettingValue::State::NotFound) {
            list += list.empty() ? "" : ", ";
            list += v.name;
        }
    }
    return list;
}

bool LineSchedule::due(std::uint32_t mapLoads, std::uint64_t nowMs) {
    if (!started_) {
        started_ = true;
        startMs_ = nowMs;
    }
    if (mapLoads != loads_) {
        // A map load restarts the wait, also one that comes while the last is still settling.
        loads_ = mapLoads;
        pending_ = true;
        loadMs_ = nowMs;
    }
    if (pending_) {
        if (nowMs - loadMs_ < kSettleMs) {
            return false;
        }
        pending_ = false;
        afterLoad_ = true;
        readMs_ = nowMs;
        return true;
    }
    if (!logged_) {
        if (loads_ != 0 || nowMs - startMs_ < kFallbackMs) {
            return false;
        }
        afterLoad_ = true; // no map load seen: the first read is logged like one
        readMs_ = nowMs;
        return true;
    }
    if (nowMs - readMs_ < kCheckMs) {
        return false;
    }
    readMs_ = nowMs;
    return true;
}

bool LineSchedule::shouldLog(const std::string& line) {
    const bool log = afterLoad_ || line != last_;
    afterLoad_ = false;
    if (log) {
        last_ = line;
        logged_ = true;
    }
    return log;
}

} // namespace evr::vkcore::game_settings
