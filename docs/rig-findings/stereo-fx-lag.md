# CPU particles and effects one tick behind in eye L under Route S

Burst captures of explosions, smoke and sparks showed eye L's render of tick N matching eye R's of tick N-1: every
CPU particle and effect sprite was one game tick behind in eye L. GPU particles and the scenery were not. RVAs are
in Steam build 25216728. Section 6 has the first rig run and what is still to check.

0.1.34 shipped the fix on by default. A player's headset captures in e1m3 (Cultist Base, 2026-10-08) showed eye R
drawing the snow and some ice effects as tiles of what looks like their whole sprite sheet, while eye L was right.
The storm deck runs never showed it. 0.1.35 turns the fix off by default until that is understood.

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

## 2. Fix: `src/vkcore/fx_sync_hooks.*`, `src/stereo_seq/fx_sync.*` (off by default since 0.1.35; `ETERNALVR_STEREO_FX_SYNC=1` turns it on, `=count` only counts)

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
- The ray-tracing particle bitmask (0x1953EE9 in the particle generation, 0x195164D in the effect bind) is not
  set in eye R's render for eye L's models: with ray tracing on, particles may be missing from eye R's reflections
  (to check on the rig).
- Sprites are built for eye L's view (the generation copies the view's origin and axes into the model,
  0x1953DC7..0x1953E40), as eye L drew sprites built for eye R's view before.

## 5. Log and switch

Every 10 s: `seq-fx: eye R reused eye L's particles N time(s) (P particle system(s), E effect(s)), light pool
kept K; eye R generated M / Q itself (eye L had not); generated by eye L A / B, eye R C / D (particle systems /
effects); ring advances eye L F, eye R G`. N counts eye R prepares (one per world with a ring) that kept eye L's
ring, K those that kept eye L's light count, P and E the generations eye R left to eye L, M and Q the ones eye R
ran itself because eye L had not generated the model, A to D the generations the engine ran (C and D include M
and Q), F and G the advances it ran (eye L's row includes mono and alternate-eye renders). In steady stereo N and
K are close to F, G is 0, C and D equal M and Q and stay small (models only eye R sees), and P and E are close to
A and B. In mono N, K, P, E, M, Q and the eye R row are 0. With `=count` nothing is reused: N, K, P and E count
what would have been, M and Q what eye R would still generate, C, D and G count every eye R generation and advance,
and the line ends with `(ETERNALVR_STEREO_FX_SYNC=count: nothing reused)`. At start-up `seq-fx: ring advance (RVA
0x18E791C), light pool reset (0x18E7ABD), particle (0x195526F) and effect (0x195166E, exit 0x195200C) generation
hooked: ...` or the reason it is not.

## 6. Rig A/B

First run (storm deck, same-view bursts, before H4 and the per-model rule): no crash, no `Bump
MAX_TRANSPARENCY_QUADS`; the `seq-fx:` line showed 558 eye R reuses, no eye R generation and no eye R advance;
fireball and bolt shapes matched between the eyes and the eye-L-behind phase was gone. At a lightning strike's
onset, surfaces lit by the strike were lit in eye L and dark in eye R for 2 frames: the pooled particle lights
hidden in eye R (H4 above).

Still to do:

- The strike onset again, and a plasma or explosion light in a dark room: lit alike in both eyes, with
  `ETERNALVR_STEREO_LIGHT_SYNC` on and off.
- Same-view bursts of explosions, smoke and sparks with `ETERNALVR_STEREO_FX_SYNC=0` and with the default: off,
  eye L(N) matches eye R(N-1); on, eye L(N) matches eye R(N).
- The `seq-fx:` line as in section 5 (K close to N, M and Q small).
- Lens flares and tracers still drawn in both eyes, per eye (`seq-flares:` line unchanged).
- With ray tracing on: particles in eye R's reflections.
- `=count` against the default for the CPU time saved in eye R's render.
- No change in mono, with alternate eyes or with Parallel Eye Rendering (the `seq-fx:` eye R row 0).
