#pragma once

// The game's grips of the held items with the weapon in the left hand (vkcore/left_hand_arms.cpp), kept
// across levels.
//
// A grip is read from the game's own pose of an arm while the game has it and the gun's root holds still
// (left_hand.hpp, SteadyPose), so the first raise of each item has no grip yet: the arms stay the game's and
// the model sits by the right glove's row until one is read, then moves into the left hand (rig: 0.8 to 2.4
// cm on the guns, 68.6 cm and 59 degrees on the Crucible). The game's animations are the same on every level,
// so a grip read on one level holds on the next: the book keeps each item's last steady grip by the item's
// decl name, and new hands (a level) drop only what is keyed by the decl pointer, which a new level may give
// to another item. The first steady read on the new hands replaces the kept grip.
//
// Keys are the item's decl pointer on the current hands; its name is learned once per pointer and hands.
// Pure: no game memory, tested on its own.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace evr::arm {

template <typename Grip>
class GripBook {
public:
    // Items per hands and names kept; one past it is not kept (its arms stay the game's).
    static constexpr std::size_t kMaxItems = 24;

    // New hands (a level, or the arms given back): every pointer key is forgotten, the grips by name kept.
    void newHands() { items_.clear(); }

    // The name learned for `key` on these hands, or nullptr: the caller reads it from the game and calls
    // learn().
    [[nodiscard]] const std::string* nameOf(const void* key) const {
        const Item* item = find(key);
        return item ? &item->name : nullptr;
    }

    // `key`'s name on these hands; false for an empty name (a name the game's memory did not give: every such
    // item would share one grip) and when the hands hold kMaxItems already.
    bool learn(const void* key, std::string name) {
        if (find(key)) {
            return true;
        }
        if (name.empty() || items_.size() >= kMaxItems) {
            return false;
        }
        items_.push_back({key, std::move(name), std::nullopt});
        return true;
    }

    // A steady read of the item `key` (learned on these hands): its grip on these hands and by its name.
    // False when `key` was not learned or the names kept are full.
    bool keep(const void* key, const Grip& grip) {
        Item* item = find(key);
        if (!item) {
            return false;
        }
        item->grip = grip;
        for (auto& [name, kept] : byName_) {
            if (name == item->name) {
                kept = grip;
                return true;
            }
        }
        if (byName_.size() >= kMaxItems) {
            return false;
        }
        byName_.emplace_back(item->name, grip);
        return true;
    }

    // The grip of `key`: read on these hands, else kept by its name from other hands when `keptHolds(grip)`
    // says it still holds, else nullptr.
    template <typename KeptHolds>
    [[nodiscard]] const Grip* gripOf(const void* key, KeptHolds&& keptHolds) const {
        const Item* item = find(key);
        if (!item) {
            return nullptr;
        }
        if (item->grip) {
            return &*item->grip;
        }
        for (const auto& [name, kept] : byName_) {
            if (name == item->name) {
                return keptHolds(kept) ? &kept : nullptr;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const Grip* gripOf(const void* key) const {
        return gripOf(key, [](const Grip&) { return true; });
    }

    // Whether `key`'s grip was read on these hands (not only kept by name).
    [[nodiscard]] bool readHere(const void* key) const {
        const Item* item = find(key);
        return item && item->grip;
    }

private:
    struct Item {
        const void* key = nullptr;
        std::string name;
        std::optional<Grip> grip;
    };

    [[nodiscard]] std::size_t indexOf(const void* key) const {
        for (std::size_t i = 0; i < items_.size(); ++i) {
            if (items_[i].key == key) {
                return i;
            }
        }
        return items_.size();
    }
    [[nodiscard]] const Item* find(const void* key) const {
        const std::size_t i = indexOf(key);
        return i < items_.size() ? &items_[i] : nullptr;
    }
    Item* find(const void* key) {
        const std::size_t i = indexOf(key);
        return i < items_.size() ? &items_[i] : nullptr;
    }

    std::vector<Item> items_;
    std::vector<std::pair<std::string, Grip>> byName_;
};

} // namespace evr::arm
