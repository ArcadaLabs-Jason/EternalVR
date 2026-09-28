#include "features/input/controller_settings.hpp"

#include "common/parse_float.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace evr::input {

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
    // Surrounding blanks are a common hand-editing slip; they never mean anything here.
    const auto first = out.find_first_not_of(" \t");
    const auto last = out.find_last_not_of(" \t");
    return first == std::string::npos ? std::string{} : out.substr(first, last - first + 1);
}

std::optional<float> number(std::string_view text) {
    return parseFloat(text);
}

class Reader {
public:
    Reader(const SettingLookup& lookup, std::vector<SettingsIssue>& issues)
        : lookup_(lookup), issues_(issues) {}

    // The value, lower-cased and trimmed; nullopt when unset or empty.
    std::optional<std::string> get(std::string_view name) {
        auto value = lookup_(name);
        if (!value) {
            return std::nullopt;
        }
        std::string v = lower(*value);
        if (v.empty()) {
            return std::nullopt;
        }
        raw_ = *value;
        return v;
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

    // One of `choices` (value, result); anything else is reported.
    template <typename T, std::size_t N>
    void choice(std::string_view name, const std::pair<const char*, T> (&choices)[N], T& out) {
        const auto v = get(name);
        if (!v) {
            return;
        }
        for (const auto& [text, value] : choices) {
            if (*v == text) {
                out = value;
                return;
            }
        }
        std::string expected;
        for (const auto& [text, value] : choices) {
            expected += expected.empty() ? "" : ", ";
            expected += text;
        }
        report(name, "expected one of " + expected + "; the default is kept");
    }

    // A number within [min, max]; outside it (or not a number) is reported and the default kept.
    void range(std::string_view name, float min, float max, float& out) {
        const auto v = get(name);
        if (!v) {
            return;
        }
        const auto n = number(*v);
        if (!n || *n < min || *n > max) {
            report(name, "expected a number from " + std::to_string(static_cast<int>(min)) + " to " +
                             std::to_string(static_cast<int>(max)) + "; the default is kept");
            return;
        }
        out = *n;
    }

    void report(std::string_view name, std::string message) {
        issues_.push_back({std::string(name), raw_, std::move(message)});
    }

    [[nodiscard]] const std::string& raw() const { return raw_; }

private:
    const SettingLookup& lookup_;
    std::vector<SettingsIssue>& issues_;
    std::string raw_;
};

} // namespace

ControllerSettingsResult parseControllerSettings(const SettingLookup& lookup) {
    ControllerSettingsResult result;
    ControllerSettings& s = result.settings;
    Reader r(lookup, result.issues);

    r.flag("ETERNALVR_CONTROLLERS", s.enabled);

    static constexpr std::pair<const char*, AimSource> kAim[] = {
        {"head", AimSource::Head}, {"hand", AimSource::Hand}, {"view", AimSource::View}};
    r.choice("ETERNALVR_AIM", kAim, s.aim);

    static constexpr std::pair<const char*, LocomotionFrame> kLocomotion[] = {
        {"head", LocomotionFrame::Head}, {"hand", LocomotionFrame::OffHand}};
    r.choice("ETERNALVR_LOCOMOTION", kLocomotion, s.locomotion);

    static constexpr std::pair<const char*, TurnMode> kTurn[] = {
        {"smooth", TurnMode::Smooth}, {"snap", TurnMode::Snap}, {"off", TurnMode::Off}};
    r.choice("ETERNALVR_TURN", kTurn, s.turn.mode);
    r.range("ETERNALVR_TURN_RATE", kMinSmoothTurnDegreesPerSecond, kMaxSmoothTurnDegreesPerSecond,
            s.turn.smoothDegreesPerSecond);
    r.range("ETERNALVR_SNAP_DEGREES", kMinSnapTurnDegrees, kMaxSnapTurnDegrees, s.turn.snapDegrees);

    static constexpr std::pair<const char*, game::Handedness> kHands[] = {
        {"right", game::Handedness::Right},
        {"left", game::Handedness::LeftButtonSwap},
        {"left_mirror", game::Handedness::LeftButtonAndStickSwap}};
    r.choice("ETERNALVR_HANDEDNESS", kHands, s.handedness);

    static constexpr std::pair<const char*, DossierPress> kDossier[] = {{"hold", DossierPress::Hold},
                                                                        {"tap", DossierPress::Tap}};
    r.choice("ETERNALVR_DOSSIER", kDossier, s.dossier);

    static constexpr std::pair<const char*, InputPath> kPath[] = {{"auto", InputPath::Auto},
                                                                  {"0", InputPath::UserCmd},
                                                                  {"off", InputPath::UserCmd},
                                                                  {"1", InputPath::XInput},
                                                                  {"on", InputPath::XInput}};
    r.choice("ETERNALVR_XINPUT", kPath, s.path);

    static constexpr std::pair<const char*, ShotOrigin> kShot[] = {{"hand", ShotOrigin::Hand},
                                                                   {"eye", ShotOrigin::Eye}};
    r.choice("ETERNALVR_SHOT_ORIGIN", kShot, s.shotOrigin);
    r.range("ETERNALVR_AIM_SMOOTHING", 0.0f, 1.0f, s.aimSmoothing);

    r.flag("ETERNALVR_VIEWMODEL", s.viewmodel);
    r.flag("ETERNALVR_WEAPON_FOV", s.weaponFov);
    r.flag("ETERNALVR_SEATED", s.seated);
    r.flag("ETERNALVR_CONTROLLERS_TRACE", s.trace);

    if (r.get("ETERNALVR_VIEWMODEL_OFFSET")) {
        game::WeaponOffset offset;
        if (game::parseOffsetList(r.raw(), offset)) {
            s.viewmodelOffset = offset;
        } else {
            r.report("ETERNALVR_VIEWMODEL_OFFSET",
                     "expected forward,left,up[,pitch,yaw,roll] (metres, degrees); the table is used");
        }
    }
    if (const auto path = lookup("ETERNALVR_CONTROLLER_DATA"); path && !path->empty()) {
        s.controllerDataPath = *path;
    }
    if (const auto path = lookup("ETERNALVR_TEST_INPUT"); path && !path->empty()) {
        s.testInputPath = *path;
    }
    return result;
}

const char* aimSourceName(AimSource aim) {
    switch (aim) {
    case AimSource::Head:
        return "head";
    case AimSource::Hand:
        return "hand";
    case AimSource::View:
        return "view";
    }
    return "head";
}

const char* inputPathName(InputPath path) {
    switch (path) {
    case InputPath::Auto:
        return "user command (virtual gamepad if its hooks fail)";
    case InputPath::UserCmd:
        return "user command";
    case InputPath::XInput:
        return "virtual gamepad";
    }
    return "user command";
}

} // namespace evr::input
