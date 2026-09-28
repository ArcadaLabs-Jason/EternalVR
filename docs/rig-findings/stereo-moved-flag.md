# Moving objects lose their motion vectors in eye R under Route S

With the object-motion fix (`stereo-object-motion.md`) in place, moving and animated demons still looked
smeared and partly see-through in eye R, with the same pose in both eyes. The first-person viewmodel and
static scenery were sharp in both eyes. RVAs are in Steam build 25216728.

## 1. Cause

Names: the render world data (RWD) is `[world + 0x71DAE0]`; each entity has a flags qword at
`[RWD + 0xA8][index]` and a status byte at `[world + 8 + index]`.

1. The world's commit (0x18D9FA0) sets flags bit 1, "moved this frame", when the entity's transform or
   bounds changed, sets the status byte to 1 and queues the entity (0x1C8AA80).
2. The next world frame's list builder (0x18DEDB0, a job started from 0x18E5070) treats status 1 as a
   cleanup: it clears the moved bit (`and [flags], ~2` at 0x18DF0FE), queues the entity again (0x18DF11E)
   and sets the status to 0 (0x18DF123).
3. At the frame's end the queued entities' draw surfaces are rebuilt (0x1CBA1C0 -> 0x1CDA860 -> job
   0x1C8A180 -> 0x1C8CDE0 -> 0x1C8B930). The rebuild adds the depth pre-pass that writes object motion
   vectors (`rpf_prez_motionvectors`, 0x1C8D050) only for a moved entity.

The game updates a moving entity once per game frame, so in mono every world frame commits it again and it
stays moved. Under Route S a tick is two world frames: eye L's commits the entity (moved), eye R's has no
game update in between and runs the cleanup, so eye R rebuilds the entity without object motion vectors
and its TAA reprojects the demon with the camera's motion only. The viewmodel is committed on every render
by the screen-views job (0x1C75290), never reaches the cleanup, and stays sharp.

## 2. Fix: `src/vkcore/moved_flag_hooks.*`, `src/stereo_seq/moved_flag.*` (default on; `ETERNALVR_STEREO_MOVED=0` turns it off, `=count` only counts)

A mid hook on the list builder's `test cl, cl` after the status byte's load (RVA 0x18DF0B0; signature at
0x18DF0AA, its skip branch checked) makes eye R's render take the engine's own "status 0" path for status
1. The status stays 1, so the next eye L world frame does the cleanup, or commits the entity again if the
game moved it, as a mono frame would. Eye R is told by the chain (`seqChainEye`), since the list builder is
a front-end world job inside eye R's render.

The log counts, every 10 s, the cleanups seen per eye, those kept for eye R, and the surface rebuilds with
and without the moved flag (a probe on the rebuild's check at 0x1C8D14D; its per-eye split is approximate,
because frame-end jobs overlap the next render).

## 3. Checks

- Rig, `e1m1_intro`: with the fix about 117,000 cleanups per 10 s are kept for eye R, and the surface
  rebuilds drop from about 185,000 per 10 s (half of them without the moved flag) to a few hundred.
- Headset, chainsaw training room: eye R's zombie has its face, glowing eyes and a solid body where before
  it was see-through; still a little softer than eye L, with fine streaks.

## 4. Cost, and the default

Eye L sees few status-1 entities (0 to 36 cleanups per 10 s against about 117,000 skipped in eye R) because
the game updates every animated entity each tick (0x18DBFE0 sets status 2 before eye L's world frame), so
eye L commits them again rather than cleaning up. Nothing stays "moved" for long; an entity that stops is
cleaned up by the next eye L, one render later than in mono. The GPU cost is that of being correct: eye R
now draws every animated entity with the motion-vector pre-pass, as eye L does, where before it drew them
as still. In the chainsaw training room the headset measured about 135 ticks/s (GPU 4.6 ms per tick) with
the fix against about 230 (GPU 2.0 ms) without; comparing with mono in the same room is the next check.

Eye R also stays a little softer than eye L on animated demons. Leads, in order: entities committed again
in eye R (status 2) copy the current model matrix over the previous one (0x1C8AE60), so their previous
equals eye L's; and the object ring's upload (0x1C00B40) is partial outside allocation changes, which can
leave stale ring entries. `r_TAAAntiGhosting` is off in both eyes. So the launcher's anti-aliasing
defaults to Off (no temporal history, sharp in both eyes), and the hook is installed only when per-eye
TAA or DLSS is on. The softness came from the object-transform ring (docs/rig-findings/stereo-object-motion.md):
with each render on its own ring slot, eye R's demons are as sharp as eye L's in the headset (2026-09-28),
and the launcher defaults to TAA again.

## 5. Eye R's previous model matrix (`src/vkcore/keep_prev_hooks.*`, `src/stereo_seq/keep_prev.*`; default on with per-eye TAA or DLSS, `ETERNALVR_STEREO_KEEP_PREV=0` turns it off, `=count` only counts)

The commit's model-matrix step (0x1C8ADE0, from the world commit 0x18D9FA0) copies the entity's current
model matrix over its previous one (`[RWD+0xC0]` to `[RWD+0xF0]`, index * 64, four `movups` from RVA
0x1C8AE60), then calls 0x399EB0 for the new current one. In mono that runs once per game frame. Under Route
S the engine commits many entities again in eye R's world frame (status 2), and the second run sets their
previous to eye L's current of the same tick: eye R's TAA then reprojects them with no object motion.

A hook on the first copy stamps each entity eye L commits with the pair's number; in eye R it saves the
previous matrix of an entity eye L committed in the same tick, and a hook after the call (RVA 0x1C8AEAC)
puts it back. An entity only eye R commits keeps the engine's behaviour, and so does the copy that follows
for an entity that does not interpolate (0x1C8AEB3).

Rig, `e1m1_intro` (run kp1): per 10 s about 260,000 copies in eye L and 53,500 in eye R, of which 99.97%
are entities eye L committed in the same tick; all of those now keep eye L's previous matrix. Stable for
over three minutes at 118 to 180 stereo pairs per second. The headset check in the chainsaw training room
(TAA on) is still to do.
