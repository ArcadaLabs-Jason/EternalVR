#include "stereo_seq/debug_cvars.hpp"

namespace evr::stereo_seq {

std::vector<std::string> debugCvarNames(std::string_view list) {
    std::vector<std::string> names;
    for (const CvarHold& c : parseCvarList(list)) {
        if (c.value == "?" || (c.value.rfind("<=", 0) == 0 && !parseCvarCap(c.value))) {
            continue;
        }
        names.push_back(c.name);
    }
    return names;
}

std::optional<std::size_t> findCvarName(const std::vector<std::string>& names, std::string_view name) {
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (sameCvarName(names[i], name)) {
            return i;
        }
    }
    return std::nullopt;
}

bool debugCvarsTakeSsr(const std::vector<std::string>& names) {
    return findCvarName(names, "r_SSR") ||
           findCvarName(names, "r_raytracedReflectionsTemporalUpscaleQuality");
}

} // namespace evr::stereo_seq
