# Warning policy shared by every EternalVR target. Warnings are errors everywhere so that a warning
# introduced on one platform cannot quietly accumulate until someone builds on another.

add_library(evr_warnings INTERFACE)

if(MSVC)
    target_compile_options(evr_warnings INTERFACE
        /W4
        /WX
        /permissive-
        # Third-party headers included as SYSTEM are compiled at warning level 0.
        /external:W0)
else()
    target_compile_options(evr_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Werror)
endif()
