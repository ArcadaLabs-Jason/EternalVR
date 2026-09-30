# Comfort options: the vignette for artificial motion and how glory kills are shown. No OpenXR or Windows here.
evr_add_module(evr_comfort
    SOURCES
        glory_kill.cpp
        vignette.cpp
    DEPENDS
        evr_common)
