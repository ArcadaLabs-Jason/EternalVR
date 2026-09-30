# Which Vulkan pools a command context owns (for Parallel Eye Rendering)

Static analysis of the retail `DOOMEternalx64vk.exe`, Steam build 25216728 (the exe of `engine-facts.md`).
The game was not run for this document. All addresses are RVAs in this build. Question from the Parallel
Eye Rendering spike: which command contexts own which `VkCommandPool`, query pool and descriptor pool, which
code creates them, and how a context picks one, so that two views recorded at once never share an externally
synchronized Vulkan object.

## 1. Answer

- **Command pools are per context.** Each of the 21 graphics command contexts owns one `VkCommandPool` and
  records from it alone; the eight async work objects and the image streamer own theirs. A command pool is
  externally synchronized in Vulkan, so **two threads must never record on the same context at once**. The
  contexts are process-global (a table of 13 groups), not per view: two views that run the same jobs record
  on the same contexts. That was the spike's "VIEW_MAIN slot 0 recorded by two threads" bug (its fix B),
  and it holds for every group a view job touches.
- **Descriptor sets are safe to allocate from two threads.** One global frame allocator hands out indices
  with an atomic counter and stripes them over 8 banks, each with its own `VkDescriptorPool` behind its own
  `CRITICAL_SECTION`.
- **Queries are global pools with atomic indices.** GPU profile blocks (at most 0x80 timestamps a frame,
  fatal when exceeded) and GPU timers (0x1000 a frame) share pools across contexts; a query index is taken
  with `lock xadd`, and the commands go on the calling context's current command buffer.

## 2. Command pools and command buffers

| Owner | Created by | Pool handle | Command buffers | Queue |
|---|---|---|---|---|
| Graphics command context (21 of them, section 3) | `0x1C663A0`, called once per context from device setup `0x1CD0A40` (loop at `0x1CD0E50`) | `ctx+0x438` ("Graphics CB pool (%s)", flags `RESET_COMMAND_BUFFER`) | 4 chunks x 2 frames at `ctx+0x440` (`+0x440 + chunk*16 + parity*8`, "Graphics CB %d:%d (%s)"), plus the same for the pre deferred barrier buffers at `ctx+0x488` | graphics family `[0x667EB50]` |
| Async work object (0x258 B): GPU Particles, Deferred Passes Prepare, GPU Streaming, GPU Culling, Water, Post Processes, TSSAA, Acceleration Structures | `0x1BF82B0`, from `0x1CD0A40` at `0x1CD0C27`..`0x1CD0DDC`, only when `[0x667EB68] != -1` | `obj+0x1D0` | allocated at `0x1BF83F2` | the async family the setup passes (2 or 3) |
| Image streamer (`0x6B26F60`) | `0x1D5DBA0`, from `0x1D5BE60` (called from device setup `0x1CD0F34`) | `+0x123C0`; `+0x123E8` ("IS async compute CB %d"); `+0x12410` ("IS copy CB %d") | allocated at `0x1D5DCE9`, `0x1D5DDA9`, `0x1D5DE69` | graphics, async compute, transfer |
| Unknown | `0x1D1E140` (no direct caller: reached through a pointer) | `+8` and one more | `0x1D1E534`, `0x1D1E6F4` | |

**How a context picks its command buffer.** `ctx+0x118` points at the current one. At the end of every frame
`0x1C39DD0` flips the frame parity `[0x667EBD0]` (0 and 1) and sets every context's `+0x118` to
`ctx+0x440 + parity*8` (chunk 0). `0x1C345E0` starts the next chunk: it ends the open one if `+0x228` says so
(through `[0x667EE40]`), increments the chunk index `ctx+0x434` and sets `+0x118` to
`ctx+0x450 + parity*8 + old_chunk*16`, then begins it. `0x1C34690` registers a submission point in
`ctx+0x4C8 + chunk*8` and starts a chunk. So the four slots are sequential chunks of one frame on one
context, **not** per-view slots. Nothing here tells the thread; two threads that call these on one context
race on `+0x118`, `+0x434` and the pool.

## 3. The graphics command contexts

Table `0x667F018`: 13 groups of 0x20 bytes (4 pointer slots each). Count per group, from the table at
`0x2EB3DB0` (read the same way by `0x1C39DD0`, `0x1C32860` and `0x1C572A0`, from their own copies at
`0x2E99930` and `0x2E9F490`): `1, 1, 1, 1, 1, 1, 1, 4, 1, 4, 4, 1, 1`, 21 in all. Each context's name is at
`ctx+8` (the `%s` of its command buffer names). Code that uses a single group:

| Group (slot address) | Count | Used by |
|---|---|---|
| 0 (`0x667F018`) | 1 | loops over all contexts: `0x1C32220`, `0x1C32860`, `0x1C32DD0`, `0x1C34FE0`, `0x1C39DD0`, `0x1C49560` |
| 1 (`0x667F038`) | 1 | the view jobs: `0x1C54650` (the MAIN to VIEW_MAIN copy), `0x1C572A0` (no-world Begin Frame), `0x1C575F0`, `0x1C57A20`, `0x1C5C2F0`, `0x1C5CEA0`, `0x1C5EF80` |
| 2 (`0x667F058`) | 1 | `0x1C572A0`, `0x1C57A20`, `0x1C5A750` |
| 3 (`0x667F078`) | 1 | `0x1C5AF10`, `0x1C5CEA0` |
| 4 (`0x667F098`) | 1 | `0x1C5CEA0`, a leaf at `0x1C590FF` |
| 5 (`0x667F0B8`) | 1 | `0x1D22A80`, `0x1D5BF70` |
| 6 (`0x667F0D8`) | 1 | `0x1C5CEA0`, `0x1C5F1B0` |
| 7 (`0x667F0F8`) | 4 | `0x1C5F810` |
| 8 (`0x667F118`) | 1 | `0x1C5F1B0` |
| 9, 10 (`0x667F138`, `0x667F158`) | 4, 4 | `0x1C60450`, `0x1C60050`, `0x1C575F0`, `0x1C5AF10`, `0x1C91430`, `0x0DAA840` |
| 11, 12 (`0x667F178`, `0x667F198`) | 1, 1 | `0x1C572A0`, `0x1C60050`, `0x1C60BF0`, `0x1CCE780` |

At frame begin `0x1C572A0` copies group 1's parm state (`[ctx+0x100]`, 0xF6A0 bytes) into every context of
groups 2 to 12 (except 5), after calling `0x1C34400` and `0x1C35400` on each (not traced further).

## 4. Descriptor pools

| Owner | Created by | Pool handle | Picked how |
|---|---|---|---|
| Frame allocator (`idDescriptorSetFrameAllocator`) | page `0x1C17DA0` ("idDescriptorSetFrameAllocator Bank DescriptorPool"), created lazily from `0x1C15F20` | per bank `bank+0x28`; 8 banks of 0x780C0 B per page, up to 16 pages at `alloc+0x28 + page*8` | `0x1C15F20`: `lock xadd [alloc+0xA8]` gives an index (at most 0x10000 a frame); page = index >> 12, bank = index & 7. `0x1C185C0` then locks that bank (`0x3E4DE0`: `TryEnterCriticalSection`, else `EnterCriticalSection`) and calls `vkAllocateDescriptorSets` through `[0x667EE48]` |
| Same, reset | `0x1C184F0` resets every bank's pool (`vkResetDescriptorPool`), no direct caller | | |
| Other descriptor pools | `0x1C170B0` (`+0xB0`, from `0x1C14F60`), `0x1C23280` (`[0x5BF1380]+0x860E0`, device setup), `0x1C82210` (two: `+0x558140`, `+0x7F8228`, from `0x1C853B0`, `0x1C85610`), `0x1CC80E0` (a global, device setup) | | long-lived sets: not in the per-view path |

## 5. Query pools

| Pool | Created by | Handle | Use |
|---|---|---|---|
| GPU profile blocks: 3 timestamp pools of 0x80 | `0x1C22120` (device setup, and `0x1CDE500`) | `0x6676298 + frame*8` | begin `0x1C22030`, end `0x1C22190`: `lock xadd [0x6676290]` (more than 0x80 a frame is fatal: "idGPUProfileBlock: ran out of timestamps in the pre-allocated pool."), frame `[0x6676294]`, `vkCmdWriteTimestamp` on the caller's `ctx+0x118`. Callers: `0x1CD9750`, `0x1C545C0`, `0x1CD8380`; counter reset `0x1C22200` |
| GPU timers: 2 pools of 0x1000 | `0x1C20DC0` (from `0x1C19C10`) | `[0x66E3B88]+0x1F8 + parity*8` | reset by `0x1C32860` at frame end, on the command buffer of the context at `[0x66879E8]`; results read by `0x1C32BB0` and `0x1C33170` (`vkGetQueryPoolResults`) |
| Device pool of 0x400 | `0x1CD1630` in device setup | `device+0xC40` | reset by `0x1C32860` when `[0x667DF50]+8 == 0` |
| Acceleration structure compacted size | `0x1BF6150` (from `0x1BF05D0`) | `obj+0x165FA0 + i*8` | "stream accel compacted size query pool %d" (ray tracing only) |

`vkCmdBeginQuery` is called once (`0x1C31C10`); `vkCmdEndQuery` from two leaf wrappers (`0x1C3281A`,
`0x1C32A75`).

## 6. What this means for two views at once

1. Give each view its own contexts, or never let two threads use one. The jobs pick contexts from the global
   table, so a second view needs either a second set of contexts (another 21 created by `0x1C663A0`, each with
   its own pool) swapped in for that view's thread, or the view jobs serialized per context. The chunk index
   `+0x434` and `+0x118` are per context, so a second set also keeps the chunk order of each view apart.
2. Descriptor sets need nothing: allocation is atomic and bank-locked. Two views take up to twice as many
   indices of the 0x10000 a frame allows.
3. Timestamps: the profile blocks are frame-level (three call sites). They stay under 0x80 unless a profile
   block ends up running once per view, which would double the count and could reach the fatal limit.
4. The query resets in `0x1C32860` and the parity flip in `0x1C39DD0` run once a frame: they must stay on one
   thread, after both views have finished recording.

Scripts: the analysis scripts on the second test PC (`vkslots.py` Vulkan function slots,
`vkiat.py` imports, `vkrefs.py` references, `calls_to.py` direct calls, `createsites.py` create calls and
their handle, `scanpat.py` instruction scan). `calls_to.py` finds `E8` calls only; tail calls (`E9`) show up
in `pe_util.Image.xrefs` instead.
