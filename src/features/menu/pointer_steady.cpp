#include "features/menu/pointer_steady.hpp"

#include <cstddef>

namespace evr::menu {

PointerSteady::PointerSteady(std::optional<input::OneEuroParams> params) {
    if (params) {
        for (std::optional<input::OneEuroRotation>& f : filters_) {
            f.emplace(*params);
        }
    }
}

Pose PointerSteady::update(input::Hand hand, const Pose& aim, double seconds) {
    std::optional<input::OneEuroRotation>& f = filters_[static_cast<std::size_t>(hand)];
    if (!f) {
        return aim;
    }
    Pose steady = aim;
    steady.orientation = f->update(aim.orientation, seconds);
    return steady;
}

void PointerSteady::reset(input::Hand hand) {
    if (std::optional<input::OneEuroRotation>& f = filters_[static_cast<std::size_t>(hand)]) {
        f->reset();
    }
}

} // namespace evr::menu
