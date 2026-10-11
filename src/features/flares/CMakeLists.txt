# Lens flares per eye: the flare models one render updated, kept for that render's eyes (flare_book.hpp),
# and what view 1 draws and tests of them under Parallel Eye Rendering (flare_draws.hpp, flare_pending.hpp).
# No engine, Windows or Vulkan here.
evr_add_module(evr_flares
    SOURCES
        flare_book.cpp
        flare_draws.cpp
        flare_pending.cpp)
