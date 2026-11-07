#include "features/roomscale/late_trace.hpp"

#include <cmath>

namespace evr::roomscale {

bool taskFinished(std::uint32_t state) {
    return (state & 7u) == 4u;
}

bool syncQueryAllowed(const PhysicsState& physics) {
    if (!physics.stepInFlight || *physics.stepInFlight != 0) {
        return false;
    }
    return !physics.taskPresent || (physics.taskState && taskFinished(*physics.taskState));
}

bool postStepIsOurs(std::uintptr_t rbx, std::uintptr_t ourPhysics) {
    return ourPhysics != 0 && rbx == ourPhysics;
}

void LateTrace::ask(Vec3 from, Vec3 to) {
    asked_ = LateAsk{from, to, ++asks_};
}

void LateTrace::answerNow(Vec3 from, Vec3 to, float fraction) {
    asked_.reset();
    taken_.reset();
    answeredAsk_ = LateAsk{from, to, ++asks_};
    answer_ = fraction;
}

std::optional<LateAsk> LateTrace::take() {
    if (!asked_) {
        return std::nullopt;
    }
    taken_ = asked_;
    asked_.reset();
    return taken_;
}

void LateTrace::answer(float fraction) {
    if (!taken_) {
        return;
    }
    answeredAsk_ = taken_;
    answer_ = fraction;
    taken_.reset();
}

void LateTrace::forget() {
    asked_.reset();
    taken_.reset();
    answeredAsk_.reset();
}

std::optional<float> LateTrace::fraction(Vec3 from, float fromSlack) const {
    if (!answeredAsk_ || !std::isfinite(answer_) || answer_ < 0.0f || answer_ > 1.0f ||
        asks_ - answeredAsk_->number > kLateMaxAsksBehind) {
        return std::nullopt;
    }
    const Vec3 moved = from - answeredAsk_->from;
    if (!(length(moved) <= fromSlack)) {
        return std::nullopt;
    }
    return answer_;
}

LateQuery lateQuery(std::size_t channel) {
    return channel == kLateHead ? LateQuery{kHeadContents, false} : LateQuery{kShotContents, true};
}

LateTrace* LateChannels::use(std::size_t channel, std::uintptr_t world, std::int32_t spawnId) {
    if (channel >= kLateChannels) {
        return nullptr;
    }
    Channel& c = channels_[channel];
    if (c.world != world) {
        c.trace.forget(); // another map: what was asked or answered was for the old one
        c.world = world;
    }
    c.spawnId = spawnId;
    return &c.trace;
}

void LateChannels::forget(std::size_t first, std::size_t count) {
    for (std::size_t i = first; i < kLateChannels && i - first < count; ++i) {
        channels_[i].trace.forget();
    }
}

std::array<std::optional<LateChannels::Taken>, kLateChannels> LateChannels::takeFor(std::uintptr_t world,
                                                                                    std::uint64_t& dropped) {
    std::array<std::optional<Taken>, kLateChannels> out{};
    for (std::size_t i = 0; i < kLateChannels; ++i) {
        Channel& c = channels_[i];
        const std::optional<LateAsk> ask = c.trace.take();
        if (!ask) {
            continue;
        }
        if (world == 0 || c.world != world) {
            ++dropped;
            continue;
        }
        out[i] = Taken{i, *ask, c.spawnId, lateQuery(i)};
    }
    return out;
}

void LateChannels::answer(std::size_t channel, float fraction) {
    if (channel < kLateChannels) {
        channels_[channel].trace.answer(fraction);
    }
}

} // namespace evr::roomscale
