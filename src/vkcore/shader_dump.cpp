// Shader and draw dump: configuration, the background writer, the device registry and the
// creation-time hooks (shader modules, pipelines, layouts, descriptor sets). See shader_dump.hpp.

#include "vkcore/shader_dump.hpp"

#include "vkcore/log.hpp"
#include "vkcore/shader_dump_impl.hpp"

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace evr::vkcore::shader_dump {

namespace {

using dump::handleText;
using dump::handleValue;

// Descriptor writes are logged until this many lines, so a game that rewrites buffer descriptors every
// frame cannot fill the disk.
constexpr std::uint32_t kMaxSetLines = 200000;

struct Config {
    bool on = false;
    std::wstring dir;
};

const Config& config() {
    static const Config value = [] {
        Config c;
        std::wstring dir;
        if (readEnv(L"ETERNALVR_DUMP_SHADERS", dir) && !dir.empty()) {
            c.on = true;
            c.dir = dir;
        }
        return c;
    }();
    return value;
}

// ---------------------------------------------------------------------------------------------------
// Writer: one thread appends queued lines to the jsonl files, so the game's threads only format.

class Writer {
public:
    void start(const std::wstring& dir) {
        static const wchar_t* const names[] = {L"\\pipelines.jsonl", L"\\layouts.jsonl", L"\\sets.jsonl",
                                               L"\\drawlog.jsonl"};
        for (int i = 0; i < 4; ++i) {
            files_[i] = _wfsopen((dir + names[i]).c_str(), L"wb", _SH_DENYWR);
        }
        std::thread([this] { run(); }).detach();
    }

    void push(Stream stream, std::string lines) {
        {
            std::lock_guard lock(mutex_);
            queue_.emplace_back(stream, std::move(lines));
        }
        wake_.notify_one();
    }

private:
    void run() {
        for (;;) {
            std::deque<std::pair<Stream, std::string>> batch;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return !queue_.empty(); });
                batch.swap(queue_);
            }
            for (auto& [stream, text] : batch) {
                if (std::FILE* f = files_[static_cast<int>(stream)]) {
                    std::fwrite(text.data(), 1, text.size(), f);
                }
            }
            for (std::FILE* f : files_) {
                if (f) {
                    std::fflush(f);
                }
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::pair<Stream, std::string>> queue_;
    std::FILE* files_[4] = {};
};

Writer& writer() {
    static Writer& w = *new Writer; // never destroyed: the thread outlives static destruction
    return w;
}

// ---------------------------------------------------------------------------------------------------
// Registry

std::shared_mutex& g_devicesMutex = *new std::shared_mutex;
auto& g_devices = *new std::unordered_map<void*, DumpDevice*>;

std::mutex& g_modulesMutex = *new std::mutex;
auto& g_moduleHashes = *new std::unordered_map<std::uint64_t, std::uint64_t>; // module handle -> hash
auto& g_writtenHashes = *new std::unordered_set<std::uint64_t>;
std::atomic<std::uint32_t> g_setLines{0};

void* keyOf(void* dispatchable) {
    return *static_cast<void**>(dispatchable);
}

// Hashes SPIR-V code and writes it once as modules/<hash>.spv.
std::uint64_t storeModule(const std::uint32_t* code, std::size_t size) {
    const std::uint64_t hash = dump::fnv1a64(code, size);
    {
        std::lock_guard lock(g_modulesMutex);
        if (!g_writtenHashes.insert(hash).second) {
            return hash;
        }
    }
    const std::string name = dump::hex64(hash);
    const std::wstring path =
        config().dir + L"\\modules\\" + std::wstring(name.begin(), name.end()) + L".spv";
    if (std::FILE* f = _wfsopen(path.c_str(), L"wb", _SH_DENYWR)) {
        std::fwrite(code, 1, size, f);
        std::fclose(f);
    } else {
        EVR_LOG("shader dump: cannot write %ls", path.c_str());
    }
    return hash;
}

std::uint64_t moduleHash(VkShaderModule module) {
    std::lock_guard lock(g_modulesMutex);
    const auto it = g_moduleHashes.find(handleValue(module));
    return it == g_moduleHashes.end() ? 0 : it->second;
}

// ---------------------------------------------------------------------------------------------------
// Creation-time hooks

VKAPI_ATTR VkResult VKAPI_CALL CreateShaderModule(VkDevice device,
                                                  const VkShaderModuleCreateInfo* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkShaderModule* pModule) {
    DumpDevice* d = deviceOf(device);
    const VkResult result = d->CreateShaderModule(device, pCreateInfo, pAllocator, pModule);
    if (result == VK_SUCCESS && d->isGame && pCreateInfo->pCode) {
        const std::uint64_t hash = storeModule(pCreateInfo->pCode, pCreateInfo->codeSize);
        std::lock_guard lock(g_modulesMutex);
        g_moduleHashes[handleValue(*pModule)] = hash;
    }
    return result;
}

// One stage of a pipeline as JSON: {"stage":bits,"module":"<hash>","entry":"main",...}.
void appendStage(std::string& out, const VkPipelineShaderStageCreateInfo& stage) {
    out += "{\"stage\":" + std::to_string(stage.stage) + ",\"module\":";
    std::string module;
    if (stage.module != VK_NULL_HANDLE) {
        const std::uint64_t hash = moduleHash(stage.module);
        module = hash ? dump::hex64(hash) : "unknown:" + handleText(handleValue(stage.module));
    } else if (const auto* inlineInfo = dump::findInChain<VkShaderModuleCreateInfo>(
                   stage.pNext, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
               inlineInfo && inlineInfo->pCode) {
        module = dump::hex64(storeModule(inlineInfo->pCode, inlineInfo->codeSize));
    } else if (const auto* id = dump::findInChain<VkPipelineShaderStageModuleIdentifierCreateInfoEXT>(
                   stage.pNext, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT);
               id && id->pIdentifier) {
        module = "id:" + dump::hexBytes(id->pIdentifier, id->identifierSize);
    } else {
        module = "none";
    }
    dump::appendJsonString(out, module);
    out += ",\"entry\":";
    dump::appendJsonString(out, stage.pName ? stage.pName : "");
    if (const VkSpecializationInfo* spec = stage.pSpecializationInfo) {
        out += ",\"spec\":{\"entries\":[";
        for (std::uint32_t i = 0; i < spec->mapEntryCount; ++i) {
            const VkSpecializationMapEntry& e = spec->pMapEntries[i];
            out += (i ? ",[" : "[") + std::to_string(e.constantID) + "," + std::to_string(e.offset) + "," +
                   std::to_string(e.size) + "]";
        }
        out += "],\"data\":\"" + (spec->pData ? dump::hexBytes(spec->pData, spec->dataSize) : "") + "\"}";
    }
    out += "}";
}

void appendLibraries(std::string& out, const void* pNext) {
    const auto* libs = dump::findInChain<VkPipelineLibraryCreateInfoKHR>(
        pNext, VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR);
    if (!libs || libs->libraryCount == 0) {
        return;
    }
    out += ",\"libraries\":[";
    for (std::uint32_t i = 0; i < libs->libraryCount; ++i) {
        out += (i ? ",\"" : "\"") + handleText(handleValue(libs->pLibraries[i])) + "\"";
    }
    out += "]";
}

template <typename Info>
void logPipelines(const char* kind, std::uint32_t count, const Info* infos, const VkPipeline* pipelines) {
    std::string lines;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (pipelines[i] == VK_NULL_HANDLE) {
            continue;
        }
        const Info& info = infos[i];
        lines += "{\"pipeline\":\"" + handleText(handleValue(pipelines[i])) + "\",\"kind\":\"" + kind +
                 "\",\"layout\":\"" + handleText(handleValue(info.layout)) +
                 "\",\"flags\":" + std::to_string(info.flags) + ",\"stages\":[";
        if constexpr (std::is_same_v<Info, VkComputePipelineCreateInfo>) {
            appendStage(lines, info.stage);
        } else {
            for (std::uint32_t s = 0; s < info.stageCount; ++s) {
                if (s) {
                    lines += ",";
                }
                appendStage(lines, info.pStages[s]);
            }
        }
        lines += "]";
        appendLibraries(lines, info.pNext);
        lines += "}\n";
    }
    write(Stream::Pipelines, std::move(lines));
}

VKAPI_ATTR VkResult VKAPI_CALL CreateGraphicsPipelines(VkDevice device,
                                                       VkPipelineCache cache,
                                                       std::uint32_t count,
                                                       const VkGraphicsPipelineCreateInfo* pInfos,
                                                       const VkAllocationCallbacks* pAllocator,
                                                       VkPipeline* pPipelines) {
    DumpDevice* d = deviceOf(device);
    const VkResult result = d->CreateGraphicsPipelines(device, cache, count, pInfos, pAllocator, pPipelines);
    if (result >= VK_SUCCESS && d->isGame) {
        logPipelines("graphics", count, pInfos, pPipelines);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateComputePipelines(VkDevice device,
                                                      VkPipelineCache cache,
                                                      std::uint32_t count,
                                                      const VkComputePipelineCreateInfo* pInfos,
                                                      const VkAllocationCallbacks* pAllocator,
                                                      VkPipeline* pPipelines) {
    DumpDevice* d = deviceOf(device);
    const VkResult result = d->CreateComputePipelines(device, cache, count, pInfos, pAllocator, pPipelines);
    if (result >= VK_SUCCESS && d->isGame) {
        logPipelines("compute", count, pInfos, pPipelines);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL CreatePipelineLayout(VkDevice device,
                                                    const VkPipelineLayoutCreateInfo* pInfo,
                                                    const VkAllocationCallbacks* pAllocator,
                                                    VkPipelineLayout* pLayout) {
    DumpDevice* d = deviceOf(device);
    const VkResult result = d->CreatePipelineLayout(device, pInfo, pAllocator, pLayout);
    if (result == VK_SUCCESS && d->isGame) {
        std::string line =
            "{\"pipelineLayout\":\"" + handleText(handleValue(*pLayout)) + "\",\"setLayouts\":[";
        for (std::uint32_t i = 0; i < pInfo->setLayoutCount; ++i) {
            line += (i ? ",\"" : "\"") + handleText(handleValue(pInfo->pSetLayouts[i])) + "\"";
        }
        line += "],\"pushRanges\":[";
        for (std::uint32_t i = 0; i < pInfo->pushConstantRangeCount; ++i) {
            const VkPushConstantRange& r = pInfo->pPushConstantRanges[i];
            line += (i ? ",[" : "[") + std::to_string(r.stageFlags) + "," + std::to_string(r.offset) + "," +
                    std::to_string(r.size) + "]";
        }
        line += "]}\n";
        write(Stream::Layouts, std::move(line));
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDescriptorSetLayout(VkDevice device,
                                                         const VkDescriptorSetLayoutCreateInfo* pInfo,
                                                         const VkAllocationCallbacks* pAllocator,
                                                         VkDescriptorSetLayout* pLayout) {
    DumpDevice* d = deviceOf(device);
    const VkResult result = d->CreateDescriptorSetLayout(device, pInfo, pAllocator, pLayout);
    if (result == VK_SUCCESS && d->isGame) {
        std::string line = "{\"setLayout\":\"" + handleText(handleValue(*pLayout)) +
                           "\",\"flags\":" + std::to_string(pInfo->flags) + ",\"bindings\":[";
        for (std::uint32_t i = 0; i < pInfo->bindingCount; ++i) {
            const VkDescriptorSetLayoutBinding& b = pInfo->pBindings[i];
            line += (i ? ",[" : "[") + std::to_string(b.binding) + "," + std::to_string(b.descriptorType) +
                    "," + std::to_string(b.descriptorCount) + "," + std::to_string(b.stageFlags) + "]";
        }
        line += "]}\n";
        write(Stream::Layouts, std::move(line));
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL AllocateDescriptorSets(VkDevice device,
                                                      const VkDescriptorSetAllocateInfo* pInfo,
                                                      VkDescriptorSet* pSets) {
    DumpDevice* d = deviceOf(device);
    const VkResult result = d->AllocateDescriptorSets(device, pInfo, pSets);
    if (result == VK_SUCCESS && d->isGame && g_setLines.load(std::memory_order_relaxed) < kMaxSetLines) {
        std::string lines;
        for (std::uint32_t i = 0; i < pInfo->descriptorSetCount; ++i) {
            lines += "{\"set\":\"" + handleText(handleValue(pSets[i])) + "\",\"layout\":\"" +
                     handleText(handleValue(pInfo->pSetLayouts[i])) + "\"}\n";
        }
        g_setLines.fetch_add(pInfo->descriptorSetCount, std::memory_order_relaxed);
        write(Stream::Sets, std::move(lines));
    }
    return result;
}

bool isBufferDescriptor(VkDescriptorType type) {
    return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
           type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

VKAPI_ATTR void VKAPI_CALL UpdateDescriptorSets(VkDevice device,
                                                std::uint32_t writeCount,
                                                const VkWriteDescriptorSet* pWrites,
                                                std::uint32_t copyCount,
                                                const VkCopyDescriptorSet* pCopies) {
    DumpDevice* d = deviceOf(device);
    d->UpdateDescriptorSets(device, writeCount, pWrites, copyCount, pCopies);
    if (!d->isGame || g_setLines.load(std::memory_order_relaxed) >= kMaxSetLines) {
        return;
    }
    std::string lines;
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < writeCount; ++i) {
        const VkWriteDescriptorSet& w = pWrites[i];
        if (!isBufferDescriptor(w.descriptorType) || !w.pBufferInfo) {
            continue;
        }
        lines += "{\"write\":\"" + handleText(handleValue(w.dstSet)) +
                 "\",\"binding\":" + std::to_string(w.dstBinding) +
                 ",\"element\":" + std::to_string(w.dstArrayElement) +
                 ",\"type\":" + std::to_string(w.descriptorType) + ",\"ranges\":[";
        for (std::uint32_t b = 0; b < w.descriptorCount; ++b) {
            const VkDescriptorBufferInfo& info = w.pBufferInfo[b];
            lines += (b ? ",[\"" : "[\"") + handleText(handleValue(info.buffer)) + "\"," +
                     std::to_string(info.offset) + "," + std::to_string(info.range) + "]";
        }
        lines += "]}\n";
        ++n;
    }
    if (n) {
        g_setLines.fetch_add(n, std::memory_order_relaxed);
        write(Stream::Sets, std::move(lines));
    }
}

PFN_vkVoidFunction findCreateHook(const char* name) {
#define EVR_DUMP_HOOK(fn)                                                                                    \
    if (std::strcmp(name, "vk" #fn) == 0) {                                                                  \
        return reinterpret_cast<PFN_vkVoidFunction>(&fn);                                                    \
    }
    EVR_DUMP_HOOK(CreateShaderModule)
    EVR_DUMP_HOOK(CreateGraphicsPipelines)
    EVR_DUMP_HOOK(CreateComputePipelines)
    EVR_DUMP_HOOK(CreatePipelineLayout)
    EVR_DUMP_HOOK(CreateDescriptorSetLayout)
    EVR_DUMP_HOOK(AllocateDescriptorSets)
    EVR_DUMP_HOOK(UpdateDescriptorSets)
#undef EVR_DUMP_HOOK
    return nullptr;
}

} // namespace

bool enabled() {
    return config().on;
}

PFN_vkVoidFunction findHook(const char* name) {
    if (!enabled()) {
        return nullptr;
    }
    if (PFN_vkVoidFunction hook = findCreateHook(name)) {
        return hook;
    }
    return findRecordHook(name);
}

DumpDevice* deviceOf(void* dispatchable) {
    std::shared_lock lock(g_devicesMutex);
    const auto it = g_devices.find(keyOf(dispatchable));
    return it == g_devices.end() ? nullptr : it->second;
}

void write(Stream stream, std::string lines) {
    if (!lines.empty()) {
        writer().push(stream, std::move(lines));
    }
}

void onDeviceCreated(VkDevice device, PFN_vkGetDeviceProcAddr nextGetDeviceProcAddr, bool isGame) {
    if (!enabled()) {
        return;
    }
    auto* d = new DumpDevice; // freed with the device; the loader keeps no pointer to it
    d->device = device;
#define EVR_DUMP_LOAD_FN(name)                                                                               \
    d->name = reinterpret_cast<PFN_vk##name>(nextGetDeviceProcAddr(device, "vk" #name));
    EVR_DUMP_FUNCTIONS(EVR_DUMP_LOAD_FN)
#undef EVR_DUMP_LOAD_FN
    if (!d->CmdDrawIndirectCount) {
        d->CmdDrawIndirectCount = reinterpret_cast<PFN_vkCmdDrawIndirectCount>(
            nextGetDeviceProcAddr(device, "vkCmdDrawIndirectCountKHR"));
    }
    if (!d->CmdDrawIndexedIndirectCount) {
        d->CmdDrawIndexedIndirectCount = reinterpret_cast<PFN_vkCmdDrawIndexedIndirectCount>(
            nextGetDeviceProcAddr(device, "vkCmdDrawIndexedIndirectCountKHR"));
    }

    static std::once_flag started;
    if (isGame) {
        std::call_once(started, [] {
            const std::wstring& dir = config().dir;
            CreateDirectoryW(dir.c_str(), nullptr);
            CreateDirectoryW((dir + L"\\modules").c_str(), nullptr);
            writer().start(dir);
            std::wstring text;
            const std::uint32_t skip =
                dump::parseCount(readEnv(L"ETERNALVR_DUMP_SKIP_FRAMES", text) ? text : L"", 0);
            const std::uint32_t frames =
                dump::parseCount(readEnv(L"ETERNALVR_DUMP_FRAMES", text) ? text : L"", 60);
            startRecording(skip, frames);
            EVR_LOG("shader dump: writing to %ls, draw log for presents [%u, %u)", dir.c_str(), skip,
                    skip + frames);
        });
        d->isGame = true;
    }
    std::unique_lock lock(g_devicesMutex);
    g_devices[keyOf(device)] = d;
}

void onDeviceDestroyed(VkDevice device) {
    if (!enabled()) {
        return;
    }
    DumpDevice* d = nullptr;
    {
        std::unique_lock lock(g_devicesMutex);
        const auto it = g_devices.find(keyOf(device));
        if (it == g_devices.end()) {
            return;
        }
        d = it->second;
        g_devices.erase(it);
    }
    delete d;
}

} // namespace evr::vkcore::shader_dump
