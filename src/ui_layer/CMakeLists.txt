# The UI layer (docs/rig-findings/ui-layer.md): the logic that needs no engine, Vulkan or OpenXR (settings,
# the layout tracker, the checks on the game's GUI target), so it is tested on every platform.
evr_add_module(evr_ui_layer
    SOURCES
        additive_wash.cpp
        backdrop.cpp
        gui_target.cpp
        layout_tracker.cpp
        ui_settings.cpp)
