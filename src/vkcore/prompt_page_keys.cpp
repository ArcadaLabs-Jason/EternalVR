#include "vkcore/prompt_page_keys.hpp"

#include "vkcore/controllers_impl.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/window_timing.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace evr::vkcore::controllers {

namespace {

// The game's key numbers of the page keys, A and D.
constexpr std::int32_t kGameKeyA = 0x1E;
constexpr std::int32_t kGameKeyD = 0x20;

constexpr std::string_view kPaginator = "paginator";
// How far into the paginator update its KeyNameText calls (+0x314, +0x40E) and the arrows' flag (+0xCE) lie.
constexpr std::size_t kPaginatorSpan = 0x600;
// Each call loads its key first: `mov ecx, 0x1E` (A), `mov ecx, 0x20` (D).
constexpr std::array<const char*, 2> kKeyLoads{"B9 1E 00 00 00", "B9 20 00 00 00"};
// The arrows are up when the pages number more than one: `setg al; mov [rsi+disp32], al`.
constexpr const char* kArrowsFlag = "0F 9F C0 88 86";
constexpr std::int32_t kMaxField = 0x1000;

// TabList's update (0x1591E70, Game Pass 0x15941F0) past the test that its sprite is visible: `mov
// [rsp+0x30], rdi; call <input platform>; mov edi, eax; cmp eax, [rbx+0x214]; je; mov rcx, rbx; call <key
// text writer>`, the writer being prompt_hooks.cpp's "TabList update" (0x1592670). Hooked at the `mov`; rbx
// is the tab list.
constexpr const char* kTabListVisible =
    "48 89 7C 24 30 E8 ?? ?? ?? ?? 8B F8 3B 83 ?? ?? 00 00 74 ?? 48 8B CB E8";
constexpr std::size_t kWriterCall = 23; // the writer's `call` in kTabListVisible
// The writer reads the tab count (`cmp dword ptr [rbx+0xA8], 1`) and loads each keyboard key right before its
// KeyNameText call (`mov ecx, [rbx+disp32]; call`: +0x20C, then +0x210).
constexpr const char* kTabCount = "83 BB ?? ?? 00 00 01";
constexpr std::size_t kTabCountSpan = 0x80;
constexpr std::size_t kWriterSpan = 0x300;
constexpr int kTabListLines = 12;
constexpr int kChangeLines = 20;

std::int32_t g_arrowsField = 0;
std::atomic<std::uint64_t> g_rewardsAt{0}; // window_timing::nowMicros() of the Rewards tab's last update
std::atomic<bool> g_arrows{false};         // its arrows were up then
std::atomic<bool> g_rewardsLogged{false};

std::int32_t g_tabCountField = 0;
std::array<std::int32_t, 2> g_tabKeyFields{};
std::atomic<std::uint64_t> g_tabsAt{
    0}; // the last update of a visible tab list on A / D with two tabs or more
std::array<std::atomic<std::uintptr_t>, kTabListLines> g_tabListsLogged{};

menu::PageKeys g_lastKeys; // XR worker only
int g_changeLines = 0;     // XR worker only

void onRewardsUpdate(const HookRegisters& r) {
    std::uint8_t arrows = 0;
    const bool read = safeRead(reinterpret_cast<const std::byte*>(r.rcx) + g_arrowsField, arrows);
    g_arrows.store(read && arrows != 0, std::memory_order_relaxed);
    g_rewardsAt.store(window_timing::nowMicros(), std::memory_order_relaxed);
    if (!g_rewardsLogged.exchange(true, std::memory_order_relaxed)) {
        EVR_LOG("prompts: the Rewards tab is up (its arrows %s)", read && arrows != 0 ? "up" : "hidden");
    }
}

// The first time each tab list shows (the first kTabListLines): its keyboard keys and tab count.
void logTabList(std::uintptr_t tabList, std::int32_t previous, std::int32_t next, std::int32_t tabs) {
    for (std::atomic<std::uintptr_t>& slot : g_tabListsLogged) {
        std::uintptr_t seen = slot.load(std::memory_order_relaxed);
        if (seen == tabList) {
            return;
        }
        if (seen == 0 && slot.compare_exchange_strong(seen, tabList, std::memory_order_relaxed)) {
            const bool pages = previous == kGameKeyA && next == kGameKeyD && tabs > 1;
            EVR_LOG("prompts: a tab list shows: keys 0x%X / 0x%X, %d tabs%s", static_cast<unsigned>(previous),
                    static_cast<unsigned>(next), tabs, pages ? " (a stick left / right sends A / D)" : "");
            return;
        }
    }
}

void onTabListVisible(const HookRegisters& r) {
    const auto* tabList = reinterpret_cast<const std::byte*>(r.rbx);
    std::int32_t previous = 0;
    std::int32_t next = 0;
    std::int32_t tabs = 0;
    if (!safeRead(tabList + g_tabKeyFields[0], previous) || !safeRead(tabList + g_tabKeyFields[1], next) ||
        !safeRead(tabList + g_tabCountField, tabs)) {
        return;
    }
    if (previous == kGameKeyA && next == kGameKeyD && tabs > 1) {
        g_tabsAt.store(window_timing::nowMicros(), std::memory_order_relaxed);
    }
    logTabList(r.rbx, previous, next, tabs);
}

// The paginator update: the one function that loads "paginator" and calls KeyNameText exactly twice, each
// call after its key's load. nullptr (logged) otherwise; `returns` gets the calls' return addresses.
const std::byte* findPaginator(const GameImage& image,
                               const std::byte* keyNameText,
                               std::array<std::uintptr_t, 2>& returns,
                               const char* tag) {
    const std::byte* text = findUniqueString(image, kPaginator);
    const std::byte* found = nullptr;
    int count = 0;
    for (const std::byte* at :
         text != nullptr ? findLeaReferences(image, text) : std::vector<const std::byte*>{}) {
        const std::byte* function = functionStart(image, at);
        const std::vector<std::uintptr_t> calls =
            function != nullptr ? callReturns(image, function, keyNameText, kPaginatorSpan)
                                : std::vector<std::uintptr_t>{};
        if (calls.size() != returns.size() || function == found) {
            continue;
        }
        bool loads = true;
        for (std::size_t i = 0; i < calls.size(); ++i) {
            loads =
                loads && matchesAt(image, reinterpret_cast<const std::byte*>(calls[i]) - 10, kKeyLoads[i]);
        }
        if (loads) {
            found = function;
            ++count;
            std::ranges::copy(calls, returns.begin());
        }
    }
    if (count != 1) {
        EVR_LOG("%s: Rewards tab: %d paginator updates with the arrows' two key texts, not 1; its arrows "
                "keep the game's keys",
                tag, count);
        return nullptr;
    }
    return found;
}

// The disp32 at `dispAt` of the single match of `signature` within `span` bytes of `function`, or 0.
std::int32_t uniqueField(const GameImage& image,
                         const std::byte* function,
                         std::size_t span,
                         const char* signature,
                         std::size_t dispAt) {
    std::int32_t field = 0;
    int count = 0;
    for (std::size_t i = 0; i < span && image.inText(function + i, 9); ++i) {
        if (matchesAt(image, function + i, signature)) {
            field = readI32(function + i + dispAt);
            ++count;
        }
    }
    return count == 1 && field > 0 && field < kMaxField ? field : 0;
}

// The start of the only function with a `jmp rel32` to `target`, or nullptr.
const std::byte* findTailCaller(const GameImage& image, const std::byte* target) {
    const std::byte* jump = nullptr;
    int count = 0;
    const std::byte* const end = image.text.data() + image.text.size();
    for (const std::byte* at = image.text.data(); at + 5 <= end; ++at) {
        if (*at == std::byte{0xE9} && at + 5 + readI32(at + 1) == target) {
            jump = at;
            ++count;
        }
    }
    return count == 1 ? functionStart(image, jump) : nullptr;
}

std::array<std::uintptr_t, 2>
installRewards(const GameImage& image, const std::byte* keyNameText, const char* tag) {
    std::array<std::uintptr_t, 2> returns{};
    const std::byte* paginator = findPaginator(image, keyNameText, returns, tag);
    if (paginator == nullptr) {
        return {};
    }
    g_arrowsField = uniqueField(image, paginator, kPaginatorSpan, kArrowsFlag, 5);
    const std::byte* update = g_arrowsField != 0 ? findTailCaller(image, paginator) : nullptr;
    if (update == nullptr) {
        EVR_LOG("%s: Rewards tab: no arrows' flag in the paginator update at RVA 0x%X or no single update "
                "that jumps to it; its arrows keep the game's keys",
                tag, image.rva(paginator));
        return {};
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(update), &onRewardsUpdate, error)) {
        EVR_LOG("%s: Rewards tab: update hook at RVA 0x%X failed: %s; its arrows keep the game's keys", tag,
                image.rva(update), error.c_str());
        return {};
    }
    EVR_LOG("%s: Rewards tab: update hooked at RVA 0x%X; paginator update at RVA 0x%X (arrows' flag +0x%X), "
            "its key texts at 0x%X, 0x%X",
            tag, image.rva(update), image.rva(paginator), static_cast<unsigned>(g_arrowsField),
            image.rva(reinterpret_cast<const std::byte*>(returns[0]) - 5),
            image.rva(reinterpret_cast<const std::byte*>(returns[1]) - 5));
    return returns;
}

// The tab lists' keyboard key fields: the `mov ecx, [rbx+disp32]` right before each of the writer's two
// KeyNameText calls. False when they are not exactly two.
bool findTabKeyFields(const GameImage& image, const std::byte* writer, const std::byte* keyNameText) {
    const std::vector<std::uintptr_t> calls = callReturns(image, writer, keyNameText, kWriterSpan);
    if (calls.size() != g_tabKeyFields.size()) {
        return false;
    }
    for (std::size_t i = 0; i < calls.size(); ++i) {
        const auto* load = reinterpret_cast<const std::byte*>(calls[i]) - 11;
        const std::int32_t field = matchesAt(image, load, "8B 8B") ? readI32(load + 2) : 0;
        if (field <= 0 || field >= kMaxField) {
            return false;
        }
        g_tabKeyFields[i] = field;
    }
    return true;
}

// The hook that tells a visible tab list on A / D (the Dossier's Arsenal, Codex and Challenges pages). True
// when it is in.
bool installTabLists(const GameImage& image,
                     const std::byte* writer,
                     const std::byte* keyNameText,
                     const char* tag) {
    g_tabCountField = writer != nullptr ? uniqueField(image, writer, kTabCountSpan, kTabCount, 2) : 0;
    const bool fields = g_tabCountField != 0 && findTabKeyFields(image, writer, keyNameText);
    const std::byte* visible = nullptr;
    int count = 0;
    const auto pattern = resolver::Pattern::parse(kTabListVisible);
    if (fields && pattern) {
        for (const std::size_t offset : resolver::findAll(image.text, *pattern)) {
            const std::byte* at = image.text.data() + offset;
            if (branchTarget(image, at + kWriterCall) == writer) {
                visible = at;
                ++count;
            }
        }
    }
    std::string error;
    if (count != 1) {
        EVR_LOG(
            "%s: tab lists: %d visible updates that write the keys, not 1 (or no key fields); a stick left / "
            "right never sends A / D there",
            tag, count);
    } else if (!installMidHook(const_cast<std::byte*>(visible), &onTabListVisible, error)) {
        EVR_LOG(
            "%s: tab lists: update hook at RVA 0x%X failed: %s; a stick left / right never sends A / D there",
            tag, image.rva(visible), error.c_str());
    } else {
        EVR_LOG("%s: tab lists: update hooked at RVA 0x%X (keys +0x%X / +0x%X, tab count +0x%X)", tag,
                image.rva(visible), static_cast<unsigned>(g_tabKeyFields[0]),
                static_cast<unsigned>(g_tabKeyFields[1]), static_cast<unsigned>(g_tabCountField));
        return true;
    }
    return false;
}

double since(std::uint64_t at, std::uint64_t now) {
    return at != 0 && now >= at ? static_cast<double>(now - at) * 1e-6 : -1.0;
}

} // namespace

PageKeyHooks installPageKeyHooks(const GameImage& image,
                                 const std::byte* keyNameText,
                                 const std::byte* tabListWriter,
                                 const char* tag) {
    PageKeyHooks hooks;
    hooks.tabLists = installTabLists(image, tabListWriter, keyNameText, tag);
    hooks.rewards = installRewards(image, keyNameText, tag);
    return hooks;
}

menu::PageKeys currentPageKeys() {
    const std::uint64_t now = window_timing::nowMicros();
    menu::PageKeysInput in;
    in.sinceRewardsUpdate = since(g_rewardsAt.load(std::memory_order_relaxed), now);
    in.rewardsArrows = g_arrows.load(std::memory_order_relaxed);
    in.sinceTabListUpdate = since(g_tabsAt.load(std::memory_order_relaxed), now);
    in.allowed = mp_guard::allowsGameTouch();
    const menu::PageKeys keys = menu::pageKeysFor(in);
    if (keys != g_lastKeys && g_changeLines < kChangeLines) {
        ++g_changeLines;
        const char* last = g_changeLines == kChangeLines ? " (the last such line)" : "";
        if (keys.any()) {
            EVR_LOG("menu: page keys up (%s): a stick left / right sends %c / %c%s",
                    menu::rewardsShowing(in) ? "the Rewards tab's arrows" : "a tab list on A / D",
                    static_cast<char>(keys.previous), static_cast<char>(keys.next), last);
        } else {
            EVR_LOG("menu: page keys gone: a stick left / right changes tabs%s", last);
        }
    }
    g_lastKeys = keys;
    return keys;
}

} // namespace evr::vkcore::controllers
