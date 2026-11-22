# Render size: each eye at the headset's size, not the window's (T-031)

Static analysis of the retail `DOOMEternalx64vk.exe`, Steam build 25216728 (PE timestamp 0x6A7B9B8C, the exe
of `engine-facts.md` and `mp-guard.md`), and offline checks of the Windows and Vulkan behaviour the approach
relies on. The game was not run for this document. Tools: the Python helpers in `analysis/scripts` (PE
loader, xrefs, cvar names, disassembly), Ghidra 12.1.4 headless on a copy of the ui-layer project
(`analysis/render-size`), and a small Vulkan and Win32 test program on the rig's RTX 4080 (driver 616.92).
All addresses are RVAs in this build.

**[static-verified]**: read in this build's code or data (every signature below matches once in `.text`).
**[offline-verified]**: measured on the rig without the game (section 4). **[inferred]**: what the verified
code implies; section 8 lists the live checks.

## 1. Answer

Stereo rendered each eye at the game window's client size. The launcher fitted that window to a desktop
display, so the eye size depended on the owner's displays (1415 x 1440 per eye with the TV on), and the game
clamps its window to the display's work area.

The game takes both its swapchain size and its output size (the eye image, `_gui`) from `GetClientRect` of
its window, at four call sites (section 2). The layer now answers exactly those four calls with the render
size (the runtime's recommended view size, section 5), reports the same size as the surface's extent, and
creates the swapchain with **present scaling** (`VK_KHR_swapchain_maintenance1`), so the driver scales each
presented image into the real window, which stays a small desktop mirror. Every other part of the layer (the
ring copy, Route S, the window present gate, the UI capture) works on the swapchain's images as before, only
larger. Behind `ETERNALVR_RENDER_SIZE=auto|WxH` and `ETERNALVR_RENDER_SCALE`; anything missing fails closed to
the window-sized render.

| Option | Verdict | Why |
|---|---|---|
| (a) Virtual swapchain: the layer owns the game's swapchain images and blits a scaled eye into a small real one | Not needed now; the fallback if a driver lacks present scaling | Correct but large: acquire and present semaphores and fences, layouts, the engine's `STORAGE` use of the images, the window present gate and the mirror all re-implemented. Present scaling gives the same result with the real swapchain |
| (b) An oversized window beyond the display | Rejected | Windows allows a client area larger than the monitor only when `WM_GETMINMAXINFO` raises the track size [offline-verified], and the game itself clamps its window to the monitor's work area (0x1CC0980, section 2.1). The desktop would compose a 4.5 Mpix window at over 100 Hz, and the desktop window could not be small |
| (c) The engine's own render scale | Rejected | `rs_forceResolution` is "0.0 to 1.0" (registration 0x264120) and DLSS renders below the output size (0x1CBFA60); nothing renders above the output size, which is the window's. No supersampling cvar exists |
| **Chosen**: the client area answered with the render size, present scaling into the real window | Implemented | Four call sites, one import slot, two surface queries and one swapchain create; the engine's own resize path does the rest (section 3) |

## 2. How the game sizes its output and its swapchain [static-verified]

### 2.1 Output size

| What | Where |
|---|---|
| Output size (the eye image, `_gui`, `_upscaledOpaqueDepth`) | globals 0x39AABE4 (width) and 0x39AABE8 (height); render system vtable 0x200 (0x1CBF8D0) and 0x208 (0x1CBF8C0) return them (render system vtable at 0x2EAD3F8) |
| Render size (`_viewColor`; below the output only with DLSS) | 0x39AABDC and 0x39AABE0; vtable 0x1E0 (0x1CBF990) and 0x1E8 (0x1CBF7B0). 0x1CBFA60 sets them to the output size, or through 0x1CC5D40 when DLSS is on (0x14667EC53): NGX's optimal render size for the output size and `PerfQualityValue` (from `r_dlssQuality`: 0 gives 3 Ultra Performance, 1 gives 0, 2 gives 1, 3 gives 2, anything else 1 Balanced). With DLAA (`PerfQualityValue` 5, set by the layer, `dlss-dll.md` section 8) the optimal size is the output size |
| Video init 0x1CC04D0 (callers 0x1CBEFC0, 0x1CC0430, 0x1CC80E0) | windowed (`r_fullscreen` 0x66E82F0 not 1): the output size is `r_windowWidth` x `r_windowHeight` (0x66E8470, 0x66E84F0) when both are positive, else the mode's size, else 960 x 540; fullscreen: `r_mode` (0x66E8050) through the video modes |
| Window apply 0x1CC0980 (after init) | windowed: the client rect at `r_windowPosX/Y` (0x66E8570, 0x66E85F0) with the output size, `AdjustWindowRect` (style 0xCE0000, resizable), then **clamped to the monitor's work area** and `SetWindowPos`: the clamp the rig saw |
| Window moved or sized | the window procedure 0x1DC3B90 handles `WM_WINDOWPOSCHANGED` (0x47, jump table at 0x1DC42D8) by queueing 0x1DC39B0 through 0x1DC0600; it runs **0x1DC3B00**: `GetClientRect` (call 0x1DC3B22) and `ClientToScreen`, then render system vtable 0x298 (**0x1CC1240**) with (x, y, width, height) |
| 0x1CC1240 | unless fullscreen (vtable 0x2C0, 0x1CBFB10: flag 0x39AABF0) or inside the window apply (vtable 0x290, 0x1CBFA40: flag 0x39AAC18, set while 0x1CC0980 runs): writes `r_windowPosX/Y/Width/Height` and the output size (at least 1 x 1), then 0x1CBFA60 |

So in windowed mode the output size follows `GetClientRect` of the game's window whenever the window moves or
changes size, which is how `ETERNALVR_WINDOW` (a `SetWindowPos` before the first swapchain) set the eye size.

### 2.2 Swapchain

The swapchain object is at the render backend + 0xB8 (HWND at +8, "create on the window thread" at +0x10,
recreate flag at +0x18, size at +0x1C / +0x20, surface at +0x28, `VkSwapchainKHR` at +0x30).

| Step | Code |
|---|---|
| Every frame | 0x1CC1320 (render system vtable 0x1C0): **0x1D09070** compares `GetClientRect` (call 0x1D09091) with +0x1C / +0x20 and sets +0x18 when they differ; if set, **0x1D090E0** recreates (then 0x1C4AE30 resizes the back buffer target at +0x1A8 to the new size), then acquire 0x1D09280 |
| Recreate 0x1D090E0 | `GetClientRect` (call 0x1D0912E) gives the size, or, with +0x10 set, `SendMessage(0x9235)` runs **0x1D09190** on the window's thread, which does the same (call 0x1D091B4) |
| Create 0x1D098B0 (window, width, height) | destroys the old swapchain, creates the surface (`vkCreateWin32SurfaceKHR`, IAT thunk 0x2267B2D), checks present support, reads the surface capabilities (`vkGetPhysicalDeviceSurfaceCapabilities2KHR` only with `r_useFullScreenExclusive`, else the plain query at 0x1D09B7F); a `currentExtent` of 0xFFFFFFFF is taken as (width, height), and **any other `currentExtent` than (width, height) returns without a swapchain**. The swapchain is created with that extent, usage 0x1F, `B8G8R8A8_UNORM` (44) unless HDR |
| Acquire 0x1D09280, present 0x1D09670 | `VK_SUBOPTIMAL_KHR` and `VK_ERROR_OUT_OF_DATE_KHR` set the recreate flag |

The Vulkan WSI calls go through the import table (`vulkan-1.dll`: caps 0x2A1CD70, create 0x2A1CD88, images
0x2A1CD98, acquire 0x2A1CDA0, present 0x2A1CDA8), so the layer's own entry points see them.

### 2.3 The four GetClientRect calls

All read the `GetClientRect` import slot 0x2A1C048 (`call [rip + disp]`, FF 15). The other game callers of
`GetClientRect` keep the real client area: the cursor centring 0x5F5970, the raw-input cursor code 0x1DC18B0 and
0x1DC1E60, and a window helper outside the renderer (0x26F5610, 0x26F5F18, 0x26F6D5C).

| Call | Signature (the call is at the given offset) | Return address |
|---|---|---|
| Swapchain resize check (0x1D09070) | `48 8D 54 24 20 48 8B 49 08 FF 15 ?? ?? ?? ?? 8B 44 24 28 2B 44 24 20 3B 43 1C 75 ?? 8B 44 24 2C 2B 44 24 24 3B 43 20` (+9) | 0x1D09097 |
| Swapchain recreate (0x1D090E0) | `48 8D 54 24 30 FF 15 ?? ?? ?? ?? 8B 4C 24 38 2B 4C 24 30 8B 44 24 3C 2B 44 24 34` (+5) | 0x1D09134 |
| Recreate on the window thread (0x1D09190) | `48 8B 09 48 8B 49 08 FF 15 ?? ?? ?? ?? 8B 54 24 28 2B 54 24 20 44 8B 44 24 2C 44 2B 44 24 24 89 53 08 44 89 43 0C` (+7) | 0x1D091BA |
| Window size (0x1DC3B00) | `48 8D 54 24 38 48 8B 09 FF 15 ?? ?? ?? ?? 85 C0 74 ?? 48 8B 0D ?? ?? ?? ?? 48 8D 54 24 30 48 C7 44 24 30 00 00 00 00` (+8) | 0x1DC3B28 |

## 3. What the layer does

`src/vkcore/client_rect.*`, `virtual_client.*`, `present_scaling.*`, `surface_entry.cpp`, `swapchain_entry.cpp`;
the pure rules in `src/features/render_size/` (tested in `tests/features/render_size/`).

1. **Client area.** At the game's first window surface, while the multiplayer guard is armed, the four call
   sites are located (each signature once in `.text`, all reading one import slot), and the game's
   `GetClientRect` import is replaced (`import_patch`, shared with key injection). The replacement calls the
   real function and, only when its return address is one of the four and the window is the game's, answers
   `{0, 0, width, height}` with the render size. It remembers the answer per thread.
2. **Surface.** `vkGetPhysicalDeviceSurfaceCapabilities(2)KHR` on the game's surface, on a thread whose last
   answer was the render size, report it as current, minimum and maximum extent. A create that started with
   the real client area (the render size switched on in between) is answered with the real extent, so the game
   never sees a mismatch it would stop at (section 2.2).
3. **Swapchain.** `vkCreateSwapchainKHR` on the game's surface at the render size gets
   `VkSwapchainPresentScalingCreateInfoKHR`: `ASPECT_RATIO_STRETCH` (the eye letterboxed into the window) with
   centred gravity when offered, else `STRETCH`, else `ONE_TO_ONE`. `VK_SUBOPTIMAL_KHR` from that swapchain's
   acquires and presents is returned as `VK_SUCCESS` (the size differs from the window on purpose; otherwise
   the game would recreate every frame).
4. **Size.** `auto` waits for the runtime (the XR worker's `xrGetSystem`, then
   `xrEnumerateViewConfigurationViews`); `WxH` is known at once. When the size is known and the device has
   `VK_KHR_swapchain_maintenance1` and the surface offers present scaling for FIFO, immediate and mailbox with
   the size inside the scaled image range, the answer switches on and the window is moved to
   `ETERNALVR_MIRROR_WINDOW` with `SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS`. Windows then sends
   `WM_WINDOWPOSCHANGED` (even when nothing moves [offline-verified]), the game reads its client area again
   (0x1DC3B00) and sets its output size, and its per-frame check recreates the swapchain at the new size. The
   presenter rebuilds its ring for the new swapchain as for any resize. **A late size** (`auto` learns it only
   once the headset is found, after the game's first swapchain, made at the window's own size without present
   scaling): moving that window would make the unscaled swapchain's next present `VK_ERROR_OUT_OF_DATE_KHR`
   (the game then recreates from the error; in one rig run its present thread never returned after that, run
   pr-d1). So while the game has a swapchain without present scaling and none with it, the window is first
   only told to read its client area again (`SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE`, logged `render size
   on, the game's swapchain is not scaled yet: the window is told to read its client area again`): the game
   recreates through its own size check while the old swapchain stays valid, and the window moves once the
   swapchain with present scaling exists (`render size on, the game's swapchain is scaled: window client area
   to ...`; only `VK_SUBOPTIMAL_KHR` follows, taken as success), or after 10 s without one (`... the game made no
   scaled swapchain within 10 s`; `render size changed, ...` when the size changed rather than switched on). A
   create without present scaling that is still running when the size arrives counts as such a swapchain (the
   game's first create took 578 ms in pr-d1). A size known before the first swapchain moves the window at once,
   as before (`features/render_size/window_steps.hpp`). Not covered: a size that arrives after the game read
   its client area for a create and before that create reaches the layer. If the game still stops presenting
   right after an out of date present, the layer reports the stop once and the launcher asks for a restart
   (VR_STEREO.md, the `present:` lines); nothing there is known to free a stuck game.
5. **Instance and device.** `VK_KHR_get_surface_capabilities2` and `VK_KHR_surface_maintenance1` on the
   instance and `VK_KHR_swapchain_maintenance1` on the device are added when the render size is wanted, as they
   already are for the Route S window gate (with the same retry without them).

**Fail closed.** The render size stays off, and the game renders at its window's size, when: the variable is
unset or `off`; a call site does not match once, the calls read another slot or the import cannot be
replaced; the device lacks `VK_KHR_swapchain_maintenance1`; the surface offers no present scaling or not for
the size; a create with present scaling fails; or the multiplayer guard is refused or tripped. Turning off
puts the window back at `ETERNALVR_WINDOW` and tells the game to read its client area again. Every change to
the game (the import slot) is made only while the guard is armed; each answer and each present checks it
again (`virtual_client::poll`).

## 4. Offline checks on the rig [offline-verified]

A Win32 and Vulkan program (NVIDIA RTX 4080, driver 616.92; not the game):

| Check | Result |
|---|---|
| Present scaling offered for a Win32 surface | `supportedPresentScaling` 0x7 (one-to-one, aspect-ratio stretch, stretch), gravity min, max and centred on both axes, scaled image 1 x 1 to 4294967294 x 4294967294, for FIFO, immediate and mailbox (also in `vulkaninfo`) |
| A 2064 x 2208 swapchain into a 640 x 360 client area, `ASPECT_RATIO_STRETCH`, 120 immediate presents | created; every acquire and present `VK_SUCCESS` |
| The window resized to 484 x 661 during it, 120 more presents | acquires `VK_SUCCESS`, presents **`VK_SUBOPTIMAL_KHR`** (never out of date): the reason for step 3's mapping |
| `SetWindowPos` with only `SWP_FRAMECHANGED` (plus `NOMOVE`, `NOSIZE`), same thread and `SWP_ASYNCWINDOWPOS` from another thread | one `WM_WINDOWPOSCHANGED` each |
| A 3000 x 3200 client area on a 1280 x 800 monitor | 1284 x 781 with the default `WM_GETMINMAXINFO`; 3000 x 3200 when it raises the track size (option (b)) |

## 5. The size

`ETERNALVR_RENDER_SIZE`:

- `auto` (the launcher's default in stereo): the largest recommended image rect over the views, scaled down
  uniformly into a budget of **2064 x 2208 pixels per eye (4.56 Mpix)** and never up, times
  `ETERNALVR_RENDER_SCALE` (default 1.0, 0.5 to 2.0), then scaled down to fit the runtime's maximum image rect
  and maximum swapchain size (Route S keeps both eyes side by side in one image, so the width limit is halved)
  and 16384, and rounded to multiples of 8.
- `WxH` (256 to 8192 per side): that size, fitted to the runtime's limits once they are known.
- `off` or unset: the window's size, as before.

The budget is the default choice: the rig ran Route S at 2064 x 2100 per eye at about 115 stereo ticks per
second with the v1 cvars (VR_STEREO.md, live results), and research 11 puts the native 2496 x 2688 of a Quest 3
at the 90 Hz limit of an RTX 4080 without upscaling. VDXR's 2496 x 2688 at 100 % becomes **2056 x 2216** (the
simulator reports the same recommendation); the owner's Virtual Desktop at "57 %" recommends less than the
budget and renders at exactly its recommendation. A scale above 1 goes past the budget on purpose.

**With DLSS the budget holds what DLSS renders** (since 0.1.101; `render_size::Request::dlssInput`, the
launcher's `RenderSizeRequest.DlssInput`). The size above is the game's output size, the swapchain's, and DLSS
renders below it: NGX's optimal render size for that output (section 2.1), about 2/3 per side at Quality, 0.58
at Balanced, 1/2 at Performance and 1/3 at Ultra Performance (DLAA: the output size). Up to 0.1.100 Auto
budgeted the output, so DLSS Quality on VDXR's 2496 x 2688 output 2056 x 2216 and rendered about 1371 x 1477
(2.0 Mpix), well under what TAA renders on Auto. Now, with the launcher's DLSS on (stereo,
`ETERNALVR_STEREO_DLSS=1` with a quality in `ETERNALVR_STEREO_DLSS_QUALITY`), the recommendation is scaled into
the budget as if it were DLSS's input: the output is the size whose input fits the budget, never above the
recommendation and never above **1.5 times the budget** (about 6.84 Mpix per eye, `render_size::kDlssOutputCap`,
the launcher's `RenderSize.DlssOutputCap`), then times the scale and the edge margin's growth and fitted to the
limits as before (a scale above 1 and the margin go past the cap on purpose, as past the budget with TAA). TAA,
Off and DLAA keep the size above; a quality the layer does not know (the game's own `r_dlssQuality`) keeps it
too. The launcher applies it only when the graphics card the game runs on is an NVIDIA RTX card by its name (the
preflight's `UntestedHardware.MainGpu`; `LaunchInputs.NvidiaRtx`): on any other card DLSS cannot run per eye, the
layer falls back to TAA, and TAA at the larger size would be the costly way, so the size stays TAA's. The layer's
own Auto (no runtime answer before the launch) does not know the card and applies it with the launcher's DLSS. The
cap keeps VDXR's 2496 x 2688 (6.71 Mpix) whole and holds larger recommendations near VDXR's cost (below):
without it Godlike's output would be 3072 x 3264, about 60 to 80% more graphics card time than before.

| Recommendation | TAA, Off, DLAA | DLSS Quality | Balanced | Performance, Ultra Performance |
|---|---|---|---|---|
| VDXR 2496 x 2688 (Quest 3, High) | 2056 x 2216 | 2496 x 2688 (input about 1664 x 1792) | 2496 x 2688 | 2496 x 2688 |
| Virtual Desktop Godlike 3072 x 3264 | 2072 x 2200 | 2536 x 2696, capped (input about 1691 x 1797) | 2536 x 2696 | 2536 x 2696 |
| 3840 x 3840 | 2136 x 2136 | 2616 x 2616, capped | 2616 x 2616 | 2616 x 2616 |
| The simulator 1280 x 1400 | 1280 x 1400 | 1280 x 1400 | 1280 x 1400 | 1280 x 1400 |

The tests of both sides hold the same cases (`tests/features/render_size/render_size_dlss_tests.cpp`, the
launcher's `RenderSizeDlssTests`), with the scale and the edge margin's growth: VDXR at Quality renders 2000 x 2152
at 0.80 and 2992 x 3224 at 1.20 or with Medium; Godlike at Quality 2032 x 2160 at 0.80, 3048 x 3232 at 1.20 and
3296 x 3504 with Large; with 1.20 and Large it wants 3957 x 4204 and is fitted to 3856 x 4096, keeping x1.267 of
the margin.

On the rig (simulator, Route S, `ETERNALVR_RENDER_SIZE=auto` with DLSS Quality): `size: render size auto x1.00,
the budget on DLSS's input x0.667`, the swapchain at the simulator's 1280 x 1400 and eye R's feature `853x933 ->
1280x1400, PerfQualityValue 2`; at fixed sizes NGX's Quality input is the output times 2/3 rounded to the nearest
pixel (`1365x1472 -> 2048x2208`, `1125x1211 -> 1688x1816`), Performance half (`1024x1104 -> 2048x2208`), DLAA the
output.

**What it costs** [rig, partly extrapolated]: the output, DLSS itself and the game's effects after it are larger.
The test PC's RTX 3080 Ti on the three bench routes (bench-routes.md; `ETERNALVR_PACE=off`, NVIDIA's DLSS 310.9.1
with preset K, RT off), the GPU's busy time per stereo tick (both eyes), mean of two runs where two were made. The
simulator stops at 2048 per eye, so the pairs stand in at the sizes it allows:

| Per eye (DLSS input) | Combat | Outdoor | Hub |
|---|---|---|---|
| DLSS Quality 1688 x 1816 (1125 x 1211) | 7.6 ms | 9.7 ms | 8.1 ms |
| DLSS Quality 2048 x 2208 (1365 x 1472) | 10.2 ms | 11.0 ms | 9.7 ms |
| DLSS Quality 1384 x 1472 (923 x 981) | 6.3 ms | 7.8 ms | 6.4 ms |
| DLSS Performance 2048 x 2208 (1024 x 1104) | 8.7 ms | 10.7 ms | 9.0 ms |
| DLAA 2048 x 2208 | 11.4 ms | 13.4 ms | 12.3 ms |
| TAA 1688 x 1816 | 7.5 ms | 8.6 ms | 7.4 ms |
| TAA 2048 x 2208 | 10.7 ms | 10.8 ms | 9.2 ms |
| DLSS Quality, preset E, 2048 x 2208 | 6.5 ms | 9.3 ms | 7.8 ms |

1688 x 1816 to 2048 x 2208 is VDXR's step from the old Auto (2056 x 2216) to the new (2496 x 2688), 1.21 per side,
at 82% of the real sizes: +34%, +13% and +20% (the combat route at 1688 x 1816 is CPU-bound, ticking at 106 a
second). The capped Godlike step (2072 x 2200 to 2536 x 2696) is 1.22 per side, the same as VDXR's, so the same
pair stands in for it. Without the cap Godlike's step would be 1.48 per side (1384 x 1472 to 2048 x 2208 here,
standing in for 2048 x 2176): +63%, +40% and +52%. At the same output DLSS Quality with preset K costs about what
TAA does, so the new Auto with DLSS Quality costs about what TAA at the runtime's own size (or the cap) costs.
Fitted per route (time = a + b x input Mpix + c x
output Mpix over the nine preset K runs; b 0.8 to 1.0 ms and c 0.9 to 1.1 ms per Mpix) and extrapolated to the real
sizes: VDXR at Quality 9.7, 11.3 and 9.9 ms before, 12.9, 14.1 and 12.7 ms now (TAA on Auto 10.8, 10.8 and 9.3);
VDXR at Performance 9.0, 10.6 and 9.0 before, 11.8, 13.0 and 11.4 now; Godlike at Quality 9.7, 11.3 and 9.9
before, 13.1, 14.3 and 12.9 now with the cap (17.8, 18.3 and 17.1 without it). Preset E took 15 to 36% less than K
at 2048 x 2208, so at VDXR's new size it
would be about TAA on Auto's cost (extrapolated from two sizes). One card, a simulator and no headset.

The eye image is near-square, as the headset's views are; the projection already maps each eye's FOV onto the
whole image (VR_STEREO.md, render size), so the pixels become square.

## 6. The UI target

`_gui` has the output size (ui-layer.md section 2.1), so it follows the eye size and is near-square. idSWF lays
the game's screens out in a centred 16:9 frame fitted to the width: in the UI captures at 2064 x 2100 and
2560 x 2100 the menus, the HUD corners, the boss bar and the subtitles all sit inside that band, and the rows
above and below it are empty (only the FPS counter sits at the image's corner) [static-verified on the captures
of `tmp-vr/m6t3-ui` and `tmp-vr/u2-ui`]. So the UI quad and the menu panel show only that band
(`ui_layer::wideContentRect`, `ETERNALVR_UI_CROP=0` shows the whole image): a 16:9 UI at the eye's width
(2056 x 1157 at the default size, near ARCHITECTURE 7a's 2048 x 1152), at the same place and size in metres
as before. The menu pointer's hits on the band are mapped onto the whole image for the game's cursor.

The frame is fitted inside the target whatever its shape: on a target wider than 16:9 it spans the height and
is centred between empty columns. A GUI capture at 2048 x 864 (a 2560 x 1080 render size in the runtime's pixel
budget, 2026-10-20) had the HUD corners and the weapon wheel within 0.002 of the band coordinates of a 2048 x
2208 capture, in its centred 1536 x 864 frame, and nothing outside it in play (full-screen backdrops such as the
loading screen fill the whole target, as they do on a near-square one). Up to 0.1.70 `wideContentRect` took a
wider target whole, so every crop measured in the band landed too far out on it: the weapon wheel's rectangle
missed the wheel's left edge (about 11% of the wheel at 2.37:1, which then stayed on the head-locked quad beside
the centred wheel), and the wrist and weapon HUD cut the corners at the wrong columns. A near-square eye image
never had this; a headset whose eye image is wider than 16:9, or a render size or window of that shape, did.

## 7. Launcher

Mono launches do all of this too, for their one image at one eye's size (section 11). Stereo launches set
`ETERNALVR_RENDER_SIZE` (`render_size` in `launcher.ini`, default `auto`) and
`ETERNALVR_RENDER_SCALE` (`render_scale`, 0.50 to 2.00, in the settings window as "Render scale"), and no
longer fit the window to a display: the window is a 1280 x 720 desktop mirror (`ETERNALVR_WINDOW` and
`ETERNALVR_MIRROR_WINDOW`, `r_windowWidth/Height`) at the top-left of a virtual display when there is one (a
virtual display driver, Virtual Desktop's or Meta's virtual monitor: `VirtualDisplays`), else the primary one.
`render_size = off` keeps the eye-sized window fitted to a display, as before. If the layer cannot set the
render size, the game renders at the mirror's size: visibly soft, and the layer log says why (`size: render
size off: ...`).

**The game starts at its final size.** Switching from the mirror's size to the render size a few seconds into
the session makes the engine resize its render targets; on cards with 12 GB or less that resize can ask for
about 1 GB more and fail with "Failed to allocate video memory" (a modal box behind the headset: it looks like a
hang). A game that starts at the render size never resizes (verified on an RTX 3080 Ti). So before a stereo
launch with the render size on, the launcher asks the runtime itself (`OpenXrProbe`: the layer's
`openxr_loader.dll` by full path, `xrCreateInstance` without a graphics extension, `xrGetSystem`,
`xrGetSystemProperties`, `xrEnumerateViewConfigurationViews` for the stereo views, `xrDestroyInstance`; on a
worker thread, at most 5 s), with the game's OpenXR environment (`XR_RUNTIME_JSON` for a chosen runtime, the
OpenXR API layers it switches off) set in its own process for the probe only. It works the size out with the
layer's rules, ported line for line (`RenderSize`, section 5, Route S's two eyes per swapchain image; the C++
tests' vectors are repeated in `RenderSizeMathTests`), and gives the game `ETERNALVR_RENDER_SIZE=WxH` (a fixed
size, which the layer applies before the first swapchain) and `+r_windowWidth W +r_windowHeight H` (video init
takes the output size from them, section 2.1). The window stays the mirror (`ETERNALVR_WINDOW`,
`ETERNALVR_MIRROR_WINDOW`): the layer answers the game's client-area queries with the render size. A fixed
`WxH` setting is fitted to the runtime's limits the same way and goes on the command line too. The launcher log
states the choice (`render:  render size 2056x2216 from the runtime's recommendation 2496x2688 (scale 1.00)`).

If the runtime does not answer (no runtime, the headset off: `XR_ERROR_FORM_FACTOR_UNAVAILABLE`, the loader
missing, the 5 s passed), the launch goes on as before: `ETERNALVR_RENDER_SIZE=auto`, the game starts at the
mirror's size and the layer switches to the render size in-game. The launcher log gives the reason and a note
("Headset not detected; the render size is decided in-game.").

Probe results on the rig (no game): Virtual Desktop's runtime with a Quest 3 recommends 2496 x 2688 (max image
and swapchain 16384), giving 2056 x 2216; the simulator recommends 1280 x 1400 (max 4096), giving 1280 x 1400,
and fits a fixed 2064 x 2208 to 2048 x 2192.

**The mirror's options (layer).** With the render size on, the layer can move and resize the launcher's mirror
window (`mirror_place.hpp`, `features/render_size/mirror_window.hpp`; docs/VR_STEREO.md, settings):
`ETERNALVR_MIRROR_DISPLAY` (`primary`, a display number, a point `x,y`, or `launcher`), `ETERNALVR_MIRROR_SIZE`
(`WxH`, a scale, or `fill`: the whole display, borderless, the image stretched into it), `ETERNALVR_MIRROR_CROP` (`16:9`: the eye image's centred band in a 16:9 window; `full`, the
default, shows the whole image in a window of the eye's shape) and `ETERNALVR_MIRROR_FRONT` (the window brought to the front once when
its surface is made, never activated). The window's rectangle is worked out once from the launcher's and the
displays Windows lists at that moment, and is used both before the first swapchain (`ETERNALVR_WINDOW`'s
placement) and when the render size turns on. The crop needs present scaling `STRETCH` (the rig's surfaces
offer it); without it the window shows the whole image.

With `full` the window takes the eye image's shape (`render_size::shapeToImage`, `mirror_place::shapeToEye`):
the rectangle above is cut to the largest one of the eye's aspect inside it, keeping its top-left corner, or
its centre when a display choice centred it (a 2056 x 2216 eye in 1280 x 720 gives 668 x 720). The eye's size
is the one the window reports, or a fixed render size before that (`auto` waits for the runtime, and the
window is reshaped when the render size turns on). Before, the window kept the 16:9 size and the swapchain's
`ASPECT_RATIO_STRETCH` (`scaling 0x2, gravity 0x4`, logged on the only swapchain create) letterboxed the image;
on an NVIDIA RTX 4080 the first presents were nevertheless stretched to the window, and only after the window
was minimised and restored a few times (no new swapchain, no other log change) did the bars appear. With the
window at the eye's shape a stretch and a letterbox give the same image, so the first frame is right whatever
the driver does. `fill` and the crops keep their shapes.

`ETERNALVR_MIRROR_SIZE=fill` is a size choice rather than a separate switch because it replaces the size (the
launcher's `Window size` row) and works with every display choice and crop. The window gets the display's
whole area (`rcMonitor`, not the work area) and loses its caption, sizing border, system menu and edges when
it is placed before the first swapchain (the same synchronous `SetWindowPos` as `ETERNALVR_WINDOW`, with
`SWP_FRAMECHANGED`); the frame is not put back, also not when the render size turns off. The swapchain is
created with `STRETCH` whatever the crop: with `full` the whole eye image is stretched to the display's shape
(a Quest 3 eye image of 2056 x 2216 on a 16:9 display is squashed to about half its height), with `16:9` the
band fills a 16:9 display exactly. Cropping to each display's own shape instead was left out: the crop setting
already gives that for the usual 16:9 and 16:10 displays. The window is raised once without being activated,
as before, so a display's taskbar (always on top) can stay over it.

## 8. Live checks (phase 2, on the virtual display, the simulator runtime)

1. **Default launch** (launcher, simulator runtime, stereo). Log: `size: render size auto x1.00`, the four
   call sites with their RVAs, `GetClientRect import (slot RVA 0x2A1C048)`, `present scaling 0x7`, `the runtime
   recommends 2496x2688 per eye ... gives 2056x2216`, `the game's window reports a 2056x2216 client area`,
   `render size on: window client area to ...,668x720`, then `vkCreateSwapchainKHR 2056x2216 ...` with
   `size: the swapchain is scaled into the window's real client area (scaling 0x2, gravity 0x4)`, `presenter:
   ring rebuilt for 4112x2216` and every 10 s `size: render size 2056x2216 (swapchain 2056x2216, eye image
   2056x2216)` with client-area answers counting up. The desktop window is 668 x 720 (`mirror: the window
   takes the eye's shape`) and shows eye L without bars.
2. **Each eye's size and projection**: the `xr: the game's own FOV setting for the headset ... the game's image
   is 2056x2216` line, the `latch:` lines (projection `[0][0]` and `[1][1]` against the per-eye tangents), `seq:`
   pairs all complete, `ui: N shared GUI image(s) of 2056x2216`, captures not refused for a size mismatch.
3. **Sharpness**: `ETERNALVR_CAPTURE_EYES=<dir>,120` in two runs, `ETERNALVR_RENDER_SIZE=off` with the old
   1415 x 1440 window (`ETERNALVR_WINDOW=0,0,1415,1440`) and the default; compare the same view's L/R PNGs at
   100 % crop (edge sharpness of HUD-free geometry, text on world surfaces), and confirm the new eye PNGs are
   2056 x 2216.
4. **Fixed size**: `ETERNALVR_RENDER_SIZE=2064x2208`: the size is on before the first swapchain (no
   recreate).
5. **Frame rate**: `rates:` line (stereo ticks per second) against the 2064 x 2100 baseline; `window:` line
   shows no present waiting for the display.
6. **Menus**: title screen, pause menu and settings on the menu panel (16:9), the laser pointer's clicks land
   where it points (`menu:` move and click counts), the HUD quad in play is 16:9 with nothing cut.
7. **Resizing the mirror** by hand: presents stay successful (`suboptimal result(s) taken as success`
   counting), no recreate loop (one `vkCreateSwapchainKHR` per change at most), the eyes keep their size.
8. **Fail closed**: `ETERNALVR_GUARD_TEST_TRIP_MS=20000`: `render size off: the multiplayer guard ...`, the
   window goes back to `ETERNALVR_WINDOW`, the game recreates at the window's size and runs flat on the cinema
   screen.
9. **Known unknowns** [inferred]: the desktop mouse in the game's menus (the game may map real client pixels
   to the larger output; the VR pointer uses relative motion and is not affected); applying video settings in
   the game re-runs 0x1CC0980, which sizes the real window to the render size clamped to the monitor (the eyes
   keep the render size); a fullscreen switch (`r_fullscreen 1`) is not supported with the render size.

## 9. Live results (simulator runtime, virtual display, RTX 4080)

| Check (section 8) | Run | Result |
|---|---|---|
| 1. Default launch, `auto` | rs-r24 | the simulator recommends 1280 x 1400 per eye; the game's swapchain is recreated at 1280 x 1400 about 0.1 s after the runtime is known and scaled into the 1280 x 720 mirror on the virtual display; 135 stereo ticks per second, XR 90 frames and 90 new images per second, eye tags in sync, per-eye TAA picks in both eyes |
| 2. Size and projection | rs-r21, rs-r24 | eye image, UI swapchain and XR image all at the render size; the FOV line reports the render size |
| 3. Sharpness | rs-r19 to rs-r22 | 2048 x 2208 visibly sharper than 1416 x 1440 at 100 % crop (`tmp-vr/rs/sharpness-1416-vs-2048.png`) |
| 4. Fixed size | rs-r21 | `2056x2216` fitted to 2048 x 2208 by the runtime's limits; 109 stereo ticks per second, XR 90 |
| 6. UI band | rs-r20 | HUD, loading screen text and objectives inside the centred 16:9 band of the 1280 x 1400 `_gui` |
| 8. Fail closed | rs-r25 | the test trip turns the size off, the window goes back to `ETERNALVR_WINDOW`, the game recreates at the window's size and the headset shows the cinema screen |
| First create | rs-r23 | the game's first `vkCreateSwapchainKHR` on a new surface can return `VK_ERROR_INITIALIZATION_FAILED` once and succeed on its immediate retry, with or without present scaling; only two failed scaled creates in a row turn the size off |

Not yet checked live: the menu pointer on the 16:9 band (7 and the menu part of 6), resizing the mirror by hand,
and the headset (Virtual Desktop's recommendation is below the budget and is used as it is).

## 10. Drivers without usable present scaling (AMD)

Two cases from players' reports (October 2026), both fail closed to the window-sized render, which capped each
eye at the mirror window (often 33 to 47 % of the planned width):

| Case | What the log showed | Where it turns off |
|---|---|---|
| AMD Radeon RX 5000 and 6000 (RDNA1, RDNA2; an RX 6750 XT) | no `VK_KHR_swapchain_maintenance1` nor the EXT one: `no swapchain maintenance extension matches the instance's ...` (before 0.1.17: `device extension VK_KHR_swapchain_maintenance1 is not supported ...`) | at the game's first surface: `the game's device has no swapchain maintenance extension (present scaling)` |
| NVIDIA driver 581.80 (RTX 3070 Ti; fixed in 0.1.17) | the device listed only `VK_EXT_swapchain_maintenance1` while the instance took `VK_KHR_surface_maintenance1`, so the layer looked for the KHR device name only | as above. The instance now takes both surface maintenance names when it can, and the device the KHR swapchain one, else the EXT one; `ETERNALVR_TEST_HIDE_KHR_MAINTENANCE=1` (a test knob) runs this path on a driver that lists both |
| AMD RDNA3.5 (ROG Ally, Radeon 890M) | the extension exists, but the scaled image range is the window's own size: `present scaling 0x5, gravity 0x0, scaled image 672x701 to 672x701` | `the render size is outside the surface's scaled image range` |

What the layer does now (`window_cap.hpp`, `features/render_size/render_cap.hpp`):

- The off line says how large each eye is against the plan and why:
  `size: render size off: <why>; the game renders at its window's size; each eye renders at the window's 652x713, 50% of the planned 1280x1400 (a driver limit: the graphics driver cannot scale presented images)`.
  For the range case the limit reads `the driver scales presented images only within 672x701 to 672x701; the window is not re-placed for this`.
- `queryScaling` logs each present mode's range and the surface's raw extents:
  `size: present mode 2: scaling 0x7, gravity x 0x7 y 0x7, scaled image 1x1 to 4294967294x4294967294; surface current 658x720, min 658x720, max 658x720`.
- Device create logs the GPU and driver once (`GPU vendor 0x10de device 0x2704, driver version 0x9a170000, driver 'NVIDIA' '616.92'`),
  which swapchain maintenance names the device lists, and when the instance has no surface maintenance extension.
- **The no-extension case is known at device create**, before the game's window is placed, so the window is placed
  at the largest client area of the planned eye's shape that the mirror display's work area allows (frame included,
  centred, never larger than the planned eye):
  `size: no present scaling on the game's device, so each eye renders at the window's size: the window takes the largest eye-shaped client area the work area 0,0 1280x752 allows, 652x713 at 314,31 (planned eye 1280x1400)`.
  `r_windowWidth` and `r_windowHeight` are then held at that window's real client area
  (`cvars: r_windowWidth is held at the placed window's 652, not 1280 (no present scaling)`), which the game already
  reads, so nothing is written and the game does not resize its window (holding the launch size resized it, which
  an AMD driver answered with `VK_ERROR_OUT_OF_DATE_KHR`; `runtime_cvars.cpp`). Not with `ETERNALVR_MIRROR_SIZE=fill`, nor for `auto` before the
  runtime's size is known (the window then goes where the mirror goes).
- The range case is known only once the game's surface exists; the window is not re-placed for it (a mid-session
  resize is what froze AMD drivers), only logged.
- The status file gets `render_planned` and `render_capped`; the launcher compares `render` with `render_planned`
  after the session and warns, and its "Each eye" line shows the real size while the last session was capped.
- `ETERNALVR_TEST_NO_PRESENT_SCALING=1` (a test knob) leaves the swapchain maintenance extension out so the fallback
  runs on any driver.

Not done: the instance's surface maintenance extension is still chosen by `vkCreateInstance` succeeding (the loader
can drop an extension the driver lacks without failing); the device's own list and the raw scaling values in the log
show when that happened. A virtual swapchain (section 1, option (a)) remains the full fix for both cases.

Rig runs (simulator, RTX 4080, the virtual display alone at 0,0 1280x800, work area 1280x752):

| Run | Result |
|---|---|
| aw2, `ETERNALVR_TEST_NO_PRESENT_SCALING=1`, render size 1280x1400 | window placed at 314,31 with client 652x713 (the mirror's eye shape would have been 658x720, whose frame does not fit the work area); one `vkCreateSwapchainKHR` at 652x713; XR swapchain 1304x713 (two eyes of 652x713); `render_capped=1`; the cvars read 652x713 already (no write); no present errors |
| aw3, without the knob | unchanged: render size on at 1280x1400 scaled into the 658x720 mirror; `render_capped=0` |

## 11. Mono at the render size

Head-tracked mono (the launcher's Mono, VR mode on the Advanced tab) renders one image both eyes see, from the
head's centre with the symmetric FOV that encloses both eyes (docs/VR_HEAD_TRACKED.md). Up to this change the
launcher passed mono no render size and no window: the game drew that image at the flat game's own window and
resolution. A player on a Quest 3 (Virtual Desktop, Godlike preset) found Mono blurry where Stereo was sharp
[player]. On the rig [log]: a mono run with the render size off draws at its window's size, and the headset gets
that size (`xr: swapchain 1280x720 (1 eye image(s) of 1280x720)` for the suite's 1280 x 720 window, run
`moff-c-1`); a launcher mono launch has no `+r_fullscreen`, `+r_windowWidth` or `ETERNALVR_WINDOW` [code], so the
player's own video mode decides (a full-screen 1920 x 1080 desktop gives 1920 x 1080 over 108 x 110 degrees:
about 700 pixels per unit of tangent across and 380 up and down, against about 930 in a stereo eye at Auto).

**The change.** A mono launch with the render size on (the default) gets what a stereo launch gets for one eye
(section 7): the runtime probe first, the size by the same rule (`RenderSizeChoice.Decide` with Resolution's base
and number, no edge margin, `MonoEyesSideBySide` 1: one image per swapchain, so the runtime's swapchain width is
not halved), `ETERNALVR_RENDER_SIZE=WxH`, the desktop mirror and the window set from `forced-cvars.txt` (`|
window`: `+r_fullscreen 0 +r_windowWidth W +r_windowHeight H`), and nothing else of the stereo set (the game's own
anti-aliasing, sharpening, dynamic resolution and vsync stay the player's). The layer needed one addition: with
`ETERNALVR_MODE` not stereo and the render size wanted, the present hook holds the window set less
`r_swapInterval` from the first present (`runtime_cvars::applyMonoWindow`, `stereo_seq::monoWindowCvars`), so a
load path that applies the player's video mode cannot take the image back to the display's size or to full
screen (logged `cvars: head-tracked mono with the render size: only the window's mode and size are held`, then
`held at run time: r_fullscreen 0, r_windowWidth W, r_windowHeight H`). Everything else (the client-area answer,
present scaling, the late size, window_cap's fallback on AMD) was already mode-independent. The QA suite's
`mono-size` scenario checks it on the simulator (1280 x 1400, one image per swapchain, the hold's lines) through the
harness's own launch: it sets `ETERNALVR_MODE=mono` itself (the launcher leaves it unset in Mono, the layer's
default) and its command line carries the window's 1280 x 720, so there the hold keeps 1280 x 720 and writes it back
over the render size the game sets, while the image keeps the render size.

**The launcher's own Mono launch [rig].** Three sessions of the built launcher (`EternalVR.Launcher.exe --launch`
with `--test-exe` naming the game's own exe, so the plan, the settings snapshot and the restore are the launcher's),
the simulator, the virtual display at 3840 x 2160 (work area 3840 x 2040), `+map` e1m2 from Extra game arguments and
one level load of the same map 30 s in (the debug schedule), then the game closed by its window:

| Session | Command line (the launcher log's `args:`) | Game window | Swapchain, headset | Flat config after the restore |
|---|---|---|---|---|
| `render_size = auto` | `... +r_fullscreen 0 +r_windowWidth 1280 +r_windowHeight 1400 +map ...` (the probe's 1280 x 1400) | a framed window at client 1280 x 1400, then the mirror at 0,0 with client 658 x 720 | 1280 x 1400 from the first swapchain to the end, the level load included; `xr: swapchain 1280x1400 (1 eye image(s) of 1280x1400)` | every window key as before the session |
| `render_size = 2472x2656` (taller than the display) | `... +r_windowWidth 2472 +r_windowHeight 2656 ...` | the game clamped its framed window to the work area (client 2472 x 1952), then the mirror (client 670 x 720) | 2472 x 2656 throughout (`the game's window reports a 2472x2656 client area`) | the game had saved `r_windowWidth 2472`, `r_windowHeight 2656`; the restore put the player's 784 x 513 back |
| `auto`, the flat config at `r_fullscreen 1` | as the first | a framed window (not full screen), then the mirror at client 658 x 720 | 1280 x 1400 throughout | the game had saved `r_fullscreen 0`, `r_windowWidth 1280`, `r_windowHeight 1400`, `r_mode 0`; the restore put `r_fullscreen 1`, `r_mode 25` and the window keys back |

In each the layer logged `held at run time: r_fullscreen 0, r_windowWidth W, r_windowHeight H` at the launch size
and no hold write (the game kept those values; no load path applied the player's video mode in these sessions, so
the hold was not put to work). No `WARNING` but the simulator's controller bindings note.

**Sizes.** Within the runtime's limits mono's Auto size is one stereo eye's: VDXR's 2496 x 2688 gives 2056 x 2216,
Godlike's 3072 x 3264 gives 2072 x 2200, the simulator's 1280 x 1400 stays (the shared cases in
`render_size_tests.cpp` "auto in mono" and `RenderSizeMonoTests`). Where the swapchain's width limits stereo, mono
gets more: the simulator's 4096-wide swapchains give stereo 2048 x 2240 per eye at Resolution x2.00 and mono
2560 x 2800. The image spans the enclosing FOV, wider than one eye (Quest 3: 2.75 x 2.86 units of tangent against
an eye's 2.22 x 2.39), so at the same size it holds about 0.80 of a stereo eye's pixels per degree across and 0.84
up and down [inferred from the FOVs]; Resolution x1.20 (2472 x 2656 on a Quest 3) is about a stereo eye's density.

**Cost [rig].** RTX 3080 Ti, the OpenXR Simulator at 90 Hz, the bench routes (`bench-routes.md`) with pacing off
(mono's pacing), per-eye TAA in stereo and the game's TAA in mono, ray tracing off, two interleaved rounds per
route; mono sizes as fixed render sizes standing for each case (the simulator recommends only 1280 x 1400):
1920 x 1080 and 2560 x 1440 for today's Mono on a 1080p and a 1440p desktop, 2056 x 2216 for the new Mono on a
Quest 3 at Auto, 2472 x 2656 for it at Resolution x1.20; stereo at 2048 x 2208 per eye (the `-q3` routes, the
simulator's largest, 0.8% fewer pixels than 2056 x 2216). GPU busy time per mono frame or stereo tick, the mean
of each run:

| Route | Mono 1920 x 1080 | Mono 2560 x 1440 | **Mono 2056 x 2216 (new Auto)** | Mono 2472 x 2656 (x1.20) | Stereo 2048 x 2208 per eye |
|---|---|---|---|---|---|
| combat | 3.05, 3.30 ms (0.37 of stereo) | 4.00, 4.38 (0.48) | **5.27, 5.26 (0.61)** | 7.11, 5.94 (0.75) | 7.96, 9.34 |
| outdoor | 3.98, 4.01 (0.36) | 4.97, 4.87 (0.44) | **5.35, 5.46 (0.49)** | 6.51, 6.50 (0.59) | 11.14, 10.98 |
| hub | 3.25, 3.28 (0.35) | 4.31, 4.07 (0.45) | **4.67, 4.78 (0.51)** | 5.85, 5.84 (0.63) | 9.30, 9.26 |

- The new default costs a mono frame 1.4 to 1.7 times a 1080p desktop's and 1.1 to 1.3 times a 1440p desktop's,
  and stays at half to 0.6 of a stereo tick at the same size per eye: clearly cheaper than Stereo. Mono's GPU time
  per pixel is higher than a stereo eye's (its wider view holds more of the scene, and stereo's eye R skips part of
  eye L's work), so it is not the 0.5 the pixel counts alone give.
- The CPU frame (present to present, p50) went from 4.1 to 5.0 ms at 1080p to 5.4 to 6.1 ms; the VRAM peak from
  7.1 to 7.3 GB to 7.7 to 7.9 GB (stereo 8.0 to 8.1 GB).
- The simulator skipped more display periods as the mono image grew (2.2 to 3.0% at 2056 x 2216, 4.9 to 9.8% at
  x1.20, under 1% at 1080p; stereo 0.1 to 5.0%), with every frame it showed a new image: its compositor competes
  with an uncapped game for the card. A headset runtime reprojects instead; not checked in a headset.
- Not chosen as the default: the x1.20 density match (0.59 to 0.75 of stereo here). It stays the player's choice
  through Resolution; whether the default should be larger is a question for a headset look at both.

Runs under the test PC's evidence folder for this job (`mnew-*`, `m1080-*`, `m1440-*`, `m12-*`, `st-*`, `moff-c-1`,
`mauto-c-1`, `scen-monosize-1`), 2026-11-20.
