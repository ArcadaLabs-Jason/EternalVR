#include "vkcore/seq_cvar_log.hpp"

#include "stereo_seq/seq_settings.hpp"
#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/log.hpp"
#include "vkcore/taa_hooks.hpp"

#include <windows.h>

#include <cstddef>
#include <string>

namespace evr::vkcore {

void logSeqCommandLineCvars() {
    const wchar_t* wide = GetCommandLineW();
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string line(bytes > 0 ? static_cast<std::size_t>(bytes) : 0, '\0');
    if (bytes > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, line.data(), bytes, nullptr, nullptr);
        line.resize(static_cast<std::size_t>(bytes - 1));
    }
    std::string summary;
    int differing = 0;
    for (const auto& c : stereo_seq::cvarsOnCommandLine(line, stereo_seq::sequentialCvars())) {
        summary += " " + c.name + " " + (c.actual ? *c.actual : std::string("(not set)"));
        if (!c.actual || *c.actual != c.expected) {
            ++differing;
        }
    }
    EVR_LOG("seq: command line cvars:%s; %s", summary.c_str(),
            taaRequested()   ? "per-eye temporal history owns the TAA cvars"
            : differing == 0 ? "the v1 set"
                             : "NOT the v1 set (temporal effects may ghost between the eyes)");
    if (taaRequested()) {
        std::string taa;
        for (const auto* set :
             {&stereo_seq::stereoTaaCommandLineCvars(), &stereo_seq::stereoTaaForcedCvars()}) {
            for (const auto& c : stereo_seq::cvarsOnCommandLine(line, *set)) {
                taa += " " + c.name + " " + (c.actual ? *c.actual : std::string("(not set)"));
            }
        }
        EVR_LOG("seq-taa: per-eye TAA requested (ETERNALVR_STEREO_TAA=1); command line:%s", taa.c_str());
    }
}

} // namespace evr::vkcore
