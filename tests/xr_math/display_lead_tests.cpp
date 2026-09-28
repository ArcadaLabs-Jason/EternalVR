#include "xr_math/display_lead.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using evr::xr_math::DisplayLead;

namespace {

constexpr std::int64_t kPeriod = 11'111'111; // 90 Hz

// Views shown `latePeriods` after predictedDisplayTime + one period, with the lead in force when each
// was predicted.
void showViews(DisplayLead& lead, std::uint64_t& seq, int count, double latePeriods) {
    for (int i = 0; i < count; ++i) {
        const auto late = static_cast<std::int64_t>(latePeriods * static_cast<double>(kPeriod));
        lead.noteShown(++seq, late - lead.leadNs(), kPeriod);
    }
}

} // namespace

TEST_CASE("the lead starts at zero and follows views shown two periods late") {
    DisplayLead lead;
    std::uint64_t seq = 0;
    CHECK(lead.leadNs() == 0);
    showViews(lead, seq, 200, 2.0);
    CHECK(lead.leadNs() == doctest::Approx(2.0 * kPeriod).epsilon(0.01));
}

TEST_CASE("the lead settles on the mean lateness of a mix") {
    DisplayLead lead;
    std::uint64_t seq = 0;
    // One, two and three periods late in turn: the mean is two.
    for (int i = 0; i < 200; ++i) {
        showViews(lead, seq, 1, 1.0);
        showViews(lead, seq, 1, 2.0);
        showViews(lead, seq, 1, 3.0);
    }
    CHECK(lead.leadNs() == doctest::Approx(2.0 * kPeriod).epsilon(0.1));
}

TEST_CASE("the lead stays within zero and two periods") {
    DisplayLead lead;
    std::uint64_t seq = 0;
    showViews(lead, seq, 400, 5.0);
    CHECK(lead.leadNs() == static_cast<std::int64_t>(DisplayLead::kMaxPeriods * kPeriod));
    showViews(lead, seq, 400, -1.0);
    CHECK(lead.leadNs() == 0);
}

TEST_CASE("repeats of a view and a single hitch barely move the lead") {
    DisplayLead lead;
    std::uint64_t seq = 0;
    showViews(lead, seq, 200, 1.0);
    const std::int64_t settled = lead.leadNs();
    for (int i = 0; i < 20; ++i) {
        lead.noteShown(seq, 3 * kPeriod, kPeriod); // the same view again
    }
    CHECK(lead.leadNs() == settled);
    lead.noteShown(++seq, 60 * kPeriod, kPeriod); // a hitch: 60 periods late
    CHECK(lead.leadNs() - settled <= static_cast<std::int64_t>(DisplayLead::kGain * kPeriod) + 1);
}

TEST_CASE("reset returns to no lead") {
    DisplayLead lead;
    std::uint64_t seq = 0;
    showViews(lead, seq, 100, 2.0);
    lead.reset();
    CHECK(lead.leadNs() == 0);
}
