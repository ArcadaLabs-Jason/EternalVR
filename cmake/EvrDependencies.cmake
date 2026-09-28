# Third-party dependencies, each pinned to an exact release archive and checksum.

include(FetchContent)

# doctest is a single header. Its own CMake project is not used: pointing SOURCE_SUBDIR at a directory
# that does not exist makes FetchContent download and verify the archive without configuring it, and
# we expose the header through our own interface target instead.
FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
    URL_HASH SHA256=73381c7aa4dee704bd935609668cf41880ea7f19fa0504a200e13b74999c2d70
    SOURCE_SUBDIR do-not-configure
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(doctest)

add_library(evr_doctest INTERFACE)
# SYSTEM keeps doctest's own warnings out of our warnings-as-errors build.
target_include_directories(evr_doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")
