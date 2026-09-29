# A newer DLSS DLL of the player's own (docs/rig-findings/dlss-dll.md): the file checks, the version, the preset
# names and the decision the layer logs. No Windows here.
evr_add_module(evr_dlss_dll
    SOURCES
        dlss_dll.cpp)
