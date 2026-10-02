#include "vkcore/log.hpp"

#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace evr::vkcore {

namespace {

std::mutex g_mutex;
std::FILE* g_file = nullptr;
bool g_opened = false;
ULONGLONG g_startTicks = 0;

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr,
                        nullptr);
    return out;
}

} // namespace

bool readEnv(const wchar_t* name, std::wstring& value) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) {
        value.clear();
        return GetLastError() != ERROR_ENVVAR_NOT_FOUND;
    }
    std::vector<wchar_t> buffer(size);
    const DWORD written = GetEnvironmentVariableW(name, buffer.data(), size);
    value.assign(buffer.data(), written);
    return true;
}

std::wstring logDirectory() {
    std::wstring dir;
    readEnv(L"ETERNALVR_LOG_DIR", dir);
    return dir;
}

void logOpen() {
    std::lock_guard lock(g_mutex);
    if (g_opened) {
        return;
    }
    g_opened = true;
    g_startTicks = GetTickCount64();
    const std::wstring dir = logDirectory();
    if (dir.empty()) {
        return;
    }
    CreateDirectoryW(dir.c_str(), nullptr);

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[128];
    swprintf_s(name, L"\\eternalvr-%04u%02u%02u-%02u%02u%02u-%lu.log", now.wYear, now.wMonth, now.wDay,
               now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
    const std::wstring path = dir + name;
    g_file = _wfsopen(path.c_str(), L"w", _SH_DENYWR);

    // Loaded marker (T-079): lets the launch script tell that the loader picked the layer up.
    const std::wstring marker = dir + L"\\LAYER_LOADED";
    if (std::FILE* m = _wfsopen(marker.c_str(), L"w", _SH_DENYWR)) {
        std::fprintf(m, "pid %lu\nlog %s\n", GetCurrentProcessId(), narrow(path).c_str());
        std::fclose(m);
    }
    // Wall-clock anchor for the relative times on every line (launch timelines).
    if (g_file) {
        std::fprintf(g_file, "[    0.000] log start %04u-%02u-%02u %02u:%02u:%02u.%03u local\n", now.wYear,
                     now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
        std::fflush(g_file);
    }
}

double logSeconds() {
    return static_cast<double>(GetTickCount64() - g_startTicks) / 1000.0;
}

void logf(const char* format, ...) {
    char message[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    char line[2200];
    std::snprintf(line, sizeof(line), "[%9.3f] [%5lu] %s\n", logSeconds(), GetCurrentThreadId(), message);

    std::lock_guard lock(g_mutex);
    if (g_file) {
        std::fputs(line, g_file);
        std::fflush(g_file);
    }
    OutputDebugStringA(line);
}

void logLoaderVersion() {
    HMODULE loader = GetModuleHandleW(L"vulkan-1.dll");
    if (!loader) {
        EVR_LOG("vulkan-1.dll: not loaded by name");
        return;
    }
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(loader, path, MAX_PATH);
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &ignored);
    std::vector<std::uint8_t> data(size);
    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoSize = 0;
    if (size && GetFileVersionInfoW(path, 0, size, data.data()) &&
        VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) && info) {
        EVR_LOG("vulkan-1.dll %ls version %u.%u.%u.%u", path, HIWORD(info->dwFileVersionMS),
                LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    } else {
        EVR_LOG("vulkan-1.dll %ls (no version info)", path);
    }
}

} // namespace evr::vkcore
