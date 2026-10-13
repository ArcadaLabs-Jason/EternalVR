#include "vkcore/adapter_order.hpp"

#include <cstdio>
#include <cstring>

namespace evr::vkcore::adapter_order {

bool ruleTakes(const Device& device) {
    return device.type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && device.apiVersion >= VK_API_VERSION_1_1;
}

std::size_t gamePick(const std::vector<Device>& devices) {
    for (std::size_t i = 0; i < devices.size(); ++i) {
        if (ruleTakes(devices[i])) {
            return i;
        }
    }
    return 0;
}

bool reorderCanMatter(const std::vector<Device>& devices) {
    std::size_t taken = 0;
    for (const Device& d : devices) {
        taken += ruleTakes(d) ? 1 : 0;
    }
    return taken != 1;
}

std::optional<std::size_t> findLuid(const std::vector<Device>& devices, const Luid& luid) {
    std::optional<std::size_t> first;
    for (std::size_t i = 0; i < devices.size(); ++i) {
        if (devices[i].luid && *devices[i].luid == luid) {
            if (ruleTakes(devices[i])) {
                return i;
            }
            first = first ? first : i;
        }
    }
    return first;
}

std::optional<std::size_t> firstOther(const std::vector<Device>& devices, const Luid& luid) {
    for (std::size_t i = 0; i < devices.size(); ++i) {
        if (!devices[i].luid || *devices[i].luid != luid) {
            return i;
        }
    }
    return std::nullopt;
}

Plan plan(const std::vector<Device>& devices, std::optional<std::size_t> target, bool allowCut) {
    Plan result;
    for (std::size_t i = 0; i < devices.size(); ++i) {
        result.order.push_back(i);
    }
    if (devices.size() < 2 || !target || *target >= devices.size()) {
        return result;
    }
    const std::size_t picked = gamePick(devices);
    const std::optional<Luid>& luid = devices[*target].luid;
    if (picked == *target || (luid && devices[picked].luid == luid)) {
        result.action = Action::AlreadyPicked;
        return result;
    }
    std::vector<std::size_t> order{*target};
    std::vector<Device> moved{devices[*target]};
    for (std::size_t i = 0; i < devices.size(); ++i) {
        if (i != *target) {
            order.push_back(i);
            moved.push_back(devices[i]);
        }
    }
    if (gamePick(moved) == 0) {
        result.action = Action::Reordered;
        result.order = order;
        return result;
    }
    if (!allowCut) {
        result.action = Action::NotPickable;
        return result;
    }
    result.action = Action::Filtered;
    result.order = {*target};
    return result;
}

std::vector<std::size_t> groupOrder(const std::vector<bool>& holdsTarget, Action action) {
    std::vector<std::size_t> order;
    std::optional<std::size_t> target;
    for (std::size_t i = 0; i < holdsTarget.size(); ++i) {
        order.push_back(i);
        if (holdsTarget[i] && !target) {
            target = i;
        }
    }
    if (!target || (action != Action::Reordered && action != Action::Filtered)) {
        return order;
    }
    if (action == Action::Filtered) {
        return {*target};
    }
    std::vector<std::size_t> moved{*target};
    for (std::size_t i = 0; i < holdsTarget.size(); ++i) {
        if (i != *target) {
            moved.push_back(i);
        }
    }
    return moved;
}

const char* actionName(Action action) {
    switch (action) {
    case Action::None:
        return "none";
    case Action::AlreadyPicked:
        return "already picked";
    case Action::Reordered:
        return "reordered";
    case Action::NotPickable:
        return "not pickable";
    case Action::Filtered:
        return "filtered";
    }
    return "?";
}

std::string luidText(const std::optional<Luid>& luid) {
    if (!luid) {
        return "none";
    }
    std::uint32_t low = 0;
    std::int32_t high = 0;
    std::memcpy(&low, luid->data(), 4);
    std::memcpy(&high, luid->data() + 4, 4);
    char text[24];
    std::snprintf(text, sizeof(text), "%08x:%08x", static_cast<unsigned>(high), static_cast<unsigned>(low));
    return text;
}

} // namespace evr::vkcore::adapter_order
