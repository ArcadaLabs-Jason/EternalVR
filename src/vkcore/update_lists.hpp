#pragma once

// Parallel Eye Rendering: the union of the two views' update lists (view_updates.hpp). Each render view has
// four lists of model indices its gather fills (particles, flares, beams, ribbons; 0x2000 entries each), and
// the world update prepares and updates the models on view 0's only. View 1's entries that are not on view
// 0's lists are added to them, within each list's room. Pure logic, unit-tested
// (tests/vkcore/update_lists_tests.cpp).

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace evr::vkcore::update_lists {

inline constexpr int kLists = 4;                  // particles, flares, beams, ribbons
inline constexpr std::int32_t kCapacity = 0x2000; // entries per list (0x8000 bytes apart, the counts after)

struct ListCounts {
    std::uint32_t added = 0;   // only on view 1's list: added to view 0's
    std::uint32_t already = 0; // on view 0's lists, or added from view 1's before
    std::uint32_t dropped = 0; // view 0's list had no room left
    std::uint32_t invalid = 0; // not a model index of the world
};

// One frame's union for one world. Not thread-safe.
class ListUnion {
public:
    // A new frame for a world with `models` models: nothing is marked.
    void start(std::int32_t models) {
        models_ = models > 0 ? models : 0;
        const std::size_t words = (static_cast<std::size_t>(models_) + 63) / 64;
        bits_.assign(words, 0);
    }

    // View 0's entries of a list, as the engine filled them.
    void mark(std::span<const std::int32_t> view0) {
        for (const std::int32_t m : view0) {
            if (valid(m)) {
                set(m);
            }
        }
    }

    // View 1's entries of a list: each one not marked yet is marked and appended to `out` while view 0's list
    // (`view0Count` entries, then `out`) has room.
    ListCounts
    add(std::span<const std::int32_t> view1, std::int32_t view0Count, std::vector<std::int32_t>& out) {
        ListCounts c;
        const std::int32_t room = kCapacity - (view0Count > 0 ? view0Count : 0);
        for (const std::int32_t m : view1) {
            if (!valid(m)) {
                ++c.invalid;
            } else if (marked(m)) {
                ++c.already;
            } else if (static_cast<std::int32_t>(out.size()) >= room) {
                ++c.dropped;
            } else {
                set(m);
                out.push_back(m);
                ++c.added;
            }
        }
        return c;
    }

private:
    bool valid(std::int32_t m) const { return m >= 0 && m < models_; }
    bool marked(std::int32_t m) const { return (bits_[static_cast<std::size_t>(m) / 64] >> (m % 64)) & 1; }
    void set(std::int32_t m) { bits_[static_cast<std::size_t>(m) / 64] |= std::uint64_t{1} << (m % 64); }

    std::vector<std::uint64_t> bits_;
    std::int32_t models_ = 0;
};

} // namespace evr::vkcore::update_lists
