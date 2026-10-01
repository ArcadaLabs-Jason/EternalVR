// Motion controllers, the OpenXR side (controllers.hpp): settings, the action sets and their suggested
// bindings, the per-frame sync into the snapshot, and locating poses in room space (the poses at the
// camera hook's time are in game_view_poses.cpp).

#include "vkcore/controllers_impl.hpp"

#include "features/input/dossier_press.hpp"
#include "features/input/interaction_profiles.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/log.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";
// How often the current interaction profile is asked for (XR frames).
constexpr std::uint64_t kProfileCheckFrames = 90;

std::size_t handIndex(input::Hand hand) {
    return static_cast<std::size_t>(hand);
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr,
                        nullptr);
    return out;
}

std::optional<std::string> envLookup(std::string_view name) {
    const std::wstring wide(name.begin(), name.end());
    std::wstring value;
    if (!readEnv(wide.c_str(), value)) {
        return std::nullopt;
    }
    return narrow(value);
}

void readSettingsOnce() {
    State& s = state();
    const auto parsed = input::parseControllerSettings(&envLookup);
    s.settings = parsed.settings;
    if (const auto smoothing = input::aimSmoothingParams(s.settings.aimSmoothing)) {
        s.aimFilter.emplace(*smoothing);
    }
    for (const auto& issue : parsed.issues) {
        EVR_LOG("%s: %s='%s': %s", kTag, issue.name.c_str(), issue.value.c_str(), issue.message.c_str());
    }
    s.offsets = game::parseWeaponOffsets(game::builtinWeaponOffsets());
    for (const std::string& issue : s.offsets.issues) {
        EVR_LOG("%s: built-in viewmodel offsets: %s", kTag, issue.c_str());
    }
}

bool loadFunctions(XrInput& xr, PFN_xrGetInstanceProcAddr gipa) {
#define EVR_LOAD(name)                                                                                       \
    if (XR_FAILED(gipa(xr.instance, #name, reinterpret_cast<PFN_xrVoidFunction*>(&xr.name))) || !xr.name) {  \
        EVR_LOG("%s: the runtime has no %s", kTag, #name);                                                   \
        return false;                                                                                        \
    }
    EVR_LOAD(xrStringToPath)
    EVR_LOAD(xrPathToString)
    EVR_LOAD(xrCreateActionSet)
    EVR_LOAD(xrDestroyActionSet)
    EVR_LOAD(xrCreateAction)
    EVR_LOAD(xrSuggestInteractionProfileBindings)
    EVR_LOAD(xrAttachSessionActionSets)
    EVR_LOAD(xrCreateActionSpace)
    EVR_LOAD(xrCreateReferenceSpace)
    EVR_LOAD(xrSyncActions)
    EVR_LOAD(xrGetActionStateBoolean)
    EVR_LOAD(xrGetActionStateFloat)
    EVR_LOAD(xrGetActionStateVector2f)
    EVR_LOAD(xrGetCurrentInteractionProfile)
    EVR_LOAD(xrLocateSpace)
    EVR_LOAD(xrDestroySpace)
    EVR_LOAD(xrResultToString)
    EVR_LOAD(xrApplyHapticFeedback)
    EVR_LOAD(xrStopHapticFeedback)
#undef EVR_LOAD
    return true;
}

XrActionType actionType(input::XrActionKind kind) {
    switch (kind) {
    case input::XrActionKind::Boolean:
        return XR_ACTION_TYPE_BOOLEAN_INPUT;
    case input::XrActionKind::Float:
        return XR_ACTION_TYPE_FLOAT_INPUT;
    case input::XrActionKind::Vector2:
        return XR_ACTION_TYPE_VECTOR2F_INPUT;
    case input::XrActionKind::Pose:
        return XR_ACTION_TYPE_POSE_INPUT;
    case input::XrActionKind::Haptic:
        break;
    }
    return XR_ACTION_TYPE_VIBRATION_OUTPUT;
}

bool createActions(XrInput& xr) {
    EVR_XR_TRY(
        xr.xrStringToPath(xr.instance, "/user/hand/left", &xr.handPaths[handIndex(input::Hand::Left)]));
    EVR_XR_TRY(
        xr.xrStringToPath(xr.instance, "/user/hand/right", &xr.handPaths[handIndex(input::Hand::Right)]));
    for (const input::XrActionSetDef& def : input::xrActionSets()) {
        XrActionSetCreateInfo info{XR_TYPE_ACTION_SET_CREATE_INFO};
        std::memcpy(info.actionSetName, def.name.data(), def.name.size());
        std::memcpy(info.localizedActionSetName, def.localizedName.data(), def.localizedName.size());
        info.priority = def.priority;
        EVR_XR_TRY(xr.xrCreateActionSet(xr.instance, &info, &xr.sets[static_cast<std::size_t>(def.id)]));
    }
    for (const input::XrActionDef& def : input::xrActions()) {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        std::memcpy(info.actionName, def.name.data(), def.name.size());
        std::memcpy(info.localizedActionName, def.localizedName.data(), def.localizedName.size());
        info.actionType = actionType(def.kind);
        info.countSubactionPaths = static_cast<std::uint32_t>(xr.handPaths.size());
        info.subactionPaths = xr.handPaths.data();
        EVR_XR_TRY(xr.xrCreateAction(xr.sets[static_cast<std::size_t>(def.set)], &info,
                                     &xr.actions[static_cast<std::size_t>(def.id)]));
    }
    return true;
}

bool createSpaces(XrInput& xr) {
    const XrPosef identity{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    for (const input::Hand hand : {input::Hand::Left, input::Hand::Right}) {
        XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        info.subactionPath = xr.handPaths[handIndex(hand)];
        info.poseInActionSpace = identity;
        info.action = xr.actions[static_cast<std::size_t>(input::XrActionId::AimPose)];
        EVR_XR_TRY(xr.xrCreateActionSpace(xr.session, &info, &xr.aimSpaces[handIndex(hand)]));
        info.action = xr.actions[static_cast<std::size_t>(input::XrActionId::GripPose)];
        EVR_XR_TRY(xr.xrCreateActionSpace(xr.session, &info, &xr.gripSpaces[handIndex(hand)]));
    }
    XrReferenceSpaceCreateInfo view{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    view.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    view.poseInReferenceSpace = identity;
    EVR_XR_TRY(xr.xrCreateReferenceSpace(xr.session, &view, &xr.viewSpace));
    return true;
}

void destroyHandles(XrInput& xr) {
    for (XrSpace& space : xr.aimSpaces) {
        if (space) {
            xr.xrDestroySpace(space);
            space = XR_NULL_HANDLE;
        }
    }
    for (XrSpace& space : xr.gripSpaces) {
        if (space) {
            xr.xrDestroySpace(space);
            space = XR_NULL_HANDLE;
        }
    }
    if (xr.viewSpace) {
        xr.xrDestroySpace(xr.viewSpace);
        xr.viewSpace = XR_NULL_HANDLE;
    }
    // Destroying a set destroys its actions.
    for (XrActionSet& set : xr.sets) {
        if (set) {
            xr.xrDestroyActionSet(set);
            set = XR_NULL_HANDLE;
        }
    }
    xr.actions.fill(XR_NULL_HANDLE);
}

Pose toPose(const XrPosef& p) {
    return {Quat{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
            Vec3{p.position.x, p.position.y, p.position.z}};
}

Pose currentRoomFromLocal() {
    State& s = state();
    std::lock_guard lock(s.roomMutex);
    return s.roomFromLocal;
}

} // namespace

bool locate(const XrInput& xr,
            XrSpace space,
            XrTime time,
            Pose& out,
            Vec3* velocity,
            bool* velocityValid,
            const Pose* roomOverride) {
    XrSpaceVelocity v{XR_TYPE_SPACE_VELOCITY};
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION, velocity ? &v : nullptr};
    if (!space || XR_FAILED(xr.xrLocateSpace(space, xr.localSpace, time, &location))) {
        return false;
    }
    constexpr XrSpaceLocationFlags needed =
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if ((location.locationFlags & needed) != needed) {
        return false;
    }
    const XrPosef& p = location.pose;
    if (!std::isfinite(p.orientation.x) || !std::isfinite(p.orientation.y) ||
        !std::isfinite(p.orientation.z) || !std::isfinite(p.orientation.w) || !std::isfinite(p.position.x) ||
        !std::isfinite(p.position.y) || !std::isfinite(p.position.z)) {
        return false;
    }
    const Pose room = roomOverride ? *roomOverride : currentRoomFromLocal();
    out = compose(room, toPose(p));
    out.orientation = normalize(out.orientation);
    if (velocity && velocityValid) {
        *velocityValid = (v.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0;
        *velocity =
            rotate(room.orientation, Vec3{v.linearVelocity.x, v.linearVelocity.y, v.linearVelocity.z});
    }
    return true;
}

namespace {

float readFloat(const XrInput& xr, input::XrActionId id, XrPath hand) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = xr.actions[static_cast<std::size_t>(id)];
    info.subactionPath = hand;
    XrActionStateFloat s{XR_TYPE_ACTION_STATE_FLOAT};
    if (XR_FAILED(xr.xrGetActionStateFloat(xr.session, &info, &s)) || !s.isActive ||
        !std::isfinite(s.currentState)) {
        return 0.0f;
    }
    return s.currentState;
}

bool readBool(const XrInput& xr, input::XrActionId id, XrPath hand) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = xr.actions[static_cast<std::size_t>(id)];
    info.subactionPath = hand;
    XrActionStateBoolean s{XR_TYPE_ACTION_STATE_BOOLEAN};
    return XR_SUCCEEDED(xr.xrGetActionStateBoolean(xr.session, &info, &s)) && s.isActive && s.currentState;
}

input::Axis2 readStick(const XrInput& xr, XrPath hand) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = xr.actions[static_cast<std::size_t>(input::XrActionId::Thumbstick)];
    info.subactionPath = hand;
    XrActionStateVector2f s{XR_TYPE_ACTION_STATE_VECTOR2F};
    if (XR_FAILED(xr.xrGetActionStateVector2f(xr.session, &info, &s)) || !s.isActive) {
        return {};
    }
    const input::Axis2 stick{s.currentState.x, s.currentState.y};
    return input::isFinite(stick) ? stick : input::Axis2{};
}

input::HandState readHand(const XrInput& xr, input::Hand hand, XrTime time, const Pose& room) {
    const XrPath path = xr.handPaths[handIndex(hand)];
    input::HandState h;
    h.trigger = readFloat(xr, input::XrActionId::Trigger, path);
    h.grip = readFloat(xr, input::XrActionId::Grip, path);
    h.stick = readStick(xr, path);
    h.stickClick = readBool(xr, input::XrActionId::ThumbstickClick, path);
    h.primaryButton = readBool(xr, input::XrActionId::Primary, path);
    h.secondaryButton = readBool(xr, input::XrActionId::Secondary, path);
    h.menuButton = readBool(xr, input::XrActionId::Menu, path);
    h.poseValid = locate(xr, xr.aimSpaces[handIndex(hand)], time, h.aimPose, &h.linearVelocity,
                         &h.velocityValid, &room);
    h.gripValid = locate(xr, xr.gripSpaces[handIndex(hand)], time, h.gripPose, nullptr, nullptr, &room);
    return h;
}

} // namespace

const char* xrText(XrResult result, char (&buffer)[XR_MAX_RESULT_STRING_SIZE]) {
    const XrInput& xr = state().xr;
    if (xr.xrResultToString && xr.instance &&
        XR_SUCCEEDED(xr.xrResultToString(xr.instance, result, buffer))) {
        return buffer;
    }
    std::snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "XrResult %d", static_cast<int>(result));
    return buffer;
}

State& state() {
    // Never destroyed: game threads can be inside a hook while the process exits.
    static State* const s = new State;
    return *s;
}

const input::ControllerSettings& settings() {
    State& s = state();
    std::call_once(s.settingsOnce, &readSettingsOnce);
    return s.settings;
}

LONGLONG nowQpc() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double secondsSince(LONGLONG qpc) {
    static const LONGLONG frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    return static_cast<double>(nowQpc() - qpc) / static_cast<double>(frequency);
}

bool attach(const XrContext& context) {
    const input::ControllerSettings& cfg = settings();
    if (!cfg.enabled) {
        EVR_LOG("%s: off (ETERNALVR_CONTROLLERS=0)", kTag);
        return false;
    }
    State& s = state();
    std::unique_lock lock(s.xrMutex);
    if (s.attached.load()) {
        return true;
    }
    XrInput& xr = s.xr;
    xr = {};
    xr.instance = context.instance;
    xr.session = context.session;
    xr.localSpace = context.localSpace;
    auto data = loadControllerData(cfg);
    const bool ok = loadFunctions(xr, context.getInstanceProcAddr) && createActions(xr);
    const bool anySuggested = ok && suggestAllBindings(xr, context.profiles, data);
    if (!ok || !anySuggested) {
        destroyHandles(xr);
        EVR_LOG("%s: OpenXR input could not be set up; the game keeps its own input only", kTag);
        return false;
    }
    std::array<XrActionSet, static_cast<std::size_t>(input::XrActionSetId::Count)> sets = xr.sets;
    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = static_cast<std::uint32_t>(sets.size());
    attachInfo.actionSets = sets.data();
    const XrResult r = xr.xrAttachSessionActionSets(xr.session, &attachInfo);
    if (XR_FAILED(r) || !createSpaces(xr)) {
        char text[XR_MAX_RESULT_STRING_SIZE];
        EVR_LOG("%s: attaching the action sets failed (%s); controllers off", kTag, xrText(r, text));
        destroyHandles(xr);
        return false;
    }
    {
        std::lock_guard mapperLock(s.mapperMutex);
        s.controllerData = std::move(data);
        s.mapper.reset();
    }
    s.attached.store(true, std::memory_order_release);
    EVR_LOG(
        "%s: on: aim %s, demon aim %s%s, locomotion %s, turn %s (%.0f deg/s, snap %.0f deg), handedness %d, "
        "input %s, viewmodel %s, weapon FOV %s, shots from the %s, aim smoothing %.2f%s, Dossier on X %s, "
        "Dossier map panned by the %s stick, weapon wheel by the %s, throw gesture %s, overhead swing %s, "
        "hands-up jump %s",
        kTag, input::aimSourceName(cfg.aim), input::aimSourceName(input::demonAimSource(cfg)),
        !cfg.demonAim                       ? " (as aim)"
        : cfg.aim == input::AimSource::View ? " (ETERNALVR_DEMON_AIM has no effect under view aim)"
                                            : " (ETERNALVR_DEMON_AIM)",
        cfg.locomotion == input::LocomotionFrame::Head ? "head" : "hand",
        cfg.turn.mode == input::TurnMode::Smooth ? "smooth"
        : cfg.turn.mode == input::TurnMode::Snap ? "snap"
                                                 : "off",
        cfg.turn.smoothDegreesPerSecond, cfg.turn.snapDegrees, static_cast<int>(cfg.handedness),
        input::inputPathName(cfg.path), cfg.viewmodel ? "at the hand" : "the game's",
        cfg.weaponFov ? "on" : "off", cfg.shotOrigin == input::ShotOrigin::Hand ? "hand" : "eye",
        cfg.aimSmoothing, s.aimFilter ? "" : " (off)", input::dossierPressName(cfg.dossier),
        cfg.mapSticks == input::MapSticks::OtherPans ? "other" : "weapon hand's",
        input::wheelSelectName(cfg.wheelSelect), cfg.throwGesture.enabled ? "on" : "off",
        cfg.swing.enabled ? "on" : "off", cfg.handsJump.enabled ? "on" : "off");
    return true;
}

void detach() {
    State& s = state();
    std::unique_lock lock(s.xrMutex);
    if (!s.attached.exchange(false)) {
        return;
    }
    destroyHandles(s.xr);
    {
        std::lock_guard snapshotLock(s.snapshotMutex);
        s.snapshot = {};
    }
    {
        std::lock_guard viewLock(s.viewMutex);
        s.poses = {};
        s.world = {};
    }
    EVR_LOG("%s: detached (%llu sync(s))", kTag, static_cast<unsigned long long>(s.syncs.load()));
}

void sync(XrTime predictedDisplayTime, bool focused) {
    State& s = state();
    if (!s.attached.load(std::memory_order_acquire)) {
        return;
    }
    refreshTestInput(); // menus run without the camera hook, which otherwise re-reads the file
    std::shared_lock lock(s.xrMutex);
    const XrInput& xr = s.xr;
    Snapshot next;
    if (focused && xr.session) {
        XrActiveActionSet active{xr.sets[static_cast<std::size_t>(input::XrActionSetId::Gameplay)],
                                 XR_NULL_PATH};
        XrActionsSyncInfo info{XR_TYPE_ACTIONS_SYNC_INFO};
        info.countActiveActionSets = 1;
        info.activeActionSets = &active;
        const XrResult r = xr.xrSyncActions(xr.session, &info);
        if (r == XR_SUCCESS) {
            next.valid = true;
            next.qpc = nowQpc();
            // One room transform for the whole frame, kept with it (the menu pointer takes the hands back
            // into LOCAL with it).
            next.frame.roomFromLocal = currentRoomFromLocal();
            const Pose& room = next.frame.roomFromLocal;
            next.frame.left = readHand(xr, input::Hand::Left, predictedDisplayTime, room);
            next.frame.right = readHand(xr, input::Hand::Right, predictedDisplayTime, room);
            next.frame.head.poseValid =
                locate(xr, xr.viewSpace, predictedDisplayTime, next.frame.head.pose, nullptr, nullptr, &room);
        }
    }
    if (const auto test = testInput()) {
        if (!next.valid) {
            next.valid = true;
            next.qpc = nowQpc();
            next.frame.roomFromLocal = currentRoomFromLocal();
            next.frame.head.poseValid = locate(xr, xr.viewSpace, predictedDisplayTime, next.frame.head.pose,
                                               nullptr, nullptr, &next.frame.roomFromLocal);
        }
        input::applyTestInput(*test, next.frame);
    }
    const std::uint64_t syncs = s.syncs.fetch_add(1) + 1;
    {
        std::lock_guard snapshotLock(s.snapshotMutex);
        next.controller = s.snapshot.controller;
        if (next.valid && (syncs == 1 || syncs % kProfileCheckFrames == 0)) {
            const auto family = currentFamily(xr, s);
            if (family && *family != next.controller) {
                EVR_LOG("%s: the runtime reports %s controllers", kTag,
                        std::string(game::controllerName(*family)).c_str());
                next.controller = *family;
            }
        }
        s.snapshot = next;
    }
    updateHaptics(xr, focused);
}

void setRoomFromLocal(const Pose& roomFromLocal) {
    State& s = state();
    std::lock_guard lock(s.roomMutex);
    s.roomFromLocal = roomFromLocal;
}

std::optional<input::InputFrame> latestFrame() {
    State& s = state();
    if (!s.attached.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    std::lock_guard snapshotLock(s.snapshotMutex);
    if (!s.snapshot.valid || secondsSince(s.snapshot.qpc) > kSnapshotStaleSeconds) {
        return std::nullopt;
    }
    return s.snapshot.frame;
}

input::Hand dominantHand() {
    return weaponHand();
}

input::MapSticks mapSticks() {
    return settings().mapSticks;
}

input::Hand weaponHand() {
    return settings().handedness == game::Handedness::Right ? input::Hand::Right : input::Hand::Left;
}

bool safeCopy(void* destination, const void* source, std::size_t size) {
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace evr::vkcore::controllers
