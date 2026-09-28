# OpenXR headers and the Khronos loader DLL for the layer, from the official OpenXR-SDK release
# package (a NuGet archive, which is a zip). Pinned to an exact release and checksum.
#
# The layer loads openxr_loader.dll explicitly from its own folder at run time (the Vulkan loader
# loads our DLL by full path, so the game's folder would otherwise be searched instead), so only the
# headers are needed at build time; the DLL is copied next to the layer.

set(EVR_OPENXR_VERSION "1.1.63")
set(EVR_OPENXR_SHA256 "4E5A50A8807EF66F25180FF224E7D8150B594AA8EE4B07590F9ADE55A8E98703")
set(evr_openxr_root "${CMAKE_BINARY_DIR}/_deps/openxr-loader-${EVR_OPENXR_VERSION}")
set(evr_openxr_archive "${CMAKE_BINARY_DIR}/_deps/OpenXR.Loader.${EVR_OPENXR_VERSION}.zip")

if(NOT EXISTS "${evr_openxr_root}/include/openxr/openxr.h")
    file(DOWNLOAD
        "https://github.com/KhronosGroup/OpenXR-SDK/releases/download/release-${EVR_OPENXR_VERSION}/OpenXR.Loader.${EVR_OPENXR_VERSION}.nupkg"
        "${evr_openxr_archive}"
        EXPECTED_HASH SHA256=${EVR_OPENXR_SHA256}
        TLS_VERIFY ON)
    file(ARCHIVE_EXTRACT INPUT "${evr_openxr_archive}" DESTINATION "${evr_openxr_root}"
        PATTERNS "include/openxr/*" "native/x64/release/bin/openxr_loader.dll")
endif()

add_library(evr_openxr_headers INTERFACE)
target_include_directories(evr_openxr_headers SYSTEM INTERFACE "${evr_openxr_root}/include")
set(EVR_OPENXR_LOADER_DLL "${evr_openxr_root}/native/x64/release/bin/openxr_loader.dll")
