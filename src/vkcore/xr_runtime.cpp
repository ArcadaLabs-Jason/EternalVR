#include "vkcore/xr_runtime.hpp"

#include <mutex>

namespace evr::vkcore {

namespace {

std::mutex g_mutex;
std::string g_name;

} // namespace

void setXrRuntimeName(const char* name) {
    std::lock_guard lock(g_mutex);
    g_name = name ? name : "";
}

std::string xrRuntimeName() {
    std::lock_guard lock(g_mutex);
    return g_name;
}

} // namespace evr::vkcore
