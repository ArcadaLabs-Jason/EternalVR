#pragma once

// A second opinion from the view frustum for the lights Umbra hides (Steam build 25216728).
//
// With Umbra culling on, the game's per-view light gather (RVA 0x1C782C0) never tests a light against the
// view frustum: a light is drawn only when Umbra reported its light object visible (gate A), or, for a light
// with no Umbra object, when its box passed Umbra's occlusion buffer (gate B), and then, for some kept
// lights, when a sphere around them passed Umbra's test too. From some camera positions Umbra hides a light
// whose light falls on what the camera sees, and the light is dropped: at the steps up to the Fortress of
// Doom's command dais the steps, the floor and the weapon go dark one step down and bright one step up
// (docs/rig-findings/umbra-lights.md). With r_useUmbraCulling 0 the gather takes a frustum test instead and
// the lighting is right, but the whole frame loses Umbra's culling (about half the frame rate on the test
// PC).
//
// This keeps every light Umbra keeps and sends only the lights it hides to that frustum test: mid hooks at
// the three drops resume at the frustum path, and a hook at its verdict sends a light the frustum drops too
// to the drop of the gate that asked. A light is dropped only when both say hidden. (The first version took
// Umbra's verdict away and gave every light the frustum test alone, so a light Umbra kept could be dropped by
// the frustum.) Geometry, decals and areas keep Umbra.
//
// ETERNALVR_LIGHT_FRUSTUM=1 turns it on (experimental, off by default). Only while the multiplayer guard is
// armed (checked at install and on every hidden light) and only on the build it was read on (signature, the
// code at every offset used, and PE timestamp); otherwise it logs why and leaves the game untouched.

namespace evr::vkcore {

// Hooks the light gather once per process when asked; later calls return the first result.
bool installLightFrustumCull();

} // namespace evr::vkcore
