# Per-view jobs that touch engine objects that exist once (Parallel Eye Rendering)

Static analysis of the retail `DOOMEternalx64vk.exe`, Steam build 25216728. The game was not run for this
document. All addresses are RVAs. It builds on `vk-pools-per-context.md` and on the spike branch
`mv-spike-0930` (78ffd02), whose `view_redirects.cpp` already gives view 1 its own command context slots, runs
skinning and the blended BLAS / TLAS jobs for view 0 only, serializes job `0x1C58050`, keeps view 1's frame
parity apart (`0x66E2EA4`), orders the light and decal binning (scratch `0x66EFFE8..0x66F035F`), serializes the
render target manager (`[0x39B28A0]`) and caps the texture streamer's gather.

Question: which per-frame jobs a VIEW runs (reached from the render-view jobs `0x1CFA640`, `0x1D03460` and
`0x1C5CEA0`) touch an object that exists once, not per render context; for each, whether it is safe or racy,
the spike-style fix, and which of the spike's cvars gate it.

Method: a reachability walk from the three roots (direct calls, jumps, and function pointers taken with `lea`
or read from job tables such as `0x39A1D50` and `0x39AD3E0`), over whole functions (all unwind chunks), 4 to 5
levels deep: 668 functions. Then every reference to the async contexts, the named objects and every global
those functions write. Confidence is **high** where the code was read, **medium** where the role is inferred
from the access pattern only.

## 1. Answer

1. **With the spike's cvars the eight async contexts do not exist**, so none of the async-context rows below can
   race there. `r_enableAsyncCompute` is read once, in device setup `0x1CC34D0` (at `0x1CC3998`): the async
   queue family `[0x667EB68]` is set only when the cvar is at least 1 with vendor 2, at least 2 with vendor 1 and
   a device flag, or at least 3 (`0x1CC3A3B`, `cmovne`); with 0 it stays -1, `0x1CD0A40` skips creating the
   async work objects (`je 0x1CD0DED` at `0x1CD0BF7`), and `0x667F1B8..0x667F1F0` stay null. Every use below is
   behind a flag that also needs that family (for example `0x1C5E15A`). **High.** If a run saw recording into
   `0x667F1C8`, it ran without `+r_enableAsyncCompute 0` taking effect at device creation; worth checking the
   command line of that run.
2. **The ray tracing acceleration structure jobs are not gated by `r_useRayTracing`.** `0x1C5CEA0` sets up
   their blocks (render context `+0x6FC858`, `+0x701888`, `+0x7038C0`) for every view with no check, and the jobs
   read only `r_skipBlendedTLAS` (`0x1C58D30`, `0x1C58EC0`; `0x1C59160` reads no cvar) and the static
   blended-BLAS pool `0x5D63F20` (`0x1C58D30`). `r_useRayTracing` only sets the global byte `0x667EC51`
   (`0x1CB9EE0`), which none of these jobs reads. So `+r_skipBlendedTLAS 1` alone leaves `0x1C58D30`'s pool and
   the opaque TLAS job `0x1C59160` running for both views; the spike's view-0-only list is the right fix.
   **High.**
3. **Shared state the spike does not handle yet** (section 3): the Umbra visibility query's statics (written
   per view by `0x1D18BC0`, "Camera outside Umbra view volumes"), deferred-passes statics (`0x1C70CE0`,
   `0x1C72480`), post-process statics (`0x1C944C0`, `0x1C976B0`, `0x1C9B5D0`, `0x1C9E100`), and a few scalars in
   the view copy `0x1C54650` and the no-world setup `0x1CDF6E0`. Any of these is a candidate for the remaining
   hang or crash.

## 2. The async contexts and the other named objects

"Records" = the job copies the view's parm state (`[ctx+0x100]`, 0xF6A0 bytes) into the async context's and
records commands on it: two views at once race on its command pool and parm state. "Waits" = `0x1C35570`
(graphics context, async context): the graphics chunk waits on the async context's semaphore; on the async
context it only sets `+0x1CB = 1` and reads its parity (idempotent). "Signals" = `0x1C34690`: the async context
records which graphics context and chunk wait for it (`+0x248`, `+0x250`): one field, last view wins.

| Job (path from a root) | Object | Access | Gate (flag, and the cvar behind it) | Safe? | Fix | Conf. |
|---|---|---|---|---|---|---|
| `0x1C6F9C0` (`0x1C5AF10` opaque dispatcher `<- 0x1C5CEA0`) | Deferred Passes Prepare `0x667F1C0` | records, signals | `r_asyncDeferredPassesPrepare`, `r_asyncLightScattering` (read in the job) and the async family | racy | view 0 only, or per-view async context | high |
| `0x1C70CE0`, `0x1C70420`, `0x1C6FF70` (under `0x1C6F9C0`, `0x1C62CE0`) | same | pick the async context or the graphics one, then record | same | racy when async | as above | medium |
| `0x1C6FCC0` | same | waits | same | safe | none | high |
| `0x1C927D0` Post Processes job (`0x1C5BBA0 <- 0x1C5CEA0`) | Post Processes `0x667F1E0` | records when its flag byte is 1 | `r_asyncPostProcess` (read in `0x1CDE740`: 2, or 1 with vendor 2) and the async family | racy | view 0 only is wrong here (both views need post): serialize the job, or give view 1 the graphics path (flag 0) | high |
| `0x1CDF6E0` (`0x1C572A0 <-`) | Post Processes | uses its parm state when the flag is set | as above | racy when async | as above | medium |
| `0x1C545C0` (`0x1C5AF10 <-`) | Post Processes; profile end | `0x1C35630` on it; `0x1C22190` | as above | profile: safe (atomic) | none | high |
| `0x1C939A0` TSSAA job (`0x1C5BBA0 <-`) | TSSAA `0x667F1E8` | records | `r_asyncPostProcessTSSAA` (`0x1CDE740` sets `+0x459`; also `0x1CD8380`) | racy | serialize, or view 1 on the graphics path | high |
| `0x1C5F1B0` (`0x1C575F0 <-`) | GPU Particles `0x667F1C8` | stores it in rc`+0x6A7DE8` and uses its parm state; particle jobs record on it | rc`+0x4D9C92` = `r_gpuParticleUseAsyncCompute` and family (`0x1C5E150`); particles skipped with rc`+0x4D9C93` = `r_skipGPUParticles` (`0x1C5E17A`) | racy when async | view 0 only (the spike's fix) | high |
| `0x1C2AEE0` (`0x1C62040`, `0x1C91360`, `0x1C709D0`) | GPU Particles | waits | same | safe | none | high |
| `0x1C222C0` (`0x1C5A750 <-`) | GPU Culling `0x667F1B8` | stores into it (`0x1C31CD0`: `+0x148`) and records | `[[x+8]+0x35]`, set from `r_initGPUTriangleCulling` (`0x1C228E0`) and the family | racy when async | view 0 only for the culling dispatch | medium |
| `0x1C72CF0` (`0x1C59A50 <-`) | GPU Culling | waits | same | safe | none | high |
| `0x1CE2A30` (`0x1C5AF10 <-`) | Water `0x667F1D8` | loads it for the water pass | `r_asyncWater` (read in `0x1CE3A90`) and the family | racy when async | view 0 only for the simulation, or serialize | medium |
| `0x1C5CEA0` (the AS block) | Acceleration Structures `0x667F1F0` or category 4 `[0x667F098]` | the AS jobs record on it | rc`+0x119` (the async AS flag) | category 4: per-view slot in the spike; async: racy | view 0 only (done for the AS jobs) | high |
| `0x1C91360`, `0x1C709D0` | Acceleration Structures | wait | rc`+0x119` | safe | none | high |
| `0x1C58D30` blended BLAS | static pool `0x5D63F20` | writes | `r_skipBlendedTLAS` only | racy | view 0 only (done) | high |
| `0x1C58EC0`, `0x1C59160` TLAS | the view's TLAS block | records on the AS context | `r_skipBlendedTLAS` (`0x1C58EC0` only) | racy on the context | view 0 only (done) | high |
| profile blocks `0x1C22030` / `0x1C22190` (via `0x1C545C0`) | counter `0x6676290`, 3 pools of 0x80 | `lock xadd` | none | safe; one more timestamp per view | none | high |
| GPU timer queries (`[0x66E3B88]`, 58 functions on the view path) | 2 pools of 0x1000 | not traced | | unknown | check the index allocation if a timer overflow shows up | low |
| image streamer `0x6B26F60` (`0x1D5DBA0`) | its 3 pools | not reached from a view | | frame-level | none | high |
| async work init `0x1BF82B0` | | device setup only | | | | high |

## 3. Globals written on a view's path that the spike does not handle yet

Every direct write (`[rip + x]` destination) in the 668 functions, less the ones the spike handles and the C
runtime's. "Racy" means two views run the writer at once with no lock; what the value is used for is known only
where named.

| Global | Writer (path) | What | Fix | Conf. |
|---|---|---|---|---|
| one global object (param`+8`, also held at `0x681B800` / `0x681B808`) | job `0x1D2D3B0` (table slot `0x39B3CB0`, queued at `0x1D2EE2D` in `0x1D2E9F0`), once per view | clears the object's lists (`0x1D549B0`), copies the view's origin and settings in (`0x1D54A00`) and reassigns the list at `+0x178` (`0x6031D0`, which frees the old buffer when the capacity differs): on a new world's first frames both views freed that buffer (heap failure type 8 at `0x1DBF341 <- 0x603216 <- 0x1D54ACC <- 0x1D2D40A`, rig run fh10). Found on the rig, not by this walk (see the note below) | serialized (`kSerialJobs`, mv-spike-0930 0b8ee2d) | high |
| `0x66F0EA0`, `0x66F5880..0x66F58C0` | `0x1D18BC0` ("Camera outside Umbra view volumes") `<- 0x1D19280 <- 0x1C54650` | the Umbra visibility query's state, written per view | serialize the Umbra query per view, or per-view copies of these statics | medium |
| `0x39A58B0..0x39A58B4`, `0x66894B8..0x66895B0` (16 entries of 0x10) | `0x1C70CE0 <- 0x1C6F9C0 <- 0x1C5AF10` | deferred passes prepare: a static table rebuilt per call | serialize `0x1C70CE0`, or view 0 only if its output is frame-level | medium |
| `0x39A58B8..0x39A58CB`, `0x66895C0` | `0x1C72480 <- 0x1C71630 <- 0x1C54650` | deferred passes, from the view copy | serialize | medium |
| `0x66E0D84`, `0x66E1190` (bytes set to 1) | `0x1C944C0 <- 0x1C96D40 <- 0x1C62CE0 <- 0x1C5A280` | post-process "done" flags | likely benign (idempotent), check where they are reset | medium |
| `0x66E0D80` | `0x1C9B5D0 <- 0x1C9B560 <- 0x1C939A0` (TSSAA) | a post-process scalar | serialize with the TSSAA job | medium |
| `0x66E12A0`, `0x66E12A4` (floats) | `0x1C976B0 <- 0x1C988E0 <- 0x1C54650` | a post-process value (exposure-like) per view | per-view copy, or view 0 writes and view 1 reads | medium |
| `0x66E1358` | `0x1C9E100 <- 0x1D0C850 <- 0x1CE8C30 <- 0x1C58EC0` | written under the TLAS job | covered once `0x1C58EC0` is view 0 only | medium |
| `0x6686EB8` | `0x1C54650` (at `0x1C55EA9`), the MAIN to VIEW_MAIN copy | a scalar per view | per-view copy | medium |
| `0x6687038` | `0x1C62CE0` (at `0x1C62EEA`) `<- 0x1C5A280` | a scalar per view | per-view copy | medium |
| `0x66EB040` | `0x1CDF6E0` (at `0x1CDFCE3`) `<- 0x1C572A0` | a scalar in the no-world setup | per-view copy | medium |
| `0x39AD3C0` (float) | `0x1CEE520 <- 0x1C54650` | a float from the view | per-view copy | medium |
| `0x667E13C` (`inc`) | `0x1C3AB90 <- 0x1C35710 <- 0x1C91360` | a plain counter incremented per call | make it view 0 only or accept a lost count | medium |
| `0x427D2F0` | `0x3E9EA0 <- 0x1941FF0 <- 0x1C57A20` (Begin Frame) | engine utility state | check what `0x1941FF0` is | low |

**Limit of the walk:** it follows direct calls, jumps and function pointers in code and job tables, not virtual
calls (`call [reg + offset]`). Job `0x1D2D3B0` above is queued from `0x1D2E9F0`, which the walk did not reach, so
jobs queued behind a virtual call are missing from sections 2 and 3; a runtime job trace per view (the job
manager's add at `[0x5BF1270]` vtable `+0x20` / `+0x30`) would complete the list.

The walk also listed `0x667EB38..0x667EC1B` (the frame-end code `0x1C39DD0`, `0x1C3A080`) through `0x1C35400`:
that edge is an artifact (`0x1C35400` is a lone `jmp 0x1C666F0` with no unwind entry, and the walker read the
padding after it). The frame end is not on a view's path.

## 4. Which spike cvar gates what

| Cvar | Where read | Gates |
|---|---|---|
| `r_enableAsyncCompute 0` | device setup `0x1CC34D0` only | the async queue family and so all eight async contexts: none exists, none of section 2's async rows can run |
| `r_skipGPUParticles 1` | `0x1C5DFD0` (rc`+0x4D9C93`), `0x1953D90` | the view's GPU particle jobs |
| `r_useRayTracing 0` | sets `0x667EC51` (`0x1CB9EE0`) | ray traced passes that read that byte; **not** the AS jobs `0x1C58D30`, `0x1C58EC0`, `0x1C59160` |
| `r_initGPUTriangleCulling 0` | `0x1C228E0` | GPU triangle culling setup, and so the GPU Culling async path |
| `r_skipBlendedTLAS` | `0x1C58D30`, `0x1C58EC0` | the blended BLAS pool and blended TLAS work; not the opaque TLAS job `0x1C59160` |

## 5. Scripts

On the second test PC, `<workspace>\analysis\scripts\`: `reach.py` (the walk: calls, jumps, `lea` and
job-table pointers, all unwind chunks of a function), `sharedview.py` (named objects), `cand.py` (the code around
each async-context use), `gwrites.py` / `gdetail.py` (globals written on the view path and their writers),
`backtrack.py` (who queues a job), `cvarobj.py` (cvar objects by name).
