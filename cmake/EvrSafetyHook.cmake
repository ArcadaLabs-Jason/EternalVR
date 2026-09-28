# safetyhook (mid-function hooks on the game's code), from the project's amalgamated release archive,
# which bundles the Zydis disassembler (with Zycore) as one C file. Pinned to an exact release and
# checksum. Only the layer uses it.

include(FetchContent)

FetchContent_Declare(safetyhook
    URL https://github.com/cursey/safetyhook/releases/download/v0.7.0/safetyhook-amalgamated-zydis.zip
    URL_HASH SHA256=505D4C07EC1C5B94A17F3906CA86AFBE1264E738D8BECAA244866694C6200C2C
    SOURCE_SUBDIR do-not-configure
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(safetyhook)

add_library(evr_safetyhook STATIC
    "${safetyhook_SOURCE_DIR}/safetyhook.cpp"
    "${safetyhook_SOURCE_DIR}/Zydis.c")
# SYSTEM keeps the library's own warnings out of our warnings-as-errors build.
target_include_directories(evr_safetyhook SYSTEM PUBLIC "${safetyhook_SOURCE_DIR}")
target_compile_features(evr_safetyhook PUBLIC cxx_std_23)
target_compile_definitions(evr_safetyhook PUBLIC ZYDIS_STATIC_BUILD ZYCORE_STATIC_BUILD)