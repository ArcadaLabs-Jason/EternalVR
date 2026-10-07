#pragma once

// Eye R keeps eye L's output slots for geometry caches (stereo_seq/geomcache_prev.hpp,
// docs/rig-findings/stereo-geomcache-motion.md; Steam build 25216728). On by default;
// ETERNALVR_STEREO_GEOMCACHE_PREV=0 turns it off, =count only counts. Only with per-eye TAA or DLSS
// (anti-aliasing on).
//
// The geometry cache's commit (0x1944950, idRenderModelGeomCache vtable slot 0x88, from the world commit
// 0x18D9FA0) flips its transform slot (store at RVA 0x1944B39) and its position slot (store at RVA
// 0x1944EBE), then calls its update (0x1949350), which takes the previous frame as valid when the cache's
// last update was in the render just before (`bpl` at RVA 0x194956A). A hook on each store makes eye R store
// the slot it already had, for a cache eye L updated in the render just before (the render system's render
// frame counter) at the same model time; a hook after the check stamps the engine's own chain's updates and
// gives eye R's update eye L's verdict for a cache whose slots it kept.
//
// Located by signature and cross-checked; anything missing leaves the game untouched and logs why.

namespace evr::vkcore {

// Locates and installs the hooks once per process; later calls return the first result.
bool installGeomCachePrevHooks();

} // namespace evr::vkcore
