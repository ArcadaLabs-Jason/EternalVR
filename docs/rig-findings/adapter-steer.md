# Adapter steering on two-GPU machines (T-057, T-111)

On a machine with two Vulkan devices (a laptop's integrated and discrete GPUs, a desktop with the integrated GPU
on), the game's Vulkan device can land on a different adapter from the headset's. The presenter's LUID check then
refuses VR at session creation (T-111). Adapter steering makes the game's own pick land on the headset's adapter
instead. It is **off by default**: `ETERNALVR_ADAPTER_STEER=1` (or `on`) turns it on, until a two-GPU machine has
been tried with a headset (section 5); the launcher's "Use the headset's graphics card" (Advanced tab, this machine's, never in a profile) sets it. Code: `src/vkcore/adapter_steer.cpp` (Vulkan and OpenXR side), `src/vkcore/adapter_order.cpp` (the order,
unit-tested in `tests/vkcore/adapter_order_tests.cpp`).

Evidence labels: [static] the game's disassembly or the Vulkan loader's source, [test] unit tests, [rig] seen on the
development PC, [inferred] a reading of the others.

## 1. How the game picks its device

Steam build 25216728 (PE timestamp 0x6A7B9B8C), the function at RVA 0x1CC34D0 [static]:

- `vkEnumeratePhysicalDevices` twice (count, then the list; RVA 0x1CC3501 and 0x1CC35D6); a failure or an empty list
  is a fatal error ("vkEnumeratePhysicalDevices failed with error (%s)").
- `r_physicalDeviceIndex` ("Override Vulkan device selection", default -1; the cvar object at 0x66E8F30) is read at
  0x1CC35EF. At 0 or more the game takes that index, clamped to the list's last entry.
- At -1 it walks the list (0x1CC361B to 0x1CC3657): `vkGetPhysicalDeviceProperties` on each, and the first device
  with `apiVersion` at least 1.1.0 (0x401000) and `deviceType` 2 (`DISCRETE_GPU`) is taken. With none, the first
  device is taken (0x1CC3665).
- The exe has no `vkEnumeratePhysicalDeviceGroups` string: the game never asks for groups.

So on a machine with one discrete GPU the game takes it whatever the list's order; anything that only reorders
the list (the Windows per-application GPU preference, as far as it acts through the loader's order) can choose
only among discrete GPUs [inferred]. The two-GPU rig (an RTX 4080 and an integrated AMD GPU, D-044) runs the game
on the 4080 by this rule alone. The Game Pass exe could not be read here (its folder refuses reads); steering needs no game hook, only
the choice between moving and cutting the list rests on this rule.

## 2. When the headset's adapter is known

The presenter's OpenXR instance is made on the XR worker, which starts at the game's first `vkCreateSwapchainKHR`
(T-122), long after the game picked its device [static, the layer's code]. The earliest point with the LUID in hand
is a query of the runtime before the game's enumeration returns. With steering on, the layer asks the runtime
once, from the first `vkEnumeratePhysicalDevices` of the game's instance, when the list's order can change the
game's pick (section 3): a short-lived OpenXR instance
(the bundled loader, `XR_KHR_D3D12_enable` only), `xrGetSystem`, `xrGetD3D12GraphicsRequirementsKHR`,
`xrDestroyInstance`.

- Why not in `vkCreateInstance`, as T-082 planned: the Vulkan loader holds its lock around both calls
  (`loader_lock` in `vkCreateInstance` and `vkEnumeratePhysicalDevices`, loader 1.4.341 `trampoline.c`) [static],
  so neither place is outside it. The lock is recursive on its own thread; the query runs on the game's thread with
  the pass-through flag set, so a `VkInstance` the runtime makes from inside it passes through the layer and takes
  the lock again on the same thread. Asking at the first enumeration means a one-device machine never asks.
- A runtime that blocks in `xrCreateInstance` blocks the game's start-up for that long; a runtime or OpenXR API
  layer that waits there on another thread using the Vulkan loader would hold it for good [inferred]. This is why
  steering is off by default.
  A worker thread with a bounded wait was not chosen: a runtime that makes a `VkInstance` on that thread would wait
  for the loader's lock held by the game's thread, so the wait would always run out there [inferred].
- The launcher's own pre-launch probe (`OpenXrProbe.cs`, for the render size: a separate process with a timeout,
  killed when it runs out) could pass the LUID in the environment instead; that is the next step (section 5).
- With steering on, a process makes two OpenXR instances one after the other: the query's, then the presenter's.

## 3. What the layer does

`adapter_order::plan`, with the game's rule from section 1 [test]:

| Case | The runtime is asked | The game is given |
|---|---|---|
| Steering off (the default) | no | its own list, unchanged (the calls go straight down) |
| One device | no | its own list, unchanged |
| Exactly one device the game's rule takes (one discrete GPU of API 1.1 or later, however many other devices: most laptops and desktops with the integrated GPU on) | no | its own list, unchanged: no order changes the pick |
| The game's command line sets `r_physicalDeviceIndex` (the player chose; a reorder would change what the index names) | no | its own list, unchanged |
| No runtime, no headset or no D3D12 support at launch; no device with the headset's LUID | yes | its own list, unchanged |
| The game's rule already picks a device with the headset's LUID (two drivers can list one adapter) | yes | its own list, unchanged |
| Moved to the front, the headset's device wins the rule (a discrete GPU, or no other discrete GPU) | yes | the list with that device first |
| Otherwise (an integrated GPU drives the headset next to a discrete one; a device below 1.1) | yes | its own list, unchanged; logged; the presenter's LUID check refuses VR |

The last case is not cut to the headset's device alone: the game would then run on the integrated GPU, or fail on
a device it rejects (section 4), where a clear refusal is better. Of the devices with the headset's LUID, the target
is the first one the rule takes. Groups follow: the group holding the device first. One log line names every device (name, type, API
version, LUID), the headset's LUID, the runtime and how long the query took, what the game alone would pick and
what it is given. The presenter's LUID check at session creation is unchanged and still refuses a mismatch.

Rig knob `ETERNALVR_TEST_STEER`: `mismatch` steers to the first device without the headset's LUID and may cut the
list to it (the positive control for the refusal; whatever the switch says), `off` is the switch at 0.

## 4. Rig check

The check ran on a development PC with one GPU (an RTX 3080 Ti) and OpenXR-Simulator as the headset. A second Vulkan physical device came from Mesa's lavapipe
(mesa-dist-win 26.2.4, `lvp_icd.x86_64.json` with `vulkan_lvp.dll`, kept in a work folder, no system-wide ICD
registration), added for the game only with `VK_ADD_DRIVER_FILES` in the run's environment. The loader lists it
after the 3080 Ti (`vulkaninfo --summary`: GPU0 the 3080 Ti, discrete, 1.4.341; GPU1 llvmpipe, CPU, 1.4.354)
[rig].

Mesa's Dozen (`dzn_icd.x86_64.json`, the same release) was tried as well: it adds two Vulkan devices on Direct3D
12, one on the 3080 Ti (discrete, API 1.2, the 3080 Ti's LUID) and one on the WARP software adapter (Microsoft
Basic Render Driver: CPU type, a LUID of its own) [rig].

The runs in the table below used the first build, where steering was on by default and could cut the list; the
forced mismatch behaves the same in the current build. Every run is the QA suite's `baseline` scenario (Route S stereo on OpenXR-Simulator, Release build of the layer),
with the extra ICD and the knob in the game's environment only:

| Run | Devices the game's instance sees | Steering line (what the game is given) | Result |
|---|---|---|---|
| steer | #0 3080 Ti, #1 llvmpipe (no LUID) | headset LUID = #0's; the game alone picks #0: already picked, list unchanged | PASS; device and presenter on the 3080 Ti, LUIDs match |
| plain (no extra ICD) | #0 3080 Ti | "1 Vulkan device(s) ... nothing to steer"; the runtime is not asked | PASS; as before the change |
| dzn | #0 3080 Ti, #1 Dozen 3080 Ti (same LUID), #2 Dozen WARP | target #0 (the first with the headset's LUID): already picked | PASS |
| mismatch | as steer, `ETERNALVR_TEST_STEER=mismatch` | target #1: filtered, the game is given #1 (llvmpipe) | FAIL: the game crashes 8 s in (below) |
| override (control) | as steer, `ETERNALVR_TEST_STEER=off`, `+r_physicalDeviceIndex 1` | steering off, list unchanged; the game's own override takes llvmpipe | FAIL: the same crash |
| dznmis | as dzn, `ETERNALVR_TEST_STEER=mismatch` | target #2: filtered, the game is given #2 | the game's device is created on WARP (LUID 00000000:0000db63, the headset's is 00000000:06201e8d); every `vkCreateSwapchainKHR` fails (-1, then -3), so no presenter starts |

The runtime query took 88 to 176 ms (five runs) [rig].

**Positive control.** The steering takes effect: with `mismatch` the game made its device on the device it was
given (WARP through Dozen: `vkCreateDevice on 'Microsoft Direct3D12 (Microsoft Basic Render Driver)'`), and on
llvmpipe it crashed exactly as with its own `r_physicalDeviceIndex 1` and steering off: an execute access
violation at an address inside its "Please update your driver: ..." strings (RVA 0x2EB04B0), returning to
0x1CC367D, right after the device pick [rig]. The presenter's refusal message itself was not reached: neither
software device can run the game to a swapchain (llvmpipe stops right after the pick, in what the crash address
suggests is the game's driver capability check [inferred]; Dozen on WARP refuses the game's swapchain [rig]). The refusal stays checked only by its code path (`presenter_xr.cpp`, unchanged)
on this PC.

**Current build (off by default, reorder only)**, the same scenario with llvmpipe added [rig]:

| Run | Steering line | Result |
|---|---|---|
| switch unset | "steering off (the default; ETERNALVR_ADAPTER_STEER=1 turns it on): the list is left as it is, the runtime is not asked" | PASS; device and presenter on the 3080 Ti, one OpenXR instance (the presenter's) |
| `ETERNALVR_ADAPTER_STEER=1` | "the game takes #0 (its one discrete GPU of api 1.1 or later) in any order: the list is left as it is, the runtime is not asked" | PASS; as above |

## 5. Open

- **Next step: the LUID from the launcher.** Add `XR_KHR_D3D12_enable` to the launcher's killable probe
  (`OpenXrProbe.cs`) and pass the LUID to the game (for example `ETERNALVR_HMD_LUID`); steering then needs no
  OpenXR work inside the game, and the in-process query stays for rig launches only. That is the way to turning it
  on by default, with a launcher option.

- A real two-GPU run (the M4.5 row: a two-GPU machine with the per-application GPU preference cleared). The PC
  of section 4 cannot make the "moved to the front" case: its extra devices are CPU devices or share the 3080 Ti's
  LUID, and the game's rule never takes a CPU device over a discrete GPU; that case is covered by the unit tests
  only.
- The refusal message on a forced mismatch (section 4): needs a second adapter the game can run on.
- With `r_physicalDeviceIndex` set by the player on the command line the list is left alone; a value set elsewhere
  (a config file or the console) is not seen, and the game takes that index of the list it is given.
- Game Pass: the pick rule was read from the Steam exe only.
