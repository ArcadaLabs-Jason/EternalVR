#include "vkcore/game_string.hpp"

#include <doctest/doctest.h>

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <string_view>

using evr::vkcore::copyGameString;
using evr::vkcore::GameString;
using evr::vkcore::kGameStringPage;
using evr::vkcore::pageChunk;

namespace {

// Access violations raised while a test runs, counted first-chance (as the layer's crash note sees them),
// then left to the guard that handles them.
std::atomic<int> g_faults{0};

LONG CALLBACK countFault(EXCEPTION_POINTERS* info) {
    if (info && info->ExceptionRecord && info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        g_faults.fetch_add(1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Two readable pages with a no-access page after them, as when a string's allocation ends at the end of a
// region and nothing is mapped after it.
struct EdgePages {
    char* base = nullptr;
    char* end = nullptr; // the first byte that cannot be read
    void* handler = nullptr;

    EdgePages() {
        base = static_cast<char*>(
            VirtualAlloc(nullptr, 3 * kGameStringPage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        REQUIRE(base != nullptr);
        end = base + 2 * kGameStringPage;
        DWORD old = 0;
        REQUIRE(VirtualProtect(end, kGameStringPage, PAGE_NOACCESS, &old));
        g_faults.store(0);
        handler = AddVectoredExceptionHandler(1, countFault);
        REQUIRE(handler != nullptr);
    }
    ~EdgePages() {
        RemoveVectoredExceptionHandler(handler);
        VirtualFree(base, 0, MEM_RELEASE);
    }
    EdgePages(const EdgePages&) = delete;
    EdgePages& operator=(const EdgePages&) = delete;

    // `text` placed so that its NUL is the last readable byte.
    const char* atEnd(std::string_view text) const {
        char* at = end - text.size() - 1;
        std::memcpy(at, text.data(), text.size());
        at[text.size()] = '\0';
        return at;
    }
};

} // namespace

TEST_CASE("a game string's chunks end at the next page") {
    CHECK(pageChunk(0x1000, 259) == 259);
    CHECK(pageChunk(0x1FF0, 259) == 0x10);
    CHECK(pageChunk(0x1FFF, 259) == 1);
    CHECK(pageChunk(0x2000, 8000) == kGameStringPage);
    CHECK(pageChunk(0x2003, 0) == 0);
}

TEST_CASE("a map name that ends at the end of its page is read whole, with no fault") {
    EdgePages pages;
    const char* map = pages.atEnd("game/sp/e1m3_cult/e1m3_cult");
    char name[260] = {};
    const GameString read = copyGameString(name, sizeof(name), map);
    CHECK(read.complete);
    CHECK(read.length == 27);
    CHECK(std::strcmp(name, "game/sp/e1m3_cult/e1m3_cult") == 0);
    CHECK(g_faults.load() == 0);
}

TEST_CASE("a name across a page boundary is read whole, with no fault") {
    EdgePages pages;
    char* at = pages.base + kGameStringPage - 100; // 100 bytes on the first page, the rest on the second
    std::memset(at, 'a', 299);
    at[299] = '\0';
    char name[400] = {};
    const GameString read = copyGameString(name, sizeof(name), at);
    CHECK(read.complete);
    CHECK(read.length == 299);
    CHECK(g_faults.load() == 0);
}

TEST_CASE("a name longer than the buffer is cut, terminated, and not read past the cut") {
    EdgePages pages;
    char* at = pages.end - 64;
    std::memset(at, 'b', 64); // no NUL before the no-access page
    char name[16] = {};
    const GameString read = copyGameString(name, sizeof(name), at);
    CHECK_FALSE(read.complete);
    CHECK(read.length == 15);
    CHECK(std::strcmp(name, "bbbbbbbbbbbbbbb") == 0);
    CHECK(g_faults.load() == 0);
}

TEST_CASE("a name that runs into a page that cannot be read keeps what was readable") {
    EdgePages pages;
    char* at = pages.end - 10;
    std::memset(at, 'c', 10); // its NUL would be on the no-access page
    char name[64] = {};
    const GameString read = copyGameString(name, sizeof(name), at);
    CHECK_FALSE(read.complete);
    CHECK(read.length == 10);
    CHECK(std::strcmp(name, "cccccccccc") == 0);
    CHECK(g_faults.load() == 1); // the string really runs on into the bad page: that one is handled
}

TEST_CASE("an unreadable or missing name reads as empty") {
    EdgePages pages;
    char name[32] = {'x', 'y'};
    GameString read = copyGameString(name, sizeof(name), pages.end);
    CHECK_FALSE(read.complete);
    CHECK(read.length == 0);
    CHECK(name[0] == '\0');
    name[0] = 'x';
    read = copyGameString(name, sizeof(name), nullptr);
    CHECK(read.length == 0);
    CHECK(name[0] == '\0');
    CHECK(copyGameString(nullptr, 8, pages.atEnd("x")).length == 0);
    CHECK(copyGameString(name, 0, pages.atEnd("x")).length == 0);
}
