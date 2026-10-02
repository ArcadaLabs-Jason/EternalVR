#include "features/input/binding_watch.hpp"

#include <algorithm>

namespace evr::input {

namespace {

constexpr std::string_view kLeftPrefix = "/user/hand/left/";
constexpr std::string_view kRightPrefix = "/user/hand/right/";

} // namespace

BindingWatchActions BindingWatch::update(const BindingWatchSample& sample) {
    BindingWatchActions out;
    const double step = last_ ? sample.seconds - *last_ : 0.0;
    last_ = sample.seconds;
    const bool anyProfile = sample.profile[0] || sample.profile[1];
    if (sample.synced && (sample.poseValid[0] || sample.poseValid[1])) {
        poseSeen_ = true;
    }

    // The profile's settle time runs while a profile is reported; a sync without focus leaves it alone.
    if (sample.synced) {
        if (!anyProfile) {
            profileSince_.reset();
        } else if (!profileSince_) {
            profileSince_ = sample.seconds;
        }
    }
    if (!listed_ && profileSince_ && sample.seconds - *profileSince_ >= kBindingSettleSeconds) {
        listed_ = true;
        out.listSources = true;
    }

    // Only time between two counted syncs adds up, so a pause or a lost focus never does.
    const bool counts = sample.synced && sample.headTracked && anyProfile && !poseSeen_;
    if (counts && waitingPrev_ && step >= 0.0 && step <= kBindingWatchMaxStep) {
        waiting_ += step;
    }
    waitingPrev_ = counts;
    if (!warned_ && waiting_ >= kPosesNeverValidSeconds) {
        warned_ = true;
        out.posesNeverValid = true;
    }
    return out;
}

std::optional<Hand> sourceHand(std::string_view path) {
    if (path.starts_with(kLeftPrefix)) {
        return Hand::Left;
    }
    if (path.starts_with(kRightPrefix)) {
        return Hand::Right;
    }
    return std::nullopt;
}

BindingVerdict judgeBoundSources(std::span<const BoundAction> actions) {
    BindingVerdict verdict;
    for (const BoundAction& action : actions) {
        ++verdict.actions;
        if (!action.sources.empty()) {
            ++verdict.boundActions;
        }
        for (const Hand hand : {Hand::Left, Hand::Right}) {
            const bool onHand = std::ranges::any_of(
                action.sources, [hand](const std::string& source) { return sourceHand(source) == hand; });
            if (!onHand) {
                verdict.unbound[static_cast<std::size_t>(hand)].push_back(action.id);
            }
        }
    }
    return verdict;
}

std::string actionNames(std::span<const XrActionId> ids) {
    if (ids.empty()) {
        return "none";
    }
    std::string out;
    for (const XrActionId id : ids) {
        out += out.empty() ? "" : ", ";
        out += xrAction(id).name;
    }
    return out;
}

std::string_view unboundAdvice(std::string_view runtimeName) {
    if (runtimeName.find("SteamVR") != std::string_view::npos) {
        return "SteamVR is using a custom or workshop controller binding for DOOM Eternal; open SteamVR > "
               "Settings > Controllers > Manage Controller Bindings, pick DOOM Eternal and choose the "
               "default binding";
    }
    return "the headset runtime binds the controllers to other actions; reset its controller bindings for "
           "DOOM Eternal to the default";
}

} // namespace evr::input
