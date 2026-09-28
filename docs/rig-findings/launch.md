# Launch behaviour and Vulkan creation parameters

Session 1, 2026-09-25. Rig: Ryzen 9 9950X3D, RTX 4080 (driver 616.92) with the integrated AMD GPU
enabled, Windows 11 build 26200, HAGS as found. Game: Steam build 25216728 (Rev 3.2, exe PE timestamp
2026-08-11), `DOOMEternalx64vk.exe`, launched by `tools/rig/run.ps1` (branch `rig-scripts`) with Steam
running and `SteamAppId=782330`, windowed on the virtual display.

## Direct launch (RIG_BRINGUP section 2 step 3)

- The retail exe started directly stays running and opens its window. No hand-off to Steam was seen in
  three runs.
- `+cvar` arguments on the command line reach the game. The console log echoes the command line, and
  `+r_fullscreen 0`, `+r_windowWidth` and `+r_windowHeight` took effect.
- `+s_volume 0` does not mute the game: the saved volume setting applies after the command line. Game
  sound is muted through the Windows audio session instead (T-098 fallback becomes the default).
- A windowed game takes keyboard and mouse focus when its window appears, even on the virtual display.
  `run.ps1` must return focus to the previous foreground window.
- The game writes `qconsole.log` and `structured.log` into `Saved Games\id Software\DOOMEternal\base`
  with `+logFile 2`. The log prints the instance and device extensions and the chosen GPU.

## Settings persistence (first data for T-100)

A windowed run changed only `Saved Games\...\base\DOOMEternalConfig.local`: it added `r_windowWidth`
and `r_windowHeight` and dropped `r_hdrDisplay "1"`. `DOOMEternalConfig.cfg`, `profile.bin` and the save
slots were unchanged, and `s_volume` did not persist. The scripts restored the file and verified its
SHA-256 against the pre-run copy.

## Vulkan creation parameters (PLAN 1.5a)

Recorded with the Vulkan SDK 1.4.357.0 `VK_LAYER_LUNARG_api_dump` layer for the first frames.

| Item | Value |
|---|---|
| `VkApplicationInfo` | application `DOOMEternal` 1.0.2, engine `idTech` 7.1.1 |
| Instance `apiVersion` | 1.1.0 |
| Instance extensions | `VK_KHR_surface`, `VK_KHR_win32_surface`, `VK_KHR_get_physical_device_properties2` (listed twice), `VK_KHR_get_surface_capabilities2`, `VK_EXT_swapchain_colorspace` |
| Physical device | RTX 4080, with the integrated AMD GPU also present (per-app GPU preference set to high performance) |
| Device extensions | `VK_KHR_swapchain`, `VK_KHR_dedicated_allocation`, `VK_NV_dedicated_allocation_image_aliasing`, `VK_KHR_8bit_storage`, `VK_KHR_16bit_storage`, `VK_KHR_shader_float16_int8`, `VK_EXT_descriptor_indexing`, `VK_KHR_driver_properties`, `VK_EXT_calibrated_timestamps`, `VK_EXT_hdr_metadata`, `VK_EXT_full_screen_exclusive`, `VK_KHR_buffer_device_address`, `VK_KHR_deferred_host_operations`, `VK_KHR_pipeline_library`, `VK_KHR_ray_tracing_pipeline`, `VK_KHR_acceleration_structure`, `VK_KHR_spirv_1_4`, `VK_KHR_shader_float_controls`, `VK_NVX_binary_import`, `VK_NVX_image_view_handle`, `VK_KHR_push_descriptor`, `VK_KHR_external_memory_win32` |
| Not enabled | timeline semaphores, external semaphores, multiview, fragment shading rate |
| Feature chain | `VkPhysicalDeviceFeatures2` followed by per-feature structs (descriptor indexing, 8-bit storage, ...); no `VkPhysicalDeviceVulkan11Features` or `Vulkan12Features` |
| Queues | family 0 x1, family 2 x1, family 1 x2 |
| Swapchain | `B8G8R8A8_UNORM`, `SRGB_NONLINEAR`, 2 images, FIFO, exclusive, usage already includes `TRANSFER_SRC`; extent follows the window |

Consequences for T-082: the layer adds `VK_KHR_timeline_semaphore`, `VK_KHR_external_semaphore` and
`VK_KHR_external_semaphore_win32` (and the multiview and shading-rate extensions when those features are
built) as extensions, and appends their feature structs as separate structs, because the game uses the
1.1-era chain. `VK_KHR_external_memory_win32` is already enabled by the game. The swapchain already
allows transfer reads, so the present-hook copy needs no usage change (T-081).

Still open from 1.5a: which image is the final eye image and its format, the viewport sign, depth
format and compare op (world captures), and the game's frames-in-flight depth.
