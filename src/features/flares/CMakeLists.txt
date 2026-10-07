# Route S lens flares per eye: the flare models one render updated, kept for that render's eye
# (flare_book.hpp). No engine, Windows or Vulkan here.
evr_add_module(evr_flares
    SOURCES
        flare_book.cpp)
