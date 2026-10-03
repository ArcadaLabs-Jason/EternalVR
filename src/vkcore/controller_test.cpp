// Motion controllers: scripted input for rig tests (ETERNALVR_TEST_INPUT, features/input/test_input.hpp),
// and reading the small text files (and folders of them) the controller settings name.

#include "vkcore/controllers_impl.hpp"

#include "features/input/player_controller_data.hpp"
#include "vkcore/log.hpp"

#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";
// How often the file's write time is looked at (game frames).
constexpr std::uint64_t kTestCheckFrames = 10;

std::wstring widen(const std::string& text) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string narrow(const wchar_t* text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    out.pop_back(); // the terminator
    return out;
}

} // namespace

std::optional<std::string> readTextFile(const std::string& utf8Path) {
    std::ifstream file(widen(utf8Path), std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::optional<std::vector<std::string>> folderFileNames(const std::string& utf8Path) {
    const DWORD attributes = GetFileAttributesW(widen(utf8Path).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return std::nullopt;
    }
    std::vector<std::string> names;
    WIN32_FIND_DATAW found{};
    const HANDLE find = FindFirstFileW(widen(input::joinFolderPath(utf8Path, "*")).c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) {
        return names;
    }
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            names.push_back(narrow(found.cFileName));
        }
    } while (FindNextFileW(find, &found));
    FindClose(find);
    return names;
}

void refreshTestInput() {
    // The camera hook and the XR worker both call this (menus run without the game view).
    static std::mutex refreshMutex;
    std::lock_guard refreshLock(refreshMutex);
    State& s = state();
    const std::string& path = settings().testInputPath;
    if (path.empty() || s.testChecks++ % kTestCheckFrames != 0) {
        return;
    }
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(widen(path).c_str(), GetFileExInfoStandard, &attributes)) {
        if (s.testFileTime != 0) {
            s.testFileTime = 0;
            std::lock_guard lock(s.testMutex);
            s.test.reset();
            EVR_LOG("%s: test input '%s' is gone; the runtime's input only", kTag, path.c_str());
        }
        return;
    }
    const ULONGLONG written = (static_cast<ULONGLONG>(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
                              attributes.ftLastWriteTime.dwLowDateTime;
    if (written == s.testFileTime) {
        return;
    }
    const auto text = readTextFile(path);
    if (!text) {
        return; // being written; tried again at the next check
    }
    s.testFileTime = written;
    input::TestInput parsed = input::parseTestInput(*text);
    for (const std::string& issue : parsed.issues) {
        EVR_LOG("%s: test input %s", kTag, issue.c_str());
    }
    EVR_LOG("%s: test input '%s' read (%zu issue(s))", kTag, path.c_str(), parsed.issues.size());
    std::lock_guard lock(s.testMutex);
    s.test = std::move(parsed);
}

bool isPlayerSafe(const std::byte* object) {
    const std::byte* vtable = nullptr;
    return object && safeRead(object, vtable) && state().player.isPlayerVtable(vtable);
}

std::optional<input::TestInput> testInput() {
    State& s = state();
    std::lock_guard lock(s.testMutex);
    return s.test;
}

} // namespace evr::vkcore::controllers
