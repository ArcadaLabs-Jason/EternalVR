# The game's render size in VR (T-031, docs/rig-findings/render-size.md): the settings, the size chosen from
# the OpenXR runtime's limits and the desktop window rectangles. No Windows, Vulkan or OpenXR here.
evr_add_module(evr_render_size
    SOURCES
        mirror_window.cpp
        render_cap.cpp
        render_size.cpp)
