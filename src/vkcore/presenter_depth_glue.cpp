// The depth layer's start in the presenter (presenter_depth.hpp): what it needs to know of the stereo mode
// and the UI layer when the ring is built.

#include "vkcore/presenter_impl.hpp"

#include "vkcore/presenter_eyes.hpp"
#include "vkcore/ui_engine.hpp"
#include "vkcore/ui_vulkan.hpp"

#include <tuple>

namespace evr::vkcore {

// The depth layer's per-slot state and command buffers follow the ring's.
static_assert(DepthLayer::kSlots == kRingSize);
static_assert(DepthLayer::kCommandBuffers == kRingSize * 2);
static_assert(std::tuple_size_v<decltype(FamilyCommands::buffers)> == DepthLayer::kCommandBuffers);

void XrPresenter::Impl::createDepthBuffers() {
    // Off by default: one line says so (and whether the runtime lists the extension), and nothing is made.
    depth::StartState start;
    start.routeS = seqActive.load(std::memory_order_acquire) && ringEyes == 2;
    // On or Auto: alternate eyes' images pair eyes of different presents and ticks, which carry no depth.
    start.alternateEyes = settings.stereo.alternateEyes;
    start.parallelEyes = eyeCopyRequested();
    // The UI layer's image hooks keep the record and follow the layout of the game's depth image.
    start.tracking = settings.ui.enabled && ui_vulkan::enabled() && ui_engine::located();
    depthLayer.createBuffers(dev, d3dDevice.Get(), start, xrFormats, ringExtent, eyeExtent);
}

} // namespace evr::vkcore
