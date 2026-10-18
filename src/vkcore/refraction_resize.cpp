#include "vkcore/refraction_resize.hpp"

#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/refraction_locate.hpp"

#include <cstdint>
#include <mutex>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-refract";
constexpr int kLogged = 8; // resize lines of each kind

std::mutex g_mutex;
refraction_sizes::ResizeImageFn g_resize = nullptr;
std::byte* const* g_dcGlobal = nullptr;
void (*g_restart)() = nullptr;
// Under g_mutex.
const std::byte* g_dc = nullptr;
refraction_sizes::ImagePairs g_pairs{};
refraction_sizes::SizeGuard g_guard;
int g_followedLogged = 0;
int g_failedLogged = 0;

void onImagesResized(const HookRegisters&) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    refraction_sizes::FollowResult r;
    {
        std::lock_guard lock(g_mutex);
        if (!g_dc || *g_dcGlobal != g_dc) {
            return;
        }
        r = refraction_sizes::followEngine(g_pairs, g_resize);
        const refraction_sizes::Extent acc = refraction_sizes::extentOf(g_pairs[1].engine);
        const refraction_sizes::Extent mask = refraction_sizes::extentOf(g_pairs[0].engine);
        if (r.changed > 0 && g_followedLogged++ < kLogged) {
            EVR_LOG("%s: refraction images resized to %dx%d (mask tiles %dx%d): %d of eye R's followed; both "
                    "eyes start over",
                    kTag, acc.width, acc.height, mask.width, mask.height, r.changed);
        }
        if (r.failed > 0 && g_failedLogged++ < kLogged) {
            EVR_LOG(
                "%s: the resize to %dx%d (mask tiles %dx%d) left %d of eye R's refraction images at their "
                "size, the first pair %d; eye R blends with no history until they match",
                kTag, acc.width, acc.height, mask.width, mask.height, r.failed, r.firstFailed);
        }
    }
    if (r.changed > 0) {
        g_restart();
    }
}

} // namespace

bool installRefractionResizeHook(const RefractionSites& sites, void (*restart)(), std::string& error) {
    g_resize = reinterpret_cast<refraction_sizes::ResizeImageFn>(const_cast<std::byte*>(sites.resizeImage));
    g_dcGlobal = reinterpret_cast<std::byte* const*>(sites.deviceContext);
    g_restart = restart;
    return installMidHook(sites.imagesResized, &onImagesResized, error);
}

void trackRefractionImages(const std::byte* deviceContext, const refraction_sizes::ImagePairs& pairs) {
    std::lock_guard lock(g_mutex);
    g_dc = deviceContext;
    g_pairs = pairs;
    g_guard.reset();
}

refraction_sizes::SizeCheck checkRefractionImageSizes(std::uint32_t render) {
    std::lock_guard lock(g_mutex);
    return g_guard.atRender(render, refraction_sizes::sameExtents(g_pairs));
}

bool refractionImagesMismatched() {
    std::lock_guard lock(g_mutex);
    return g_guard.mismatched();
}

} // namespace evr::vkcore
