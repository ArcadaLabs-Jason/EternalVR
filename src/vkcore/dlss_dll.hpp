#pragma once

// A newer DLSS DLL of the player's own, from outside the game folder (docs/rig-findings/dlss-dll.md).
//
// The game links NGX statically: its renderer calls the exported NVSDK_NGX_VULKAN_Init (RVA 0x2268E40 on
// build 25216728) right after vkCreateDevice with no NVSDK_NGX_FeatureCommonInfo, so the driver's NGX
// finds nvngx_dlss.dll (2.3.0.0) in the exe's folder. With ETERNALVR_DLSS_DLL naming a valid NVIDIA
// nvngx_dlss.dll elsewhere, the Init export is detoured to pass a FeatureCommonInfo whose PathListInfo
// holds that DLL's folder (and an NGX log callback into the layer log). If NGX refuses it, the game's own
// Init is called again unchanged. ETERNALVR_DLSS_PRESET (a letter such as K) is set as the render preset
// hint of every DLSS quality on the parameter block of each DLSS feature create (the game's and eye R's
// twin, taa_ngx.cpp, both go through the detoured NVSDK_NGX_VULKAN_CreateFeature export).
//
// Unset (the default): nothing is hooked and NGX loads the game's DLL as before.

namespace evr::vkcore {

// Reads the settings and, when a valid DLL is named, detours the Init and CreateFeature exports. Once per
// process; call it for the game's device from vkCreateDevice, before the call returns to the game.
void installDlssDll();

} // namespace evr::vkcore
