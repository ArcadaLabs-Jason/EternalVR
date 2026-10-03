#pragma once

// A newer DLSS DLL of the player's own (docs/rig-findings/dlss-dll.md). The game ships nvngx_dlss.dll 2.3.0.0
// in its folder; with ETERNALVR_DLSS_DLL set to a newer one elsewhere, the layer puts that DLL's folder first
// in the search path NGX is initialised with, and ETERNALVR_DLSS_PRESET picks the DLSS render preset (K: the
// transformer model) for the features the game and the layer create. Nothing is copied into the game folder.
//
// Plain logic: the file checks, the version, the preset names and the decision the layer logs. No Windows.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace evr::dlss_dll {

// A file version (VS_FIXEDFILEINFO: major.minor.build.revision).
struct Version {
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint16_t build = 0;
    std::uint16_t revision = 0;
    friend constexpr auto operator<=>(const Version&, const Version&) = default;
};

// From VS_FIXEDFILEINFO's dwFileVersionMS and dwFileVersionLS.
Version versionFromFixed(std::uint32_t ms, std::uint32_t ls);
std::string versionText(Version v);

// The DLL the game ships (its own folder).
inline constexpr Version kGameDllVersion{2, 3, 0, 0};
// The first DLSS with render presets (NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_*).
inline constexpr Version kFirstPresetVersion{3, 1, 0, 0};
// The first DLSS the layer runs DLAA with (NVSDK_NGX_PerfQuality_Value_DLAA, and its render preset hint,
// in NVIDIA's SDK headers). The game's 2.3.0.0 predates the documented DLAA mode: it is not relied on.
inline constexpr Version kFirstDlaaVersion{3, 1, 0, 0};
// NVSDK_NGX_PerfQuality_Value_DLAA: the quality whose optimal render size is the output size.
inline constexpr int kPerfQualityDlaa = 5;

// The file name NGX looks for in each folder of its search path.
inline constexpr std::wstring_view kDllName = L"nvngx_dlss.dll";

// True when the last component of `path` is nvngx_dlss.dll (either separator, any letter case).
bool hasDllName(std::wstring_view path);

// The folder part of `path` without the trailing separator; empty when `path` has none.
std::wstring folderOf(std::wstring_view path);

// True when `a` and `b` name the same file, ignoring letter case and the separator kind.
bool samePath(std::wstring_view a, std::wstring_view b);

// The start of a PE file: an x86-64 DLL.
enum class PeCheck : std::uint8_t { Ok, TooShort, NotMz, NotPe, NotX64, NotDll };
PeCheck checkPeHeaders(const std::uint8_t* bytes, std::size_t size);
std::string_view describe(PeCheck check);

// ETERNALVR_DLSS_PRESET: empty or `default` (0: the DLL's own choice), or a preset letter (`K`, any case):
// A to F, or J to M (the transformer model). nullopt for anything else, including the letters NVIDIA marks
// "do not use" (G, H, I, N, O).
std::optional<std::uint32_t> parsePreset(std::string_view text);
// `default`, or the preset's letter.
std::string presetName(std::uint32_t preset);

// How NGX is made to load the chosen DLL (ETERNALVR_DLSS_ROUTE): its folder first in the search path NGX is
// initialised with (`path`, the default: NVIDIA's documented way), or every load of a file named
// nvngx_dlss.dll in the process sent to the chosen one (`redirect`: for when NGX prefers the exe's folder).
enum class Route : std::uint8_t { FolderFirst, Redirect };
std::optional<Route> parseRoute(std::string_view text);

// The redirect route: a load of `requested` goes to `chosen` instead when it names an nvngx_dlss.dll other
// than the chosen one.
bool shouldRedirect(std::wstring_view requested, std::wstring_view chosen);

// What the layer found about the file ETERNALVR_DLSS_DLL names.
struct FileFacts {
    bool exists = false;
    bool nameOk = false;
    PeCheck pe = PeCheck::TooShort;
    bool hasVersion = false; // a version resource with a file version
    Version version;
    std::string company; // the version resource's CompanyName
};

struct Decision {
    bool use = false;         // put the file's folder first in NGX's search path
    std::string reason;       // why not (use false), or a note (use true, may be empty)
    std::uint32_t preset = 0; // the preset for every DLSS quality (0: the DLL's own)
    bool applyPreset = false; // set the preset hints at feature creation
    std::string presetNote;   // why the preset is not applied, when one was asked for
    bool applyDlaa = false;   // PerfQualityValue set to DLAA (kPerfQualityDlaa) on every write of it
    std::string dlaaNote;     // why DLAA is not applied, when it was asked for
};

// The file is used when it exists, is named nvngx_dlss.dll, is an x86-64 DLL and carries an NVIDIA version
// resource. A preset other than the default is applied only with a used DLL of kFirstPresetVersion or later,
// and DLAA (`dlaa`: ETERNALVR_STEREO_DLSS_QUALITY=dlaa) only with a used DLL of kFirstDlaaVersion or later.
Decision decide(const FileFacts& facts, std::string_view presetText, bool dlaa = false);

// Why DLAA does not run without a newer DLL (ETERNALVR_DLSS_DLL unset): DLSS runs at Quality.
std::string dlaaWithoutNewerDll();

} // namespace evr::dlss_dll
