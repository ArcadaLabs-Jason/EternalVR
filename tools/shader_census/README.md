# Stereo shader census

Counts what a multiview stereo route (`MultiviewTransform`, T-004) would have to patch in the game's
shaders, following DECISIONS T-049, T-051, T-053, T-070 and T-105. Two parts: an opt-in dump in the
layer, and `census.py`, which classifies the dumped SPIR-V and joins it with the draw log.

## 1. Dump (layer, in the game)

Set these in the game's environment (`tools/rig/launch-ht.ps1 -ExtraEnv NAME=value`):

| Variable | Default | Meaning |
|---|---|---|
| `ETERNALVR_DUMP_SHADERS` | unset (off) | output folder; created if missing |
| `ETERNALVR_DUMP_SKIP_FRAMES` | 0 | presents to wait before the draw log starts |
| `ETERNALVR_DUMP_FRAMES` | 60 | presents the draw log covers |

Output:

- `modules/<fnv1a64>.spv`: every shader module the game creates, once per distinct code (also SPIR-V
  passed inline to a pipeline stage);
- `pipelines.jsonl`: per pipeline, its stages (module hash, entry point, specialization constants),
  layout and pipeline libraries;
- `layouts.jsonl`: descriptor set layouts (binding, type, count, stages) and pipeline layouts (set
  layouts, push-constant ranges);
- `sets.jsonl`: descriptor set allocations and buffer descriptor writes (buffer, offset, range),
  capped at 200000 lines;
- `drawlog.jsonl`: every pipeline bind, descriptor set bind (with dynamic offsets), push-constant update
  (values, up to 256 bytes), draw, dispatch and secondary-buffer execution the game records in the
  window, tagged `f` (presents before it was recorded) and `cb` (command buffer).

Shader modules are created while a map loads, so start the game with the variable set and load e1m1;
for the draw log, skip to a frame in gameplay (at about 140 frames per second, `SKIP_FRAMES` 4000 is
about 30 s). The dump only observes Vulkan calls; outside the window the recording hooks pass through
after one atomic load. It is off unless the variable is set, and then only the game's device is logged.

Limits:

- A draw's frame is the present count when it was *recorded*; work recorded ahead for the next frame is
  counted in the frame being recorded then.
- Pipeline and module handles can be reused after the game destroys them (destruction is not logged);
  the census takes the last definition of a handle.
- Stages given only as a module identifier (`VK_EXT_shader_module_identifier`) show as `id:<hex>` with no
  code. Push descriptors, descriptor buffers, mesh-shader and ray-tracing commands are not logged.
- Indirect draws are one record each (`count` or `max` is the API argument, not the GPU's draw count).

## 2. Census (offline)

```
python tools/shader_census/census.py <dump folder> [--out DIR] [--frames FIRST:LAST]
                                     [--view-members members.json] [--disasm]
```

Writes `census.json` (every module's classification, the shares, per-module draw counts) and
`census.md` to `<dump folder>/census` (or `--out`). A folder of `.spv` files alone also works (no
shares). `--disasm` runs `spirv-dis` from the Vulkan SDK (`VULKAN_SDK`, `E:\VulkanSDK\1.4.357.0\Bin` or
`PATH`) on every module that needs a semantic patch; the classification itself needs no SDK.

Per module: stages; whether `gl_Position` is written from a matrix read from a uniform, storage or
push-constant block (the `gl_Position = C_e * gl_Position` patch); matrix reads in any other stage;
`gl_FragCoord` uses split into bin lookups (a buffer index or texel-buffer fetch) and screen fetches;
`GlobalInvocationID` compute; vec4 outputs other than `gl_Position` written from a matrix product
(previous-clip candidates); and the image declarations that promotion to arrays would change
(T-052, runtime arrays = bindless).

A module needs a **semantic patch** (T-105) when a non-position stage reads view constants, when
`gl_FragCoord` feeds a bin lookup, or when a position stage writes another clip-space varying. The
report gives the two T-105 shares for draws (and the same for dispatches): modules needing a semantic
patch over the distinct modules bound by draws, and draws whose pipeline holds such a module over all
draws, with counts and denominators. Either above 15.0% means synchronized sequential stereo is
reweighed.

**View constants.** Without `--view-members`, any 4x4 or 4x3 matrix (or 3-4 `vec4` array) read from
a block counts, which is an upper bound: object, bone and light matrices look the same. After a live
value match (T-050) has located the view constants, pass them as
`[{"set": 0, "binding": 1, "offset": 0, "size": 64}, {"storage": "push", "offset": 0}]` and only
reads overlapping those members count. The data-flow is a heuristic over SPIR-V: it follows values
through local variables and function calls, not through memory the shader writes and reads back.

Tests: `python -m unittest discover -s tests -t .` from this folder (run by `ctest` as
`evr_shader_census_tests`). They build their SPIR-V by hand; with the Vulkan SDK present, `spirv-val`
also checks those modules.
