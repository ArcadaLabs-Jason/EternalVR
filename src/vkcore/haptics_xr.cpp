// Motion controllers, vibration (controllers.hpp, features/input/haptics_policy.hpp).
//
// The mapper (user-command hook or pad sampler), the menu pointer and the game's rumble leave their events
// in State::hapticEvents; the XR worker's sync takes them once a frame, runs the policy and sends its
// pulses with xrApplyHapticFeedback on the gameplay set's haptic action, under the shared xrMutex like
// every other action call. Nothing is sent while the session is not focused.

#include "vkcore/controllers_impl.hpp"

#include "vkcore/log.hpp"
#include "vkcore/menu_input.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "haptics";
// The fire action as the mapper last sent it counts as released when the mapper has not run for this long.
constexpr double kFireStaleSeconds = 0.25;
// The game's rumble counts as ended when its hook has not run for this long (a load, the game stalled).
constexpr double kRumbleStaleSeconds = 0.1;
constexpr ULONGLONG kSummaryTicks = 10000;

double qpcSeconds() {
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    return static_cast<double>(nowQpc()) / frequency;
}

std::uint64_t pulses(const State& s, input::HapticSource source) {
    return s.hapticPulses[static_cast<std::size_t>(source)].load(std::memory_order_relaxed);
}

void logSummary(State& s) {
    const ULONGLONG now = GetTickCount64();
    if (now - s.hapticLogTicks < kSummaryTicks) {
        return;
    }
    s.hapticLogTicks = now;
    std::uint64_t total = 0;
    for (const auto& count : s.hapticPulses) {
        total += count.load(std::memory_order_relaxed);
    }
    const std::uint64_t refused = s.hapticRefused.load(std::memory_order_relaxed);
    if (total == s.hapticLoggedTotal && refused == s.hapticLoggedRefused) {
        return; // nothing new
    }
    s.hapticLoggedTotal = total;
    s.hapticLoggedRefused = refused;
    EVR_LOG("%s: %llu pulses (fire %llu, punch %llu, menu %llu, game %llu, capture %llu), %llu refused", kTag,
            static_cast<unsigned long long>(total),
            static_cast<unsigned long long>(pulses(s, input::HapticSource::Fire)),
            static_cast<unsigned long long>(pulses(s, input::HapticSource::Punch)),
            static_cast<unsigned long long>(pulses(s, input::HapticSource::Menu)),
            static_cast<unsigned long long>(pulses(s, input::HapticSource::Game)),
            static_cast<unsigned long long>(pulses(s, input::HapticSource::Capture)),
            static_cast<unsigned long long>(refused));
}

void send(const XrInput& xr, State& s, input::Hand hand, const input::HapticCommand& command) {
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = xr.actions[static_cast<std::size_t>(input::XrActionId::Haptic)];
    info.subactionPath = xr.handPaths[static_cast<std::size_t>(hand)];
    XrResult r = XR_SUCCESS;
    if (command.kind == input::HapticCommandKind::Stop) {
        r = xr.xrStopHapticFeedback(xr.session, &info);
    } else {
        XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
        vibration.duration = static_cast<XrDuration>(static_cast<double>(command.seconds) * 1.0e9);
        vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
        vibration.amplitude = command.amplitude;
        r = xr.xrApplyHapticFeedback(xr.session, &info,
                                     reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
        if (XR_SUCCEEDED(r)) {
            s.hapticPulses[static_cast<std::size_t>(command.source)].fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (XR_FAILED(r) && s.hapticRefused.fetch_add(1, std::memory_order_relaxed) == 0) {
        char text[XR_MAX_RESULT_STRING_SIZE];
        EVR_LOG("%s: the runtime refused a %s (%s); counted from now on", kTag,
                command.kind == input::HapticCommandKind::Stop ? "stop" : "pulse", xrText(r, text));
    }
}

} // namespace

void noteMapperHaptics(const game::GameActionSet& sent, const input::GameInput& input) {
    State& s = state();
    std::lock_guard lock(s.hapticsMutex);
    s.hapticEvents.fireHeld = game::contains(sent, game::GameAction::Fire);
    s.hapticEvents.fireQpc = nowQpc();
    for (std::size_t i = 0; i < input.punch.size(); ++i) {
        s.hapticEvents.punch[i] = s.hapticEvents.punch[i] || input.punch[i];
    }
    s.hapticEvents.capture = s.hapticEvents.capture || input.capture;
}

void noteMenuHaptic(input::Hand hand, input::MenuTick tick) {
    State& s = state();
    std::lock_guard lock(s.hapticsMutex);
    input::MenuTick& pending = s.hapticEvents.menu[static_cast<std::size_t>(hand)];
    if (tick == input::MenuTick::Click || pending == input::MenuTick::None) {
        pending = tick;
    }
}

void noteGameRumble(float low, float high) {
    State& s = state();
    std::lock_guard lock(s.hapticsMutex);
    s.hapticEvents.rumble = input::GameRumble{low, high};
    s.hapticEvents.rumbleQpc = nowQpc();
}

void updateHaptics(const XrInput& xr, bool focused) {
    State& s = state();
    HapticEvents events;
    {
        std::lock_guard lock(s.hapticsMutex);
        events = s.hapticEvents;
        // The events are taken; the levels stay.
        s.hapticEvents.punch = {};
        s.hapticEvents.capture = false;
        s.hapticEvents.menu = {};
    }
    if (!s.haptics) {
        const float strength = settings().haptics;
        s.haptics.emplace(strength);
        if (strength > 0.0f) {
            EVR_LOG("%s: on, strength %.2f (fire, punch, menu, capture, game rumble)", kTag, strength);
        } else {
            EVR_LOG("%s: off (ETERNALVR_HAPTICS=0)", kTag);
        }
    }
    if (!focused || !xr.session || s.haptics->strength() <= 0.0f) {
        s.haptics->reset();
        return;
    }
    input::HapticsFrame frame;
    frame.seconds = qpcSeconds();
    frame.weaponHand = weaponHand();
    frame.fireHeld = events.fireHeld && events.fireQpc && secondsSince(events.fireQpc) < kFireStaleSeconds;
    frame.punch = events.punch;
    frame.capture = events.capture;
    frame.menu = events.menu;
    // No game rumble while a menu holds gameplay back (the game may keep its last mix under a menu).
    if (events.rumbleQpc && secondsSince(events.rumbleQpc) < kRumbleStaleSeconds &&
        !menu_input::suppressGameplay()) {
        frame.rumble = events.rumble;
    }
    const std::array<input::HapticCommand, 2> commands = s.haptics->update(frame);
    for (const input::Hand hand : {input::Hand::Left, input::Hand::Right}) {
        const input::HapticCommand& command = commands[static_cast<std::size_t>(hand)];
        if (command.kind != input::HapticCommandKind::None) {
            send(xr, s, hand, command);
        }
    }
    logSummary(s);
}

} // namespace evr::vkcore::controllers
