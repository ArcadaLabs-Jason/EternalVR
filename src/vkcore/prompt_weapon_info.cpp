#include "vkcore/prompt_weapon_info.hpp"

#include "vkcore/log.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <string_view>
#include <vector>

namespace evr::vkcore::controllers {

namespace {

// How far past a function's start its calls are looked for; the helper spans 0x410 bytes, the update 0x224.
constexpr std::size_t kFunctionSpan = 0x1000;
// The weapon info update's bind for its first label, the equipment's.
constexpr std::string_view kEquipmentBind = "_quickuse";
// The frames captured: this file's two, the detour's, the helper's, the helper's caller's, and room for
// frames the compiler may add between.
constexpr std::size_t kFrames = 8;

std::atomic<bool> g_shortLogged{false};
std::atomic<bool> g_unwindLogged{false};

// The return addresses of the `call target`s in `function`'s own code (not in a chunk of the next
// function's).
std::vector<std::uintptr_t>
callsIn(const GameImage& image, const std::byte* function, const std::byte* target) {
    std::vector<std::uintptr_t> found;
    for (const std::uintptr_t at : callReturns(image, function, target, kFunctionSpan)) {
        if (functionStart(image, reinterpret_cast<const std::byte*>(at) - 5) == function) {
            found.push_back(at);
        }
    }
    return found;
}

// The one function that loads the equipment's bind name and calls the helper, or nullptr.
const std::byte* findWeaponInfoUpdate(const GameImage& image, const std::byte* helper, const char* tag) {
    const std::byte* bind = findUniqueString(image, kEquipmentBind);
    if (bind == nullptr) {
        EVR_LOG("%s: weapon info: \"%.*s\" is not in .rdata once", tag,
                static_cast<int>(kEquipmentBind.size()), kEquipmentBind.data());
        return nullptr;
    }
    std::vector<const std::byte*> updates;
    for (const std::byte* reference : findLeaReferences(image, bind)) {
        const std::byte* function = functionStart(image, reference);
        if (function != nullptr && std::ranges::find(updates, function) == updates.end() &&
            !callsIn(image, function, helper).empty()) {
            updates.push_back(function);
        }
    }
    if (updates.size() != 1) {
        EVR_LOG("%s: weapon info: %zu functions load \"%.*s\" and call the HUD helper, not one", tag,
                updates.size(), static_cast<int>(kEquipmentBind.size()), kEquipmentBind.data());
        return nullptr;
    }
    return updates.front();
}

// The return address a frame above the one that returns to `returnAddress` on this thread's stack, or 0 when
// `returnAddress` is not on it.
std::uintptr_t callerReturnAbove(std::uintptr_t returnAddress) {
    std::array<void*, kFrames> frames{};
    const USHORT count =
        RtlCaptureStackBackTrace(0, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
    for (USHORT i = 0; i + 1 < count; ++i) {
        if (reinterpret_cast<std::uintptr_t>(frames[i]) == returnAddress) {
            return reinterpret_cast<std::uintptr_t>(frames[i + 1]);
        }
    }
    return 0;
}

} // namespace

input::HudKeyCallers findWeaponInfoKeyCalls(const GameImage& image,
                                            const std::byte* helper,
                                            const std::byte* keyText,
                                            const char* tag) {
    input::HudKeyCallers callers;
    const std::vector<std::uintptr_t> keyTextCalls = callsIn(image, helper, keyText);
    if (keyTextCalls.size() != 1) {
        EVR_LOG("%s: weapon info: the HUD helper calls LocalizedKeyName %zu times, not once; its labels keep "
                "the full texts",
                tag, keyTextCalls.size());
        return callers;
    }
    const std::byte* update = findWeaponInfoUpdate(image, helper, tag);
    if (update == nullptr) {
        return callers;
    }
    const std::vector<std::uintptr_t> labels = callsIn(image, update, helper);
    if (labels.size() > callers.weaponInfo.size()) {
        EVR_LOG("%s: weapon info: %zu calls to the HUD helper (room for %zu); its labels keep the full texts",
                tag, labels.size(), callers.weaponInfo.size());
        return callers;
    }
    callers.helperKeyText = keyTextCalls.front();
    std::ranges::copy(labels, callers.weaponInfo.begin());
    const auto rva = [&image](std::uintptr_t at) {
        return image.rva(reinterpret_cast<const std::byte*>(at));
    };
    EVR_LOG("%s: weapon info: update at RVA 0x%X, %zu calls to the HUD helper (first 0x%X, last 0x%X); the "
            "helper's LocalizedKeyName call at 0x%X; its labels get the short texts",
            tag, image.rva(update), labels.size(), rva(labels.front()), rva(labels.back()),
            rva(callers.helperKeyText));
    return callers;
}

const std::string& hudKeyAnswer(std::size_t slot,
                                std::uintptr_t returnAddress,
                                const input::HudKeyCallers& callers,
                                const input::PromptLabelSet& labels,
                                const char* tag) {
    const bool helper = returnAddress == callers.helperKeyText && returnAddress != 0;
    const std::uintptr_t above = helper ? callerReturnAbove(returnAddress) : 0;
    if (helper && above == 0 && !g_unwindLogged.exchange(true, std::memory_order_relaxed)) {
        EVR_LOG("%s: weapon info: the HUD helper's frame is not on the captured stack; full texts", tag);
    }
    const input::HudKeyForm form = input::hudKeyForm(returnAddress, above, callers);
    const std::string* text = input::hudKeyText(slot, form, labels);
    if (text == nullptr) {
        return labels.distinct[slot];
    }
    if (form == input::HudKeyForm::Short && !g_shortLogged.exchange(true, std::memory_order_relaxed)) {
        EVR_LOG("%s: weapon info: the row names %s as %s (called from RVA 0x%X)", tag,
                labels.distinct[slot].c_str(), text->c_str(),
                static_cast<unsigned>(above - reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))));
    }
    return *text;
}

} // namespace evr::vkcore::controllers
