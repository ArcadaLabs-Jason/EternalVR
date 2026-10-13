#include "vkcore/adapter_order.hpp"

#include <doctest/doctest.h>

#include <vector>

using evr::vkcore::adapter_order::Action;
using evr::vkcore::adapter_order::Device;
using evr::vkcore::adapter_order::findLuid;
using evr::vkcore::adapter_order::firstOther;
using evr::vkcore::adapter_order::gamePick;
using evr::vkcore::adapter_order::groupOrder;
using evr::vkcore::adapter_order::Luid;
using evr::vkcore::adapter_order::luidText;
using evr::vkcore::adapter_order::plan;
using evr::vkcore::adapter_order::reorderCanMatter;

namespace {

Luid luid(std::uint8_t low) {
    return Luid{low, 0, 0, 0, 0, 0, 0, 0};
}

Device discrete(std::uint8_t low) {
    return {luid(low), VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, VK_API_VERSION_1_3};
}

Device integrated(std::uint8_t low) {
    return {luid(low), VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, VK_API_VERSION_1_3};
}

Device software() {
    return {std::nullopt, VK_PHYSICAL_DEVICE_TYPE_CPU, VK_API_VERSION_1_3};
}

using Order = std::vector<std::size_t>;

} // namespace

TEST_CASE("the game's rule takes the first discrete device of api 1.1 or later, else the first") {
    CHECK(gamePick({}) == 0u);
    CHECK(gamePick({integrated(1), discrete(2)}) == 1u);
    CHECK(gamePick({discrete(1), discrete(2)}) == 0u);
    CHECK(gamePick({software(), integrated(2)}) == 0u);
    Device old = discrete(1);
    old.apiVersion = VK_API_VERSION_1_0;
    CHECK(gamePick({old, discrete(2)}) == 1u);
    CHECK(gamePick({old, integrated(2)}) == 0u);
}

TEST_CASE("devices are found by LUID; a device without one never matches") {
    const std::vector<Device> devices{software(), integrated(2), discrete(3)};
    CHECK(findLuid(devices, luid(3)) == 2u);
    CHECK_FALSE(findLuid(devices, luid(9)).has_value());
    CHECK(firstOther(devices, luid(3)) == 0u);
    CHECK(firstOther({discrete(3), integrated(2)}, luid(3)) == 1u);
    CHECK_FALSE(firstOther({discrete(3)}, luid(3)).has_value());
}

TEST_CASE("one device, no target or a target out of range leave the list as it is") {
    CHECK(plan({discrete(1)}, 0u, false).action == Action::None);
    CHECK(plan({discrete(1)}, 0u, false).order == Order{0});
    CHECK(plan({discrete(1), integrated(2)}, std::nullopt, false).action == Action::None);
    CHECK(plan({discrete(1), integrated(2)}, 5u, false).order == Order{0, 1});
    CHECK(plan({}, std::nullopt, false).order.empty());
}

TEST_CASE("a target the game picks by itself is left in place") {
    const auto p = plan({integrated(1), discrete(2)}, 1u, false);
    CHECK(p.action == Action::AlreadyPicked);
    CHECK(p.order == Order{0, 1});
}

TEST_CASE("a discrete headset GPU behind another discrete GPU moves to the front") {
    const auto p = plan({discrete(1), integrated(2), discrete(3)}, 2u, false);
    CHECK(p.action == Action::Reordered);
    CHECK(p.order == Order{2, 0, 1});
}

TEST_CASE("a device the game would skip for a discrete one is left where it is") {
    // An integrated GPU drives the headset next to a discrete one: in front it would still lose, and the
    // list is not cut (the presenter's LUID check refuses VR).
    const auto p = plan({discrete(1), integrated(2)}, 1u, false);
    CHECK(p.action == Action::NotPickable);
    CHECK(p.order == Order{0, 1});
}

TEST_CASE("with cutting allowed (the rig's forced mismatch) that device is given alone") {
    auto p = plan({discrete(1), integrated(2)}, 1u, true);
    CHECK(p.action == Action::Filtered);
    CHECK(p.order == Order{1});
    p = plan({discrete(1), software()}, 1u, true);
    CHECK(p.action == Action::Filtered);
    CHECK(p.order == Order{1});
    // Cutting is never needed where moving is enough.
    CHECK(plan({discrete(1), integrated(2), discrete(3)}, 2u, true).action == Action::Reordered);
}

TEST_CASE("an integrated target with no discrete device to lose to only moves") {
    const auto p = plan({software(), integrated(2)}, 1u, false);
    CHECK(p.action == Action::Reordered);
    CHECK(p.order == Order{1, 0});
}

TEST_CASE("a target below api 1.1 among discrete devices is not pickable") {
    Device old = discrete(2);
    old.apiVersion = VK_API_VERSION_1_0;
    const auto p = plan({discrete(1), old}, 1u, false);
    CHECK(p.action == Action::NotPickable);
    CHECK(p.order == Order{0, 1});
}

TEST_CASE("two drivers listing one adapter: the device the rule takes is the target") {
    // A translation driver's device (CPU type here) with the headset's LUID ahead of the native one.
    const Device translated{luid(5), VK_PHYSICAL_DEVICE_TYPE_CPU, VK_API_VERSION_1_2};
    const std::vector<Device> devices{translated, discrete(5), integrated(2)};
    CHECK(findLuid(devices, luid(5)) == 1u);
    CHECK(findLuid({translated, integrated(2)}, luid(5)) == 0u);
    // Already on the headset's adapter even when the target named is the other device with its LUID.
    const auto p = plan(devices, 0u, true);
    CHECK(p.action == Action::AlreadyPicked);
    CHECK(p.order == Order{0, 1, 2});
}

TEST_CASE("the runtime is worth asking only when the order can change the game's pick") {
    CHECK_FALSE(reorderCanMatter({discrete(1), integrated(2)}));
    CHECK_FALSE(reorderCanMatter({integrated(2), discrete(1), software()}));
    CHECK(reorderCanMatter({discrete(1), discrete(2)}));
    CHECK(reorderCanMatter({integrated(1), software()}));
    Device old = discrete(2);
    old.apiVersion = VK_API_VERSION_1_0;
    CHECK(reorderCanMatter({old, integrated(1)}));
    CHECK_FALSE(reorderCanMatter({old, discrete(1)}));
}

TEST_CASE("groups follow the device plan") {
    const std::vector<bool> holds{false, true, false};
    CHECK(groupOrder(holds, Action::None) == Order{0, 1, 2});
    CHECK(groupOrder(holds, Action::AlreadyPicked) == Order{0, 1, 2});
    CHECK(groupOrder(holds, Action::NotPickable) == Order{0, 1, 2});
    CHECK(groupOrder(holds, Action::Reordered) == Order{1, 0, 2});
    CHECK(groupOrder(holds, Action::Filtered) == Order{1});
    CHECK(groupOrder({false, false}, Action::Filtered) == Order{0, 1});
    CHECK(groupOrder({true, true}, Action::Reordered) == Order{0, 1});
    CHECK(groupOrder({}, Action::Reordered).empty());
}

TEST_CASE("LUIDs are written high half first, as the presenter logs the runtime's") {
    Luid value{0x78, 0x56, 0x34, 0x12, 0x01, 0, 0, 0};
    CHECK(luidText(value) == "00000001:12345678");
    CHECK(luidText(std::nullopt) == "none");
}
