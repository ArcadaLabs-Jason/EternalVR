#pragma once

// What a multiplayer guard trip gives back: the cvars the layer wrote at run time, each with the value it had
// before the layer's first write (vkcore/cvar_book.hpp, docs/rig-findings/mp-guard.md). Portable so the
// rules can be tested without the game.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace evr::mp_policy {

// Cvar names are compared as the engine does, without regard to ASCII case.
bool sameCvarName(std::string_view a, std::string_view b);

// The text to hand the engine's setter for a cvar whose values block reads `integer` and `number` (its
// integer and float values): the integer when the float equals it, else the shortest decimal that reads back
// as the same float. The cvars the layer writes are all numbers.
std::string cvarValueText(int integer, float number);

// Why a trip leaves this cvar as the layer set it, or nullptr when the trip gives it back. The window and
// present cvars (r_windowWidth, r_windowHeight, r_fullscreen, r_swapInterval) and HDR output (r_hdrDisplay)
// stay: writing them would resize the window, switch its mode or change the swapchain while the game keeps
// running, and none of them changes play.
const char* cvarLeftOnTrip(std::string_view name);

// The value each cvar had before the layer's first write, kept once and handed back once. `Target` is the
// owner's handle for the cvar (its object and setter). Not thread-safe: the owner's lock.
template <typename Target>
class SavedCvars {
public:
    struct Entry {
        std::string name;
        std::string value; // before the layer's first write (cvarValueText)
        Target target{};
    };

    // Before the layer writes over `name`: keeps `value`, its value now, unless a value is kept already (a
    // later write, after the game put its own value back, keeps the first one) or the values were taken.
    // True when this call kept it.
    bool beforeWrite(std::string_view name, std::string value, Target target) {
        if (taken_ || saved(name)) {
            return false;
        }
        entries_.push_back({std::string(name), std::move(value), std::move(target)});
        return true;
    }

    [[nodiscard]] bool saved(std::string_view name) const {
        for (const Entry& e : entries_) {
            if (sameCvarName(e.name, name)) {
                return true;
            }
        }
        return false;
    }

    // Every kept value, once, in the order of the first writes; later calls give nothing and nothing is kept
    // after it.
    std::vector<Entry> takeAll() {
        taken_ = true;
        return std::exchange(entries_, {});
    }

    [[nodiscard]] bool taken() const { return taken_; }
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

private:
    std::vector<Entry> entries_;
    bool taken_ = false;
};

} // namespace evr::mp_policy
