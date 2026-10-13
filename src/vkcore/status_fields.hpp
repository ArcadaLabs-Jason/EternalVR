#pragma once

// The status file's keys after its first five, and whether the file on disk has caught up with them
// (status_file.hpp); pure, so the write-once and lost-write rules are tested without a file.

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace evr::vkcore::status {

// The keys in the order first set.
class Fields {
public:
    // Sets `key` to `value`; with `once`, a key already set keeps its first value. True when a value changed.
    bool set(const std::string& key, const std::string& value, bool once) {
        auto it =
            std::find_if(items_.begin(), items_.end(), [&key](const auto& f) { return f.first == key; });
        if (it == items_.end()) {
            items_.emplace_back(key, value);
            return true;
        }
        if (once || it->second == value) {
            return false;
        }
        it->second = value;
        return true;
    }

    const std::vector<std::pair<std::string, std::string>>& items() const { return items_; }

private:
    std::vector<std::pair<std::string, std::string>> items_;
};

// Whether the last save reached the file: a save whose move failed every try leaves the file behind, and the
// next call writes it again even when nothing changed.
class SaveState {
public:
    enum class Outcome {
        Saved,     // the move worked (as the one before)
        Lost,      // the move failed, the first time since the last good one (logged once)
        StillLost, // failed again
        Recovered, // worked after a lost one (logged)
    };

    // Whether a call must write the file: its value changed, or the last save was lost.
    bool due(bool changed) const { return changed || dirty_; }

    bool dirty() const { return dirty_; }

    // Records a save's move.
    Outcome saved(bool moved) {
        const bool was = dirty_;
        dirty_ = !moved;
        if (moved) {
            return was ? Outcome::Recovered : Outcome::Saved;
        }
        return was ? Outcome::StillLost : Outcome::Lost;
    }

private:
    bool dirty_ = false;
};

// The rate of the saves flush() makes while the file is behind: the first after a loss at once, then at most
// once per interval.
class FlushGate {
public:
    explicit FlushGate(std::uint64_t intervalMs) : intervalMs_(intervalMs) {}

    // A save was lost (SaveState::Outcome::Lost): the next flush goes at once.
    void lost() { armed_ = false; }

    // Whether a flush at `nowMs` saves; true takes the slot.
    bool due(std::uint64_t nowMs) {
        if (armed_ && nowMs - lastMs_ < intervalMs_) {
            return false;
        }
        armed_ = true;
        lastMs_ = nowMs;
        return true;
    }

private:
    std::uint64_t intervalMs_;
    std::uint64_t lastMs_ = 0;
    bool armed_ = false;
};

} // namespace evr::vkcore::status
