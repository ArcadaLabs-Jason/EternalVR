#pragma once

// The latch's centred view-projection matrix under an explicit projection (docs/VR_STEREO.md).
//
// The latch (RVA 0x1CE1400) builds idRenderView's centeredViewProjectionMatrix (+0x296B0) from a
// camera-relative (rotation-only) view and a projection. Without an explicit projection that projection
// is the engine's builder with the depth parameters the hands-and-guns pass uses; with
// useExplicitProjectionMatrix set it is the explicit matrix, depth rows included, so the centred matrix
// gets the world's depth range instead. Rows 0, 1 and 3 of the product only depend on rows 0, 1 and 3
// of the projection (row 3 is (0, 0, -1, 0) for every perspective matrix of the engine), and row 2 is
// a * V2 + (0, 0, 0, b) where V2 = -row 3 of the product (the view is rotation only). So the depth row
// (a, b) is read from a centred matrix the latch built without the explicit projection (the world-views
// pass latches the game's view first) and written back into the eye's.

#include <array>
#include <optional>

namespace evr::stereo_seq {

using Matrix4 = std::array<float, 16>; // row-major, element [row * 4 + column]

struct CenteredDepth {
    float a = 0.0f; // projection [2][2]
    float b = 0.0f; // projection [2][3]
};

// The depth row of the projection behind a centred view-projection matrix; nullopt when row 3 has no
// usable element (not a perspective product) or the rows disagree.
std::optional<CenteredDepth> centeredDepthOf(const Matrix4& centeredViewProjection);

// Rewrites row 2 of a centred view-projection matrix for the depth row `depth`.
void setCenteredDepth(Matrix4& centeredViewProjection, const CenteredDepth& depth);

// The hands-and-guns matrices (customViewProjectionMatrix and its variants) are always built from the
// symmetric weapon FOV. Under stereo each eye has to draw them with its own frustum, as
// inhibitModelFovScale asks ("hands and weapons use the main projection"): rows 0 and 1 of the product
// are replaced by the eye projection's rows 0 and 1 applied to the same view. The view rows are recovered
// from the product itself (rows 0 and 1 of a symmetric projection scale one view row each; row 3 is
// -view row 2). False, and nothing written, when `viewProjection` is not such a product.
bool retargetViewProjection(Matrix4& viewProjection, const Matrix4& eyeProjection);

} // namespace evr::stereo_seq
