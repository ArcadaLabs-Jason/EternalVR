# Both eyes as two views of one render: per-view storage, cost and risk

Static feasibility pass for the CPU project "both eyes as two render views in one render", in place of Route S
(eye L's render, then eye R's render nested in eye L's frame-end job; `perf-baselines.md`,
`perf-stereo-cpu.md`). Retail `DOOMEternalx64vk.exe`, Steam build 25216728; all addresses are RVAs in that build.
The game was not run for this page. It builds on the live E1 to E8 results in `stereo-reentry.md` section 12,
which found the per-view storage one crash at a time; this pass lists all of it statically.

Tags as in `stereo-reentry.md`: **[static-verified]** read in the code or data of this build, **[inferred]** a
reading the rig has to confirm.

## 1. Verdict

- **`r_allowParallelViewProcessing` does nothing in this build.** It is registered at 0x244BF0 (object
  0x66860E0, default 0, "when enabled job chains for multiple render views will be allowed to run
  simultaneously"), and the registration is the only code that touches the object. `r_maxRenderViews`, for
  comparison, has 12 readers through the same kind of reference. There is no dormant parallel path to switch
  on. [static-verified]
- **The render thread's job graph already treats views as siblings.** The per-view dispatcher 0x1C5CD00 kicks
  each view's job chain in turn, and each chain hangs between the same start and end nodes of the frame
  (section 4). Two views in one frame would therefore run their chains at the same time, with nothing that
  orders view 1 after view 0. [static-verified for the edges; inferred for the scheduling]
- **The storage is the blocker, and all of it can be listed.** Eight places hold per-view state for exactly
  one view (section 3), and all eight can be handled with the layer's mid-hooks and relocations (section 9).
- **The gain only comes with real overlap.** A stereo render costs about a mono frame, and the game frame
  outside the renders is small (`perf-stereo-cpu.md` section 2). Two views rendered one after the other in one
  frame would save little. The win needs the two chains to overlap, and whether they can do so without racing
  on the resources the views share (section 5) is the main risk.

## 2. How the one-view limit is built

`r_maxRenderViews` (object 0x6676110, `CVAR_INTEGER | CVAR_INIT`, default 1) sizes only two things: the
visibility contexts of each world (0x1D16F90) and the loops over the static per-view block (0x1CFD310,
0x1D00FD0, 0x1D04390, 0x1D04510) and the device context constructor (0x1C19C10). The storage those loops walk
is sized for one view at compile time. [static-verified]

## 3. Per-view storage for one view

| # | Storage | Size and layout | Access sites | Handling | Tag |
|---|---|---|---|---|---|
| 1 | Device context view slot | One `idDeviceContext`, 0x680 bytes, allocated by `idRenderSystemLocal` vtable slot 0x340 (0x1CC6440, `mov ecx, 0x680`) and stored at renderSystem + 0xF58 (global 0x66E3B88, 342 references in 81 functions). The view slot is inline at +0x8, stride 0xA8, one entry; +0xB0 onwards are other members (the backend frame counter is +0xB0). The slot holds the per-view images the constructor creates through 0x1C1CC40 and 0x1C20150 (`accumulationBuffer%d%d`, `accumulationBufferOpaque%d`, `viewColor%d`, `distortion%d`). | 28 sites in 18 functions (the 28th, 0x1CBB587, is the leaf selector 0x1CBB580 that has no unwind entry and was found by the spike), all `imul reg, viewIndex, 0xA8`, then `[reg + dc + field]`: 0x1C54B20, 0x1C5663D, 0x1C568DD, 0x1C56946, 0x1C56B2D, 0x1C56B7A, 0x1C56DB1, 0x1C57F4C, 0x1C5CBE0, 0x1C5F131, 0x1C5FB41, 0x1C5FFD7, 0x1C60368, 0x1C6089A, 0x1C609D5, 0x1C60FF0, 0x1C92CEF, 0x1C98CCA, 0x1CBB5B0, 0x1CBB6D0, 0x1CDF8B8, 0x1CDF9B2, 0x1CEEECE, 0x1CF0307, 0x1CFB7DF, 0x1D00841, 0x1D02053, plus the constructor loop 0x1C1A196. Almost all read a resource handle. Only two sites use slot 0 with a fixed offset: 0x1CDAC6E (+0x80) and 0x1CE3922 (+0x20). | Keep slot 0 in place; allocate slot 1 elsewhere and build it with 0x1C1CC40 and 0x1C20150. Every site adds the device context to `index * 0xA8`, so one mid-hook per site after the `imul` that adds the constant `slot1 - (dc + 8 + 0xA8)` when the index is 1 redirects all of them, including 0x1D02053, which multiplies `index + 1`. Enlarging the object instead is not possible: every member after +0xB0 would move. | [static-verified sites; inferred fix] |
| 2 | Static per-view block | 0x66EF4F0, stride 0xAF8, one entry; the next global starts at +0xAF8. History ping-pong and about a dozen history image handles. | 13 functions `imul reg, viewIndex, 0xAF8` (0x1CEEDF0, 0x1CEF430, 0x1CEF620, 0x1CEF930, 0x1CEFCB0, 0x1CFB7B0, 0x1CFBF90, 0x1CFC050, 0x1CFD310, 0x1D015D0, 0x1D01A50, 0x1D03460, 0x1D04390) through 14 RIP-relative references. | Relocate the block (done and live-tested in E4, commit 1a368f3), or the same delta hook as row 1. | [static-verified; E4 live] |
| 3 | World visibility contexts | One per `r_maxRenderViews`, allocated when a map loads (0x1D16F90). The Umbra context is `world->vfunc 0x2D8(viewIndex)`, read by the kick wrapper 0x1D19280. | the count read at 0x1D17147 | Raise the count while a map loads (done in E4, commit 870d56d, on main behind `ETERNALVR_STEREO_EXPERIMENT`). | [E4 live] |
| 4 | The world's render views | Each `idRenderWorldLocal` builds one `idRenderView` (0x29950 bytes); list inline capacity 1 at world + 0x5E71C0. | `RenderViewForIndex` 0x18E6C00 | Second view from the engine's own constructor, served through the world vtable (on main, `stereo_hooks.cpp`). | [E4 live] |
| 5 | Render thread job descriptor | 0x1CDCD90 builds the frame's descriptor on its stack: views at +0x28 as a one-element array, the view count at +0x30 directly after it, more fields to +0x88. It then calls 0x1C5CD00. | 0x1CDCD90 only (0x1FC bytes) | Replace the tail of 0x1CDCD90: call 0x1C5CD00 once per view with a one-view descriptor, and set descriptor + 0x36 on the first call so the view is flagged as "more views follow" (0x1C5CD9B). | [static-verified; inferred fix] |
| 6 | Dispatcher packet | 0x1C5CD00 fills a 0xA8-byte per-view packet in a local array at rsp + 0x30 in a 0xE0-byte frame: room for one packet; a second would overwrite the saved registers. | 0x1C5CD00 | Avoided by the one-view-per-call fix in row 5. | [static-verified] |
| 7 | Per-view render context | `idRenderThread` (vtable 0x2EB59F8, constructor 0x1CD8930) holds one render context pointer at +0x2F8; +0x300 is the next member. The holder constructor 0x1C59380 allocates 0x71B170 bytes through the engine allocator (`(*0x4271B98)->vfunc 0x20`, alignment 0x80), builds the context with 0x1C593E0 (no global state) and clears +0x71AE00 and +0x71AE08. | callers that pass `thread + 0x2F8` as an array: 0x1CDCF5B, 0x1CD840A, 0x1CD84C7, 0x1CD8507; one reader indexes it itself: 0x1CD7721 (`[rcx + rax*8 + 0x2F8]`) | A second context built the same way; a two-entry array of our own passed at the four call sites; the index moved to a cell of ours at 0x1CD7721. | [static-verified; live: built and used by the spike] |
| 8 | Occlusion-query state | The device context holds one pointer at +0x220 to a 0x2BC200-byte table built with the context (0x1C20DC0, next to the two `occlusion query pool %d` pools at +0x1F8 and +0x200); +0x228 is another member. | 8 reads `[rax + rcx*8 + 0x220]` with rcx = view index: 0x1C5C64D, 0x1C5C6A3, 0x1C5C9CD, 0x1C5DF48, 0x1C5F3E3, 0x1C5FE44, 0x1C607A6, 0x1C7FF78 | A second table of ours (the two counters at +0x2BC1F4 and +0x2BC1F8 set to 1 as the builder does); the query pools stay shared. | [static-verified; found by the spike] |

The render thread body 0x1CD8380 loops over the render list (stride 0x118) per view and calls 0x1C5DED0 and
0x1C5EF80 for each entry; those follow the viewIndex of the entry, so they are covered by rows 1, 2 and 7.
[static-verified loops; inferred coverage]

## 4. How the views' job chains are scheduled

0x1C5CD00 loops over the descriptor's views. For each one it calls 0x1C5DFD0 (per-view setup), builds the
packet, then 0x1C5C2F0 and 0x1C5CEA0. 0x1C5C2F0 resets the view's own job handles (packet + 0x18 and + 0x30) to
the null handle. 0x1C5CEA0 builds the view's chain of about a dozen jobs (the job manager at `*(0x5BF1270)`,
vtable slot 0x30 adds an edge) when descriptor + 0x35 is set, and otherwise runs the same stages inline. The
chain's first edge comes from the frame's start node (descriptor + 0x0) and its last edge goes to the frame's
end node (descriptor + 0x18), both shared by every view. No edge links one view's chain to another's.
[static-verified for the edges; inferred for the direction of each edge and for the scheduling]

So the dead cvar's behaviour appears to be compiled in: the chains are already allowed to run side by side.

## 5. Risks

1. **More one-view storage than the static pass found.** E4 found four places one crash at a time; this pass
   adds two more (rows 5 and 6) and the full list of row 1's sites. Structures reached through pointers, and
   scratch arrays sized for one view on other stacks, can hide from a pattern scan.
2. **Shared per-device-context resources raced by two chains.** 0x1C1CD80 creates, once per device context,
   the auto-exposure, ambient occlusion accumulation, light scattering, DOF, SSR and water simulation images and
   `umbraOcclusionBuffer%d` (`stereo-reentry.md` section 5). Under Route S the eyes use them one after the
   other; two views in one frame running at the same time would write them concurrently. The fix is either
   duplicating them per view (a second device context's worth of images for slot 1) or ordering the chains,
   and ordering them removes the gain.
3. **The second render context** (row 7) may be tied to a render thread or to a frame in flight.
4. **Everything built on Route S has to move.** The presenter, the per-eye hooks, the UI layer, per-eye TAA,
   keep-prev, the object-motion fixes, motion-vector capture and the view-overlay fix all assume two render
   frames per tick. Some would no longer be needed: each view keeps its own TAA accumulation and ping-pong (rows
   1 and 2), its own previous matrices (0x1CE2340 runs per `idRenderView`), and objects move once per frame for
   both eyes, which is the cause of the eye-R object-motion work under Route S. The rest needs porting and
   headset tests.
5. **GPU.** One render with two views does not reduce GPU work. On a GPU-bound rig the tick does not improve.

## 6. Payoff bound

From `perf-stereo-cpu.md` (the second test PC, 1280 x 1400): stereo 143 ticks/s (7.0 ms), mono 275 frames/s (3.6
ms), and a stereo render costs about 25% more than a mono one. [measured]

- Views rendered one after the other in one frame: saves only the per-frame work done twice under Route S (the
  world jobs, the frame start and end, one submission); the profile puts that at a small share. Estimate: 0 to
  10%. [inferred]
- Views fully overlapped: bounded by one render plus the shared frame work, about 4 to 5 ms, or 200 to 250
  ticks/s (+40 to 75%), if the 8 workers have room for both chains. Measured utilisation is about 4.8 of 8
  cores in stereo, so the overlap would be partial. [inferred]

## 7. Effort

| Stage | Work | Estimate |
|---|---|---|
| A. Two slots, both views render (simulator) | rows 1 to 7: 27 site hooks + slot 1 construction, block relocation from 1a368f3, the E4 hooks already on main, descriptor rewrite, second render context | 3 to 5 days; row 7 and unknown crashes dominate |
| B. Parallel correctness | per-view copies of the shared images of risk 2, or a measured decision to order the chains | 2 to 4 days |
| C. Layer port and headset | presenter halves, per-eye hooks, UI layer, TAA features, rig and headset rounds | 4 to 7 days plus headset time |

About two to three weeks in total. The chance of a stop somewhere in A or B (row 7, or risk 2 forcing ordered
chains) is high, around even.

## 8. Cheaper steps first

1. **Measure the bound before building.** Stage timers (the layer's `cpu:` split) for eye R's nested render
   against a mono frame, and the serial sampler (`perf-stereo-cpu.md` section 7) on eye R's frame, give the
   share of eye R that is per-frame work. Under a day.
2. **Spike stage A to the first frame, time-boxed to two days.** On the simulator behind
   `ETERNALVR_STEREO_EXPERIMENT`: the row 1 delta hooks, the row 2 relocation, the row 5 rewrite, and the
   second render context. If both views render once, the rest is engineering. If row 7 or a new blocker stops
   it, stop there.

## 9. Spike, live results so far (the second test PC, OpenXR-Simulator, 2026-09-28)

Branch `multiview-spike`, `ETERNALVR_STEREO_EXPERIMENT=two-views` with `ETERNALVR_VIEW_SLOTS=1`; the sites are
RVAs, used only when the PE timestamp is build 25216728's.

- From the game's `vkCreateInstance`: the static block moved to two entries and `r_maxRenderViews` set to 2 in
  memory. The renderer's start-up (0x1CD0A40) initialises the static block (0x1CFD310) before it builds the
  device context (vtable slot 0x340), so both have to be in place that early.
- The device context constructor's own slot loop builds slot 1 into memory of ours (the hook on its `imul`
  moves the address). The slot builder rebuilds it on every device context resize, as per-eye TAA does for its
  eye R slot. Logged: slot 1 fully populated next to slot 0.
- View 1's render context built; the dispatcher runs once per view; the render thread's per-view calls get a
  two-entry array.
- Result: the renderer starts with two views, both views latch (view 1 with view index 1), and the first
  two-view frame is kicked. Crash 1 (a null pass pointer from the leaf selector 0x1CBB580) was row 1's 28th
  site. Crash 2 is open: 0x1C31021 in the descriptor-set bind 0x1C30D00, a null `[command context + 0x120]`,
  reached through 0x1C32500 from 14 call sites.
