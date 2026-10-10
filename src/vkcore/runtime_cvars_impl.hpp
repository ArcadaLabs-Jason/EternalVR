#pragma once

// Internal state of the cvars the layer holds (runtime_cvars.hpp), shared by runtime_cvars.cpp (the held
// list, the engine's setter, apply() and the Route S sets) and runtime_cvars_pe.cpp (Parallel Eye
// Rendering's set, runtime_cvars_pe.hpp).

#include "stereo_seq/seq_settings.hpp"

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace evr::vkcore::runtime_cvars {

inline constexpr const char* kTag = "cvars";

// One cvar the layer holds.
struct Held {
    std::string name;
    std::string value;     // "?": only logged
    bool stereo = false;   // part of the Route S set (not ETERNALVR_DEBUG_CVARS)
    bool temporal = false; // a TAA cvar: left to the per-eye module once it is active
    bool saver = false;    // from ETERNALVR_CPU_SAVER
    bool scatter = false;  // r_lightScatteringTAA: follows the scattering history (historyFilterHold)
    bool ssdo = false;     // r_SSDOTemporalAA: follows SSDO's history (historyFilterHold)
    bool cap = false;      // value "<=N": lowered to N while above it, never raised (capValue)
    float capValue = 0.0f;
    bool exact = false; // a float cvar held at exactValue, compared as a float (ETERNALVR_SHARPENING)
    float exactValue = 0.0f;
    bool written = false; // the first write is always logged
    std::byte* object = nullptr;
    bool logged = false;
    bool windowSize = false; // r_windowWidth / r_windowHeight of the window set: left alone with the render
                             // size off (windowSizeLeft)
    bool left = false;       // ... and that was logged
    bool placed = false;     // ... held at the window placed for a device without present scaling instead
    bool parallel = false;   // Parallel Eye Rendering's own set: an ETERNALVR_DEBUG_CVARS entry replaces it
    bool menuSsdo = false;   // r_SSDO: follows the game's Directional Occlusion setting (followSsdoSetting)
    bool viewDlss = false;   // Parallel Eye Rendering's DLSS set: held as viewDlssHolds says (view_dlss.hpp)
};

// The lock apply() and the setters take, and under it whether the first apply() has run: the held list is
// built then and stays as it is.
std::mutex& heldMutex();
bool applied();

// An environment variable as ASCII (cvar names and values are ASCII); empty when it is not set.
std::string narrowEnv(const wchar_t* name);

// One of the comfort set (stereo_seq::stereoComfortCvars); a float one is compared as a float.
Held comfortHeld(const stereo_seq::CvarExpectation& c);

// The window and present set (stereo_seq::stereoWindowCvars), as the command line and ETERNALVR_WINDOW size
// it, added to `held`; r_windowWidth / r_windowHeight follow the render size (windowSizeLeft). `parallel`:
// as part of Parallel Eye Rendering's set.
void addWindowSet(std::vector<Held>& held, bool parallel);

// runtime_cvars_pe.cpp: Parallel Eye Rendering's set added to `held` once setParallelEyes asked for it, else
// nothing. From the first apply, under heldMutex().
void addParallelEyeSet(std::vector<Held>& held);

} // namespace evr::vkcore::runtime_cvars
