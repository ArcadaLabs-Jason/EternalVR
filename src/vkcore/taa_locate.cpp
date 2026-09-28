#include "vkcore/taa_locate.hpp"

#include "vkcore/game_code.hpp"
#include "vkcore/log.hpp"

#include <cstring>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-taa";

// Device context constructor, per-view slot loop (RVA 0x1C1A190): prepare slot i, build its images
// (`call 0x1C20150` at +0x24), then `mov rax, [rip + r_maxRenderViews]` (+0x29), `inc ebx`, loop.
constexpr const char* kSlotLoopSignature =
    "8B C3 49 8D 4E 08 48 69 F8 A8 00 00 00 8B D3 48 03 CF E8 ?? ?? ?? ?? 45 8B 06 49 8D 56 08 48 03 D7 49 "
    "8B CE E8 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? FF C3 3B 58 08 7C C9";
constexpr std::size_t kSlotBuilderCall = 0x24;
constexpr std::size_t kSlotHook = 0x29;

// Accumulation selectors: slot = deviceContext (renderSystem + 0xF58) + viewIndex (idRenderView +
// 0x28990) * 0xA8; render target = slot[0x60 + i * 8], i = (backend frame + 1) & 1 for the output
// (RVA 0x1CBB5A0) and backend frame & 1 for the history (RVA 0x1CBB6C0).
constexpr const char* kOutputSelectorSignature =
    "40 53 48 83 EC 20 48 63 82 90 89 02 00 48 8B 11 48 69 D8 A8 00 00 00 48 03 99 58 0F 00 00 FF 52 70 FF "
    "C0 25 01 00 00 80 7D 07 FF C8 83 C8 FE FF C0 48 98 48 8B 44 C3 60";
// Opaque accumulation selector (RVA 0x1CBB580): slot[0x68] (`accumulationBufferOpaque`).
constexpr const char* kOpaqueSelectorSignature =
    "48 63 82 90 89 02 00 48 69 D0 A8 00 00 00 48 8B 81 58 0F 00 00 48 8B 44 02 70 C3";
constexpr const char* kHistorySelectorSignature =
    "40 53 48 83 EC 20 48 63 82 90 89 02 00 48 8B 11 48 69 D8 A8 00 00 00 48 03 99 58 0F 00 00 FF 52 70 25 "
    "01 00 00 80 7D 07 FF C8 83 C8 FE FF C0 48 98 48 8B 44 C3 60";

// idCVar::SetString (RVA 0x376020).
constexpr const char* kSetCvarSignature =
    "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B D9 48 8B 09 41 0F "
    "B6 F0 48 8B FA 48 85 D2 75 04 48 8B 79 30 48 8B 09 48 8B D7 E8";

// The auto-exposure index (RVA 0x1C98D0E): the backend frame (+0x148) against the frame the view's exposure
// was last updated (device context slot + 0x28), the view's skip flag, then `mov [rsi + 0x140], ecx`.
constexpr const char* kExposureIndexSignature =
    "8B 8E 48 01 00 00 8B 54 1F 30 3B CA 74 0E 48 8B 46 38 80 78 21 01 74 04 B0 01 EB 02 32 C0 3C 01 0F 45 "
    "CA "
    "81 E1 01 00 00 80 7D 07 FF C9 83 C9 FE FF C1 89 8E 40 01 00 00";
constexpr std::size_t kExposureIndexHook = 0x38;

// distortionLastFrameMap (RVA 0x1C5664B): `mov r8, [rdx + rax + 0x50]` (slot + 0x48), the parameter name,
// `add r8, 0xC8`.
constexpr const char* kDistortionSignature = "4C 8B 44 02 50 48 8B 15 ?? ?? ?? ?? 49 81 C0 C8 00 00 00";
constexpr std::size_t kDistortionHook = 0xC;

// A cvar registration: lea r8, [default]; lea rdx, [name]; lea rcx, [object]; call.
constexpr const char* kCvarRegistration = "4C 8D 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8";
constexpr std::size_t kRegistrationName = 7;
constexpr std::size_t kRegistrationObject = 14;

} // namespace

// Device context render-target resize (RVA 0x1C21600).
constexpr const char* kContextResizeSignature = "40 53 55 56 57 41 54 41 56 41 57 48 81 EC 50 01 00 00 48 8B "
                                                "05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 40 01 00 00 "
                                                "C6 81 78 06 00 00 01 48 8B F9 8B 32";
constexpr std::size_t kTargetResizeCall = 0xCB;

bool locateTaaSlotSite(const GameImage& image, TaaSlotSite& out) {
    const std::byte* loop = findUnique(image, kTag, "device context slot loop", kSlotLoopSignature);
    if (!loop) {
        return false;
    }
    const std::byte* builder = relativeTarget(loop + kSlotBuilderCall);
    if (!builder || !image.inText(builder) || functionStart(image, builder) != builder) {
        EVR_LOG("%s: the slot builder call does not reach the start of a function", kTag);
        return false;
    }
    const std::byte* resize = findUnique(image, kTag, "device context resize", kContextResizeSignature);
    const std::byte* targetResize = resize ? relativeTarget(resize + kTargetResizeCall) : nullptr;
    if (!targetResize || !image.inText(targetResize) || functionStart(image, targetResize) != targetResize) {
        EVR_LOG("%s: the device context resize or its render-target resize call is missing", kTag);
        return false;
    }
    out.hookSite = loop + kSlotHook;
    out.slotBuilder = builder;
    out.contextResize = resize;
    out.targetResize = targetResize;
    EVR_LOG("%s: slot builder at RVA 0x%X", kTag, image.rva(builder));
    return true;
}

bool locateTaaEngine(const GameImage& image, TaaEngine& out) {
    out.outputSelector = findUnique(image, kTag, "accumulation output selector", kOutputSelectorSignature);
    out.historySelector = findUnique(image, kTag, "accumulation history selector", kHistorySelectorSignature);
    out.opaqueSelector = findUnique(image, kTag, "opaque accumulation selector", kOpaqueSelectorSignature);
    out.setCvar = findUnique(image, kTag, "cvar SetString", kSetCvarSignature);
    const std::byte* exposure = findUnique(image, kTag, "auto-exposure index", kExposureIndexSignature);
    out.exposureSite = exposure ? exposure + kExposureIndexHook : nullptr;
    const std::byte* distortion = findUnique(image, kTag, "distortion last-frame bind", kDistortionSignature);
    out.distortionSite = distortion ? distortion + kDistortionHook : nullptr;
    return out.outputSelector && out.historySelector && out.opaqueSelector && out.setCvar;
}

std::vector<std::byte*> findCvarObjects(const GameImage& image, const std::vector<std::string_view>& names) {
    std::vector<std::byte*> objects(names.size(), nullptr);
    std::vector<const std::byte*> strings(names.size(), nullptr);
    for (std::size_t i = 0; i < names.size(); ++i) {
        strings[i] = findUniqueString(image, names[i]);
    }
    auto pattern = resolver::Pattern::parse(kCvarRegistration);
    if (!pattern) {
        return objects;
    }
    std::vector<int> found(names.size(), 0);
    for (const std::size_t offset : resolver::findAll(image.text, *pattern)) {
        const std::byte* at = image.text.data() + offset;
        const std::byte* name = ripTarget(image, at + kRegistrationName + 3, at + kRegistrationName + 7);
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (name && name == strings[i]) {
                objects[i] = const_cast<std::byte*>(
                    ripTarget(image, at + kRegistrationObject + 3, at + kRegistrationObject + 7));
                ++found[i];
            }
        }
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (found[i] != 1) {
            EVR_LOG("%s: cvar %.*s registered %d time(s), expected 1", kTag,
                    static_cast<int>(names[i].size()), names[i].data(), found[i]);
            objects[i] = nullptr;
        }
    }
    return objects;
}

int cvarInt(const std::byte* object) {
    if (!object) {
        return 0;
    }
    const std::byte* values = nullptr;
    std::memcpy(&values, object, sizeof(values));
    if (!values) {
        return 0;
    }
    int value = 0;
    std::memcpy(&value, values + 8, sizeof(value));
    return value;
}

} // namespace evr::vkcore
