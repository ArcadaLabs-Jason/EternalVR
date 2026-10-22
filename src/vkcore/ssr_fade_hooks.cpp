#include "vkcore/ssr_fade_hooks.hpp"

#include "vkcore/dump_format.hpp"
#include "vkcore/log.hpp"
#include "vkcore/shader_dump.hpp"
#include "vkcore/ssr_fade_patch.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace evr::vkcore::ssr_fade {

namespace {

constexpr const char* kTag = "ssr-fade";
constexpr std::uint32_t kAllKnown = (1u << kKnownModules) - 1;

struct Device {
    VkDevice device = VK_NULL_HANDLE;
    PFN_vkCreateShaderModule next = nullptr;
    bool game = false;
};
std::mutex g_mutex;
std::vector<Device> g_devices; // every device this layer created, with its next vkCreateShaderModule

Device deviceOf(VkDevice device) {
    std::lock_guard lock(g_mutex);
    for (const Device& d : g_devices) {
        if (d.device == device) {
            return d;
        }
    }
    return {};
}

std::atomic<std::uint32_t> g_patched{0}; // bit i: knownModules()[i] patched
std::atomic<bool> g_broken{false};       // a known module failed, or an unknown one has the signature

void breakCoherence(const char* what, std::uint64_t hash, const std::string& why) {
    g_broken.store(true, std::memory_order_release);
    EVR_LOG(
        "%s: %s (module %016llx%s%s): screen-space reflections keep the game's fade and the view rotation "
        "is not sheared for the rest of the session",
        kTag, what, static_cast<unsigned long long>(hash), why.empty() ? "" : ": ", why.c_str());
}

VKAPI_ATTR VkResult VKAPI_CALL CreateShaderModule(VkDevice device,
                                                  const VkShaderModuleCreateInfo* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkShaderModule* pModule) {
    const Device d = deviceOf(device);
    if (!d.next) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!d.game || !pCreateInfo || !pCreateInfo->pCode || pCreateInfo->codeSize % 4 != 0) {
        return d.next(device, pCreateInfo, pAllocator, pModule);
    }
    const std::span<const std::uint32_t> code(pCreateInfo->pCode, pCreateInfo->codeSize / 4);
    const std::uint64_t hash = dump::fnv1a64(code.data(), code.size_bytes());
    const std::optional<std::size_t> index = knownIndex(code);
    if (!index) {
        if (hasFadeSignature(code)) {
            breakCoherence("a module of unknown hash has the SSR fade", hash, {});
        }
        return d.next(device, pCreateInfo, pAllocator, pModule);
    }
    const KnownModule& known = knownModules()[*index];
    std::string why;
    const std::optional<std::vector<std::uint32_t>> patched =
        patchFade(code, known.rowsMember, known.packedMember, why);
    if (!patched || dump::fnv1a64(patched->data(), patched->size() * 4) != known.outputHash) {
        breakCoherence("an SSR module did not patch as validated", hash, patched ? "another output" : why);
        return d.next(device, pCreateInfo, pAllocator, pModule);
    }
    VkShaderModuleCreateInfo info = *pCreateInfo;
    info.codeSize = patched->size() * 4;
    info.pCode = patched->data();
    const VkResult result = d.next(device, &info, pAllocator, pModule);
    if (result != VK_SUCCESS) {
        breakCoherence("the driver refused a patched SSR module", hash, "created unpatched");
        return d.next(device, pCreateInfo, pAllocator, pModule);
    }
    const std::uint32_t bit = 1u << *index;
    const std::uint32_t before = g_patched.fetch_or(bit, std::memory_order_acq_rel);
    if ((before | bit) == kAllKnown && before != kAllKnown) {
        EVR_LOG(
            "%s: all %zu SSR shader modules patched: reflections fade over the horizontal frame both eyes "
            "share",
            kTag, kKnownModules);
    }
    return result;
}

} // namespace

void onDeviceCreated(VkDevice device, PFN_vkGetDeviceProcAddr nextGetDeviceProcAddr, bool isGame) {
    const PFN_vkVoidFunction dump = shader_dump::findHook("vkCreateShaderModule");
    const auto next = reinterpret_cast<PFN_vkCreateShaderModule>(
        dump ? dump : nextGetDeviceProcAddr(device, "vkCreateShaderModule"));
    {
        std::lock_guard lock(g_mutex);
        g_devices.push_back(Device{device, next, isGame});
    }
    if (isGame) {
        EVR_LOG("%s: watching the game's shader modules for the %zu known SSR modules", kTag, kKnownModules);
    }
}

void onDeviceDestroyed(VkDevice device) {
    std::lock_guard lock(g_mutex);
    std::erase_if(g_devices, [device](const Device& d) { return d.device == device; });
}

PFN_vkVoidFunction findHook(const char* name) {
    if (std::strcmp(name, "vkCreateShaderModule") != 0) {
        return nullptr;
    }
    return reinterpret_cast<PFN_vkVoidFunction>(&CreateShaderModule);
}

bool coherent() {
    return !g_broken.load(std::memory_order_acquire) &&
           g_patched.load(std::memory_order_acquire) == kAllKnown;
}

} // namespace evr::vkcore::ssr_fade
