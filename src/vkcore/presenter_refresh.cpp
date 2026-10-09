// The headset's display period in the log (presenter_refresh.hpp).

#include "vkcore/presenter_refresh.hpp"

#include "features/pacing/prediction_period.hpp"
#include "vkcore/log.hpp"
#include "vkcore/presenter_types.hpp"
#include "vkcore/status_file.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace evr::vkcore {

void writeHeadsetStatus(const XrFunctions& xr,
                        XrInstance instance,
                        XrSystemId system,
                        const std::vector<XrViewConfigurationView>& views) {
    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xr.xrGetInstanceProperties(instance, &ip))) {
        status::field("runtime", ip.runtimeName);
    }
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
    if (XR_SUCCEEDED(xr.xrGetSystemProperties(instance, system, &sp))) {
        status::field("system", sp.systemName);
    }
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    for (const XrViewConfigurationView& v : views) {
        width = std::max(width, v.recommendedImageRectWidth);
        height = std::max(height, v.recommendedImageRectHeight);
    }
    if (width && height) {
        status::field("recommended", std::to_string(width) + "x" + std::to_string(height));
    }
}

void RefreshLog::onInstance(PFN_xrGetInstanceProcAddr getProcAddr, XrInstance instance) {
    getRate_ = nullptr;
    session_ = XR_NULL_HANDLE;
    if (extension && getProcAddr && instance != XR_NULL_HANDLE) {
        getProcAddr(instance, "xrGetDisplayRefreshRateFB", reinterpret_cast<PFN_xrVoidFunction*>(&getRate_));
    }
    if (!getRate_ && !loggedNoExtension_) {
        loggedNoExtension_ = true;
        EVR_LOG("xr: %s %s; the refresh rate is read from the display period only",
                XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME, extension ? "has no getter" : "not offered");
    }
}

void RefreshLog::onSessionRunning(XrSession session) {
    session_ = session;
    if (getRate_ && !readRate("")) {
        hz_ = 0.0;
    }
}

void RefreshLog::onRateChanged(const XrEventDataDisplayRefreshRateChangedFB& event) {
    EVR_LOG("xr: refresh rate %.1f -> %.1f Hz (%s event)", static_cast<double>(event.fromDisplayRefreshRate),
            static_cast<double>(event.toDisplayRefreshRate), XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    if (event.toDisplayRefreshRate > 0.0f) {
        hz_ = static_cast<double>(event.toDisplayRefreshRate);
    }
}

bool RefreshLog::readRate(const char* why) {
    if (!getRate_ || session_ == XR_NULL_HANDLE) {
        return false;
    }
    float rate = 0.0f;
    const XrResult r = getRate_(session_, &rate);
    if (XR_FAILED(r) || !(rate > 0.0f) || rate > 1000.0f) {
        if (!loggedReadFailure_) {
            loggedReadFailure_ = true;
            EVR_LOG(
                "xr: xrGetDisplayRefreshRateFB gave no rate (result %d, %.1f Hz); the refresh rate is read "
                "from the display period only",
                static_cast<int>(r), static_cast<double>(rate));
        }
        return false;
    }
    const double hz = static_cast<double>(rate);
    if (hz_ <= 0.0) {
        EVR_LOG("xr: refresh rate %.1f Hz (%s)%s", hz, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME, why);
    } else if (std::abs(hz - hz_) >= 0.05) {
        EVR_LOG("xr: refresh rate %.1f -> %.1f Hz (%s)%s", hz_, hz, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
                why);
    }
    hz_ = hz;
    return true;
}

void RefreshLog::onFrame(XrDuration period) {
    // The watch counts time between frames, which needs the performance counter (the log's tick count moves
    // in 16 ms steps); its times are shifted to the log's, so a change line's time matches the log.
    const double counter = qpcSeconds(qpcNow());
    if (!started_) {
        started_ = true;
        logOffset_ = logSeconds() - counter;
        lastSummaryTicks_ = GetTickCount64();
    }
    const auto change = watch_.onFrame(static_cast<double>(period) / 1e6, counter + logOffset_);
    if (const double base = baseMs(); base > 0.0 && !pacing::samePeriod(base, statusBaseMs_)) {
        statusBaseMs_ = base;
        status::field("refresh_hz", pacing::refreshHzValue(base));
    }
    if (GetTickCount64() - lastSummaryTicks_ >= kSummaryMs) {
        lastSummaryTicks_ = GetTickCount64();
        logSummary("");
    }
    if (change && changeLines_ < kMaxChangeLines) {
        ++changeLines_;
        readRate(", read at the display period change"); // SteamVR sends no change event
        EVR_LOG("xr: %s", pacing::changeText(*change, runtimeMs()).c_str());
        if (changeLines_ == kMaxChangeLines) {
            EVR_LOG("xr: display period: %u lines logged; later changes are only counted", kMaxChangeLines);
        }
    }
}

XrDuration RefreshLog::predictionPeriod(XrDuration reported, bool poseLead) {
    if (poseLead) {
        if (!loggedLeadPrediction_) {
            loggedLeadPrediction_ = true;
            EVR_LOG("xr: pose prediction: the pose lead is on, so the display period is added as the runtime "
                    "reports it (the lead measures how late frames are shown)");
        }
        return reported;
    }
    const double measured = watch_.baseMs() > 0.0 ? watch_.baseMs() : watch_.referenceMs();
    const double base = pacing::predictionBaseMs(measured, runtimeMs());
    const XrDuration used = pacing::predictionPeriodNs(reported, base, false);
    const bool held = used != reported;
    predictionHeldFrames_ += held ? 1 : 0;
    if (held != predictionHeld_ && predictionLines_ < kMaxPredictionLines) {
        ++predictionLines_;
        if (held) {
            EVR_LOG("xr: pose prediction: the display period %.2f ms is over %.1fx the headset's %.2f ms; "
                    "the head and hands are predicted %.2f ms past the display time instead",
                    static_cast<double>(reported) / 1e6, pacing::kMaxBaseMultiple, base,
                    static_cast<double>(used) / 1e6);
        } else {
            EVR_LOG("xr: pose prediction: one display period (%.2f ms) past the display time again",
                    static_cast<double>(reported) / 1e6);
        }
        if (predictionLines_ == kMaxPredictionLines) {
            EVR_LOG("xr: pose prediction: %u lines logged; later holds are counted in the refresh summary",
                    kMaxPredictionLines);
        }
    }
    predictionHeld_ = held;
    return used;
}

double RefreshLog::baseMs() const {
    const double measured = watch_.baseMs() > 0.0 ? watch_.baseMs() : watch_.referenceMs();
    return pacing::summaryBase(measured, runtimeMs());
}

void RefreshLog::logSummary(const char* when) {
    const pacing::RefreshShares shares = pacing::shares(watch_.times(), baseMs());
    const std::string text = pacing::summaryText(shares, watch_.changes());
    if (!text.empty()) {
        EVR_LOG("xr: refresh summary%s: %s", when, text.c_str());
        status::field("throttled_share", pacing::throttledShareValue(shares));
    }
    if (predictionHeldFrames_ > 0) {
        EVR_LOG("xr: pose prediction held to %.1fx the headset's refresh period in %llu frame(s) so far",
                pacing::kMaxBaseMultiple, static_cast<unsigned long long>(predictionHeldFrames_));
    }
}

} // namespace evr::vkcore
