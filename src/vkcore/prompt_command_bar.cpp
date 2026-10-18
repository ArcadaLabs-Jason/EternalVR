#include "vkcore/prompt_command_bar.hpp"

#include "vkcore/log.hpp"

#include <string_view>
#include <vector>

namespace evr::vkcore::controllers {

namespace {

constexpr std::string_view kGetKeyStringError =
    "idSWFWidget_Button_CommandBar::GetKeyString() - Empty String for command bar button ID '%d'";
// How far past a function's start its calls are looked for; the update spans 0xEF7 bytes.
constexpr std::size_t kFunctionSpan = 0x1000;
// The hold flag's test, `cmp byte ptr [reg+0x3F0], 0`, which each answered call follows closely (0xF and 9
// bytes before the call in build 25216728 and in Game Pass 1.0.56.0): a call without it is not the hold
// buttons' key text.
constexpr const char* kHoldFlagTest = "80 ?? F0 03 00 00 00";
constexpr std::size_t kHoldFlagReach = 0x20;

// The return address of the one `call target` in `function`'s own code (a call in a chunk of the function's
// that is the next function's is not counted), or 0; `count` is how many there were.
std::uintptr_t
singleCallIn(const GameImage& image, const std::byte* function, const std::byte* target, std::size_t& count) {
    std::uintptr_t found = 0;
    count = 0;
    for (const std::uintptr_t at : callReturns(image, function, target, kFunctionSpan)) {
        if (functionStart(image, reinterpret_cast<const std::byte*>(at) - 5) == function) {
            found = at;
            ++count;
        }
    }
    return count == 1 ? found : 0;
}

// The call that returns to `returnAddress` follows the hold flag's test within kHoldFlagReach bytes.
bool followsHoldFlagTest(const GameImage& image, std::uintptr_t returnAddress) {
    const std::byte* call = reinterpret_cast<const std::byte*>(returnAddress) - 5;
    for (std::size_t back = 1; back <= kHoldFlagReach; ++back) {
        if (matchesAt(image, call - back, kHoldFlagTest)) {
            return true;
        }
    }
    return false;
}

// `found` when it follows the hold flag's test, else 0 (logged under `tag` for `what`).
std::uintptr_t holdCall(const GameImage& image, std::uintptr_t found, const char* what, const char* tag) {
    if (found != 0 && !followsHoldFlagTest(image, found)) {
        EVR_LOG("%s: command bar: %s's KeyNameText call (RVA 0x%X) has no hold flag test before it; its hold "
                "keys stay the game's",
                tag, what, image.rva(reinterpret_cast<const std::byte*>(found)));
        return 0;
    }
    return found;
}

const std::byte* findGetKeyString(const GameImage& image, const char* tag) {
    const std::byte* error = findUniqueString(image, kGetKeyStringError);
    if (error == nullptr) {
        EVR_LOG("%s: command bar: GetKeyString's error string is not in .rdata once", tag);
        return nullptr;
    }
    const std::vector<const std::byte*> references = findLeaReferences(image, error);
    const std::byte* function = references.size() == 1 ? functionStart(image, references.front()) : nullptr;
    if (function == nullptr) {
        EVR_LOG("%s: command bar: GetKeyString's error string has %zu references, not one in a function", tag,
                references.size());
        return nullptr;
    }
    EVR_LOG("%s: command bar: GetKeyString at RVA 0x%X", tag, image.rva(function));
    return function;
}

} // namespace

CommandBarCalls
findCommandBarKeyNameCalls(const GameImage& image, const std::byte* keyNameText, const char* tag) {
    CommandBarCalls calls;
    const std::byte* getKeyString = findGetKeyString(image, tag);
    if (getKeyString == nullptr) {
        return calls;
    }
    calls.getKeyStringFunction = getKeyString;
    std::size_t count = 0;
    calls.getKeyString = singleCallIn(image, getKeyString, keyNameText, count);
    if (calls.getKeyString == 0) {
        EVR_LOG("%s: command bar: GetKeyString calls KeyNameText %zu times, not once; its hold keys stay the "
                "game's",
                tag, count);
    }
    calls.getKeyString = holdCall(image, calls.getKeyString, "GetKeyString", tag);
    const std::vector<std::uintptr_t> callers =
        callReturns(image, image.text.data(), getKeyString, image.text.size());
    const std::byte* update =
        callers.size() == 1 ? functionStart(image, reinterpret_cast<const std::byte*>(callers.front()) - 5)
                            : nullptr;
    if (update == nullptr) {
        EVR_LOG(
            "%s: command bar: GetKeyString has %zu callers, not one function; the update's hold keys stay "
            "the game's",
            tag, callers.size());
        return calls;
    }
    calls.updateFunction = update;
    calls.update = singleCallIn(image, update, keyNameText, count);
    if (calls.update == 0) {
        EVR_LOG("%s: command bar: its update (RVA 0x%X) calls KeyNameText %zu times, not once; its hold keys "
                "stay the game's",
                tag, image.rva(update), count);
    }
    calls.update = holdCall(image, calls.update, "the update", tag);
    return calls;
}

} // namespace evr::vkcore::controllers
