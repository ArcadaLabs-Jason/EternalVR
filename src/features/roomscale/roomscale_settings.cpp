#include "features/roomscale/roomscale_settings.hpp"

#include "common/parse_float.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>

namespace evr::roomscale {

namespace {

std::string trimmedLower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
    const auto first = out.find_first_not_of(" \t");
    const auto last = out.find_last_not_of(" \t");
    return first == std::string::npos ? std::string{} : out.substr(first, last - first + 1);
}

// Comma-separated numbers; nullopt if any part is not one.
std::optional<std::vector<float>> numbers(std::string_view text) {
    std::vector<float> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = std::min(text.find(',', start), text.size());
        const auto value = parseFloat(trimmedLower(text.substr(start, comma - start)));
        if (!value) {
            return std::nullopt;
        }
        out.push_back(*value);
        start = comma + 1;
    }
    return out;
}

class Reader {
public:
    Reader(const RoomScaleLookup& lookup, std::vector<RoomScaleIssue>& issues)
        : lookup_(lookup), issues_(issues) {}

    std::optional<std::string> get(std::string_view name) {
        const auto value = lookup_(name);
        if (!value) {
            return std::nullopt;
        }
        std::string v = trimmedLower(*value);
        if (v.empty()) {
            return std::nullopt;
        }
        raw_ = *value;
        return v;
    }

    void report(std::string_view name, std::string message) {
        issues_.push_back({std::string(name), raw_, std::move(message)});
    }

    void flag(std::string_view name, bool& out) {
        const auto v = get(name);
        if (!v) {
            return;
        }
        if (*v == "1" || *v == "on" || *v == "true" || *v == "yes") {
            out = true;
        } else if (*v == "0" || *v == "off" || *v == "false" || *v == "no") {
            out = false;
        } else {
            report(name, "expected 1 or 0; the default is kept");
        }
    }

    // A number in [min, max], or exactly `off` when `allowOff`.
    void number(std::string_view name, float min, float max, float& out, std::optional<float> off = {}) {
        const auto v = get(name);
        if (!v) {
            return;
        }
        const auto n = parseFloat(*v);
        if (n && off && *n == *off) {
            out = *n;
        } else if (n && *n >= min && *n <= max) {
            out = *n;
        } else {
            report(name, "expected a number from " + std::to_string(min) + " to " + std::to_string(max) +
                             "; the default is kept");
        }
    }

private:
    const RoomScaleLookup& lookup_;
    std::vector<RoomScaleIssue>& issues_;
    std::string raw_;
};

} // namespace

RoomScaleSettingsResult parseRoomScaleSettings(const RoomScaleLookup& lookup) {
    RoomScaleSettingsResult result;
    RoomScaleSettings& s = result.settings;
    Reader r(lookup, result.issues);

    if (const auto v = r.get("ETERNALVR_POSTURE")) {
        if (*v == "auto") {
            s.posture = posture::PostureOverride::Auto;
        } else if (*v == "seated") {
            s.posture = posture::PostureOverride::Seated;
        } else if (*v == "standing") {
            s.posture = posture::PostureOverride::Standing;
        } else {
            r.report("ETERNALVR_POSTURE", "expected auto, seated or standing; auto is kept");
        }
    }
    if (const auto v = r.get("ETERNALVR_HEIGHT")) {
        if (*v == "slayer") {
            s.height = HeightMode::Slayer;
        } else if (*v == "real") {
            s.height = HeightMode::Real;
        } else {
            r.report("ETERNALVR_HEIGHT", "expected slayer or real; slayer is kept");
        }
    }
    r.flag("ETERNALVR_AUTO_ANCHOR", s.autoAnchor);
    r.number("ETERNALVR_RECENTER_HOLD", 0.3f, 5.0f, s.recenterHoldSeconds, 0.0f);
    r.number("ETERNALVR_LEAN_CAP", 0.05f, 2.0f, s.limits.leanCapMetres);
    r.flag("ETERNALVR_HEAD_COLLISION", s.collision);
    r.flag("ETERNALVR_HEAD_FADE", s.fade);
    float ipdMillimetres = 0.0f;
    r.number("ETERNALVR_IPD", 50.0f, 80.0f, ipdMillimetres, 0.0f);
    s.ipdMetres = ipdMillimetres / 1000.0f;
    r.flag("ETERNALVR_BODY_FOLLOW", s.follow.enabled);
    r.number("ETERNALVR_BODY_FOLLOW_DEADZONE", 0.01f, 0.5f, s.follow.deadzoneMetres);
    float walk = static_cast<float>(s.follow.walkCommand);
    r.number("ETERNALVR_BODY_FOLLOW_WALK", 30.0f, 127.0f, walk);
    s.follow.walkCommand = static_cast<int>(std::lround(walk));
    float creep = static_cast<float>(s.follow.creepCommand);
    r.number("ETERNALVR_BODY_FOLLOW_CREEP", 20.0f, 127.0f, creep);
    s.follow.creepCommand = static_cast<int>(std::lround(creep));
    r.number("ETERNALVR_BODY_FOLLOW_COAST", 0.0f, 0.5f, s.follow.coastSeconds);
    r.number("ETERNALVR_BODY_FOLLOW_SPEED", 0.5f, 5.0f, s.follow.maxSpeed);
    if (const auto v = r.get("ETERNALVR_TEST_HEAD_OFFSET")) {
        const auto n = numbers(*v);
        const bool sizeOk = n && (n->size() == 3 || n->size() == 4);
        const bool inRange =
            sizeOk && std::all_of(n->begin(), n->begin() + 3, [](float x) { return std::fabs(x) <= 3.0f; });
        const bool periodOk = sizeOk && (n->size() == 3 || ((*n)[3] >= 0.0f && (*n)[3] <= 600.0f));
        if (inRange && periodOk) {
            s.testOffset = TestHeadOffset{{(*n)[0], (*n)[1], (*n)[2]}, n->size() == 4 ? (*n)[3] : 0.0f};
        } else {
            r.report("ETERNALVR_TEST_HEAD_OFFSET", "expected x,y,z[,period] in metres (up to 3) and seconds");
        }
    }
    r.number("ETERNALVR_TEST_RECENTER", 0.0f, 3600.0f, s.testRecenterSeconds);
    if (const auto v = r.get("ETERNALVR_TEST_STEPS")) {
        const auto n = numbers(*v);
        if (n && std::all_of(n->begin(), n->end(),
                             [](float x) { return std::fabs(x) >= 0.005f && std::fabs(x) <= 1.0f; })) {
            s.testSteps = TestSteps{*n};
        } else {
            r.report("ETERNALVR_TEST_STEPS",
                     "expected step sizes in metres (0.005 to 1), separated by commas");
        }
    }
    if (const auto v = r.get("ETERNALVR_TEST_MOVE")) {
        const auto n = numbers(*v);
        if (n && std::all_of(n->begin(), n->end(), [](float x) {
                return std::fabs(x) >= 1.0f && std::fabs(x) <= 127.0f && x == std::floor(x);
            })) {
            s.testSteps = TestSteps{*n};
            s.testSteps->commands = true;
        } else {
            r.report("ETERNALVR_TEST_MOVE", "expected move command values (whole numbers 1 to 127, negative "
                                            "backward), separated by commas");
        }
    }
    if (s.testSteps) {
        r.number("ETERNALVR_TEST_STEP_SECONDS", 0.5f, 60.0f, s.testSteps->holdSeconds);
        if (const auto v = r.get("ETERNALVR_TEST_STEP_AXIS")) {
            if (*v == "forward" || *v == "right") {
                s.testSteps->sideways = *v == "right";
            } else {
                r.report("ETERNALVR_TEST_STEP_AXIS", "expected forward or right; forward is kept");
            }
        }
    }
    return result;
}

Vec3 testOffsetAt(const TestHeadOffset& offset, double seconds) {
    if (!(offset.periodSeconds > 0.0f) || !std::isfinite(seconds)) {
        return offset.metres;
    }
    const double phase = 2.0 * std::numbers::pi * seconds / static_cast<double>(offset.periodSeconds);
    return offset.metres * static_cast<float>(0.5 * (1.0 - std::cos(phase)));
}

const char* postureOverrideName(posture::PostureOverride value) {
    switch (value) {
    case posture::PostureOverride::Seated:
        return "seated";
    case posture::PostureOverride::Standing:
        return "standing";
    case posture::PostureOverride::Auto:
        break;
    }
    return "auto";
}

const char* postureName(posture::Posture value) {
    switch (value) {
    case posture::Posture::Seated:
        return "seated";
    case posture::Posture::Standing:
        return "standing";
    case posture::Posture::Unknown:
        break;
    }
    return "unknown";
}

const char* heightModeName(HeightMode value) {
    return value == HeightMode::Real ? "real" : "slayer";
}

} // namespace evr::roomscale
