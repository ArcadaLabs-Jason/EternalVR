# A newer DLSS DLL from the mod's side (Improved DLSS)

Static analysis of the retail `DOOMEternalx64vk.exe`, Steam build 25216728 (the exe of `engine-facts.md`), NVIDIA's
DLSS SDK (github.com/NVIDIA/DLSS, release v310.9.1: headers, `doc/DLSS_Programming_Guide_Release.pdf` revision
310.6.0, `LICENSE.txt`) and the sources of two community tools that swap the DLL (DLSSTweaks, OptiScaler). The game
was not run for this document; section 7 is the rig check. All addresses are RVAs in this build.

**[static-verified]**: read in this build's code. **[documented]**: NVIDIA's SDK or guide says so. **[inferred]**:
what those imply; section 7 checks it live.

## 1. Answer

The game ships `nvngx_dlss.dll` 2.3.0.0 in its folder: the convolutional model, no render presets. A newer DLL
(DLSS 310.x: the transformer model, presets J to M) can run instead without touching the game folder. The player
picks the file in the launcher (Advanced tab, DLSS group); the launcher passes `ETERNALVR_DLSS_DLL` and
`ETERNALVR_DLSS_PRESET`, and the layer:

1. checks the file (named `nvngx_dlss.dll`, an x86-64 DLL, a version resource naming NVIDIA) and logs its version,
2. detours the game's exported `NVSDK_NGX_VULKAN_Init` to pass an `NVSDK_NGX_FeatureCommonInfo` whose
   `PathListInfo` holds the DLL's folder (NVIDIA's documented way to load feature DLLs from another folder), plus
   an NGX log callback into the layer log,
3. detours the exported `NVSDK_NGX_VULKAN_CreateFeature` to set the six `DLSS.Hint.Render.Preset.*` parameters on
   every DLSS feature create: the game's feature and eye R's twin (`taa_ngx.cpp` creates the twin through the same
   export),
4. logs which `nvngx_dlss.dll` the process actually loaded, after Init and after the first two feature creates.

Off (the default: no file chosen, or DLSS not chosen) nothing is hooked. Any failure (a file that fails a check,
a missing export, a hook that cannot be placed, NGX refusing the init info) is logged and leaves the game's own DLL
in use. Code: `src/features/dlss_dll/` (checks, versions, presets, the decision; tested in
`tests/features/dlss_dll/`), `src/vkcore/dlss_dll.cpp` (the hooks), launcher `Settings/DlssDll.cs`.

## 2. How the game initialises NGX [static-verified]

The exe links NGX statically (the SDK's static library, source path `.../NGX/core/rel_1_6/source/api`) and exports
its entry points by name, so no signature scan is needed:

| Export | RVA | Role |
|---|---|---|
| `NVSDK_NGX_VULKAN_Init` | 0x2268E40 | app-facing Init: `(appId, dataPath, VkInstance, VkPhysicalDevice, VkDevice, const FeatureCommonInfo*, version)` |
| `NVSDK_NGX_VULKAN_CreateFeature` | 0x2268B30 | calls the driver's CreateFeature through a pointer the loader filled |
| `NVSDK_NGX_Parameter_SetUI` | 0x22680E0 | calls the parameter object's vtable slot +0x20 |
| `NVSDK_NGX_VULKAN_GetCapabilityParameters` | 0x2268D30 | the game's parameter block, kept at 0x66E8B28 |

- **The call.** The renderer's device setup (function at 0x1CC38FA) calls Init at 0x1CC4B77, right after
  `vkCreateDevice` and loading the device functions (0x1CC64C0): `appId 0x6039927`, `dataPath L"base/generated"`,
  the instance, physical device and device from 0x667E240 / 0x667E250 / 0x667E248, **FeatureCommonInfo = nullptr**
  (r14, which the function uses as its zero register: it is zeroed at 0x1CC4696 and used as 0 on both sides of the
  call) and version 0x14. It then reads `SuperSampling.NeedsUpdatedDriver`, `.MinDriverVersionMajor/Minor` and
  `SuperSampling.Available` from the capability parameters and logs `Nvidia NGX: DLSS initialized.` Init runs
  only when the flags at 0x66E8AA8 and 0x66E8AA9 are set (DLSS support).
- **The loader** (0x2268410) loads the driver's `_nvngx.dll` (found from the physical device through the driver
  store, 0x22682C0) and resolves `NVSDK_NGX_VULKAN_Init`, `_Init_Ext`, `_Init_ProjectID`, `_Shutdown(1)`,
  `_GetParameters`, `_GetScratchBufferSize`, `_CreateFeature(1)`, `_EvaluateFeature`, `_ReleaseFeature`,
  `_AllocateParameters`, `_GetCapabilityParameters`, `_DestroyParameters` into 0x6CC2030..0x6CC2098.
- **FeatureCommonInfo checks** (0x2268466), with version >= 0x14: `LoggingInfo.DisableOtherLoggingSinks` (+0x24)
  set needs a callback (+0x18), and the level (+0x20) must be 0 to 2, else 0xBAD00005. The layout is the SDK's:
  `PathListInfo {const wchar_t* const* Path; unsigned Length}` at 0, `InternalData` at 0x10, `LoggingInfo
  {callback, level, bool}` at 0x18; 40 bytes.
- **Which driver entry gets it** (0x2268E91 onwards): with `_Init_Ext` present (current drivers) the static Init
  calls `_Init_Ext(appId, dataPath, instance, physicalDevice, device, version, featureInfo)` (note: version before
  the info, the driver's order). Without it, an info with a non-empty path list returns 0xBAD0000C (out of date)
  and the plain `_Init` is called only without paths.

Where the driver looks for the DLL [documented]: the exe's folder by default (guide 4.2), plus
`PathListInfo`, "a list of paths where feature DLLs can be located in addition to the default path (the
application directory)"; the header's comment: "List of all paths in descending order of search sequence to
locate a feature dll in, other than the default path - application folder". The list is searched in order and
the first loadable DLL wins (guide 9.7). **Whether the exe's folder comes before or after the list is not
documented.** NVIDIA's own Streamline passes its plugin folder first ("Always check first where our plugins
are"), and OptiScaler puts its own folders first and appends the exe's folder itself, which suggests the list is
searched first; section 7 settles it. NGX's over-the-air copies (`C:\ProgramData\NVIDIA\NGX\models\dlss\...`) and
the NVIDIA App override apply only to titles NVIDIA lists, and reportedly not to games that ship a 2.x DLL.

The DLL must carry NVIDIA's signature, which NGX checks on load wherever the file is (guide 4.2.2); a genuine DLL
from NVIDIA's release passes from any folder, and the signature check is never bypassed here.

## 3. The mechanism, and why

| Option | Verdict |
|---|---|
| Copy the DLL into the game folder | Ruled out: nothing is installed into the game folder |
| **Init detour adding the DLL's folder to `PathListInfo`** | **Chosen (route `path`, the default).** NVIDIA's documented alternate-folder mechanism; confined to NGX; one detour on an exported function, found by name like `taa_ngx.cpp`'s; fails safe (NGX refusing the info re-runs the game's own Init unchanged) |
| Redirect `LoadLibraryExW` for `nvngx_dlss.dll` (DLSSTweaks, OptiScaler) | Kept as a switch (`ETERNALVR_DLSS_ROUTE=redirect`) in case the rig shows the driver prefers the exe's folder. A detour on KernelBase's `LoadLibraryExW` that sends any load of a file named `nvngx_dlss.dll` other than the chosen one to the chosen one, flags unchanged; everything else passes through. Works whatever the search order, but touches every library load in the process |
| Pre-loading the DLL | Ruled out: a load by full path does not reuse a module of the same name from another folder |
| Patching the exe's NGX code or the NGX registry keys | Ruled out: the registry is not ours, and the signature override would bypass NVIDIA's check |

The detours go in from `vkCreateDevice` for the game's device (`layer_entry.cpp`), before the call returns to the
game, which calls Init right after it. The OptiScaler note that "Doom Eternal is sending junk data" in
`PathListInfo` concerns the driver-side entry it replaces (the static library's `_Init_Ext` passes the version
before the info); the layer detours the app-facing function, whose arguments are read above, and builds its own
info rather than editing the game's (the game passes none).

## 4. Render presets [documented]

`DLSS.Hint.Render.Preset.{DLAA,Quality,Balanced,Performance,UltraPerformance,UltraQuality}` (SDK 3.1 and later),
read when a feature is created; the preset "chosen for the init-time input size will persist across the lifetime
of the feature" (guide 3.2.2). Values (SDK 310.9): Default 0, A to D removed, E and F deprecated (the convolutional
model), G, H, I, N, O "do not use", **J 10, K 11, L 12, M 13: the transformer model**. With 310.5 and later the
DLL's own default is already the transformer model (K for DLAA, Quality and Balanced, M for Performance, L for
Ultra Performance), so `default` is a good choice; `K` forces K at every quality.

The layer sets all six keys with `NVSDK_NGX_Parameter_SetUI` on the parameter block passed to CreateFeature. The
game uses one block for its create and evaluate calls, and eye R's twin is created from the same block through
the same (detoured) export, so both eyes' features get the same preset. The hint is set only with a chosen DLL of
3.1 or later (a DLL without presets ignores the keys anyway). It is not gated on the multiplayer guard: it changes
no game state, only how the player's chosen DLL runs, and both features must get it from the game's first create.

## 5. Where the player gets the DLL, and the licence

NVIDIA publishes the DLL with the SDK: `lib/Windows_x86_64/rel/nvngx_dlss.dll` in github.com/NVIDIA/DLSS (the `rel`
one; the `dev` one carries a watermark and must not be distributed). Release v310.9.1 (2026-09-08), 58,956,912
bytes, SHA-256 `3975567b8943c53acce397f2b72380092f84f162d00b0d2c7d08a1025c563983`, Authenticode valid (NVIDIA
Corporation), file version 310.9.1.0, CompanyName `NVIDIA`:
`https://raw.githubusercontent.com/NVIDIA/DLSS/v310.9.1/lib/Windows_x86_64/rel/nvngx_dlss.dll` (not Git LFS).

The licence (`LICENSE.txt`, "NVIDIA RTX SDKs LICENSE", 2024-03-14) allows distributing SDK files "as incorporated
in object code format into a software application" (1(c)) that has "material additional functionality" (2(a)),
under terms "at least as protective" (2(c)); it forbids distributing the SDK "as a stand-alone product" (4(b)),
bypassing its authentication (4(d)) and making it subject to an open source licence or "redistributable at no
charge" (4(e)). The application that integrates the SDK is the game, not the mod; shipping the DLL in the mod's
open source release is at best unclear and plausibly conflicts with 2(c), 4(b) and 4(e). **Nothing NVIDIA's is in
the repo or the release: the player downloads the DLL and chooses it.** An opt-in download from NVIDIA's repository on
the player's machine is what the launcher offers (Jason's go, 2026-09-29): "Download from NVIDIA..." on the
Advanced tab names the source and NVIDIA's license, links it, and downloads only once the player accepts it; the
file (the newest line of `launcher/data/dlss-downloads.txt`, pinned by size and SHA-256) is kept in the data
folder's `dlss\<version>\` and chosen as "From a file". EternalVR itself never ships or hosts it. Nothing third-party was added, so
`THIRD_PARTY_NOTICES.md` is unchanged.

## 6. Settings and logs

| Variable | Default | Meaning |
|---|---|---|
| `ETERNALVR_DLSS_DLL` | unset | full path of a newer `nvngx_dlss.dll`; unset: the game's own, nothing hooked |
| `ETERNALVR_DLSS_PRESET` | `default` | `default` or a preset letter (A to F, J to M); applied only with a used DLL of 3.1 or later |
| `ETERNALVR_DLSS_ROUTE` | `path` | `path`: the folder first in NGX's search path; `redirect`: every `nvngx_dlss.dll` load sent to the chosen file |
| `ETERNALVR_DLSS_NGX_LOG` | on | `verbose`: NGX's verbose log level (at most 400 NGX lines are logged) |

The launcher sets the first two only in stereo with a DLSS anti-aliasing choice and "DLSS version: From a file"
(`launcher.ini`: `dlss_dll = game|file`, `dlss_dll_path`, `dlss_preset`; the path is this machine's, kept by Reset
and left out of profiles).

Log lines (prefix `dlss:`; NGX's own lines are `ngx[level/feature]: ...`):

- `dlss: <path> is DLSS 310.9.1.0 (NVIDIA)` and `dlss: render preset K for every DLSS quality`, or
  `dlss: not using <path>: <reason>; the game's own nvngx_dlss.dll is used`
- `dlss: NGX Init (RVA 0x2268E40) and CreateFeature (RVA 0x2268B30) hooked; route: ...`
- `dlss: NVSDK_NGX_VULKAN_Init with 1 folder(s) in the search path, first <folder> (game info none, API 0x14): 0x00000001`
- `dlss: using <path> (version 310.9.1.0) [after Init | first DLSS feature | second DLSS feature]`, or
  `dlss: NGX loaded <game folder>\nvngx_dlss.dll (version 2.3.0.0), not <path>: the newer DLL is NOT in use [...]`
- `dlss: DLSS feature create #N: result 0x00000001, feature <id>, preset K`

## 7. Rig check (not run yet)

Build: this branch; the DLL above in `<workspace>\tmp-vr\dlss\310.9.1\`. Stereo, any map, Anti-aliasing
DLSS Quality (`ETERNALVR_STEREO_DLSS=1`, `ETERNALVR_STEREO_DLSS_QUALITY=quality`, `+r_antialiasing 2`).

1. **Off.** No `ETERNALVR_DLSS_DLL`: no `dlss:` line at all; DLSS per eye as before.
2. **Route `path`, preset K.** `ETERNALVR_DLSS_DLL=<workspace>\tmp-vr\dlss\310.9.1\nvngx_dlss.dll`,
   `ETERNALVR_DLSS_PRESET=K`. Pass: `dlss: using ...tmp-vr\dlss\310.9.1\nvngx_dlss.dll (version 310.9.1.0)` after
   the first feature, two `DLSS feature create` lines with result 0x00000001 and `preset K` (the game's feature,
   then eye R's, right before `seq-taa: eye R's DLSS feature for the game's feature N: created (M, ...)` with M the
   second create's id), no `NOT in use` line, both eyes sharp in the headset and no per-eye fallback to TAA.
3. **If step 2 says `NOT in use`** (the driver prefers the exe's folder): repeat with `ETERNALVR_DLSS_ROUTE=redirect`.
   Pass: `dlss: redirecting the load of <game folder>\nvngx_dlss.dll ... to <path>`, then the step 2 lines.
4. **Fail safe.** `ETERNALVR_DLSS_DLL` naming a missing file, then a renamed copy (`nvngx_dlss_310.dll`): one
   `not using` line each, the game's DLL, DLSS as in step 1.
5. **Preset default.** `ETERNALVR_DLSS_PRESET=default`: `preset the DLL's own` on both creates.
6. **Quality change and map change** (feature recreate): each recreate logs two creates with the preset.

Record the `ngx[...]` lines around the DLL load (with `ETERNALVR_DLSS_NGX_LOG=verbose` if the default level says
nothing about the path or preset), the per-eye GPU times against 2.3 (`ETERNALVR_GPU_TIMING=1`), and what the
search order turned out to be, in this section.
