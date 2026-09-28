#include "vkcore/test_keys.hpp"

#include "vkcore/key_inject.hpp"
#include "vkcore/log.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <optional>
#include <string>
#include <vector>

namespace evr::vkcore::test_keys {

namespace {

struct Press {
    ULONGLONG at = 0;   // ms after the first poll
    ULONGLONG hold = 0; // ms
    std::uint8_t key = 0;
    std::string name;
    bool down = false;
    bool done = false;
};

std::optional<std::uint8_t> virtualKey(const std::wstring& name) {
    struct Named {
        const wchar_t* name;
        std::uint8_t key;
    };
    static constexpr Named kNamed[] = {{L"ESC", VK_ESCAPE}, {L"ENTER", VK_RETURN}, {L"SPACE", VK_SPACE},
                                       {L"TAB", VK_TAB},    {L"UP", VK_UP},        {L"DOWN", VK_DOWN},
                                       {L"LEFT", VK_LEFT},  {L"RIGHT", VK_RIGHT}};
    for (const Named& n : kNamed) {
        if (name == n.name) {
            return n.key;
        }
    }
    if (name.size() == 1 && ((name[0] >= L'A' && name[0] <= L'Z') || (name[0] >= L'0' && name[0] <= L'9'))) {
        return static_cast<std::uint8_t>(name[0]);
    }
    if (name.size() >= 2 && name[0] == L'F') {
        const unsigned long n = std::wcstoul(name.c_str() + 1, nullptr, 10);
        if (n >= 1 && n <= 12) {
            return static_cast<std::uint8_t>(VK_F1 + n - 1);
        }
    }
    return std::nullopt;
}

// "<ms>:<key>[:<hold ms>]" entries separated by commas; a malformed entry is logged and skipped.
std::vector<Press> parse(const std::wstring& text) {
    std::vector<Press> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find(L',', start), text.size());
        const std::wstring entry = text.substr(start, end - start);
        start = end + 1;
        if (entry.empty()) {
            continue;
        }
        const std::size_t c1 = entry.find(L':');
        const std::size_t c2 = c1 == std::wstring::npos ? c1 : entry.find(L':', c1 + 1);
        const std::wstring keyName = c1 == std::wstring::npos
                                         ? L""
                                         : entry.substr(c1 + 1, c2 == std::wstring::npos ? c2 : c2 - c1 - 1);
        const std::optional<std::uint8_t> key = virtualKey(keyName);
        if (!key) {
            EVR_LOG("test keys: \"%ls\" is not <ms>:<key>[:<hold ms>]; skipped", entry.c_str());
            continue;
        }
        Press p;
        p.at = std::wcstoull(entry.c_str(), nullptr, 10);
        p.hold = c2 == std::wstring::npos ? 100 : std::wcstoull(entry.c_str() + c2 + 1, nullptr, 10);
        p.key = *key;
        for (const wchar_t c : keyName) {
            p.name.push_back(static_cast<char>(c)); // ASCII key names only (virtualKey accepted them)
        }
        out.push_back(p);
    }
    return out;
}

struct State {
    bool read = false;
    ULONGLONG start = 0;
    std::vector<Press> presses;
};

} // namespace

void poll(HWND gameWindow) {
    static State s;
    if (!s.read) {
        s.read = true;
        std::wstring text;
        if (readEnv(L"ETERNALVR_TEST_KEYS", text) && !text.empty()) {
            s.presses = parse(text);
            s.start = GetTickCount64();
            EVR_LOG("test keys: %zu scripted key press(es) (ETERNALVR_TEST_KEYS)", s.presses.size());
        }
    }
    if (s.presses.empty() || !gameWindow) {
        return;
    }
    const ULONGLONG now = GetTickCount64() - s.start;
    for (Press& p : s.presses) {
        if (p.done || now < p.at) {
            continue;
        }
        if (!p.down) {
            p.down = injectKey(p.key, true, gameWindow);
            EVR_LOG("test keys: %s down at %llu ms%s", p.name.c_str(), static_cast<unsigned long long>(now),
                    p.down ? "" : " (injection unavailable)");
            p.done = !p.down;
        } else if (now >= p.at + p.hold) {
            injectKey(p.key, false, gameWindow);
            p.done = true;
        }
    }
}

} // namespace evr::vkcore::test_keys
