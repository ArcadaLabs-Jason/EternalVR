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

void readThumbRest(Reader& r, ControllerSettings& s) {
    static constexpr std::pair<const char*, RestWheelMode> kModes[] = {{"edge", RestWheelMode::Edge},
                                                                       {"full", RestWheelMode::Full},
                                                                       {"extreme", RestWheelMode::Extreme},
                                                                       {"off", RestWheelMode::Off},
                                                                       {"0", RestWheelMode::Off}};
    r.choice("ETERNALVR_THUMBREST_WHEEL", kModes, s.thumbRest.mode);
    static constexpr std::pair<const char*, RestWheelPick> kPicks[] = {{"wheel", RestWheelPick::Wheel},
                                                                       {"slots", RestWheelPick::Slots}};
    r.choice("ETERNALVR_THUMBREST_PICK", kPicks, s.thumbRest.pick);
    r.flag("ETERNALVR_THUMBREST_FACE_TOUCH", s.thumbRestFaceTouch);
    r.flag("ETERNALVR_THUMBREST_SLOWDOWN", s.thumbRestSlowdown);
    if (const auto v = r.get("ETERNALVR_THUMBREST_WINDOW")) {
        const auto n = number(*v);
        if (n && *n >= kMinEdgeWindowSeconds && *n <= kMaxEdgeWindowSeconds) {
            s.thumbRest.windowSeconds = *n;
        } else {
            r.report("ETERNALVR_THUMBREST_WINDOW", "expected seconds from 0.2 to 1; the default is kept");
        }
    }
    if (const auto v = r.get("ETERNALVR_THUMBREST_HOLDOVER")) {
        const auto n = number(*v);
        if (n && *n >= kMinRestHoldOverSeconds && *n <= kMaxRestHoldOverSeconds) {
            s.thumbRest.holdOverSeconds = *n;
        } else {
            r.report("ETERNALVR_THUMBREST_HOLDOVER", "expected seconds from 0.1 to 0.3; the default is kept");
        }
    }
    if (r.get("ETERNALVR_WEAPON_DIRECTIONS")) {
        WeaponDirectionsResult directions = parseWeaponDirections(r.raw());
        s.thumbRest.directions = directions.table;
        for (const std::string& issue : directions.issues) {
            r.report("ETERNALVR_WEAPON_DIRECTIONS", issue + "; that direction keeps its slot");
        }
    }
}

} // namespace

ControllerSettingsResult parseControllerSettings(const SettingLookup& lookup) {
    ControllerSettingsResult result;
    ControllerSettings& s = result.settings;
    Reader r(lookup, result.issues);

    r.flag("ETERNALVR_CONTROLLERS", s.enabled);

    static constexpr std::pair<const char*, AimSource> kAim[] = {
        {"head", AimSource::Head}, {"hand", AimSource::Hand}, {"view", AimSource::View}};
    r.choice("ETERNALVR_AIM", kAim, s.aim);
    // View is not one of the choices: it stays when the variable is unset or unusable.
    static constexpr std::pair<const char*, AimSource> kDemonAim[] = {{"head", AimSource::Head},
                                                                      {"hand", AimSource::Hand}};
    AimSource demonAim = AimSource::View;
    r.choice("ETERNALVR_DEMON_AIM", kDemonAim, demonAim);
    if (demonAim != AimSource::View) {
        s.demonAim = demonAim;
    }
    // hand is the weapon hand, the same as unset.
    static constexpr std::pair<const char*, ActionAimSource> kActionAim[] = {
        {"head", ActionAimSource::Head},
        {"offhand", ActionAimSource::OffHand},
        {"hand", ActionAimSource::Same}};
    r.choice("ETERNALVR_MELEE_AIM", kActionAim, s.actionAim.melee);
    r.choice("ETERNALVR_EQUIPMENT_AIM", kActionAim, s.actionAim.equipment);

    // head and hand are the values from before look, left and right; hand is the hand with the move stick.
    static constexpr std::pair<const char*, LocomotionFrame> kLocomotion[] = {
        {"look", LocomotionFrame::Head},
        {"left", LocomotionFrame::LeftHand},
        {"right", LocomotionFrame::RightHand},
        {"head", LocomotionFrame::Head},
        {"hand", LocomotionFrame::MoveHand}};
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
    static constexpr std::pair<const char*, MapSticks> kMapSticks[] = {{"weapon", MapSticks::WeaponPans},
                                                                       {"other", MapSticks::OtherPans}};
    r.choice("ETERNALVR_MAP_STICKS", kMapSticks, s.mapSticks);

    static constexpr std::pair<const char*, WheelSelect> kWheel[] = {{"stick", WheelSelect::Stick},
                                                                     {"hand", WheelSelect::Hand}};
    r.choice("ETERNALVR_WHEEL_SELECT", kWheel, s.wheelSelect);
    r.range("ETERNALVR_WHEEL_HAND_DEGREES", kMinWheelHandDegrees, kMaxWheelHandDegrees, s.wheelHandDegrees);
    readThumbRest(r, s);

    r.flag("ETERNALVR_THROW", s.throwGesture.enabled);
    r.range("ETERNALVR_THROW_SPEED", kMinGestureSpeed, kMaxGestureSpeed, s.throwGesture.speed);
    r.flag("ETERNALVR_SWING", s.swing.enabled);
    r.range("ETERNALVR_SWING_SPEED", kMinGestureSpeed, kMaxGestureSpeed, s.swing.speed);
    r.flag("ETERNALVR_HANDS_JUMP", s.handsJump.enabled);
    r.range("ETERNALVR_PUNCH_SPEED", kMinPunchMetresPerSecond, kMaxPunchMetresPerSecond, s.punchSpeed);
    r.range("ETERNALVR_HOLD_SECONDS", kMinHoldSettingSeconds, kMaxHoldSettingSeconds, s.holdSeconds);

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
    r.range("ETERNALVR_HAPTICS", 0.0f, 1.0f, s.haptics);
    r.flag("ETERNALVR_BHAPTICS", s.bhaptics);
    r.range("ETERNALVR_BHAPTICS_INTENSITY", 0.0f, 1.0f, s.bhapticsIntensity);

    r.flag("ETERNALVR_VIEWMODEL", s.viewmodel);
    r.flag("ETERNALVR_BUTTON_PROMPTS", s.buttonPrompts);
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
    r.flag("ETERNALVR_WEAPON_SIGHTS", s.weaponSights);
    static constexpr std::pair<const char*, OffhandMode> kOffhand[] = {
        {"game", OffhandMode::Game}, {"free", OffhandMode::Free}, {"probe", OffhandMode::Probe}};
    r.choice("ETERNALVR_OFFHAND", kOffhand, s.offhand);
    if (r.get("ETERNALVR_OFFHAND_OFFSET") && !game::parseOffsetList(r.raw(), s.offhandOffset)) {
        s.offhandOffset = kDefaultOffhandOffset;
        r.report("ETERNALVR_OFFHAND_OFFSET",
                 "expected forward,left,up[,pitch,yaw,roll] (metres, degrees); the default is kept");
    }
    if (const auto v = r.get("ETERNALVR_OFFHAND_SHOULDER")) {
        game::WeaponOffset offset;
        if (*v == "head") {
            s.offhandShoulder = ShoulderAnchor::Head;
        } else if (*v == "model") {
            s.offhandShoulder = ShoulderAnchor::Model;
        } else if (game::parseOffsetList(r.raw(), offset)) {
            s.offhandShoulder = ShoulderAnchor::Head;
            s.offhandShoulderOffset = offset;
        } else {
            r.report("ETERNALVR_OFFHAND_SHOULDER",
                     "expected head, model or forward,left,up (metres from the head); the default is kept");
        }
    }
    if (r.get("ETERNALVR_OFFHAND_ELBOW")) {
        game::WeaponOffset elbow;
        if (game::parseOffsetList(r.raw(), elbow) &&
            std::fabs(elbow.forward) + std::fabs(elbow.left) + std::fabs(elbow.up) > 1e-3f) {
            s.offhandElbow = elbow;
        } else {
            r.report("ETERNALVR_OFFHAND_ELBOW",
                     "expected forward,left,up (a direction); the default is kept");
        }
    }
    if (r.get("ETERNALVR_OFFHAND_PROBE")) {
        game::WeaponOffset probe;
        if (game::parseOffsetList(r.raw(), probe)) {
            s.offhandProbe = probe;
        } else {
            r.report("ETERNALVR_OFFHAND_PROBE", "expected forward,left,up (metres); the default is kept");
        }
    }
    r.range("ETERNALVR_OFFHAND_BLEND", 0.0f, 1.0f, s.offhandBlendSeconds);
    r.flag("ETERNALVR_OFFHAND_TRACE", s.offhandTrace);
    static constexpr std::pair<const char*, WeaponArmMode> kWeaponArm[] = {{"ik", WeaponArmMode::Ik},
                                                                           {"game", WeaponArmMode::Game}};
    r.choice("ETERNALVR_WEAPON_ARM", kWeaponArm, s.weaponArm);
    if (r.get("ETERNALVR_WEAPON_ARM_TEST_SHOULDER")) {
        game::WeaponOffset shoulder;
        if (game::parseOffsetList(r.raw(), shoulder)) {
            s.weaponArmTestShoulder = shoulder;
        } else {
            r.report("ETERNALVR_WEAPON_ARM_TEST_SHOULDER",
                     "expected forward,left,up (metres from the eyes); the shoulder stays the default");
        }
    }
    static constexpr std::pair<const char*, bool> kArms[] = {{"shown", false}, {"hidden", true}};
    // `hands` is for rig tests: taken, but not named in the message players may see for a mistyped value.
    if (const auto v = r.get("ETERNALVR_ARMS"); v && *v == "hands") {
        s.armsHidden = false;
        s.armsHandsOnly = true;
    } else {
        r.choice("ETERNALVR_ARMS", kArms, s.armsHidden);
    }
    r.choice("ETERNALVR_CUTSCENE_ARMS", kArms, s.cutsceneArmsHidden);
    static constexpr std::pair<const char*, ArmSolve> kArmSolve[] = {{"animated", ArmSolve::Animated},
                                                                     {"bind", ArmSolve::Bind}};
    r.choice("ETERNALVR_ARM_SOLVE", kArmSolve, s.armSolve);
    if (const auto v = r.get("ETERNALVR_ARM_MODEL")) {
        if (*v == "game") {
            s.armModel = ArmModel::Game;
        } else if (*v == "base") {
            s.armModel = ArmModel::Base;
        } else if (v->rfind("skin", 0) == 0) {
            const std::string set = v->size() > 5 && (*v)[4] == ':' ? v->substr(5) : std::string{};
            const auto n = set.empty() ? std::optional<float>(3.0f) : number(set);
            if ((v->size() == 4 || !set.empty()) && n && *n >= 1.0f && *n <= 99.0f && std::floor(*n) == *n) {
                s.armModel = ArmModel::Skin;
                s.armModelSkin = static_cast<int>(*n);
            } else {
                r.report("ETERNALVR_ARM_MODEL", "expected skin or skin:<set 1 to 99>; the default is kept");
            }
        } else {
            r.report("ETERNALVR_ARM_MODEL", "expected one of game, base, skin[:N]; the default is kept");
        }
    }
    if (const auto path = lookup("ETERNALVR_CONTROLLER_DATA"); path && !path->empty()) {
        s.controllerDataPath = *path;
    }
    if (const auto path = lookup("ETERNALVR_TEST_INPUT"); path && !path->empty()) {
        s.testInputPath = *path;
    }
    if (const auto path = lookup("ETERNALVR_TEST_ARM_DUMP"); path && !path->empty()) {
        s.testArmDumpPath = *path;
    }
    static constexpr std::pair<const char*, HandsCuff> kCuff[] = {
        {"fold", HandsCuff::Fold}, {"stub", HandsCuff::Stub}, {"ring", HandsCuff::Ring}};
    r.choice("ETERNALVR_TEST_HANDS_CUFF", kCuff, s.handsCuff);
    static constexpr std::pair<const char*, bool> kArmFault[] = {{"none", false}, {"twist", true}};
    r.choice("ETERNALVR_TEST_ARM_FAULT", kArmFault, s.testArmTwistFault);
    if (r.get("ETERNALVR_TEST_ARM_MOD_WEIGHT")) {
        float weight = -1.0f;
        r.range("ETERNALVR_TEST_ARM_MOD_WEIGHT", 0.0f, 1.0f, weight);
        if (weight >= 0.0f) {
            s.testArmModWeight = weight;
        }
    }
    return result;
}

const char* armModelName(ArmModel model) {
    switch (model) {
    case ArmModel::Game:
        return "game";
    case ArmModel::Base:
        return "base";
    case ArmModel::Skin:
        return "skin";
    }
    return "game";
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

AimSource demonAimSource(const ControllerSettings& settings) {
    if (settings.aim == AimSource::View || !settings.demonAim) {
        return settings.aim;
    }
    return *settings.demonAim;
}

const char* shoulderAnchorName(ShoulderAnchor anchor) {
    return anchor == ShoulderAnchor::Model ? "model" : "head";
}

game::WeaponOffset offhandOffsetFor(const game::WeaponOffset& offset, game::Handedness handedness) {
    if (handedness == game::Handedness::Right) {
        return offset;
    }
    game::WeaponOffset mirrored = offset;
    mirrored.left = -offset.left;
    mirrored.yaw = -offset.yaw;
    mirrored.roll = -offset.roll;
    return mirrored;
}

ShoulderAnchor shoulderAnchorFor(ShoulderAnchor anchor, game::Handedness handedness) {
    return handedness == game::Handedness::Right ? anchor : ShoulderAnchor::Head;
}

game::WeaponOffset weaponArmOffsetFor(const game::WeaponOffset& offset, game::Handedness handedness) {
    // The weapon arm is on the side the off hand is not: the off hand's mirroring, the other way round.
    return offhandOffsetFor(offset, handedness == game::Handedness::Right ? game::Handedness::LeftButtonSwap
                                                                          : game::Handedness::Right);
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
