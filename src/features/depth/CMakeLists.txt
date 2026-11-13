# The depth layer (ETERNALVR_DEPTH_LAYER, depth_layer.hpp): the switch, the depth swapchain's format, the game's depth
# values in metres for OpenXR's planes, the source image's checks and read layout, why the layer is off, and the
# statistics of a depth image read back. No Vulkan, D3D12, OpenXR or Windows here.
evr_add_module(evr_depth
    SOURCES
        depth_layer.cpp)
