// Head-tracked mono's held set (runtime_cvars.hpp, applyMono): the launcher's Mono, one image for both eyes.

#include "stereo_seq/seq_settings.hpp"
#include "vkcore/log.hpp"
#include "vkcore/runtime_cvars_impl.hpp"
#include "vkcore/virtual_client.hpp"

#include <string>
#include <vector>

namespace evr::vkcore::runtime_cvars {

// - With the render size on: the window set of stereo_seq::monoWindowCvars (r_fullscreen 0 and the window's
//   size, not r_swapInterval), so a load path that applies the player's video mode cannot take the one image
//   to the display's size or to full screen.
// - Head-tracked (not the cinema screen): the comfort set but the underwater warp (monoVrHeldCvars in
//   stereo_seq), as under Route S: on the rig most of it never took from mono's command line, and photo mode
//   shows the same in mono. view_skipShakes is saved in the profile: the profile hooks keep the player's own
//   value there (profile_hooks.hpp). Each map load sets g_allowPhotoMode and nothing saves it, so the
//   player's own Photo mode setting is left alone.
void addMonoSet(std::vector<Held>& held, bool headTracked) {
    std::string what;
    if (virtual_client::wanted()) {
        addWindowSet(held, true);
        what = "the window's mode and size (the render size)";
    }
    if (headTracked) {
        const auto comfort = stereo_seq::monoVrHeldCvars();
        for (const auto& c : comfort) {
            held.push_back(comfortHeld(c));
        }
        what += (what.empty() ? "" : " and ") + std::string("the comfort set but the underwater warp (") +
                std::to_string(comfort.size()) + " cvars)";
    }
    what = what.empty() ? std::string("nothing") : "only " + what;
    EVR_LOG("%s: %s mono: %s held", kTag, headTracked ? "head-tracked" : "cinema", what.c_str());
}

} // namespace evr::vkcore::runtime_cvars
