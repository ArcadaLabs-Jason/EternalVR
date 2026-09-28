# Per-eye motion-vector capture, and what it shows under Route S

TAA and DLSS reproject each eye's history with the game's velocity image. When eye R's velocity is wrong for
a moving object, eye R smears it while eye L stays sharp (`stereo-object-motion.md`, `stereo-moved-flag.md`).
The capture below reads both eyes' velocity images directly, so the rig can check an object's motion vectors
without a headset. RVAs are in Steam build 25216728.

## 1. The capture (`src/vkcore/motion_capture.*`, `src/ui_layer/motion_target.*`)

`ETERNALVR_CAPTURE_MOTION=<dir>[,<every N pairs>]` (default every 60 pairs; per-eye TAA and the UI layer
must be on, as they are by default under Route S):

- Images that could be velocity targets (2D, one mip and layer, a two-channel format, sampled and drawn to)
  get `TRANSFER_SRC` when the game creates them. The game makes about 200 `R16G16_SFLOAT` images of that
  shape, so their layouts are followed only once the capture finds one bound as velocity.
- The exposure hook of per-eye TAA (render-view job, the eye's tag known) reads the post-process context's
  velocity pointer (+0x60; +0x68 is the other one of the pair). It is an idImage, not a render target:
  engine format 26, flags 0x307, bit 8 set, so an image set. The set is idImage + 0xE8; its first int is the
  member in use and the VkImages start at + 0x108 (the barrier builder 0x1C49270 reads it so). The member
  changes from frame to frame, so the VkImage is resolved there, while the render is recorded.
- With eye R's copy of each Nth pair both eyes' images are copied to host memory and written as
  `mv-<pid>-p<pair>-L.raw` / `-R.raw` with a `.txt` file (format, size, tick, pointers, statistics). The
  in-headset capture takes one too, as `<capture>-MV-L.raw` and so on next to its PNG files.
- `tools/stereo/motion_diff.py <dir>` compares the eyes.

The velocity image is `VK_FORMAT_R16G16_SFLOAT` at the render size (`_motionVector0` and `_motionVector1`,
one per eye under Route S). With the simulator's head still, static scenery reads 1e-8 to 1e-6 and the idle
gun about 1e-5.

## 2. Findings (rig, OpenXR simulator, `e1m2_battle` start view, head still, 2026-09-28)

The two eyes' frustums are asymmetric (eye L -54 to 40 degrees, eye R -40 to 54), so the same object lies
about 280 px apart in the two images; pixel for pixel comparisons need `ETERNALVR_STEREO_SAME_VIEW=1`, where
both eyes render one view and should match exactly.

| Run | Gun: eye R vs eye L | Hanging banner in eye R | Eye L's moving pixels at 0 in eye R |
|---|---|---|---|
| same view, fixes on (sv1) | identical (difference 0) | 0 everywhere eye L moves | 22 to 40% |
| same view, `ETERNALVR_STEREO_OBJECT_PREV=0` (svop0) | 97% lost | 0 | 84% |
| same view, `ETERNALVR_STEREO_MOVED=0` (svmd0) | 76% lost | 0 | 83% |
| same view, `+r_useComputeSkinning 0` (svcs0) | identical | 0 | 12 to 31% |

- The object-motion and moved-flag fixes are both needed and, together, make eye R's motion vectors match
  eye L's for skinned meshes such as the gun.
- The swaying banner at the top of the start view (Havok cloth) has motion in eye L and exactly zero in eye
  R in every run. Exactly zero is what a mesh drawn as still gives: none of the existing fixes covers it,
  and it does not use compute skinning.
- The remaining 12 to 40% of eye L's moving pixels that are zero in eye R are the next thing to explain;
  the banner is the largest part of them in this view.

What the banner is not (same-view probes):

- Not its pose: with per-eye TAA off (`ETERNALVR_STEREO_TAA=0`) the banner is identical in both eyes' images;
  with TAA on, eye R's differs along its emblem and lower edge. Eye R draws it right and its TAA ghosts it
  because its velocity is zero.
- Not the object ring (`stereo-object-motion.md`): a probe that makes eye L read its own render as the
  previous one (branch `banner-probe`) stops eye L's gun completely, and eye L's banner keeps moving. Its
  motion comes from another source that steps per render, still to be found.

Skinned meshes step a ring of vertex buffers in `idSkinningBuffers` (idJointAnimator + 0x20: `index`
+0x2D8, `indexCommitted` +0x2DC, `indexCommittedPrevious` +0x2E0; the step at 0x19D1540 copies committed
to previous, and rotates the draw offsets at +0x2E4..+0x2EC). Earlier probes found that only eye L steps it.
