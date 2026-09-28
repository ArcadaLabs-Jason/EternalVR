# Menus in VR (docs/VR_MENUS.md): the laser pointer's geometry and the router that turns controller input
# into the game's own mouse and key input while a menu is up. No OpenXR or Windows here.
evr_add_module(evr_menu
    SOURCES
        map_drag.cpp
        menu_router.cpp
        panel_pointer.cpp
        wheel_cursor.cpp
    DEPENDS
        evr_common
        evr_game_eternal)
