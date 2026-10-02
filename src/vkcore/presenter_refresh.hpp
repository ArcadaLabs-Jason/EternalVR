#pragma once

// The headset's display period in the log (features/pacing/display_period_watch.hpp, docs/VR_STEREO.md
// "Headset refresh rate"): every settled change of XrFrameState::predictedDisplayPeriod with the time it
// came, named as throttling (a multiple of the base) or a refresh change, and every kSummaryMs and at
// session end the share of time at each multiple of the base. XR worker only.
//
// With XR_FB_display_refresh_rate (enabled only when the runtime lists it: VDXR, SteamVR, WiVRn; never used
// to request a rate) the runtime's own refresh rate is logged once the session runs, again at each settled
// period change and on its change event, and changes are named against it: a period at 2x the headset's
// refresh period is throttling, a period that matches a new refresh rate is a refresh change. Every failure
// leaves the display period alone to go by.
//
// The base refresh rate and the throttled share also go to the status file (status_file.hpp), as do the
// runtime, the system and the sizes (writeHeadsetStatus).

#include "features/pacing/display_period_watch.hpp"

#include <openxr/openxr.h>

#include <cstdint>
#include <vector>

namespace evr::vkcore {

struct XrFunctions;

// The status file's runtime=, system= and recommended= (the largest of `views`), once the session exists.
void writeHeadsetStatus(const XrFunctions& xr,
                        XrInstance instance,
                        XrSystemId system,
                        const std::vector<XrViewConfigurationView>& views);

class RefreshLog {
public:
    // Change lines logged; later changes are only counted.
    static constexpr std::uint32_t kMaxChangeLines = 200;
    static constexpr std::uint64_t kSummaryMs = 60000;

    // Set by createXrInstance: XR_FB_display_refresh_rate is enabled on the instance.
    bool extension = false;

    // A new instance: the extension's getter (when enabled), or a line saying the runtime has none.
    void onInstance(PFN_xrGetInstanceProcAddr getProcAddr, XrInstance instance);
    // The session began: the runtime's refresh rate.
    void onSessionRunning(XrSession session);
    // XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB.
    void onRateChanged(const XrEventDataDisplayRefreshRateChangedFB& event);
    // Every XR frame: its predicted display period.
    void onFrame(XrDuration period);
    // The summary line now (`when`: appended to "refresh summary", e.g. " at session end"); nothing before a
    // period settled.
    void logSummary(const char* when);

private:
    // Reads the runtime's refresh rate into hz_ (logged as `why` when it changed); false when it gave none.
    bool readRate(const char* why);
    [[nodiscard]] double runtimeMs() const { return hz_ > 0.0 ? 1000.0 / hz_ : 0.0; }
    // The base for the summary and the status file (pacing::summaryBase); 0 before a period settled.
    [[nodiscard]] double baseMs() const;

    PFN_xrGetDisplayRefreshRateFB getRate_ = nullptr;
    XrSession session_ = XR_NULL_HANDLE;
    double hz_ = 0.0; // the runtime's refresh rate; 0 while unknown
    bool loggedNoExtension_ = false;
    bool loggedReadFailure_ = false;
    pacing::DisplayPeriodWatch watch_;
    double logOffset_ = 0.0; // log seconds minus performance counter seconds
    bool started_ = false;
    std::uint32_t changeLines_ = 0;
    std::uint64_t lastSummaryTicks_ = 0;
    double statusBaseMs_ = 0.0; // the base in the status file's refresh_hz
};

} // namespace evr::vkcore
