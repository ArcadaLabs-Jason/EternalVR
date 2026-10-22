#include "platform/mp_policy/profile_cvars.hpp"

#include "common/parse_float.hpp"
#include "platform/mp_policy/saved_cvars.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::mp_policy {

const std::vector<std::string_view>& profileStoredCvars() {
    static const std::vector<std::string_view> cvars{"view_skipShakes", "r_sharpening", "swf_anchorProfile"};
    return cvars;
}

bool profileStoredCvar(std::string_view name) {
    for (const std::string_view c : profileStoredCvars()) {
        if (sameCvarName(c, name)) {
            return true;
        }
    }
    return false;
}

bool sameCvarValue(std::string_view a, std::string_view b) {
    // The engine keeps a float: compared at its precision.
    const std::optional<float> x = parseFloat(a);
    const std::optional<float> y = parseFloat(b);
    if (x && y) {
        return *x == *y;
    }
    return sameCvarName(a, b);
}

std::optional<std::string> profileLoadSeed(std::string_view live,
                                           const std::optional<std::string>& beforeLayer) {
    if (!beforeLayer || sameCvarValue(live, *beforeLayer)) {
        return std::nullopt;
    }
    return *beforeLayer;
}

ProfileWrite profileWrite(std::string_view live,
                          const std::optional<std::string>& loaded,
                          const std::optional<std::string>& beforeLayer) {
    ProfileWrite w;
    if (!beforeLayer) {
        w.source = "the layer has not written it";
        return w;
    }
    w.value = loaded ? *loaded : *beforeLayer;
    w.source =
        loaded ? "the profile's load" : "no profile load seen; its value before the layer's first write";
    w.swap = !sameCvarValue(live, w.value);
    return w;
}

ProfileRepair profileRepair(bool requested, const std::optional<std::string>& loadedShakes) {
    if (!requested) {
        return ProfileRepair::Off;
    }
    return loadedShakes && sameCvarValue(*loadedShakes, "1") ? ProfileRepair::Repair
                                                             : ProfileRepair::NotNeeded;
}

YieldingHold::Step YieldingHold::step(std::string_view live, std::string_view held) {
    if (yielded_) {
        return Step::Keep;
    }
    if (sameCvarValue(live, held)) {
        confirmed_ = true;
        return Step::Keep;
    }
    if (confirmed_) {
        yielded_ = true;
        return Step::Yield;
    }
    return Step::Hold;
}

const char* profileRepairStatus(ProfileRepair repair, bool savedSince) {
    switch (repair) {
    case ProfileRepair::Off:
        return "off";
    case ProfileRepair::NotNeeded:
        return "not_needed";
    case ProfileRepair::Repair:
        return savedSince ? "done" : nullptr;
    }
    return nullptr;
}

} // namespace evr::mp_policy
