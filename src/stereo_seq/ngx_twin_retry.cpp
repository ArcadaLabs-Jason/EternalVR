#include "stereo_seq/ngx_twin_retry.hpp"

namespace evr::stereo_seq {

std::uint64_t ngxTwinRetryMs(std::uint32_t failures) {
    std::uint64_t wait = kNgxTwinRetryMs;
    // Doubling stops at the cap, so a long run of failures never shifts past the width of the type.
    for (std::uint32_t i = 1; i < failures && wait < kNgxTwinRetryMaxMs; ++i) {
        wait *= 2;
    }
    return wait < kNgxTwinRetryMaxMs ? wait : kNgxTwinRetryMaxMs;
}

void NgxTwinRetry::failed(std::uint64_t nowMs) {
    recovering_ = true;
    if (fallback_) {
        return; // the game's other feature, in the same try
    }
    if (failures_ < UINT32_MAX) {
        ++failures_;
    }
    failedAtMs_ = nowMs;
    fallback_ = true;
    now_ = false;
}

bool NgxTwinRetry::created() {
    const bool recovered = recovering_;
    failures_ = 0;
    fallback_ = false;
    now_ = false;
    recovering_ = false;
    return recovered;
}

bool NgxTwinRetry::due(std::uint64_t nowMs) {
    if (!fallback_ || exhausted()) {
        return false;
    }
    if (!now_ && (nowMs < failedAtMs_ || nowMs - failedAtMs_ < ngxTwinRetryMs(failures_))) {
        return false;
    }
    fallback_ = false;
    now_ = false;
    return true;
}

bool NgxTwinRetry::requested() {
    if (!fallback_) {
        return false;
    }
    failures_ = 0;
    now_ = true;
    return true;
}

void NgxTwinRetry::released() {
    if (fallback_) {
        now_ = true;
    }
}

} // namespace evr::stereo_seq
