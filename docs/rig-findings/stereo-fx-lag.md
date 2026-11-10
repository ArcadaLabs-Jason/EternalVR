# CPU particles and effects one tick behind in eye L under Route S

Burst captures of explosions, smoke and sparks showed eye L's render of tick N matching eye R's of tick N-1: every
CPU particle and effect sprite was one game tick behind in eye L. GPU particles and the scenery were not. RVAs are
in Steam build 25216728. Section 6 has the rig runs and what is still to check, section 7 the GPU particle
stages behind the e1m3 tiles.

0.1.34 shipped the fix on by default. Players' headset captures in e1m3 (Cultist Base, 2026-10-08) showed eye R
drawing the snow and some ice effects as tiles of what looks like their whole sprite sheet, while eye L was right.
The storm deck runs never showed it. 0.1.35 turns the fix off by default until that is understood. Section 7 has
the cause found by reading the code (particle systems with GPU particle stages) and the change for it. Section 8
has the ray-traced reflections (eye L's particles were missing from eye R's, fixed), section 9 the rig checks of
2026-11-08 after which the fix is on by default again (T-230).

## 1. Cause

Names: the world's particle vertex ring P is `[world + 0x25060]`; it has three slots of 0x68 bytes at P + 0x4D8,
the current slot's index at P + 0x7C8, the current and previous slots' mapped pointers at P + 0x7B8 and P + 0x7C0,
their fills at P + 0x7CC and P + 0x7D0, and a frame counter at P + 0x4D0. The world's particle light pool is
`[world + 0x25070]`: 0x80 lights at +8 and a count at +0x408.

1. The world-views pass (0x1C75CF0) runs the world job 0x18E5070 once per render. Its prepare 0x18E78D0 waits
   for the last render's deferred fill (`[world + 0x71DA58]`, 0x18E78EE), then advances the ring (call 0x1A0F5D0
   at 0x18E791C): the previous pointer and fill take the current slot's, the atlas allocator (+0x7D8) is reset,
   the index steps ((index + 1) % 3), the new current slot is opened (0x1BFC280) and the frame counter goes up.
   Further on it resets the light pool's count (call 0x1953D80 at 0x18E7ABD; its only caller).
2. The world job then queues the update jobs: particles 0x18E17A0 -> 0x1955150 per particle model, effects
   0x18E1420 -> 0x19511E0. Each checks the model's stamp (particles +0x61C at 0x1955248, effects +0x4528 at
   0x19512CC) against the ring's frame - 1. A model generated in the render before is bound to the previous slot
   ((index + 2) % 3) with the ranges that generation wrote (particles 0x1955670, effects 0x19512D8..0x19515F5),
   and its particle lights take pool slots (0x1955290: `lock xadd` on the count at 0x195539D, cap 0x80; the only
   writer besides the reset). Then the simulation steps by the view's time and new vertices are generated into
   the current slot (particles: call 0x1953D90 at 0x195526F; effects: from 0x195166E), the model stamped with the
   current frame on every path (0x19545ED, 0x19519AC) and the deferred fill entries counted at `[world +
   0x71DA50]` (0x195507B, 0x1951F77).
3. The world job also queues 0x18DF6A0 every render, whose show pass 0x19525D0 shows the pool's lights below the
   count (0x18D88E0) and hides the rest (0x18D8AF0); both mark the light changed for the light copy.
4. The frame-middle job (0x1CBA0A0 -> 0x1C75E60 -> world vtable 0x130, 0x18E64C0) queues 0x18E0CE0, which
   closes the previous slot (0x1A0F580: P + 0x7C0 = 0, the slot's open flag cleared) and starts the deferred
   fill (0x1955D20) of the current slot when the count is above 0.

So every render draws the vertices the render before it generated, and generates the ones the render after it
draws. Lens flares (0x1936415) and tracers (0x1A0E82A) allocate in the previous slot (0x1A0F4F0, through P +
0x7C0) and are drawn in the same render. In mono that is a one-frame lag nobody sees. Under Route S eye L's
render of tick N drew what eye R's render of tick N-1 generated, and eye R's drew eye L's of tick N: eye L was
one tick behind. The deferred fill (`r_useParticleGenJobs`, default 1) is not the cause: the next prepare waits
for it.

## 2. Fix: `src/vkcore/fx_sync_hooks.*`, `src/stereo_seq/fx_sync.*` (on by default; off from 0.1.35 until T-230; `ETERNALVR_STEREO_FX_SYNC=0` turns it off, `=count` only counts)

Eye R of a Route S tick leaves the ring, the light pool and the generation of what eye L generated to eye L. Four
mid hooks:

- **H1, the advance (RVA 0x18E791C, the `call 0x1A0F5D0`).** Eye R does not advance. The index, frame counter,
  fills and atlas stay as eye L left them; the previous slot's pointer, which eye L's frame middle zeroed, is
  opened again the way the advance opens a slot: P + 0x7C0 = 0x1BFC280(P + 0x4D8 + ((index + 2) % 3) * 0x68).
  Eye R resumes after the call (0x18E7921). Without the reopen eye R's flares and tracers would write through a
  null pointer.
- **H4, the light pool's reset (RVA 0x18E7ABD, the `call 0x1953D80`), in the same prepare.** It does what H1 did
  in that prepare (same thread, same world): a kept ring keeps eye L's count and eye R resumes after the call
  (0x18E7AC2); an advanced ring, or a prepare whose H1 did not act (no ring, the hooks not live yet), gets the
  engine's reset. Without it eye R's count was 0, eye R binds none of eye L's models, and the show pass hid all
  128 pooled lights in eye R: lightning-lit and explosion-lit surfaces were lit in eye L and dark in eye R.
- **H2, the particle generation (RVA 0x195526F, the `call 0x1953D90`).** For a model eye L generated in this
  tick eye R resumes at the update's epilogue (0x1955274).
- **H3, the effect generation (RVA 0x195166E).** For an effect eye L generated in this tick eye R jumps to the
  update's epilogue (0x195200C), the exit `r_skipEffectParticles` takes (0x19512B0).

"Eye L generated it in this tick" is its stamp equal to the ring's frame (`stereo_seq::fxGenerationFor`):
particles `[rcx + 0x61C] == [[r8] + 0x4D0]` (rcx is the model, `mov rcx, rbx`; r8 the update's ring pointer,
`mov r8, rdi`, the same `[rdi]` the stamp check reads), effects `[r15 + 0x4528] == [[[rbp + 0x28]] + 0x4D0]` (r15
the effect, `mov r15, rcx`; `[rbp + 0x28]` the update's r9, stored at 0x1951231 and not written again; `[r9]` is
the ring, `mov r8, [r9]` before the stamp check, read the same way where the generation stamps, 0x1951998). Any
other model runs the engine's code in eye R: a model eye L did not have in its list (seen by eye R only), whose
stamp is older. Eye R binds it when eye L of the tick before generated it (adding its lights after eye L's in the
pool), generates it into the current slot after eye L's allocations, and its frame middle queues the fill; eye L's
next render binds that. In mono such a model is drawn the same way.

Eye R's bind of eye L's models is skipped by the engine's own stamp check: eye L stamped them with the frame eye
R still sees, not frame - 1. So eye R draws the slot eye L drew, with eye L's binding, and both eyes show tick N-1,
the tick a mono frame shows. The pool's slots keep the lights eye L's bind wrote (the show pass only clears or sets
a light's hidden flag and marks it changed), so eye R's show pass shows eye L's lights unchanged. A light is never
added twice: the generation stamps the model on every path, so a model whose lights eye L added has the frame as
its stamp and eye R does not bind it. The next eye L advances and resets as the engine does.

One rule for every site (`stereo_seq::fxActionFor`): only a render whose chain is eye R (`seqChainEye()`, set
only inside the eye R render Route S's wrapper nests after eye L's, including a paired tick of
`ETERNALVR_ALTERNATE_EYES=auto`), while the multiplayer guard lets the layer touch the game. Mono frames,
alternate-eye renders and the engine's own chain run the engine's code; Parallel Eye Rendering does not install
Route S's hooks. When eye R is not rendered nothing changes: the next eye L advances, as in mono. The guard only
goes from allowing to not allowing, so within a render it can only stop the generation skips after H1 kept the
ring: eye R then generates into eye L's current slot, which the engine's own fill and bind handle, and the session
is mono from the next tick on. If the hooks go live inside an eye R render after its prepare advanced the ring,
no stamp equals the new frame and eye R generates everything, as the engine does.

Signature checks (`locate` in `fx_sync_hooks.cpp`): the prepare around the advance's call, with the engine's own
previous slot computation after it; the call's target (the advance, with its call of the opener at +0x38 and the
opener's whole code); the pool reset's call 0x1A1 bytes after it in the same prepare and its target's whole code;
the particle lights' pool load and `lock xadd` with the 0x80 cap; the show pass's call and its count read with
the same cap; the frame middle's close (0x1A0F580, its `jmp` to the closer 0x1BFD7A0), so a reopened slot is
closed again by eye R's own frame middle; the particle update's bind (0x1955670) and generation (0x1953D90) calls
by their prologues, the stamp check and the operands above, and its epilogue; the effect update from its start
to the stamp check, with `r_skipEffectParticles`' `jle` and `jmp` reaching the same epilogue (checked) and the
stamp check's `jne` reaching the generation (checked) before it. Any mismatch installs nothing and logs why. The
hooks go in with a gate closed (H1 last) and act only once all four are in: a failed install leaves them running
the engine's code.

## 3. Registers at each hook

- **H1.** At the call rcx is P (`mov rcx, [rdi + 0x25060]`, then `test rcx, rcx`, `je` past the whole ring
  block). From 0x18E7921 the prepare reloads P from `[rdi + 0x25060]` and sets eax, ecx, edx, r8 and r9 before
  reading them; rdi and rsi are callee-saved. Nothing reads what the advance returned.
- **H4.** 0x1953D80 is `mov dword [rcx + 0x408], 0`, `ret`: it changes no register, so 0x18E7AC2 sees the same
  registers either way. rdi is the world there as at H1 (`mov rdi, rcx` at 0x18E78D7, never written again).
- **H2.** The update returns nothing (its caller 0x18E1800..0x18E184E does not read rax; the engine's own early
  exits reach 0x1955274 with rax holding a cvar pointer or a flag). The epilogue restores rbx and rsi from `[rsp
  + 0x30]`, `[rsp + 0x38]` and pops rdi; rsp at the call equals rsp at 0x1955274.
- **H3.** rbp is the update's frame from its prologue (`lea rbp, [rsp + 0x30]`) to its epilogue and is not
  written in between; the epilogue checks the cookie at `[rbp + 0xB0]`, restores xmm6/xmm7 and rbx, rsi, rdi from
  rbp and sets `rsp = rbp + 0xE0`, so the stack the hook leaves does not matter. The alloca (0x195174D) comes
  after the hook. The update is a tail call of 0x18E1420's job and returns nothing.

## 4. Why it is safe

- **Slots.** The previous slot is written by eye L of tick N-1, drawn by eye L and eye R of tick N, and written
  again by eye L of tick N+2: two renders after its last draw, where mono has one, so more time for the GPU to be
  done with it. Eye L's deferred fill writes the current slot while eye R draws the previous one; eye R's prepare
  still waits for that fill.
- **Eye R's frame middle** closes the reopened slot again (the open flag is a bit the opener sets and the closer
  clears), sums the two fills for the quad count (`[RWD + 0x1362B0]`, eye L's values plus eye R's flares) and
  queues a fill only for what eye R generated itself (the count is reset by the prepare at 0x18E7909, only the
  generations add to it).
- **Flares of both eyes share the previous slot.** Eye R's flares and tracers go after eye L's (the fill at P +
  0x7D0 is not reset), so nothing either eye draws is overwritten. The slot holds 0x18000 vertices; an
  allocation past it fails cleanly (the engine's `Bump MAX_TRANSPARENCY_QUADS` warning, the flare not drawn).
- **The light pool.** Its count has no other reader or writer than the reset, the particle lights and the show
  pass (the world's set-up and tear-down aside), so keeping it changes nothing else in eye R's render. A pool
  past 0x80 lights stays capped as the engine caps it.
- **The simulation** steps once per tick for eye L's models, in eye L: eye R no longer moves their time stamps
  (+0x5FC), so eye L's next step covers the whole tick, as in mono.

What eye R loses, the same as mono:

- Particle lights are written once per tick, in eye L's bind; eye R shows them as eye L left them. Before the fix
  they were written in both renders.
- (Fixed, section 8.) The binds set each model's bit in the world's ray-traced particle mask, which a job clears
  every render: eye R's mask held none of eye L's models, so with ray tracing on they were missing from eye R's
  reflections. Eye R now keeps eye L's mask. (The read at 0x1953EE9 in the particle generation is of the view's
  own ray tracing gather mask, into the model's +0x5C8; eye R keeps what eye L's generation read there.)
- Sprites are built for eye L's view (the generation copies the view's origin and axes into the model,
  0x1953DC7..0x1953E40), as eye L drew sprites built for eye R's view before.

## 5. Log and switch

Every 10 s: `seq-fx: eye R reused eye L's particles N time(s) (P particle system(s), E effect(s)), light pool kept
K; eye R generated M / Q itself (eye L had not) and U GPU-staged particle system(s) eye L had (S GPU stage(s)
bound); eye L's particle systems with GPU particles W; generated by eye L A / B, eye R C / D (particle systems /
effects); ring advances eye L F, eye R G; GPU particle manager per render (emitter records / draw list / light
atlas): eye L r / d / a, eye R r / d / a (n / m GPU steps); ray-traced particle mask kept by eye R k, cleared eye L
l / eye R r` (the mask's part, section 8: k the eye R renders that kept eye L's mask, or would have with `=count`; l
and r the engine's clears; in steady stereo k is close to N and r is 0, with `=count` r is close to k). N counts eye
R prepares (one per world with a ring) that kept eye L's ring, K those that kept eye L's light count, P and E the
generations eye R left to eye L, M and Q the ones eye R ran itself because eye L had not generated the model, U the
particle systems eye L generated that eye R bound the GPU stages of and generated itself (section 7), S the GPU
stages it bound for them, W the particle systems eye L generated that eye R found GPU particles in (with the GPU
handling W equals U; with `ETERNALVR_STEREO_FX_SYNC_GPU=0` they are in P and U is 0), A to D the generations the
engine ran (C includes M and U, D includes Q), F and G the advances it ran (eye L's row includes mono and
alternate-eye renders). The GPU part is the averages per GPU particle step (one per world and render) of the
manager's emitter records, draw list 0 and light atlas list, read when the step starts, per eye of the frame end
that called or queued the step (section 7); it is missing when its hook did not check out. In steady stereo N and K
are close to F, G is 0, M and Q stay small (models only eye R sees), P + U and E are close to A and B, and eye R's
GPU averages are close to eye R's in a `=count` run (not to eye L's: eye R's generation runs right after eye L's, so
its counts can differ). In mono N, K, P, E, M, Q, U, W and the eye R rows are 0. With `=count` nothing is reused: N,
K, P, E and U count what would have been, M and Q what eye R would still generate, C, D and G count every eye R
generation and advance, and the line ends with `(ETERNALVR_STEREO_FX_SYNC=count: nothing reused)`.
`(ETERNALVR_STEREO_FX_SYNC_GPU=0: GPU-staged particle systems reused)` and `(GPU particle stages not checked: U and
W not counted)` say so. At start-up `seq-fx: ring advance (RVA 0x18E791C), light pool reset (0x18E7ABD), particle
(0x195526F) and effect (0x195166E, exit 0x195200C) generation hooked: ...` and `seq-fx: GPU particle stages bound by
eye R ... and generated by eye R; GPU particle step readout hooked (RVA 0x1C28DD0)` (or `left to eye L like the
rest`, `not checked`, `off`) and `seq-fx: ray-traced particle mask: eye R keeps eye L's, so eye L's particles are in
eye R's ray-traced reflections (RVA 0x18DEB6D)` (or `clear hooked, counting only`, `the engine's clear in eye R
(ETERNALVR_STEREO_FX_SYNC_RT=0)`, `not hooked; ...`), or the reason it is not (the RVAs are Steam's here; the log
prints the ones the build has, a failed check names Steam's). With `ETERNALVR_STEREO_FX_SYNC=0`: `seq-fx: off
(ETERNALVR_STEREO_FX_SYNC=0; on by default): eye L is a tick behind on CPU particles`.

## 6. Rig A/B

First run (storm deck, same-view bursts, before H4 and the per-model rule): no crash, no `Bump
MAX_TRANSPARENCY_QUADS`; the `seq-fx:` line showed 558 eye R reuses, no eye R generation and no eye R advance;
fireball and bolt shapes matched between the eyes and the eye-L-behind phase was gone. At a lightning strike's
onset, surfaces lit by the strike were lit in eye L and dark in eye R for 2 frames: the pooled particle lights
hidden in eye R (H4 above).

A/B on 2026-10-08 with H4 in (fx-sync 0db6e507), `ETERNALVR_STEREO_FX_SYNC=0` against the default: storm deck,
same view, 3-frame bursts. Without firing, bursts with an eye L/R difference above 2: fix off 11 of 17, fix on 0
of 17 (the difference stays at 1.2 to 1.4, the noise floor), lightning-strike onsets and strike-lit surfaces
included. With firing: fix off 6 of 17 (difference up to 16.6), fix on 0 of 17 (at most 1.6), so muzzle flashes,
their lights and impact effects match. In normal stereo (the real eye separation) the particles keep their
parallax: no flat sprites. The separate light-update fix (branch light-lag, `ETERNALVR_STEREO_LIGHT_SYNC`) showed
no measurable gain on top of this and is parked. Not tried in a headset yet.

Still to do (2026-11-08: section 9 has the results):

- The `seq-fx:` line as in section 5 (K close to N, M and Q small).
- Lens flares and tracers still drawn in both eyes, per eye (`seq-flares:` line unchanged).
- With ray tracing on: particles in eye R's reflections.
- `=count` against `=1` for the CPU time saved in eye R's render.
- No change in mono, with alternate eyes or with Parallel Eye Rendering (the `seq-fx:` eye R row 0).

## 7. GPU particle stages (the e1m3 tiles)

Read from the code (Steam build 25216728); the first rig run is at the end of this section.

### Cause

- A particle system's stages can run on the GPU. The world's GPU particle manager M (`[world + 0x71DBD0]`; the
  frame end reaches it as `[X + 0x26B8]`) holds, per render, draw list 0 (count `[M + 0x12048]`, drawn from
  0x1C62770), list 2 for the light atlas (`[M + 0x12050]`, drawn from 0x1C6219C) and the emitter records (`[M +
  0x1CDF0]`, 0xE8 bytes each). The world job 0x18DEAF0 (queued every render, eye R's included) ends in a jump to
  the reset 0x1C28320 (0x18DEB83), which zeroes all three.
- Only the particle update fills them. The bind 0x1955670 walks the stage records the model's last generation
  wrote (`[model + 0x620]`, 0x50 bytes, count `[model + 0x628]`); for a GPU stage (`[record + 8]` not 0, index
  `[record + 0x1C]`) whose entry in the model's GPU runtime is enabled (`[[rt + 0x10] + index * 0x38 + 8]`, rt =
  `[model + 0x658]`) it calls 0x1C2B670(rt, stage, index, index) (RVA 0x1955949). That returns when `[rt + 0x730]`
  is set or the entry has no instance (`[entry + 4]` -1); otherwise the append 0x1C295F0 adds an entry to list 0
  (unless the GPU stage's +0x1BC) and to list 2 (with its +0x175 and `r_particlesLightAtlas` on), both by `lock
  xadd`. rdx is not read. The generation 0x1953D90 calls 0x1C2B6C0(rt, ...) per GPU stage (RVA 0x1954F28), which
  appends an emitter record (0x1C2A0E0, `lock xadd [M + 0x1CDF0]` at 0x1C2A28D).
- The GPU step 0x1C28DD0 runs once per world for every render, from its frame end 0x1CBA1C0: called there (->
  0x1CDA860, the call at 0x1CDAA1D) unless the mode `[[render system + 0xF38] + 0x3D4]` is 1 (0x1CBA31C), which
  queues it instead as the job 0x1CD6FA0 (0x1CDAFE0 -> 0x1CDBAE0, the job's pointer at .data 0x39AB2C0 read at
  0x1CDC1A1; one branch of 0x1CDBAE0 calls 0x1CDA860 itself, 0x1CDCBDB). It flips the buffer parity (`[M +
  0x1205C]`), marks every instance not updated (bit 30, 0x1C28EB6) and gives only the instances with an emitter
  record this render's buffer offset (`[instance + 0x54]` from a running total that starts at 0 every render,
  0x1C28E52, 0x1C28F61).
- With the particle sync on, eye R's own stamp check skips the bind and H2 skips the generation of every model eye
  L generated: eye R's render has no draw list entries, light atlas entries or emitter records for their GPU
  stages. Their instances stay marked not updated with eye L's offsets, into eye R's parity, whose buffer is laid
  out for eye R's own records only, and the surface draw path (0x1C75020 -> 0x1C29940) still draws them. Wrong
  per-particle data (the sheet frame or UV rectangle, the size) or stale light atlas tiles would draw them as
  squares or tiles of their sheet (the shader side is inferred, not read). Snow, ice, leaves and embers are typical
  lit GPU stages; the storm deck's effects are mostly CPU stages, which would be why its A/B never showed it.
- Ruled out by reading: the CPU side (the CPU light atlas at P + 0x7D8, reset at 0x195DE50, its tiles 0x1955DB0
  -> vertex +0x20 / +0x24, the quad count P + 0x7D4 and the fill entries `[model + 0x608]` all sit in eye L's
  reused vertices) and the effect update (0x19511E0 makes no GPU manager calls). Not read: how the backend feeds
  the CPU light atlas update (gate `[RWD + 0x705C7C]`, 0x1C60A82).

### Change: `src/vkcore/fx_gpu_stages.*` (with the particle sync; `ETERNALVR_STEREO_FX_SYNC_GPU=0` turns just this off)

At H2, in eye R, a particle system eye L generated in this tick that has a GPU instance (an entry of its runtime
whose `[entry + 4]` is not -1; `stereo_seq::fxGenerationFor` takes it as an input and answers `BindGpuThenRun`) is
no longer left to eye L. Eye R first calls 0x1C2B670 for each enabled GPU stage of its stage records as the bind
does (the records eye L's generation wrote, which is what 0.1.33's eye R bound), then lets the engine's generation
run: draw list entries, light atlas entries and emitter records go into eye R's render, and the GPU step gives
its instances eye R's offsets. Its CPU stages keep eye L's binding, so they still match between the eyes: the
whole bind is not run, as with the ring kept it would bind the previous slot to ranges in the current one. The
next eye L binds eye R's generation (its stamp is then the frame before), as it does for a model only eye R sees.
Every other particle system and every effect keeps the reuse. Not chosen: keeping eye L's lists and records
through eye R's reset (eye R's GPU step would consume the records a second time: double spawn bursts), and
replaying 0x1C2A0E0's inputs (its struct's size is not checked anywhere).

Checks (`fx_gpu::locate`): the bind's `mov r14, rcx` (+0xF), its load of the record count (+0x136), the loop head
(+0x1F0) with the record, stage and index offsets, the GPU branch (+0x2AB) with the runtime and the enabled byte,
its `je` and `jmp` to the loop's step (+0x41D, `add rbp, 0x50`) and the step's `jne` back to the head; the whole
code of 0x1C2B670 and of the append 0x1C295F0 (the list counts); the generation's call of 0x1C2B6C0 with `[model +
0x658]` and 0x1C2B6C0's walk of the runtime's `[rt + 0x18]` entries of 0x38 bytes, reading +4. If any of them
fails with the particle sync on, nothing is installed and eye L is a tick behind as in 0.1.35: without them eye R
cannot tell which particle systems have GPU particles, and reusing those is the bug. Generating every model with
GPU stages through the engine's path is no fallback: telling which they are needs the same layout, and without the
GPU part of the bind eye R would have emitter records but no draw list entries, which 0.1.33 never had. If they
fail with `=count` or `ETERNALVR_STEREO_FX_SYNC_GPU=0`, the four hooks go in without them (U and W not counted).

Not redone in eye R: the rest of the bind's GPU branch. The bind zeroes every stage's render data `[stage]`
(0x1955700) and, for each stage in its records, sets the distance LOD bit `[stage + 0x44]` 0x20 (0x19558F2 to
0x1955918) and `[stage]` = `[[[[model + 0x4E8] + 0x110] + index * 0x20] + 0x690]` (0x195591B). Eye L's bind did
that for the records of the generation before its own, so a GPU stage that first appears in eye L's generation of
this tick gets its draw list entry in eye R with `[stage]` still 0: at worst it is missing from eye R for that tick
(0.1.34 had no entry for it either), and the next tick's binds set it. Writing `[stage]` and the LOD bit in eye R
too would mean redoing the bind's distance test (the view position, the model's position, two globals read through
pointers at RVA 0x5BEB580 and 0x5BEB600 and the GPU stage's +0x1AC, +0x1B0, +0x1B4, +0x120, +0x170 and +0x3C)
outside the engine for one tick of one eye.

The readout: a read-only hook on the GPU step's first instruction (0x1C28DD0; its code up to the record count's
read and the reset's whole code checked) sums the three counts for the 10 s line (section 5). The step writes none
of them; they are zeroed only by the reset at the start of the next render (and the manager's set-up, 0x1C2621D),
and the step consumes the records, so it reads the render whose frame end called or queued it. When it is queued
as a job it can run after that frame end has returned, in the next render's chain: the counts go to the eye of
the frame-end job that started last (`seqFrameEndEye()`), not to the chain running at the step (`seqChainEye()`,
which the first rig run used: its rows came out the other way round, below). Draw list 0 is written only by the
reset, the set-up and the append, which skips it for a GPU stage with its +0x1BC set (0x1C29603): a draw list
average of 0 next to a light atlas average above 0 means the bound GPU stages were all of that kind, not a count
read too early or too late. The hook goes in only after the four hooks (with `=count` and `=1`, never off), and
nothing depends on it.

What it costs, and the risks:

- Eye R runs the generation of those particle systems again, as every one in 0.1.33.
- The CPU vertices eye L generated for them are left unused in the current slot, eye R's go after them: the slot
  fills further than before (watch for `Bump MAX_TRANSPARENCY_QUADS`).
- The CPU sprites of those particle systems that eye L draws next are built for eye R's view, as in 0.1.33.
- Their CPU light atlas tiles are taken again after eye L's (the allocator is reset only by the advance); the
  allocator wraps (0x1955DB0), so a full atlas would share tiles: wrong light on lit sprites, not wrong shapes.
- Their GPU simulation steps in both renders, as in 0.1.33 (the GPU step's time comes from the render).

### Rig 2026-10-08 (e1m3, teleports to `game_player_start_N`)

- `ETERNALVR_STEREO_FX_SYNC=1 ETERNALVR_STEREO_FX_SYNC_GPU=0` (0.1.34): eye R drew the tiles at those spots, eye L
  was clean.
- `ETERNALVR_STEREO_FX_SYNC=1` (this change): eye R clean.
- Counters with this change: W about 4800 per 10 s, U equal to W, S 1190; no `Bump MAX_TRANSPARENCY_QUADS`.
- GPU readout, rows by the chain running at the step (before `seqFrameEndEye()`): with `_GPU=0` eye L 0.0 / 0.0 /
  0.0, eye R 25.0 / 0.0 / 2.0; with this change eye L 17.0 / 0.0 / 2.0, eye R 25.0 / 0.0 / 2.0.

### Still to check on the rig (done 2026-11-08: section 9)

1. e1m3 with `=1 ETERNALVR_STEREO_FX_SYNC_GPU=0` (0.1.34): W above 0, and eye R's GPU averages far below eye R's
   in a `=count` run. The storm deck: W about 0.
2. `r_skipGPUParticles 1` makes the squares go; `r_particlesLightAtlas 0` tells the light atlas from the records
   and draw lists (also `r_particlesLightAtlasDebug 2`, `r_showParticleInfo 1`; `r_snow 0` is the camera snow
   renderer, not the ring).
3. e1m3 with `=1`: eye R's GPU averages close to eye R's in a `=count` run (not to eye L's: eye R's generation
   runs right after eye L's), no `Bump MAX_TRANSPARENCY_QUADS` in a dense outdoor fight, CPU particles still alike
   in both eyes; the eye R CPU time against `=count`.
4. `=count`: nothing changes, U equal to W as with `=1`.
5. The readout's rows with `seqFrameEndEye()`: with `_GPU=0` the low row is eye R's.

## 8. Particles in eye R's ray-traced reflections

Read from the code (Steam build 25216728), then checked on the rig (section 9).

### Cause

- With ray tracing on (the gate byte the render-frame job writes from `r_useRayTracing`, read at 0x1955AD1 and
  0x19515FF), the particle bind sets the model's bit in a mask of the world's ray tracing scene: `[[model + 0x140]]`
  -> +0x240 gives the scene, the model's index comes from 0x18DC5A0 (`[[model + 0xA8] + 0x30] >> 8`), and `lock or
  [[scene + 0x298] + index / 64 * 8]` sets it (0x1955B40, when the model has stage records). The effect bind does
  the same for an effect (0x195164D). The blended acceleration structure (transparent and particle geometry,
  0x1C58D30) takes the mask (`[rdi + 0x298]`, ANDed with a second mask) to choose its instances.
- The job 0x18DEAF0, queued by the world job every render (its descriptor at .data 0x397BF40, queued at 0x18E5697)
  and the one whose tail call resets the GPU particle manager (section 7), clears the mask with a memset of
  `[scene + 0x2A4]` qwords (the call at 0x18DEB6D; the scene is `[world + 0x71DAE0]`) before the binds.
- Eye R skips the binds of what eye L generated in the tick (the engine's own stamp check, section 2), so eye R's
  mask held only the bits of its own binds (models only eye R sees): eye L's particles and effects were not in eye
  R's blended acceleration structure, and so missing from eye R's ray-traced reflections. Section 4's earlier
  note named 0x1953EE9 for it; that is a read of the view's own gather mask (`[view + 0x20980]`), not this mask.

### Change: `src/vkcore/fx_rt_mask.*` (with the particle sync; `ETERNALVR_STEREO_FX_SYNC_RT=0` turns just this off, for A/B)

A mid hook on the clear's call (0x18DEB6D): in eye R of a Route S tick, with the particle sync acting
(`stereo_seq::fxRtMaskClearFor`: the render's answer, the same rule as every other site, and a world with a
particle ring), eye R resumes after the call (0x18DEB72) and the mask keeps eye L's bits. Those are the models eye
L bound, whose vertices eye R draws from eye L's slot; eye R's own binds add theirs. The next eye L clears as the
engine does. Nothing after the call reads rax, and memset changes only volatile registers. With `=count` it only
counts. Checks (`fx_rt::install`): the job's code from its start to the tail jump with the clear's operands, the
call's target (memset 0x288D7C0, its first instructions), and both binds' mask writes (the scene's virtual call,
the gate, the bit's `lock or` at `+0x298`). If they fail, the rest of the sync still goes in and the start-up line
says the particles may be missing from eye R's reflections. Installed after the four hooks, like the readout.
The eye is the chain's (`seqChainEye()`), as for the particle and effect hooks: the job is queued by each render's
world job and clears before that render's binds, so it runs while that render's eye is current; the readout's
counts (eye R's clears kept, eye L's cleared, section 9) show no clear taken for the other eye.

## 9. Rig 2026-11-08: the checks of sections 6 and 7, the reflections and the cost; on by default (T-230)

A second test PC (RTX 3080 Ti, OpenXR-Simulator, 2048 x 2208 per eye, Route S, per-eye TAA, ray tracing on: `game
settings: r_useRayTracing 1 ... r_raytracedReflections 1` in every run), the Release layer of this branch before the
default flip (the sync set per run), runs `tmp-vr/rs/fxd-*-a` and `fxd-dkrt0norr-b` (scripts `fxd-chain1.sh` to
`fxd-chain5.sh`, checks `tourcheck.py`, `burstlr.py`, `fxd-parse.py`, `fxd-lr.py`, `fxd-floorsign.py`). Counts are
medians of the 10 s lines in play. e1m3 runs: `cp_03_shoot_gate` and the ten `game_player_start_N` teleports of
fxg-tour3.sh (13 to 21 capture stops), same view unless named.

Section 7's list:

1. `=1 ETERNALVR_STEREO_FX_SYNC_GPU=0` (0.1.34's path) in e1m3: W 26141, U 0; GPU readout eye L 108.1 / 0.0 / 2.8,
   eye R 0.2 / 0.0 / 0.0 (a `=count` run: eye L 107.2 / 0.0 / 2.8, eye R 107.3 / 0.0 / 2.8). The tiles came back:
   `tourcheck.py` against off FAILs 2 of 15 stops, dark squares in eye R over the snow at `game_player_start_4`
   and `_5`; eye L clean. The storm deck is not "W about 0": at `checkpoints_player_start_18` W is 21669 (17548
   over the Atlantica tour). The 2026-10-08 storm deck A/B ran with `r_skipGPUParticles 1`, which keeps GPU
   particles off the screen: that, not a lack of GPU-staged particle systems, is why it never showed the tiles.
2. With `_GPU=0`: `r_skipGPUParticles 1` takes the squares away (PASS 0 of 14 stops against off with the same
   cvar; eye R still shows a few extra, softer snowflakes: at most 0.82% of pixels differ by more than 16, 2.34%
   without the cvar); `r_particlesLightAtlas 0` does not (squares still in eye R at a stop, at most 0.91%; under
   `tourcheck.py`'s threshold, PASS 0 of 18). So the records and draw lists, not the light atlas, as section 7
   read (one run each).
3. `=1` in e1m3: W 27063 and U 27063 in every window, GPU readout eye L 131.1 / 0.0 / 2.8, eye R 131.1 / 0.0 / 2.8;
   in real stereo eye L 68.6, eye R 64.9 emitter records; no `Bump MAX_TRANSPARENCY_QUADS` in the game's
   `qconsole.log` of any of the 30 runs (no run made the warning appear, so it has no positive control here).
   CPU particles alike: `tourcheck.py` against off PASS 0 of 13 stops; the real-stereo tour's eye pairs show
   normal parallax and no squares (`fxd-m3rsfix-a`). The cost is below.
4. `=count`: U equal to W (183760 summed), `tourcheck.py` against off PASS 0 of 15, nothing reused (eye R
   advanced the ring in every window, the mask cleared in both eyes).
5. The readout's rows by `seqFrameEndEye()`: with `_GPU=0` the low row is eye R's (above).

Section 6's list: N equal to K (one off where a 10 s line falls between the two counts), M 52 and Q 0 against A
40385 and B 695, G 0 (e1m3, same view). The storm deck bursts (3 frames every 10 s, same view, no firing,
`r_skipGPUParticles 1`, as on 2026-10-08): bursts with an eye L/R difference above 2, off 8 of 16 (up to 16.9),
`=1` 0 of 16 (1.1 to 1.3); the first burst of each run, in the level's intro cinematic (eye L shows its subtitle
frame), is left out. Not checked: the `seq-flares:` lines (no flares in these scenes) and mono, alternate eyes and
Parallel Eye Rendering (the menus before the map, mono, show eye R rows of 0).

Ray-traced reflections (section 8): the storm deck's wet metal floor, same view, firing in bursts with demons
around (muzzle flashes, fireballs, impacts), 3-frame bursts every 6 s, 50 frames a run. Of the pixels in the lower
40% of the picture (the floor) whose luminance differs by more than 16 between the eyes, the share where eye L is
the brighter one, and the frames with 200 or more such pixels that have eye L brighter in 80% or more of them:

| Run | Pixels differing | Eye L brighter | Frames, eye L brighter in 80%+ |
|---|---|---|---|
| `=1` | 15287 | 58.3% | 0 of 27 |
| `=1` (again) | 25461 | 67.6% | 3 of 32 |
| `=1 ETERNALVR_STEREO_FX_SYNC_RT=0` (before section 8) | 43787 | 84.8% | 21 of 37 |
| `=1 ETERNALVR_STEREO_FX_SYNC_RT=0` (again) | 172256 | 92.7% | 23 of 39 |
| off (eye L a tick behind) | 779348 | 72.3% | 8 of 34 |
| `=1`, ray-traced reflections off (control) | 15121 | 55.0% | 0 of 24 |
| `=1 _RT=0`, ray-traced reflections off (control) | 17248 | 52.4% | 0 of 21 |

The repeats were meant as the controls, with `+r_raytracedReflections 0` on the command line, which the game's
settings did not keep (`game settings: ... r_raytracedReflections 1`); the controls set it at run time
(`ETERNALVR_DEBUG_CVARS=r_raytracedReflections=0`, `cvars: r_raytracedReflections 1 -> 0`). With `_RT=0` eye L's
floor shows the reflections of the fireballs and impacts and eye R's does not (`fxd-dkrt0-a`, capture
`20261108-121429-p002197`); with the change both eyes show them, and without ray-traced reflections the mask makes
no difference. The e1m3 tour (no firing) and the e1m1_intro corridor (firing) showed no difference between `=1` and
`_RT=0` (no particles over a reflective floor there).

Cost, e1m3 `cp_03_shoot_gate` in real stereo with firing, GPU timing every frame, no captures, 13 windows from 30 s
(11 in the last run), in the order off, `=1`, `=count`, `=1`, off:

| | off | `=1` | `=count` | `=1` | off |
|---|---|---|---|---|---|
| Stereo ticks/s | 80.7 | 81.3 | 80.6 | 80.9 | 81.1 |
| Eye R render wall per tick (ms) | 5.50 | 5.43 | 5.50 | 5.44 | 5.46 |
| Process CPU per tick (ms) | 49.51 | 48.01 | 48.67 | 48.12 | 48.61 |
| GPU busy eye L / eye R (ms) | 6.65 / 5.38 | 6.56 / 5.30 | 6.63 / 5.37 | 6.61 / 5.36 | 6.62 / 5.31 |

Eye R ran 61568 particle system and 0 effect generations per 10 s with `=1` against 81394 and 807 with `=count`:
three quarters of e1m3's particle systems have GPU particles, and eye R generates those itself (section 7). The
change costs nothing measurable here: eye R's render wall and the process CPU are within the off runs' spread or a
little under it.

Decision (T-230): on by default. Every check above is clean with `=1`, the 0.1.34 tiles come back only on the
0.1.34 path (`_GPU=0`, the positive control), and the reflections are fixed. `ETERNALVR_STEREO_FX_SYNC=0` turns it
off, `=count` counts; the launcher passes no value (its Troubleshooting row went in 77c48ddb), so the layer's
default is the player's. The rig QA scenario `fx-sync` (tools/rig/qa/README.md) checks the start-up lines and
the counters; on the default build with nothing set it passed (with `baseline`, 2 of 2): W 242007 equal to U, the
mask kept 6321 times, no eye R advance. Not seen in a headset yet: the e1m3 snow and ice, explosions and sparks close by, and reflections
on wet floors are what a headset look should cover.
