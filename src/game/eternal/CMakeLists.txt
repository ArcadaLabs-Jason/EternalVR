# Game-design data for DOOM Eternal: the action list, the built-in control maps and the player's
# dimensions. Plain data with no dependencies; engine internals live in engine/eternal instead.

# The controller data files (data/input/controllers) are the source of the default OpenXR bindings
# and control maps. They are built in here, so the tests check the files themselves.
set(EVR_CONTROLLER_DATA_DIR "${PROJECT_SOURCE_DIR}/data/input/controllers")
foreach(controller oculus_touch valve_index hp_reverb_g2 windows_mixed_reality htc_vive_cosmos htc_vive_wand
                   pico4 steam_frame)
    set(data_file "${EVR_CONTROLLER_DATA_DIR}/${controller}.toml")
    file(READ "${data_file}" EVR_CONTROLLER_DATA_${controller})
    # MSVC limits one string literal to about 16 KB.
    string(LENGTH "${EVR_CONTROLLER_DATA_${controller}}" data_length)
    if(data_length GREATER 16000)
        message(FATAL_ERROR "${data_file} is too long to build in as one string literal")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${data_file}")
endforeach()
configure_file(controller_data.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/controller_data.cpp" @ONLY)

# The viewmodel offset table (data/weapons), built in the same way.
set(EVR_WEAPON_OFFSET_FILE "${PROJECT_SOURCE_DIR}/data/weapons/viewmodel_offsets.toml")
file(READ "${EVR_WEAPON_OFFSET_FILE}" EVR_WEAPON_OFFSET_DATA)
string(LENGTH "${EVR_WEAPON_OFFSET_DATA}" data_length)
if(data_length GREATER 16000)
    message(FATAL_ERROR "${EVR_WEAPON_OFFSET_FILE} is too long to build in as one string literal")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${EVR_WEAPON_OFFSET_FILE}")
configure_file(weapon_offsets_data.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/weapon_offsets_data.cpp" @ONLY)

evr_add_module(evr_game_eternal
    SOURCES
        game_action.cpp
        quest_touch_bindings.cpp
        usercmd_buttons.cpp
        weapon_offsets.cpp
        controller_data.hpp
        "${CMAKE_CURRENT_BINARY_DIR}/controller_data.cpp"
        "${CMAKE_CURRENT_BINARY_DIR}/weapon_offsets_data.cpp")
