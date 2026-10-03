#pragma once

// Parallel Eye Rendering's install changes the game's code and read-only data all or nothing where it can
// (view_install.cpp): every range a change writes is made writable before the first change, and each one's
// protection is put back after the last. A write to a range that is already writable cannot fail.

#include <windows.h>

#include <cstddef>
#include <vector>

namespace evr::vkcore {

struct CodeRange {
    std::byte* at = nullptr;
    std::size_t size = 0;
    DWORD writable = PAGE_EXECUTE_READWRITE; // PAGE_READWRITE for read-only data
};

class WritableRanges {
public:
    // Makes every range writable. False, with each range made writable so far put back, when one cannot be.
    bool make(const std::vector<CodeRange>& ranges) {
        for (const CodeRange& r : ranges) {
            DWORD old = 0;
            if (!VirtualProtect(r.at, r.size, r.writable, &old)) {
                restore();
                return false;
            }
            ranges_.push_back(r);
            old_.push_back(old);
        }
        return true;
    }

    // Puts each range's protection back, in reverse order (ranges can share a page, and the first one saw the
    // original protection), and flushes the instruction cache.
    void restore() {
        for (std::size_t i = ranges_.size(); i-- > 0;) {
            DWORD ignored = 0;
            VirtualProtect(ranges_[i].at, ranges_[i].size, old_[i], &ignored);
        }
        ranges_.clear();
        old_.clear();
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    }

private:
    std::vector<CodeRange> ranges_;
    std::vector<DWORD> old_;
};

} // namespace evr::vkcore
