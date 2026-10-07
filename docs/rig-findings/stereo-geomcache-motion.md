# Geometry caches have no motion in eye R under Route S

With the object-motion, moved-flag and previous-matrix fixes in place (`stereo-object-motion.md`,
`stereo-moved-flag.md`), the swaying banner at the top of the `e1m2_battle` start view still had motion in eye
L and exactly zero in eye R in every same-view capture (`stereo-motion-capture.md`). It is not Havok cloth:
the banners in that view are Alembic geometry caches
(`decltree/geomcache/art/kit/sentinel/prop/banner_knight_*` and `banner_0{1..4}.abc`, loaded by
`e1m2_battle`), and so are the hanging corpses of `e2m1_nest` (`art/kit/alembic/hanging_corpse_01/02.abc`,
`hanging_trio_b.abc`) and the caged damned (`e3m2_metal_hell/limbs_alive_*`, loaded by `e1m1_intro`,
`e3m2_hell`, `e3m2_hell_b` and `shell`). Static read of Steam build 25216728; RVAs are in that build.

## 1. Cause

`idRenderModelGeomCache` interpolates a cache's vertices (and, for auto-skinned caches, its transforms) on the
GPU into one of two output slots per kind. The previous frame the motion vectors come from is the other slot.

| What | RVA |
|---|---|
| Commit (vtable 0x2E72A40, slot 0x88), called from the world commit 0x18D9FA0 at 0x18DA6FB | 0x1944950 |
| Transform slot flip, `[cache + 0x534] = ([cache + 0x534] + 1) mod 2` | 0x1944B19..0x1944B39 |
| Position slot flip, `[cache + 0x530]` the same way | 0x1944EA8..0x1944EBE |
| A cache that does not animate (`[cache + 0x504]` 0) pins the position slot to `[cache + 0x500]` first | 0x1944B41..0x1944B68 |
| Update 0x1949350: valid previous frame = `[cache + 0x538] == counter - 1 && [cache + 0x504] == 1` (`bpl`), counter 0x1CBB2D0 (the backend frame counter) | 0x1949546..0x1949567 |
| Position pick: current = `+0x530`, previous = valid ? current ^ 1 : current | 0x194A1DC..0x194A207 (in 0x194A1A0) |
| Transform pick, the same on `+0x534` | 0x194A819..0x194A842 (in 0x194A7E0) |
| The GPU update (render thread, from the backend frame 0x1CDAFE0 through 0x1CD7020): `[cache + 0x538]` = the update's counter | 0x19459EF (in 0x1945520) |

In mono every commit flips, so the previous slot holds the render just before, one game frame back. Under
Route S the world commit runs again in eye R's render (the same function that commits the previous model
matrix, 0x1C8ADE0 at 0x18DA626, `stereo-moved-flag.md` section 5), so with slots a and b:

| Render | Writes | Previous | Motion |
|---|---|---|---|
| tick t - 1, eye L | a | b | |
| tick t - 1, eye R | b | a, eye L of t - 1 | 0 |
| tick t, eye L | a | b, tick t - 1 | one tick |
| tick t, eye R | b | a, eye L of t | 0 |

The moved-flag fix keeps the caches in eye R's motion-vector pre-pass, which is why eye R's zero is exact.
`g_geomCacheSkipPlayback`, tried earlier, is read only at 0xC42910, a playback-start gate of one scripted
entity.

## 2. Fix: `src/vkcore/geomcache_prev_hooks.*`, `src/stereo_seq/geomcache_prev.*` (default on with per-eye TAA or DLSS; `ETERNALVR_STEREO_GEOMCACHE_PREV=0` turns it off, `=count` only counts)

For a cache eye L updated in the same tick at the same model time, eye R's commit stores both slots back
unchanged. Eye R then writes eye L's slot again (the same model time) and reads the same previous slot:

| Render | Writes | Previous | Motion |
|---|---|---|---|
| tick t - 1, eye L | a | b | |
| tick t - 1, eye R (kept) | a | b | |
| tick t, eye L | b | a, tick t - 1 | one tick |
| tick t, eye R (kept) | b | a, tick t - 1 | one tick |

Three mid hooks, found by signature (each once) and installed all or none (`g_live` is set once all three are
in; until then every hook leaves the engine alone):

| Hook | Signature | At | Instruction | What it does |
|---|---|---|---|---|
| transform flip | RVA 0x1944B19 | + 0x20 (0x1944B39) | `mov [rdi + 0x534], ecx` | rdi the cache, rbx its model time; eye R keeps: ecx = `[rdi + 0x534]` |
| position flip | RVA 0x1944EA8 | + 0x16 (0x1944EBE) | `mov [rdi + 0x530], eax` | for the cache the first hook kept: eax = `[rdi + 0x530]` |
| previous-frame check | RVA 0x194953F | + 0x2B (0x194956A) | `mov r14, [rsp + 0xF0]` | rsi the cache, bpl the engine's verdict; eye L stamps, eye R takes eye L's verdict |

The install checks that the position flip is 0x38F bytes after the transform flip (the same function) and that
the check's call (+ 0x7) reaches the counter read (`mov rax, [rcx + 0xF58]`, ..., `mov eax, [rax + 0xB0]`,
`ret`, `mov eax, [rcx + 0x10]`, `ret`), whose `rcx` the check's `lea` (+ 0x0) points at the render system
(0x66E2C30); its render frame counter (+ 0x10) is what the stamps use. The transform hook sits between a `cmp`
and its `jne`: safetyhook's mid-hook stub saves the flags before the callback and restores them after
(`pushfq` / `popfq`), and the relocated store does not touch them.

The decision (`stereo_seq::keepGeomCacheSlots`, unit tested):

- The key is the cache's render entity index, `[[cache + 0xA8] + 0x30] >> 8`, as the commit reads it at
  0x194495F (a table of 2^20 entries; a larger index keeps the engine's behaviour). Each entry also holds the
  cache's address, so if two caches ever shared a render entity, a stamp would answer for the cache that wrote
  it only (the other keeps the engine's flips). The commit's thread keeps the cache, its index, model time,
  render frame and decision from the first hook to the check (`thread_local`): the first
  flip comes before every path to the second flip and to the update, and the update is called only from this
  commit (0x1944EED, 0x1944F57).
- The engine's own chain (eye L, and mono and alternate renders) stamps a cache at its update's check, so a
  commit that aborts (0x1943F10 marks the cache invalid) is not counted as updated: the stamp is the render's
  frame number (render system + 0x10, one up per render in the render-frame job 0x1CB9EE0, before the
  world-views pass runs the world's jobs), the engine's verdict and the low 30 bits of the model time
  (`[cache + 0x4F0]`, held in rbx through the commit).
- Eye R (`seqChainEye`) keeps when the stamp's render frame is eye R's minus one, the model time is eye L's,
  and the cache animates (`[cache + 0x504]`, the byte the commit branches on right after the
  first flip). A cache that does not animate has no valid previous frame anyway, and its commit pins the
  position slot first, which keeping would undo. Anything else is the engine's flip: a cache only eye R
  updates, one at another model time, mono renders. Eye R's render is nested right after its eye L's frame
  end, so the render just before is always eye L of the same tick: a stamp from a mono render (loading,
  menus, a tick waiting for its tag base), from an earlier tick, or from the map before a load never matches.
- Validity: for a kept cache eye R's update uses eye L's verdict, which judged the same pair of slots. The
  engine's own check needs eye L's GPU update (render thread) to have stamped `[cache + 0x538]` before eye R's
  commit reads it, which the frontend does not wait for; with eye L's verdict, a kept cache's previous slot is
  used in eye R whenever eye L used it. A cache eye L found invalid (its first update in a while) gets no
  previous frame in eye R either.

Where it does nothing: with alternate eyes (`ETERNALVR_ALTERNATE_EYES=1`) there is no eye R render, so
`seqChainEye()` is always Left and every commit flips as in mono. Parallel Eye Rendering does not install
the Route S hooks, so these are never installed with it. Off when the multiplayer guard is not armed, and
each hook asks the guard first.

With `ETERNALVR_ALTERNATE_EYES=auto`, ticks that render both eyes keep, as under Route S, and one-eye ticks
flip as the engine does. That is intended, and matches the moved-flag fix (eye R nested in its eye L's tick
is the case both handle), not `keep_prev`, which in alternate eyes and auto replaces its rule for every
render with "each eye's own last render" (`alternate-eye.md` section 9.1). On a both-eyes tick, eye R
without the rule reads eye L's slot of the same tick (no motion); with it, both eyes read the slot of the
render before eye L, as eye L does. On a one-eye tick the engine's flip reads the render just before, one
tick back, while that eye's TAA history may be two ticks old: the same mismatch the object ring's skinned
meshes have with alternation.

Log, at install:

```
seq-geomcache: geometry cache slot flips (RVA 0x1944B39, 0x1944EBE) and previous-frame check (0x194956A)
hooked: eye R keeps eye L's slots for caches eye L updated in the render before
```

Every 10 s:

```
seq-geomcache: commits L N R N; eye R commits of caches eye L updated in the render before N (N at another
model time), slots kept N; updates with a valid previous frame L N of N, R N of N (the engine's own check: N)
```

Expected: slots kept close to eye R's commits of caches eye L updated, none at another model time, and eye R's
valid updates close to eye L's. The engine's own check in eye R shows how often its check alone would have
been valid.

## 3. Checks

- Unit tests, `tests/stereo_seq/geomcache_prev_tests.cpp`: the switch; the stamps (render frame and its wrap,
  validity, model time, out of range, two caches on one entity index, stamps from mono renders before a load
  and from a tick whose eye R updated nothing); the decision; a model of one cache's two slots over stereo
  ticks: without the rule eye R moves 0, with it both eyes move one tick, also when eye L's update stamps the
  cache only after eye R's check; a cache only eye R updates, another model time, a still cache, mono and
  alternate renders, the first stereo tick after mono and a tick without eye R keep the engine's behaviour.
- Rig, still to do (`e1m2_battle` start view, OpenXR simulator, `ETERNALVR_STEREO_SAME_VIEW=1`,
  `ETERNALVR_CAPTURE_MOTION`): `+r_skipGeomCacheModels 1` without the fix should take the banners out of both
  eyes and eye L's moving pixels at 0 in eye R (20 to 40%, `stereo-motion-capture.md`) to about 0; with the
  fix `tools/stereo/motion_diff.py` should show the same, with the banner's motion in eye R matching eye L's;
  `ETERNALVR_STEREO_GEOMCACHE_PREV=0` should bring eye R's zero back.
- Headset, still to do: the `e1m2_battle` banners, the `e2m1_nest` hanging corpses and the `e1m1_intro` cages
  with TAA on, in both eyes, watching for smearing in eye R and for strobing. No player-visible result is
  claimed before these checks.

## 4. Open

- Eye R's GPU update rewrites the slot eye L's draws read. The data should be the same (same model time,
  checked per cache, and the same keyframes), and mono has the same pattern a render apart; the object ring's
  shared slot strobed because the two renders' transforms differed (`stereo-object-motion.md` section 2.1),
  so the headset check watches for strobing on these models.
- Whether a given cache uses the position slot or the transform slot (auto-skinned) is not known; both are
  kept.
