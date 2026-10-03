# Each module has its own test executable so a failure points straight at the module.

add_library(evr_test_main STATIC test_main.cpp)
target_include_directories(evr_test_main PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(evr_test_main PUBLIC evr_doctest PRIVATE evr_warnings)

# doctest's TEST_CASE expands to __COUNTER__, which recent Clang reports under -Wpedantic as a C2y
# extension. The warning is about the test framework's macro, not our code, so silence it where the
# compiler knows the warning (GCC accepts any -Wno- flag, so the positive form is tested).
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wc2y-extensions EVR_HAS_C2Y_EXTENSIONS_WARNING)
if(EVR_HAS_C2Y_EXTENSIONS_WARNING)
    target_compile_options(evr_test_main PUBLIC -Wno-c2y-extensions)
endif()

evr_add_test(evr_common_tests
    SOURCES
        common/mat4_tests.cpp
        common/parse_float_tests.cpp
        common/pose_tests.cpp
        common/quat_tests.cpp
        common/result_tests.cpp
        common/retire_queue_tests.cpp
        common/xr_recovery_tests.cpp
    DEPENDS evr_common)

evr_add_test(evr_xr_math_tests
    SOURCES
        xr_math/aim_check_tests.cpp
        xr_math/camera_anim_tests.cpp
        xr_math/cinema_quad_tests.cpp
        xr_math/display_lead_tests.cpp
        xr_math/enclosing_fov_tests.cpp
        xr_math/eye_clip_transform_tests.cpp
        xr_math/hand_aim_tests.cpp
        xr_math/head_aim_tests.cpp
        xr_math/forced_view_tests.cpp
        xr_math/head_view_tests.cpp
        xr_math/offhand_pose_tests.cpp
        xr_math/weapon_pose_tests.cpp
        xr_math/projection_tests.cpp
        xr_math/stereo_view_tests.cpp
        xr_math/upright_space_tests.cpp
    DEPENDS evr_xr_math)

evr_add_test(evr_resolver_tests
    SOURCES
        engine/eternal/resolver/pattern_tests.cpp
        engine/eternal/resolver/pe_builder.cpp
        engine/eternal/resolver/pe_image_tests.cpp
        engine/eternal/resolver/rip_xref_tests.cpp
        engine/eternal/resolver/string_search_tests.cpp
    DEPENDS evr_resolver)

evr_add_test(evr_settings_tests
    SOURCES
        platform/settings/builtin_tests.cpp
        platform/settings/profile_store_tests.cpp
        platform/settings/profile_tests.cpp
        platform/settings/schema_tests.cpp
    DEPENDS evr_settings)

# The built-in maps are checked by compiling them, so these tests also need the input module.
evr_add_test(evr_game_eternal_tests
    SOURCES
        game/eternal/controller_data_tests.cpp
        game/eternal/controls_coverage_tests.cpp
        game/eternal/game_action_tests.cpp
        game/eternal/quest_touch_bindings_tests.cpp
        game/eternal/usercmd_buttons_tests.cpp
        game/eternal/weapon_offsets_tests.cpp
    DEPENDS evr_input)

evr_add_test(evr_posture_tests
    SOURCES
        features/posture/anchor_detector_tests.cpp
        features/posture/eye_height_tests.cpp
        features/posture/posture_detector_tests.cpp
        features/posture/posture_tracker_tests.cpp
    DEPENDS evr_posture)

evr_add_test(evr_roomscale_tests
    SOURCES
        features/roomscale/body_follow_tests.cpp
        features/roomscale/driven_offset_tests.cpp
        features/roomscale/follow_test_steps_tests.cpp
        features/roomscale/head_fade_tests.cpp
        features/roomscale/head_offset_tests.cpp
        features/roomscale/long_press_tests.cpp
        features/roomscale/room_anchor_tests.cpp
        features/roomscale/roomscale_settings_tests.cpp
    DEPENDS evr_roomscale)

evr_add_test(evr_comfort_tests
    SOURCES
        features/comfort/glory_kill_tests.cpp
        features/comfort/vignette_tests.cpp
    DEPENDS evr_comfort)

evr_add_test(evr_pacing_tests
    SOURCES
        features/pacing/display_period_watch_tests.cpp
        features/pacing/pace_policy_tests.cpp
    DEPENDS evr_pacing)

evr_add_test(evr_foveation_tests
    SOURCES
        features/foveation/attachment_images_tests.cpp
        features/foveation/eye_targets_tests.cpp
        features/foveation/foveation_preset_tests.cpp
        features/foveation/foveation_region_tests.cpp
        features/foveation/rate_pattern_tests.cpp
    DEPENDS evr_foveation)

evr_add_test(evr_input_tests
    SOURCES
        features/input/aim_smoothing_tests.cpp
        features/input/analog_button_tests.cpp
        features/input/arm_gestures_tests.cpp
        features/input/binding_compiler_tests.cpp
        features/input/binding_keys_tests.cpp
        features/input/binding_overrides_tests.cpp
        features/input/binding_text_tests.cpp
        features/input/binding_watch_tests.cpp
        features/input/button_labels_tests.cpp
        features/input/capture_chord_tests.cpp
        features/input/controller_bindings_tests.cpp
        features/input/controller_family_tests.cpp
        features/input/controller_settings_tests.cpp
        features/input/cutscene_skip_tests.cpp
        features/input/dashboard_pause_tests.cpp
        features/input/dossier_press_tests.cpp
        features/input/forced_angles_tests.cpp
        features/input/hands_jump_tests.cpp
        features/input/haptics_policy_tests.cpp
        features/input/input_mapper_stick_tests.cpp
        features/input/input_mapper_chord_tests.cpp
        features/input/input_mapper_tests.cpp
        features/input/input_mapper_menu_tests.cpp
        features/input/input_mapper_wheel_tests.cpp
        features/input/input_mapper_locomotion_tests.cpp
        features/input/locomotion_direction_tests.cpp
        features/input/menu_release_latch_tests.cpp
        features/input/offhand_policy_tests.cpp
        features/input/player_controller_data_tests.cpp
        features/input/punch_detector_tests.cpp
        features/input/stick_chord_tests.cpp
        features/input/stick_response_tests.cpp
        features/input/tap_hold_tests.cpp
        features/input/test_input_tests.cpp
        features/input/turn_policy_tests.cpp
        features/input/turn_stick_arbiter_tests.cpp
        features/input/usercmd_injection_tests.cpp
        features/input/usercmd_motion_tests.cpp
        features/input/virtual_gamepad_tests.cpp
        features/input/wheel_hand_tests.cpp
        features/input/wheel_mouse_tests.cpp
        features/input/xr_action_set_tests.cpp
    DEPENDS evr_input)

evr_add_test(evr_bhaptics_tests
    SOURCES
        features/bhaptics/bhaptics_message_tests.cpp
        features/bhaptics/body_haptics_tests.cpp
        features/bhaptics/crystal_tests.cpp
        features/bhaptics/landing_tests.cpp
        features/bhaptics/launch_tests.cpp
        features/bhaptics/pickups_tests.cpp
        features/bhaptics/pickup_wave_tests.cpp
    DEPENDS evr_bhaptics)

evr_add_test(evr_arm_tests
    SOURCES
        features/arm/arm_frames_tests.cpp
        features/arm/arm_skeleton_tests.cpp
        features/arm/arm_solve_tests.cpp
        features/arm/arm_surfaces_tests.cpp
        features/arm/hand_offset_tests.cpp
        features/arm/mod_room_tests.cpp
        features/arm/two_bone_ik_tests.cpp
    DEPENDS evr_arm)

evr_add_test(evr_layer_gate_tests
    SOURCES
        platform/layer_gate/layer_gate_tests.cpp
    DEPENDS evr_layer_gate)

evr_add_test(evr_mp_policy_tests
    SOURCES
        platform/mp_policy/argument_cases_tests.cpp
        platform/mp_policy/mp_policy_tests.cpp
        platform/mp_policy/saved_cvars_tests.cpp
        platform/mp_policy/trip_listeners_tests.cpp
    DEPENDS evr_mp_policy)
# The argument cases the launcher's ArgumentPolicy tests read as well.
target_compile_definitions(evr_mp_policy_tests
    PRIVATE EVR_MP_ARGUMENT_CASES="${CMAKE_CURRENT_SOURCE_DIR}/platform/mp_policy/argument-cases.txt")

evr_add_test(evr_key_injection_tests
    SOURCES
        platform/key_injection/injected_keys_tests.cpp
    DEPENDS evr_key_injection)

# Layer pieces that need only the Vulkan headers (built with the layer, on Windows).
if(TARGET evr_device_features)
    evr_add_test(evr_vkcore_tests
        SOURCES
            vkcore/debug_script_tests.cpp
            vkcore/device_features_tests.cpp
            vkcore/dump_format_tests.cpp
            vkcore/game_settings_line_tests.cpp
            vkcore/job_nodes_tests.cpp
            vkcore/log_cap_tests.cpp
            vkcore/parallel_eyes_settings_tests.cpp
            vkcore/view_frames_tests.cpp
        DEPENDS evr_debug_script evr_device_features evr_dump_format evr_game_settings_line
            evr_parallel_eyes_settings evr_mp_policy)
endif()

# The shader census tool (tools/shader_census) is tested on synthetic SPIR-V, with no SDK needed.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME evr_shader_census_tests
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s tests -t . -v
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/tools/shader_census")
    # The eye pair comparison of the Route S experiments (tools/stereo/eye_diff.py); temporary files go
    # into the build tree.
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tmp")
    add_test(NAME evr_eye_diff_tests
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s tests -t . -v
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/tools/stereo")
    set_tests_properties(evr_eye_diff_tests PROPERTIES
        ENVIRONMENT "TMPDIR=${CMAKE_BINARY_DIR}/tmp;TEMP=${CMAKE_BINARY_DIR}/tmp;TMP=${CMAKE_BINARY_DIR}/tmp")
endif()

evr_add_test(evr_stereo_seq_tests
    SOURCES
        stereo_seq/adaptive_eyes_tests.cpp
        stereo_seq/adaptive_pairs_tests.cpp
        stereo_seq/alternate_eyes_tests.cpp
        stereo_seq/alternate_prev_tests.cpp
        stereo_seq/bin_tiles_tests.cpp
        stereo_seq/centered_matrix_tests.cpp
        stereo_seq/desktop_window_tests.cpp
        stereo_seq/dlss_menu_tests.cpp
        stereo_seq/drain_backoff_tests.cpp
        stereo_seq/eye_pairing_tests.cpp
        stereo_seq/eye_tags_tests.cpp
        stereo_seq/eye_view_plan_tests.cpp
        stereo_seq/keep_prev_tests.cpp
        stereo_seq/moved_flag_tests.cpp
        stereo_seq/ngx_eye_tests.cpp
        stereo_seq/ngx_twin_retry_tests.cpp
        stereo_seq/object_prev_tests.cpp
        stereo_seq/pass_frames_tests.cpp
        stereo_seq/world_gui_tests.cpp
        stereo_seq/png_writer_tests.cpp
        stereo_seq/present_watch_tests.cpp
        stereo_seq/prev_matrices_tests.cpp
        stereo_seq/render_idle_tests.cpp
        stereo_seq/scatter_history_tests.cpp
        stereo_seq/seq_settings_tests.cpp
        stereo_seq/stack_budget_tests.cpp
        stereo_seq/stereo_taa_tests.cpp
    DEPENDS evr_stereo_seq evr_xr_math)

evr_add_test(evr_menu_tests
    SOURCES
        features/menu/drag_cursor_hide_tests.cpp
        features/menu/enter_tick_tests.cpp
        features/menu/map_drag_tests.cpp
        features/menu/map_pan_keys_tests.cpp
        features/menu/menu_kind_tests.cpp
        features/menu/menu_router_map_tests.cpp
        features/menu/menu_router_popup_tests.cpp
        features/menu/menu_router_tests.cpp
        features/menu/model_camera_tests.cpp
        features/menu/panel_follow_tests.cpp
        features/menu/panel_pointer_tests.cpp
        features/menu/panel_reanchor_tests.cpp
        features/menu/wheel_cursor_tests.cpp
    DEPENDS evr_menu evr_roomscale)

evr_add_test(evr_render_size_tests
    SOURCES
        features/render_size/mirror_window_tests.cpp
        features/render_size/render_cap_tests.cpp
        features/render_size/render_size_tests.cpp
    DEPENDS evr_render_size)

evr_add_test(evr_dlss_dll_tests
    SOURCES
        features/dlss_dll/dlss_dll_tests.cpp
    DEPENDS evr_dlss_dll)

evr_add_test(evr_ui_layer_tests
    SOURCES
        ui_layer/additive_wash_tests.cpp
        ui_layer/backdrop_tests.cpp
        ui_layer/gui_target_tests.cpp
        ui_layer/hud_regions_tests.cpp
        ui_layer/layout_tracker_tests.cpp
        ui_layer/motion_target_tests.cpp
        ui_layer/ui_settings_tests.cpp
        ui_layer/weapon_hud_tests.cpp
        ui_layer/wrist_hud_tests.cpp
    DEPENDS evr_ui_layer)

evr_add_test(evr_gpu_timing_tests
    SOURCES
        gpu_timing/cpu_split_tests.cpp
        gpu_timing/submit_census_tests.cpp
        gpu_timing/frame_stats_tests.cpp
        gpu_timing/present_stall_tests.cpp
        gpu_timing/timestamps_tests.cpp
    DEPENDS evr_gpu_timing)
