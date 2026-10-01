# The Vulkan implicit layer DLL (ARCHITECTURE sections 4 and 7). Windows only.

include(EvrVulkanHeaders)
include(EvrOpenXr)
include(EvrSafetyHook)

# The mid-function hook wrapper is the only code that sees safetyhook, which needs C++23.
add_library(evr_mid_hook STATIC mid_hook.cpp)
target_include_directories(evr_mid_hook PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(evr_mid_hook PRIVATE cxx_std_23)
target_compile_definitions(evr_mid_hook PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(evr_mid_hook PRIVATE evr_warnings evr_safetyhook)

# The device feature chain edit is its own library so its tests can link it without the layer.
add_library(evr_device_features STATIC device_features.cpp)
target_include_directories(evr_device_features PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(evr_device_features PUBLIC cxx_std_20)
target_compile_definitions(evr_device_features PUBLIC VK_NO_PROTOTYPES)
target_link_libraries(evr_device_features PRIVATE evr_warnings PUBLIC Vulkan::Headers)

# The debug command schedule (debug_commands.cpp), tested without the game.
add_library(evr_debug_script STATIC debug_script.cpp)
target_include_directories(evr_debug_script PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(evr_debug_script PUBLIC cxx_std_20)
target_link_libraries(evr_debug_script PRIVATE evr_warnings evr_mp_policy)

# Pure helpers of the shader dump, tested without a device.
add_library(evr_dump_format STATIC dump_format.cpp)
target_include_directories(evr_dump_format PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(evr_dump_format PUBLIC cxx_std_20)
target_compile_definitions(evr_dump_format PUBLIC VK_NO_PROTOTYPES)
target_link_libraries(evr_dump_format PRIVATE evr_warnings PUBLIC Vulkan::Headers)

add_library(evr_layer SHARED
    backdrop_probe.cpp
    bin_tile_hooks.cpp
    body_follow.cpp
    bug_capture.cpp
    camera_anim_hook.cpp
    cinema_view.cpp
    client_rect.cpp
    cpu_timing.cpp
    debug_commands.cpp
    demon_aim.cpp
    demon_view.cpp
    facing_hook.cpp
    device_augment.cpp
    dlss_dll.cpp
    dlss_menu_hooks.cpp
    eye_capture.cpp
    frame_pacing.cpp
    game_build.cpp
    game_code.cpp
    game_text.cpp
    glory_view.cpp
    gpu_timing.cpp
    gpu_timing_report.cpp
    head_sweep.cpp
    import_patch.cpp
    keep_active.cpp
    key_inject.cpp
    layer_entry.cpp
    log.cpp
    mirror_place.cpp
    keep_prev_hooks.cpp
    motion_capture.cpp
    moved_flag_hooks.cpp
    mp_guard.cpp
    object_prev_hooks.cpp
    world_gui_hooks.cpp
    player_aim.cpp
    presenter_alt.cpp
    presenter_copy.cpp
    presenter_fade.cpp
    presenter_mirror.cpp
    presenter_reticle.cpp
    presenter_vignette.cpp
    presenter_window.cpp
    ring_trace.cpp
    window_timing.cpp
    runtime_cvars.cpp
    presenter_frame.cpp
    presenter_frame_log.cpp
    presenter_head.cpp
    presenter_ring.cpp
    presenter_seq.cpp
    presenter_snapshot.cpp
    presenter_stereo.cpp
    presenter_ui.cpp
    presenter_wrist.cpp
    presenter_reconnect.cpp
    presenter_xr.cpp
    presenter_xr_events.cpp
    room_follow.cpp
    reticle_depth.cpp
    room_scale.cpp
    scatter_hooks.cpp
    seq_alternate.cpp
    seq_hooks.cpp
    seq_locate.cpp
    seq_stack.cpp
    shader_dump.cpp
    shader_dump_record.cpp
    stall_watch.cpp
    status_file.cpp
    stereo_hooks.cpp
    stereo_present.cpp
    surface_entry.cpp
    swapchain_entry.cpp
    taa_env.cpp
    taa_hooks.cpp
    taa_locate.cpp
    taa_ngx.cpp
    taa_resize.cpp
    test_cpu_load.cpp
    test_keys.cpp
    ui_capture.cpp
    ui_engine.cpp
    ui_vulkan.cpp
    ui_wash.cpp
    view_hook.cpp
    virtual_client.cpp
    vram_watch.cpp
    xr_presenter.cpp)
# Motion controllers (M5, controllers.hpp).
target_sources(evr_layer PRIVATE
    aim_hooks.cpp
    bhaptics_game.cpp
    bhaptics_link.cpp
    bhaptics_portal.cpp
    controller_test.cpp
    controllers.cpp
    game_view_poses.cpp
    haptics_xr.cpp
    input_profiles.cpp
    input_xr.cpp
    offhand_arm.cpp
    offhand_hook.cpp
    offhand_mods.cpp
    rumble_hook.cpp
    usercmd_hook.cpp
    viewmodel_hook.cpp
    vrs_gui.cpp
    vrs_nv.cpp
    xinput_hook.cpp
    xr_runtime.cpp)
# Menus on a panel with a laser pointer (M6, docs/VR_MENUS.md).
target_sources(evr_layer PRIVATE
    menu_cursor.cpp
    menu_input.cpp
    presenter_menu.cpp)
target_link_libraries(evr_layer PRIVATE evr_arm evr_bhaptics evr_comfort evr_foveation evr_input evr_menu evr_pacing evr_roomscale)
set_target_properties(evr_layer PROPERTIES OUTPUT_NAME EternalVR)
target_compile_features(evr_layer PRIVATE cxx_std_20)
target_compile_definitions(evr_layer PRIVATE
    VK_NO_PROTOTYPES
    VK_USE_PLATFORM_WIN32_KHR
    XR_USE_PLATFORM_WIN32
    XR_USE_GRAPHICS_API_D3D12
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_link_libraries(evr_layer PRIVATE
    evr_warnings
    evr_debug_script
    evr_device_features
    evr_dlss_dll
    evr_dump_format
    evr_gpu_timing
    evr_layer_gate
    evr_key_injection
    evr_mid_hook
    evr_mp_policy
    evr_render_size
    evr_resolver
    evr_stereo_seq
    evr_ui_layer
    evr_xr_math
    Vulkan::Headers
    evr_openxr_headers
    d3d12
    dxgi
    version)
# The manifest sits next to the DLL and refers to it by a relative path, which the loader resolves
# against the manifest's own directory. That lets the launcher register the build output directly.
# openxr_loader.dll is loaded from the same folder.
set(EVR_LAYER_LIBRARY_NAME "EternalVR.dll")
configure_file(VK_LAYER_ETERNALVR.json.in "${CMAKE_CURRENT_BINARY_DIR}/VK_LAYER_ETERNALVR.json" @ONLY)

# The version resource the launcher checks against its own version (ROADMAP M4.5, "User data and
# updates"). A Debug build is marked -dev, which the launcher only warns about.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(EVR_PRODUCT_VERSION "${PROJECT_VERSION}-dev")
    set(EVR_VERSION_FLAGS "VS_FF_DEBUG|VS_FF_PRERELEASE")
else()
    set(EVR_PRODUCT_VERSION "${PROJECT_VERSION}")
    set(EVR_VERSION_FLAGS "0")
endif()
configure_file(version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/eternalvr_version.rc" @ONLY)
target_sources(evr_layer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/eternalvr_version.rc")
set_source_files_properties(status_file.cpp PROPERTIES COMPILE_DEFINITIONS "EVR_LAYER_VERSION=\"${EVR_PRODUCT_VERSION}\"")
add_custom_command(TARGET evr_layer POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/VK_LAYER_ETERNALVR.json"
        "$<TARGET_FILE_DIR:evr_layer>/VK_LAYER_ETERNALVR.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${EVR_OPENXR_LOADER_DLL}"
        "$<TARGET_FILE_DIR:evr_layer>/openxr_loader.dll"
    VERBATIM)
