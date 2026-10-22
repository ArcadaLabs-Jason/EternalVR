# The UI layer (docs/rig-findings/ui-layer.md): the logic that needs no engine, Vulkan or OpenXR (settings,
# the layout tracker, the checks on the game's GUI target), so it is tested on every platform.
evr_add_module(evr_ui_layer
    SOURCES
        additive_wash.cpp
        backdrop.cpp
        crosshair.cpp
        gui_target.cpp
        hud_blocks.cpp
        hud_view.cpp
        hud_regions.cpp
        layout_tracker.cpp
        motion_target.cpp
        ui_settings.cpp
        weapon_hud.cpp
        weapon_wheel.cpp
        wheel_bounds.cpp
        wrist_hud.cpp
    DEPENDS
        evr_common)
