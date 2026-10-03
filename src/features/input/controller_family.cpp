#include "features/input/controller_family.hpp"

namespace evr::input {

game::Controller pickControllerFamily(std::optional<game::Controller> left,
                                      std::optional<game::Controller> right,
                                      game::Controller current) {
    if (!left || !right) {
        return right ? *right : left.value_or(current);
    }
    if (*left == *right || *left == current) {
        return *left;
    }
    return *right;
}

bool buildControlMapAhead(bool synced, bool profileReported, bool scripted) {
    // Before a controller is known the family is only the default (Touch): an Index player's menus would
    // name Touch's buttons until the next check.
    return synced && (profileReported || scripted);
}

} // namespace evr::input
