#include "vkcore/prompt_hint_keys.hpp"

#include "features/menu/hint_keys.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/keep_active.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/prompt_load_tips.hpp"
#include "vkcore/window_timing.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace evr::vkcore::controllers {

namespace {

// In GetKeyString's own code (0x221 bytes): the call to the key token writer, `mov rdx, rbx; mov ecx, edi;
// call`; the button's data and its key, `mov rax, [rsi+0x2A0]; mov edi, [rax+0x400]`; its slot, `movsxd rax,
// dword ptr [rsi+0x2A8]`.
constexpr const char* kTokenCall = "48 8B D3 8B CF E8";
constexpr std::size_t kTokenCallAt = 5; // the `call` in kTokenCall
constexpr const char* kKeyRead = "48 8B 86 ?? ?? 00 00 8B B8 ?? ?? 00 00";
constexpr const char* kSlotRead = "48 63 86 ?? ?? 00 00";
constexpr std::size_t kGetKeyStringSpan = 0x300;
// The per-frame update's visibility test: `mov rax, [rbx+disp8]; test rax, rax; je; cmp byte ptr
// [rax+disp8], 0; jne`, within its first 0x40 bytes.
constexpr const char* kVisibleTest = "48 8B 43 ?? 48 85 C0 74 ?? 80 78 ?? 00 75 ??";
constexpr std::size_t kVisibleReach = 0x40;
constexpr std::int32_t kMaxField = 0x1000;
constexpr int kLogLines = 20;

HintStrAssignFn g_strAssign = nullptr;
std::uintptr_t g_tokenResume = 0; // the address after the token writer's call
std::int32_t g_dataField = 0;
std::int32_t g_keyField = 0;
std::int32_t g_slotField = 0;
std::int32_t g_spriteField = 0;
std::int32_t g_visibleField = 0;
std::atomic<bool> g_installed{false};
// window_timing::nowMicros() of a visible bar button's last update in each shortcut's slot with its key.
std::array<std::atomic<std::uint64_t>, menu::kHintShortcuts.size()> g_shownAt{};
std::array<std::atomic<bool>, menu::kHintShortcuts.size()> g_named{};

menu::HintKeys g_keys;                                      // XR worker only
std::array<bool, menu::kHintShortcuts.size()> g_wasShown{}; // XR worker only
int g_lines = 0;                                            // XR worker only

std::size_t buttonIndex(menu::HintButton button) {
    return static_cast<std::size_t>(button);
}

// The token writer's call in GetKeyString: rsi the bar button, ecx its key, rdx the text. A shortcut the
// controllers send gets its button's name instead of the key token. Skipping the call leaves rax as it was:
// the call is GetKeyString's last before its epilogue, and its one caller does not read the result.
void onKeyToken(HookRegisters& r) {
    std::int32_t slot = -1;
    if (!mp_guard::allowsGameTouch() ||
        !safeRead(reinterpret_cast<const std::byte*>(r.rsi) + g_slotField, slot)) {
        return;
    }
    const std::optional<std::size_t> shortcut = menu::hintShortcut(slot, static_cast<std::int32_t>(r.rcx));
    const std::shared_ptr<const input::PromptLabelSet> labels = currentPromptLabels();
    if (!shortcut || !labels) {
        return;
    }
    const std::string& name = labels->menuHints[buttonIndex(menu::kHintShortcuts[*shortcut].button)];
    if (name.empty()) {
        return;
    }
    // The game's assignment copies the text now.
    const std::string text = "[" + name + "] ";
    g_strAssign(reinterpret_cast<void*>(r.rdx), text.c_str());
    r.resumeAt = g_tokenResume;
    if (!g_named[*shortcut].exchange(true, std::memory_order_relaxed)) {
        EVR_LOG("prompts: the hint bar names %c (slot %d): %s",
                static_cast<char>(menu::kHintShortcuts[*shortcut].virtualKey), slot, text.c_str());
    }
}

// A bar button's per-frame update, past its visibility branch: rbx the button.
void onButtonFrame(const HookRegisters& r) {
    const auto* button = reinterpret_cast<const std::byte*>(r.rbx);
    const std::byte* sprite = nullptr;
    std::uint8_t visible = 0;
    const std::byte* data = nullptr;
    std::int32_t slot = -1;
    std::int32_t key = 0;
    if (!safeRead(button + g_spriteField, sprite) || sprite == nullptr ||
        !safeRead(sprite + g_visibleField, visible) || visible == 0 ||
        !safeRead(button + g_dataField, data) || data == nullptr || !safeRead(button + g_slotField, slot) ||
        !safeRead(data + g_keyField, key)) {
        return;
    }
    if (const std::optional<std::size_t> shortcut = menu::hintShortcut(slot, key)) {
        g_shownAt[*shortcut].store(window_timing::nowMicros(), std::memory_order_relaxed);
    }
}

// The single match of `signature` in `function`'s own code within `span`, or nullptr.
const std::byte*
uniqueIn(const GameImage& image, const std::byte* function, std::size_t span, const char* signature) {
    const std::byte* found = nullptr;
    int count = 0;
    for (std::size_t i = 0; i < span && image.inText(function + i, 16); ++i) {
        if (matchesAt(image, function + i, signature) && functionStart(image, function + i) == function) {
            found = function + i;
            ++count;
        }
    }
    return count == 1 ? found : nullptr;
}

std::int32_t field(std::int32_t value) {
    return value > 0 && value < kMaxField ? value : 0;
}

// The bar button's per-frame update: the vtable entry after `update` whose code has the visibility test
// (`update` is in two vtables: the bar button's and a subclass's that runs the button's own first). Its
// test, or nullptr unless exactly one such entry; `frame` gets the function.
const std::byte* findButtonFrame(const GameImage& image, const std::byte* update, const std::byte*& frame) {
    const auto target = reinterpret_cast<std::uintptr_t>(update);
    const std::byte* test = nullptr;
    int count = 0;
    for (std::size_t i = 0; i + 16 <= image.rdata.size(); i += 8) {
        std::uintptr_t value = 0;
        std::memcpy(&value, image.rdata.data() + i, sizeof(value));
        if (value != target) {
            continue;
        }
        std::uintptr_t next = 0;
        std::memcpy(&next, image.rdata.data() + i + 8, sizeof(next));
        const auto* entry = reinterpret_cast<const std::byte*>(next);
        const std::byte* found = image.inText(entry) && functionStart(image, entry) == entry
                                     ? uniqueIn(image, entry, kVisibleReach, kVisibleTest)
                                     : nullptr;
        if (found != nullptr) {
            frame = entry;
            test = found;
            ++count;
        }
    }
    return count == 1 ? test : nullptr;
}

// GetKeyString's parts: the token call and the button's fields. False (logged) when any is missing.
bool findKeyStringParts(const GameImage& image, const std::byte* getKeyString, const char* tag) {
    const std::byte* call = uniqueIn(image, getKeyString, kGetKeyStringSpan, kTokenCall);
    const std::byte* keyRead = uniqueIn(image, getKeyString, kGetKeyStringSpan, kKeyRead);
    const std::byte* slotRead = uniqueIn(image, getKeyString, kGetKeyStringSpan, kSlotRead);
    g_dataField = keyRead != nullptr ? field(readI32(keyRead + 3)) : 0;
    g_keyField = keyRead != nullptr ? field(readI32(keyRead + 9)) : 0;
    g_slotField = slotRead != nullptr ? field(readI32(slotRead + 3)) : 0;
    if (call == nullptr || g_dataField == 0 || g_keyField == 0 || g_slotField == 0) {
        EVR_LOG("%s: hint bar: GetKeyString's key token call or its button fields are not found; the hint "
                "bar's shortcuts keep the game's keys",
                tag);
        return false;
    }
    g_tokenResume = reinterpret_cast<std::uintptr_t>(call + kTokenCallAt + 5);
    return true;
}

} // namespace

void installHintKeyHooks(const GameImage& image,
                         const CommandBarCalls& bar,
                         HintStrAssignFn strAssign,
                         const char* tag) {
    g_strAssign = strAssign;
    if (g_strAssign == nullptr || bar.getKeyStringFunction == nullptr ||
        !findKeyStringParts(image, bar.getKeyStringFunction, tag)) {
        return;
    }
    const std::byte* frame = nullptr;
    const std::byte* test =
        bar.updateFunction != nullptr ? findButtonFrame(image, bar.updateFunction, frame) : nullptr;
    if (test == nullptr) {
        EVR_LOG("%s: hint bar: no bar button update with its visibility test; the hint bar's shortcuts keep "
                "the game's keys",
                tag);
        return;
    }
    // One-byte displacements: from 0x80 up they are negative, which no field of the button is.
    g_spriteField = std::to_integer<std::int32_t>(test[3]);
    g_visibleField = std::to_integer<std::int32_t>(test[11]);
    if (g_spriteField == 0 || g_spriteField >= 0x80 || g_visibleField == 0 || g_visibleField >= 0x80) {
        EVR_LOG(
            "%s: hint bar: the button update's fields read 0x%X / 0x%X; the hint bar's shortcuts keep the "
            "game's keys",
            tag, static_cast<unsigned>(g_spriteField), static_cast<unsigned>(g_visibleField));
        return;
    }
    const std::byte* visible = test + 15 + static_cast<std::int8_t>(test[14]);
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(visible), &onButtonFrame, error)) {
        EVR_LOG("%s: hint bar: button update hook at RVA 0x%X failed: %s; the hint bar's shortcuts keep the "
                "game's keys",
                tag, image.rva(visible), error.c_str());
        return;
    }
    const std::byte* call = reinterpret_cast<const std::byte*>(g_tokenResume) - 5;
    if (!installMidHookEdit(const_cast<std::byte*>(call), &onKeyToken, error)) {
        // The button hook stays in, but nothing reads its times (g_installed stays false).
        EVR_LOG(
            "%s: hint bar: key token hook at RVA 0x%X failed: %s; the hint bar's shortcuts keep the game's "
            "keys",
            tag, image.rva(call), error.c_str());
        return;
    }
    g_installed.store(true, std::memory_order_release);
    EVR_LOG("%s: hint bar: key token call at RVA 0x%X; button update at RVA 0x%X hooked at 0x%X (data +0x%X, "
            "key +0x%X, slot +0x%X, sprite +0x%X visible +0x%X): F, X and T in slots 2, 3 and 7 are sent by "
            "the left primary, the left secondary and the right stick's click",
            tag, image.rva(call), image.rva(frame), image.rva(visible), static_cast<unsigned>(g_dataField),
            static_cast<unsigned>(g_keyField), static_cast<unsigned>(g_slotField),
            static_cast<unsigned>(g_spriteField), static_cast<unsigned>(g_visibleField));
}

namespace {

void sendKeys(const std::vector<menu::HintKey>& keys) {
    for (const menu::HintKey& k : keys) {
        const bool sent = injectKey(k.key, k.down, gameWindow());
        if (g_lines < kLogLines) {
            ++g_lines;
            EVR_LOG("menu: hint bar: %c %s%s%s", static_cast<char>(k.key), k.down ? "down" : "up",
                    sent ? "" : " (not delivered)", g_lines == kLogLines ? " (the last such line)" : "");
        }
    }
}

} // namespace

void updateHintKeys(menu::RouterInput& in) {
    if (!g_installed.load(std::memory_order_acquire)) {
        return;
    }
    const std::shared_ptr<const input::PromptLabelSet> labels = currentPromptLabels();
    const std::uint64_t now = window_timing::nowMicros();
    menu::HintKeysInput hin;
    hin.seconds = in.seconds;
    hin.menuUp = in.menuActive && !in.popup;
    hin.allowed = mp_guard::allowsGameTouch();
    menu::RouterHand& left = in.hands[static_cast<std::size_t>(input::Hand::Left)];
    menu::RouterHand& right = in.hands[static_cast<std::size_t>(input::Hand::Right)];
    std::array<bool*, menu::kHintShortcuts.size()> buttons{};
    for (std::size_t i = 0; i < menu::kHintShortcuts.size(); ++i) {
        const menu::HintButton button = menu::kHintShortcuts[i].button;
        buttons[i] = button == menu::HintButton::LeftPrimary     ? &left.primary
                     : button == menu::HintButton::LeftSecondary ? &left.secondary
                                                                 : &right.stickClick;
        hin.held[i] = *buttons[i];
        const std::uint64_t at = g_shownAt[i].load(std::memory_order_relaxed);
        const bool named = labels && !labels->menuHints[buttonIndex(button)].empty();
        hin.sinceShown[i] = named && at != 0 && now >= at ? static_cast<double>(now - at) * 1e-6 : -1.0;
    }
    const menu::HintKeysOutput out = g_keys.update(hin);
    sendKeys(out.keys);
    for (std::size_t i = 0; i < menu::kHintShortcuts.size(); ++i) {
        const bool shown = g_keys.shown(i, hin);
        if (shown != g_wasShown[i] && g_lines < kLogLines) {
            ++g_lines;
            EVR_LOG("menu: hint bar: %c %s%s", static_cast<char>(menu::kHintShortcuts[i].virtualKey),
                    shown ? "shown: its button sends it" : "gone: its button is the menu's again",
                    g_lines == kLogLines ? " (the last such line)" : "");
        }
        g_wasShown[i] = shown;
        if (out.taken[i]) {
            *buttons[i] = false;
        }
    }
}

void releaseHintKeys() {
    if (g_installed.load(std::memory_order_acquire)) {
        sendKeys(g_keys.release());
    }
}

} // namespace evr::vkcore::controllers
