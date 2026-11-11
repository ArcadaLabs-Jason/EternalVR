#pragma once

// What a multiplayer guard trip gives back: the cvars the layer wrote at run time, each with the value it had
// before the layer's first write (vkcore/cvar_book.hpp, docs/rig-findings/mp-guard.md). Portable so the
// rules can be tested without the game.

#include <cstddef>
#include <exception>
#include <optional>
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
        std::optional<std::string> trip; // what a trip gives back instead of `value` (setTripValue)
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

    [[nodiscard]] bool saved(std::string_view name) const { return find(name) != nullptr; }

    // The kept entry of `name`, or nullptr (none kept, or the values were taken).
    [[nodiscard]] const Entry* find(std::string_view name) const {
        for (const Entry& e : entries_) {
            if (sameCvarName(e.name, name)) {
                return &e;
            }
        }
        return nullptr;
    }

    // A trip gives `value` back for `name` instead of its value before the layer's first write: a cvar the
    // game's profile load set to the player's own value after the layer wrote it (vkcore/profile_hooks.hpp).
    // False when no value is kept for `name`.
    bool setTripValue(std::string_view name, std::string value) {
        for (Entry& e : entries_) {
            if (sameCvarName(e.name, name)) {
                e.trip = std::move(value);
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

struct GiveBack {
    std::size_t restored = 0; // given back (written, or read as the value already)
    std::size_t left = 0;     // left as the layer set them (cvarLeftOnTrip)
    std::size_t failed = 0;   // a read or a report that threw (no memory for its text)
};

// A trip's give-back of `entries` (SavedCvars::takeAll), on the thread that tripped. For each entry not left
// as the layer set it, `write(entry, value)` gives back its trip value or its value before the layer's first
// write, unless `read(entry)` (the value's text now) shows it there already. Each entry stands alone and its
// write comes first: a read that throws still writes, and `report(entry, now, left)` (the log line; `now`
// null when the read failed, `left` the reason it stays or null) runs after the write and may throw without
// keeping a later entry from going back. `write` must not throw (the engine's setter).
template <typename Entry, typename Read, typename Write, typename Report>
GiveBack giveBack(const std::vector<Entry>& entries, Read&& read, Write&& write, Report&& report) noexcept {
    GiveBack out;
    for (const Entry& e : entries) {
        const char* const left = cvarLeftOnTrip(e.name);
        std::string now;
        bool known = false;
        try {
            now = read(e);
            known = true;
        } catch (const std::exception&) {
            ++out.failed;
        }
        if (left) {
            ++out.left;
        } else {
            const std::string& back = e.trip ? *e.trip : e.value;
            if (!known || now != back) {
                write(e, back);
            }
            ++out.restored;
        }
        try {
            report(e, known ? &now : nullptr, left);
        } catch (const std::exception&) {
            ++out.failed;
        }
    }
    return out;
}

} // namespace evr::mp_policy
