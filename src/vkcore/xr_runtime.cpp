#include "vkcore/xr_runtime.hpp"

#include "vkcore/log.hpp"

#include <windows.h>

#include <mutex>

namespace evr::vkcore {

namespace {

std::mutex g_mutex;
std::string g_name;

} // namespace

void setXrRuntimeName(const char* name) {
    char test[128] = {};
    const DWORD n = GetEnvironmentVariableA("ETERNALVR_TEST_RUNTIME_NAME", test, sizeof(test));
    std::lock_guard lock(g_mutex);
    if (n > 0 && n < sizeof(test)) {
        EVR_LOG("xr: ETERNALVR_TEST_RUNTIME_NAME: the runtime is taken as '%s'", test);
        g_name = test;
    } else {
        g_name = name ? name : "";
    }
}

std::string xrRuntimeName() {
    std::lock_guard lock(g_mutex);
    return g_name;
}

} // namespace evr::vkcore
