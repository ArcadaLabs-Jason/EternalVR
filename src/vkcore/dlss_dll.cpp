#include "vkcore/dlss_dll.hpp"

#include "features/dlss_dll/dlss_dll.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"

#include <windows.h>

#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "dlss";

// nvsdk_ngx.h / nvsdk_ngx_defs.h as the game links them (NGX core rel_1_6, API version 0x14).
constexpr int kFeatureSuperSampling = 1;
constexpr int kLoggingOn = 1;
constexpr int kLoggingVerbose = 2;
constexpr int kApiWithLogging = 0x14; // the game's static NGX reads LoggingInfo from this version on

bool ngxSucceeded(int result) {
    return (static_cast<unsigned>(result) & 0xFFF00000u) != 0xBAD00000u;
}

using NgxLogCallback = void (*)(const char* message, int level, int sourceComponent);

struct PathListInfo {
    const wchar_t* const* path;
    unsigned int length;
};
struct LoggingInfo {
    NgxLogCallback callback;
    int minimumLevel;
    bool disableOtherSinks;
};
struct FeatureCommonInfo {
    PathListInfo pathList;
    void* internalData;
    LoggingInfo logging;
};
// The game's static Init checks these fields (RVA 0x2268466): callback +0x18, level +0x20, flag +0x24.
static_assert(offsetof(FeatureCommonInfo, logging) == 0x18);
static_assert(offsetof(FeatureCommonInfo, logging) + offsetof(LoggingInfo, minimumLevel) == 0x20);
static_assert(offsetof(FeatureCommonInfo, logging) + offsetof(LoggingInfo, disableOtherSinks) == 0x24);

using InitFn = int (*)(unsigned long long appId,
                       const wchar_t* dataPath,
                       void* instance,
                       void* physicalDevice,
                       void* device,
                       const FeatureCommonInfo* info,
                       int version);
using CreateFn = int (*)(void* commandBuffer, int feature, void* parameters, void** handle);
using SetUIntFn = void (*)(void* parameters, const char* name, unsigned int value);
using LoadLibraryExWFn = HMODULE(WINAPI*)(LPCWSTR name, HANDLE file, DWORD flags);

// NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_* : one per quality mode, read when the feature is created.
constexpr const char* kPresetParameters[] = {
    "DLSS.Hint.Render.Preset.DLAA",
    "DLSS.Hint.Render.Preset.Quality",
    "DLSS.Hint.Render.Preset.Balanced",
    "DLSS.Hint.Render.Preset.Performance",
    "DLSS.Hint.Render.Preset.UltraPerformance",
    "DLSS.Hint.Render.Preset.UltraQuality",
};

InitFn g_init = nullptr;
CreateFn g_create = nullptr;
SetUIntFn g_setUInt = nullptr;
LoadLibraryExWFn g_loadLibraryExW = nullptr;

std::once_flag g_installOnce;
std::wstring g_path;                      // the DLL, as given (full path)
std::wstring g_folder;                    // its folder: first in NGX's search path
std::vector<const wchar_t*> g_searchPath; // empty with the redirect route
dlss_dll::Route g_route = dlss_dll::Route::FolderFirst;
dlss_dll::Decision g_decision;
int g_ngxLogLevel = kLoggingOn;

std::atomic<int> g_ngxLines{0};
constexpr int kMaxNgxLines = 400;
std::atomic<int> g_creates{0};
std::atomic<int> g_redirects{0};

std::string narrow(std::wstring_view text) {
    std::string out;
    for (const wchar_t c : text) {
        out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return out;
}

// The file's first bytes, its version resource and the vendor it names.
dlss_dll::FileFacts inspect(const std::wstring& path) {
    dlss_dll::FileFacts facts;
    facts.nameOk = dlss_dll::hasDllName(path);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return facts;
    }
    facts.exists = true;
    std::uint8_t head[4096] = {};
    DWORD read = 0;
    const BOOL ok = ReadFile(file, head, sizeof(head), &read, nullptr);
    CloseHandle(file);
    facts.pe = dlss_dll::checkPeHeaders(head, ok ? read : 0);

    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    std::vector<std::uint8_t> data(size);
    if (!size || !GetFileVersionInfoW(path.c_str(), 0, size, data.data())) {
        return facts;
    }
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT fixedSize = 0;
    if (VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixedSize) && fixed &&
        fixedSize >= sizeof(VS_FIXEDFILEINFO)) {
        facts.hasVersion = true;
        facts.version = dlss_dll::versionFromFixed(fixed->dwFileVersionMS, fixed->dwFileVersionLS);
    }
    struct Translation {
        WORD language;
        WORD codePage;
    }* translations = nullptr;
    UINT translationSize = 0;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations),
                       &translationSize) &&
        translations && translationSize >= sizeof(Translation)) {
        wchar_t key[64];
        swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\CompanyName", translations[0].language,
                   translations[0].codePage);
        wchar_t* company = nullptr;
        UINT companySize = 0;
        if (VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&company), &companySize) && company) {
            facts.company = narrow(company);
        }
    }
    return facts;
}

// Every nvngx_dlss.dll loaded in the process, with its file version.
void logLoadedDlls(const char* when) {
    HMODULE modules[1024];
    DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
        EVR_LOG("%s: %s: the process's modules cannot be listed (%lu)", kTag, when, GetLastError());
        return;
    }
    const DWORD count = std::min<DWORD>(needed / sizeof(HMODULE), static_cast<DWORD>(std::size(modules)));
    int found = 0;
    for (DWORD i = 0; i < count; ++i) {
        wchar_t path[MAX_PATH * 2] = {};
        const DWORD n = GetModuleFileNameW(modules[i], path, static_cast<DWORD>(std::size(path)));
        if (n == 0 || !dlss_dll::hasDllName(std::wstring_view(path, n))) {
            continue;
        }
        ++found;
        const dlss_dll::FileFacts facts = inspect(path);
        if (dlss_dll::samePath(path, g_path)) {
            EVR_LOG("%s: using %ls (version %s) [%s]", kTag, path,
                    dlss_dll::versionText(facts.version).c_str(), when);
        } else {
            EVR_LOG("%s: NGX loaded %ls (version %s), not %ls: the newer DLL is NOT in use [%s]", kTag, path,
                    dlss_dll::versionText(facts.version).c_str(), g_path.c_str(), when);
        }
    }
    if (!found) {
        EVR_LOG("%s: no nvngx_dlss.dll is loaded yet [%s]", kTag, when);
    }
}

void ngxLog(const char* message, int level, int sourceComponent) {
    const int line = g_ngxLines.fetch_add(1);
    if (line > kMaxNgxLines) {
        return;
    }
    if (line == kMaxNgxLines) {
        EVR_LOG("ngx: (further NGX log lines are dropped)");
        return;
    }
    std::string text = message ? message : "";
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    if (text.size() > 1800) {
        text.resize(1800);
    }
    EVR_LOG("ngx[%d/%d]: %s", level, sourceComponent, text.c_str());
}

// The redirect route: every load of a file named nvngx_dlss.dll (NGX's, from any folder) takes the chosen
// one.
HMODULE WINAPI loadLibraryExWHook(LPCWSTR name, HANDLE file, DWORD flags) {
    if (name && dlss_dll::shouldRedirect(name, g_path)) {
        if (g_redirects.fetch_add(1) < 8) {
            EVR_LOG("%s: redirecting the load of %ls (flags 0x%lX) to %ls", kTag, name, flags,
                    g_path.c_str());
        }
        return g_loadLibraryExW(g_path.c_str(), file, flags);
    }
    return g_loadLibraryExW(name, file, flags);
}

int initHook(unsigned long long appId,
             const wchar_t* dataPath,
             void* instance,
             void* physicalDevice,
             void* device,
             const FeatureCommonInfo* info,
             int version) {
    // The folder first, then any the game gave (build 25216728 gives none). NGX may keep the list: it is
    // never freed.
    const std::vector<const wchar_t*>* paths = &g_searchPath;
    if (info && info->pathList.path && info->pathList.length) {
        auto* joined = new std::vector<const wchar_t*>(g_searchPath);
        joined->insert(joined->end(), info->pathList.path, info->pathList.path + info->pathList.length);
        paths = joined;
    }
    FeatureCommonInfo mine{};
    mine.pathList = {paths->data(), static_cast<unsigned>(paths->size())};
    if (info) {
        mine.internalData = info->internalData;
        mine.logging = info->logging;
    }
    if (version >= kApiWithLogging && !mine.logging.callback) {
        mine.logging = {&ngxLog, g_ngxLogLevel, false};
    }
    const int result = g_init(appId, dataPath, instance, physicalDevice, device, &mine, version);
    EVR_LOG("%s: NVSDK_NGX_VULKAN_Init with %u folder(s) in the search path%s%ls (game info %s, API 0x%X): "
            "0x%08X",
            kTag, static_cast<unsigned>(paths->size()), g_searchPath.empty() ? "" : ", first ",
            g_searchPath.empty() ? L"" : g_folder.c_str(), info ? "given" : "none",
            static_cast<unsigned>(version), static_cast<unsigned>(result));
    if (ngxSucceeded(result)) {
        logLoadedDlls("after Init");
        return result;
    }
    // 0xBAD0000C (out of date): the driver's NGX has no Init_Ext and takes no search path. Initialised again
    // exactly as the game asked; the redirect route (if chosen) still applies.
    const int again = g_init(appId, dataPath, instance, physicalDevice, device, info, version);
    EVR_LOG("%s: NGX refused the layer's init info (0x%08X): initialised as the game asked (0x%08X)%s", kTag,
            static_cast<unsigned>(result), static_cast<unsigned>(again),
            g_searchPath.empty() ? "" : "; the game's own nvngx_dlss.dll is used");
    return again;
}

int createHook(void* commandBuffer, int feature, void* parameters, void** handle) {
    const bool dlss = feature == kFeatureSuperSampling && parameters;
    // Not gated on the multiplayer guard: the hint changes no game state, only how the player's chosen DLL
    // runs, and both eyes' features must get it from the game's very first create on.
    const bool preset = dlss && g_decision.applyPreset;
    if (preset) {
        for (const char* name : kPresetParameters) {
            g_setUInt(parameters, name, g_decision.preset);
        }
    }
    const int result = g_create(commandBuffer, feature, parameters, handle);
    if (dlss) {
        const int n = g_creates.fetch_add(1);
        if (n < 16) {
            const unsigned id = handle && *handle ? *static_cast<const unsigned*>(*handle) : 0u;
            EVR_LOG("%s: DLSS feature create #%d: result 0x%08X, feature %u, preset %s", kTag, n + 1,
                    static_cast<unsigned>(result), id,
                    preset ? dlss_dll::presetName(g_decision.preset).c_str() : "the DLL's own");
        }
        if (n < 2) {
            logLoadedDlls(n == 0 ? "first DLSS feature" : "second DLSS feature");
        }
    }
    return result;
}

void* exported(const GameImage& image, const char* name) {
    auto* p = reinterpret_cast<std::byte*>(GetProcAddress(GetModuleHandleW(nullptr), name));
    if (!p || !image.inText(p)) {
        EVR_LOG("%s: the game exports no %s in its code", kTag, name);
        return nullptr;
    }
    return p;
}

void install() {
    std::wstring value;
    if (!readEnv(L"ETERNALVR_DLSS_DLL", value) || value.empty()) {
        return; // the default: the game's own DLL, nothing hooked
    }
    wchar_t full[MAX_PATH * 2] = {};
    const DWORD n = GetFullPathNameW(value.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
    g_path = n > 0 && n < std::size(full) ? std::wstring(full, n) : value;
    g_folder = dlss_dll::folderOf(g_path);

    std::wstring presetText;
    readEnv(L"ETERNALVR_DLSS_PRESET", presetText);
    std::wstring routeText;
    readEnv(L"ETERNALVR_DLSS_ROUTE", routeText);
    const std::optional<dlss_dll::Route> route = dlss_dll::parseRoute(narrow(routeText));
    if (!route) {
        EVR_LOG("%s: unknown ETERNALVR_DLSS_ROUTE '%ls' (path or redirect): the search path is used", kTag,
                routeText.c_str());
    }
    g_route = route.value_or(dlss_dll::Route::FolderFirst);
    std::wstring logLevel;
    if (readEnv(L"ETERNALVR_DLSS_NGX_LOG", logLevel) && logLevel == L"verbose") {
        g_ngxLogLevel = kLoggingVerbose;
    }

    const dlss_dll::FileFacts facts = inspect(g_path);
    g_decision = dlss_dll::decide(facts, narrow(presetText));
    if (!g_decision.use || g_folder.empty()) {
        EVR_LOG("%s: not using %ls: %s; the game's own nvngx_dlss.dll is used", kTag, g_path.c_str(),
                g_decision.reason.empty() ? "it has no folder" : g_decision.reason.c_str());
        return;
    }
    EVR_LOG("%s: %ls is DLSS %s (%s)%s%s", kTag, g_path.c_str(), dlss_dll::versionText(facts.version).c_str(),
            facts.company.c_str(), g_decision.reason.empty() ? "" : "; ", g_decision.reason.c_str());
    if (g_decision.applyPreset) {
        EVR_LOG("%s: render preset %s for every DLSS quality", kTag,
                dlss_dll::presetName(g_decision.preset).c_str());
    } else if (!g_decision.presetNote.empty()) {
        EVR_LOG("%s: %s", kTag, g_decision.presetNote.c_str());
    }

    GameImage image;
    if (!locateGameImage(image, kTag)) {
        EVR_LOG("%s: the game module cannot be read; the game's own nvngx_dlss.dll is used", kTag);
        return;
    }
    void* init = exported(image, "NVSDK_NGX_VULKAN_Init");
    void* create = exported(image, "NVSDK_NGX_VULKAN_CreateFeature");
    g_setUInt = reinterpret_cast<SetUIntFn>(exported(image, "NVSDK_NGX_Parameter_SetUI"));
    if (!init || !create || !g_setUInt) {
        EVR_LOG("%s: the game's NGX entry points are missing; the game's own nvngx_dlss.dll is used", kTag);
        return;
    }
    // Create first, so every feature create is seen once the newer DLL can load; the search path is in place
    // before the Init detour is.
    std::string error;
    if (!installInlineHook(create, reinterpret_cast<void*>(&createHook), reinterpret_cast<void**>(&g_create),
                           error)) {
        EVR_LOG("%s: CreateFeature hook failed (%s); the game's own nvngx_dlss.dll is used", kTag,
                error.c_str());
        return;
    }
    const bool searchPath = g_route == dlss_dll::Route::FolderFirst;
    if (searchPath) {
        g_searchPath = {g_folder.c_str()};
    }
    if (!installInlineHook(init, reinterpret_cast<void*>(&initHook), reinterpret_cast<void**>(&g_init),
                           error)) {
        EVR_LOG("%s: Init hook failed (%s)%s", kTag, error.c_str(),
                searchPath ? "; the game's own nvngx_dlss.dll is used" : ": no NGX log");
        if (searchPath) {
            g_decision.applyPreset = false;
            return;
        }
    }
    if (!searchPath) {
        HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
        void* load =
            kernelBase ? reinterpret_cast<void*>(GetProcAddress(kernelBase, "LoadLibraryExW")) : nullptr;
        if (!load || !installInlineHook(load, reinterpret_cast<void*>(&loadLibraryExWHook),
                                        reinterpret_cast<void**>(&g_loadLibraryExW), error)) {
            EVR_LOG("%s: LoadLibraryExW hook failed (%s); the game's own nvngx_dlss.dll is used", kTag,
                    load ? error.c_str() : "no KernelBase export");
            g_decision.applyPreset = false;
            return;
        }
    }
    EVR_LOG("%s: NGX Init (RVA 0x%X) and CreateFeature (RVA 0x%X) hooked; route: %s", kTag,
            image.rva(static_cast<std::byte*>(init)), image.rva(static_cast<std::byte*>(create)),
            searchPath ? "the DLL's folder first in NGX's search path"
                       : "every nvngx_dlss.dll load redirected");
}

} // namespace

void installDlssDll() {
    std::call_once(g_installOnce, install);
}

} // namespace evr::vkcore
