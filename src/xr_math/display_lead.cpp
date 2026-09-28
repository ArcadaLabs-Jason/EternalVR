#include "xr_math/display_lead.hpp"

#include <algorithm>

namespace evr::xr_math {

void DisplayLead::noteShown(std::uint64_t seq, std::int64_t lateNs, std::int64_t periodNs) {
    if (seq == 0 || seq == lastSeq_ || periodNs <= 0) {
        return;
    }
    lastSeq_ = seq;
    const double period = static_cast<double>(periodNs);
    const double error = std::clamp(static_cast<double>(lateNs), -period, period);
    lead_ = std::clamp(lead_ + kGain * error, 0.0, kMaxPeriods * period);
}

} // namespace evr::xr_math
