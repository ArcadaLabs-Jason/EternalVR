cmake_minimum_required(VERSION 3.24)

project(EternalVR
    VERSION 0.1.38
    DESCRIPTION "VR mod for DOOM Eternal"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The C++ runtime is linked into each binary: the layer then loads on a PC without (or with an old) Visual
# C++ Redistributable, where the game would otherwise start flat with nothing in the log, and an older
# msvcp140.dll than the one built against is a known std::mutex crash.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(EVR_BUILD_TESTS "Build the portable unit tests" ON)

# The layer is a Windows DLL loaded by the Vulkan loader inside the game process. It cannot be built
# or used anywhere else, so it defaults to off outside Windows.
if(WIN32)
    set(evr_layer_default ON)
else()
    set(evr_layer_default OFF)
endif()
option(EVR_BUILD_LAYER "Build the Vulkan layer DLL (Windows only)" ${evr_layer_default})

# The layer's hook library bundles a C disassembler.
if(EVR_BUILD_LAYER)
    enable_language(C)
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(EvrWarnings)
include(EvrAddModule)

add_subdirectory(src)

# Stand-in for the game in the rig script tests (tools/rig/tests).
if(WIN32)
    add_subdirectory(tools/rig/testapp)
endif()

if(EVR_BUILD_TESTS)
    enable_testing()
    include(EvrDependencies)
    add_subdirectory(tests)
endif()
