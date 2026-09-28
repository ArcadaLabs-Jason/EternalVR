# VR first light (cinema mode)

The first VR build shows DOOM Eternal's presented frame on a flat screen inside the headset: one
OpenXR quad layer, 2.4 m wide at 2.5 m distance, placed at head height in front of the head pose seen
when the session starts. No stereo, no head-tracked camera, no controller input (keyboard and mouse
drive the game as usual).

## How it works

- `src/vkcore/layer_entry.cpp`: the implicit layer `VK_LAYER_ETERNALVR`. It declines to load unless the
  process is `DOOMEternalx64vk.exe`, `ETERNALVR_ENABLE_LAYER=1` and `ETERNALVR_DISABLE_LAYER` is unset
  or empty (T-079). For the game's device it adds `VK_KHR_external_memory(_win32)`,
  `VK_KHR_external_semaphore(_win32)` and `VK_KHR_timeline_semaphore` plus the timeline feature
  (`device_augment.cpp`, `device_features.cpp`: merged into a copy of the game's feature struct when it
  chains one, never written into the game's own create info); if the device cannot be created with them
  it retries with the game's own create info and stays inert (T-082).
- The presenter (`xr_presenter.cpp`, `presenter_*.cpp`, state in `presenter_impl.hpp`): at the game's
  first `vkCreateSwapchainKHR` a worker thread creates
  the OpenXR instance (1.1, falling back to 1.0; `XR_KHR_D3D12_enable` required), waits for the
  headset, checks that the runtime's adapter LUID equals the game's device LUID (a mismatch leaves VR
  off, T-111), creates a D3D12 device on that adapter, the session, an sRGB swapchain and a ring of 3
  shared images plus a shared fence, which the layer imports into Vulkan (T-040, T-080).
- Present hook: copies the presented swapchain image into a free ring slot on the game's queue, signals
  the shared fence (a Vulkan timeline semaphore) and hands the present its own per-image semaphore
  (T-081). A slot still being read by D3D12 is skipped, never waited for. The game presents from more
  than one queue, so each copy also waits on the timeline's previous value: the shared fence only moves
  forward. The downstream `vkQueuePresentKHR` runs outside the presenter's lock; after a present error
  other than out-of-date or surface loss the image's present semaphore is replaced. Replaced semaphores
  and those of destroyed swapchains are destroyed once the shared timeline shows eight later copies done
  (T-081's later fence).
- Exit: the game usually exits without `vkDestroyDevice`. The layer's global state is never destroyed,
  and at process exit (`DLL_PROCESS_DETACH` with a non-null reserved argument) nothing is torn down. The
  worker holds its own reference to the presenter, pins the DLL, and stops touching the game's device
  once shutdown begins. A D3D12 copy that misses its 2 s wait holds its slot and XR image until it
  completes, instead of reusing a command allocator that may still be executing.
- XR loop on the worker: waits for the newest written slot on its D3D12 queue, copies it into the XR
  swapchain image unchanged (the game's bytes are already sRGB-encoded), and submits the quad every
  frame, repeating the last image when no new frame arrived.
- Deviation from ARCHITECTURE 6.1 and T-082 for this build: OpenXR lives on a worker thread started at
  swapchain creation instead of in `vkCreateInstance` and the present hook, so a missing or slow
  runtime can never stall the game.

## Build

```
cmd /c "call E:\VS\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat && cmake --preset windows-msvc && cmake --build --preset windows-msvc"
```

`build\windows-msvc\src\vkcore\` then holds `EternalVR.dll`, `VK_LAYER_ETERNALVR.json` (relative
`library_path`) and `openxr_loader.dll` (Khronos release 1.1.63, loaded from the layer's own folder).

## Launch (rig scripts, branch `rig-scripts`)

Start the OpenXR runtime first (Virtual Desktop streamer connected, VDXR active). Then:

```
$layer = '<checkout>\build\windows-msvc\src\vkcore'
& tools\rig\run.ps1 -Exe retail -Args @('+r_hdrDisplay 0', '+map game/sp/e1m1_intro/e1m1_intro') `
    -GameEnv @("VK_ADD_IMPLICIT_LAYER_PATH=$layer", 'ETERNALVR_ENABLE_LAYER=1', 'ETERNALVR_LOG_DIR=<workspace>\tmp-vr\logs')
```

- `+r_hdrDisplay 0` is required: with HDR output the game presents a PQ-encoded 10-bit image and the
  headset colours are wrong (the log warns).
- `+map` stops at "Press SPACE to continue"; click the game window and press SPACE.
- `ETERNALVR_LOG_DIR` receives `LAYER_LOADED` and one `eternalvr-<date>-<pid>.log` per run. Healthy runs
  log `ring of 3 imported`, `first game frame on the screen` and, every 10 s, the copy and XR frame
  counts.
- Stop with `tools\rig\stop.ps1`, then wait for the game process to exit before `cleanup.ps1`.

With the headset absent the worker logs `XR_ERROR_FORM_FACTOR_UNAVAILABLE` once and retries every
second; the game runs flat meanwhile.

## Known gaps

- Mono only; the quad does not follow the game's resolution changes (the ring keeps the size and format
  of the first swapchain, and later shapes are scaled into it with a blit on a graphics queue; presents
  from the compute queue with another shape are not copied and are counted in the log). The ring's
  format is chosen to match the game's swapchain.
- No recenter binding; the screen is placed once when the session starts (and again after a
  reference-space change).
- Session loss leaves the game flat until restart; instance recreation (T-110) is not built yet.
- The game usually exits without `vkDestroyDevice`, so the OpenXR session ends with the process.
