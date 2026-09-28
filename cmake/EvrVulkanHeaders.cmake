# Vulkan headers for the layer. The layer only needs the headers (it is loaded by the Vulkan loader
# and calls through dispatch tables), so a Vulkan SDK is optional: when none is installed (CI), the
# Khronos Vulkan-Headers release matching the rig's SDK is downloaded, pinned to a checksum.

find_package(Vulkan QUIET)

if(NOT TARGET Vulkan::Headers)
    set(EVR_VULKAN_HEADERS_VERSION "1.4.357")
    set(EVR_VULKAN_HEADERS_SHA256 "7dc0dbcf1d49dd3d7da3761c251c6097dfbaac475321a4a8a99269d3d5abecdc")
    set(evr_vk_root "${CMAKE_BINARY_DIR}/_deps/vulkan-headers-${EVR_VULKAN_HEADERS_VERSION}")
    set(evr_vk_archive "${CMAKE_BINARY_DIR}/_deps/Vulkan-Headers-${EVR_VULKAN_HEADERS_VERSION}.tar.gz")

    if(NOT EXISTS "${evr_vk_root}/Vulkan-Headers-${EVR_VULKAN_HEADERS_VERSION}/include/vulkan/vulkan.h")
        file(DOWNLOAD
            "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v${EVR_VULKAN_HEADERS_VERSION}.tar.gz"
            "${evr_vk_archive}"
            EXPECTED_HASH SHA256=${EVR_VULKAN_HEADERS_SHA256}
            TLS_VERIFY ON)
        file(ARCHIVE_EXTRACT INPUT "${evr_vk_archive}" DESTINATION "${evr_vk_root}"
            PATTERNS "Vulkan-Headers-${EVR_VULKAN_HEADERS_VERSION}/include/*")
    endif()

    add_library(Vulkan::Headers INTERFACE IMPORTED)
    set_target_properties(Vulkan::Headers PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
        "${evr_vk_root}/Vulkan-Headers-${EVR_VULKAN_HEADERS_VERSION}/include")
    message(STATUS "Vulkan SDK not found; using Vulkan-Headers ${EVR_VULKAN_HEADERS_VERSION}")
endif()
