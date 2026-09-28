#pragma once

// The light and decal binning's tile grid per eye (docs/rig-findings/stereo-bin-tiles.md; Steam build
// 25216728).
//
// The render-view job calls the binning setup (RVA 0x1CFC050) once per view render, from RVA 0x1C57999.
// The setup sets binTileWidth, binTileHeight, binTileLeft and binTileTop from the render view's fov_x and
// fov_y, i.e. for a symmetric frustum; each Route S eye draws an asymmetric one, so the binning put lights
// and decals into shifted tiles (lit areas ending in tile-shaped steps, decals missing in parts of one eye).
// A hook right after the call sets the four parameters again, with the engine's own parameter setter
// (RVA 0x1C53420), from the view's latched projection (stereo_seq::binTileParams). For a symmetric
// projection the values are the engine's, so mono frames are unchanged.
//
// Located by signature and cross-checked (the call's target is the function holding the four parameter
// loads; the setter is called there). Anything missing leaves the game untouched and logs why.

namespace evr::vkcore {

// Locates and installs the hook once per process (ETERNALVR_STEREO_BIN_TILES, default on); later calls
// return the first result.
bool installBinTileHook();

} // namespace evr::vkcore
