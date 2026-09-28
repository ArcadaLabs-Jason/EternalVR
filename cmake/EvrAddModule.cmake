# Helpers that keep per-module CMakeLists files short and uniform.

# evr_add_module(<target> SOURCES <files...> [DEPENDS <targets...>])
#
# Declares a static library for one portable module. Headers are included relative to src/, so a
# consumer writes `#include "xr_math/projection.hpp"` regardless of where it lives.
function(evr_add_module target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPENDS")

    add_library(${target} STATIC ${arg_SOURCES})
    target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_link_libraries(${target} PRIVATE evr_warnings)
    if(arg_DEPENDS)
        target_link_libraries(${target} PUBLIC ${arg_DEPENDS})
    endif()
endfunction()

# evr_add_test(<target> SOURCES <files...> DEPENDS <targets...>)
#
# Declares one doctest executable and registers it with CTest.
function(evr_add_test target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPENDS")

    add_executable(${target} ${arg_SOURCES})
    # evr_warnings comes first so that the test-only suppressions carried by evr_test_main appear
    # after it on the command line and take effect.
    target_link_libraries(${target} PRIVATE evr_warnings evr_test_main ${arg_DEPENDS})
    add_test(NAME ${target} COMMAND ${target})
endfunction()
