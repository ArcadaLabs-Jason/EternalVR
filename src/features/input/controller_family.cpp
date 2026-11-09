#include "features/input/controller_family.hpp"

#include <cstddef>
#include <string_view>

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

bool familyListed(std::string_view list, std::string_view familyName) {
    if (list.find_first_not_of(" ,") == std::string_view::npos) {
        return true;
    }
    while (!list.empty()) {
        const std::size_t comma = list.find(',');
        std::string_view name = list.substr(0, comma);
        const std::size_t first = name.find_first_not_of(' ');
        name = first == std::string_view::npos ? std::string_view{} : name.substr(first);
        name = name.substr(0, name.find_last_not_of(' ') + 1);
        if (name == familyName) {
            return true;
        }
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
    }
    return false;
}

} // namespace evr::input
