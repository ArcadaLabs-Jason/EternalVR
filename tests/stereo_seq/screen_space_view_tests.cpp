#include "stereo_seq/screen_space_view.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>

using evr::stereo_seq::Matrix4;
using evr::stereo_seq::ParamRow;
using evr::stereo_seq::screenSpaceShear;
using evr::stereo_seq::shearRotationRows;
using evr::stereo_seq::ViewShear;

namespace {

using Vec3 = std::array<double, 3>;
using Vec4 = std::array<float, 4>;

float tanDeg(float d) {
    return std::tan(d * 0.017453292f);
}

// A projection in xr_math::engineProjection's layout for the frustum's tangents (left and down negative),
// with the engine's reversed infinite depth rows (as bin_tiles_tests.cpp builds it).
Matrix4 projection(float left, float right, float up, float down) {
    const float w = right - left;
    const float h = up - down;
    return {2.0f / w, 0, (right + left) / w, 0, 0, 2.0f / h, (up + down) / h, 0, 0, 0, 0, 1, 0, 0, -1, 0};
}

// Quest 3 eye L as the layer renders it: 54 degrees out, 40 in, 43.98 up, 54.27 down.
Matrix4 eyeL() {
    return projection(-tanDeg(54.0f), tanDeg(40.0f), tanDeg(43.98f), -tanDeg(54.27f));
}

Matrix4 eyeR() {
    return projection(-tanDeg(40.0f), tanDeg(54.0f), tanDeg(43.98f), -tanDeg(54.27f));
}

Matrix4 centred() {
    return projection(-tanDeg(54.0f), tanDeg(54.0f), tanDeg(54.27f), -tanDeg(54.27f));
}

// The inverse of a projection of the shape above (rows 2 and 3 have z and w terms only).
Matrix4 inverseOf(const Matrix4& m) {
    Matrix4 i{};
    i[0] = 1.0f / m[0];
    i[3] = -m[2] / (m[0] * m[14]);
    i[5] = 1.0f / m[5];
    i[7] = -m[6] / (m[5] * m[14]);
    i[11] = 1.0f / m[14];
    i[14] = 1.0f / m[11];
    i[15] = -m[10] / (m[14] * m[11]);
    return i;
}

// The packed parameters as the SSR/SSDO setup (RVA 0x1C71630) builds them: diagonal terms only.
struct Packed {
    Vec4 projection;
    Vec4 inverse0;
    Vec4 inverse1;
};

Packed packedOf(const Matrix4& m) {
    const Matrix4 i = inverseOf(m);
    return {{0.5f * m[0] / m[14], -0.5f * m[5] / m[14], m[10] / m[14], m[11] / m[14]},
            {2.0f * i[0], -2.0f * i[5], -i[0], i[5]},
            {i[14], i[15], i[11], 0.0f}};
}

// The opaque pass's SSR functions, as the game's shaders compute them: a pixel's view position from (uv,
// depth), and a view position's (uv, depth).
Vec3 reconstruct(const Vec3& uvDepth, const Packed& p) {
    const double w = p.inverse1[0] * uvDepth[2] + p.inverse1[1];
    return {(p.inverse0[0] * uvDepth[0] + p.inverse0[2]) / w,
            (p.inverse0[1] * uvDepth[1] + p.inverse0[3]) / w, p.inverse1[2] / w};
}

Vec3 project(const Vec3& view, const Packed& p) {
    return {0.5 + p.projection[0] * view[0] / view[2], 0.5 + p.projection[1] * view[1] / view[2],
            p.projection[3] / view[2] + p.projection[2]};
}

// The true screen position of a view-space point under the full matrix (the image the eye shows).
Vec3 trueScreen(const Vec3& v, const Matrix4& m) {
    const double cw = m[12] * v[0] + m[13] * v[1] + m[14] * v[2] + m[15];
    const double cx = m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3];
    const double cy = m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7];
    const double cz = m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11];
    return {0.5 + 0.5 * cx / cw, 0.5 - 0.5 * cy / cw, cz / cw};
}

// A world-to-view rotation (yaw about the up axis, then pitch), as rows.
std::array<Vec3, 3> rotation(double yawDeg, double pitchDeg) {
    const double y = yawDeg * 0.017453292519943;
    const double p = pitchDeg * 0.017453292519943;
    const std::array<Vec3, 3> yaw = {
        {{std::cos(y), 0, -std::sin(y)}, {0, 1, 0}, {std::sin(y), 0, std::cos(y)}}};
    const std::array<Vec3, 3> pitch = {
        {{1, 0, 0}, {0, std::cos(p), std::sin(p)}, {0, -std::sin(p), std::cos(p)}}};
    std::array<Vec3, 3> r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            for (int k = 0; k < 3; ++k) {
                r[i][j] += pitch[i][k] * yaw[k][j];
            }
        }
    }
    return r;
}

// inverseViewMatrixX/Y/Z as the engine sets them for that rotation: row j of its transpose (translation 0).
std::array<ParamRow, 3> inverseViewRows(const std::array<Vec3, 3>& r) {
    std::array<ParamRow, 3> rows{};
    for (int j = 0; j < 3; ++j) {
        rows[j] = {static_cast<float>(r[0][j]), static_cast<float>(r[1][j]), static_cast<float>(r[2][j]),
                   0.0f};
    }
    return rows;
}

// The shader's view-space reflection: mat3(X.xyz, Y.xyz, Z.xyz) * world (the rows as the matrix's columns).
Vec3 turn(const std::array<ParamRow, 3>& rows, const Vec3& world) {
    Vec3 v{};
    for (int i = 0; i < 3; ++i) {
        v[i] = rows[0][i] * world[0] + rows[1][i] * world[1] + rows[2][i] * world[2];
    }
    return v;
}

// The largest gap, in eye image pixels (2048 wide), between where the SSR march samples and the reflected
// ray's true screen positions, over the march (the setup's step length 4 m) for a few pixels and rays.
double marchError(const Matrix4& eye, const std::array<ParamRow, 3>& rowsForShader) {
    const Packed packed = packedOf(eye);
    const auto r = rotation(25.0, -20.0);
    const std::array<Vec3, 3> pixels = {{{0.3, 0.8, 0}, {0.7, 0.75, 0}, {0.5, 0.9, 0}}};
    const std::array<Vec3, 3> rays = {{{0.2, 0.7, 0.68}, {-0.5, 0.6, -0.62}, {0.1, 0.95, -0.3}}};
    double worst = 0.0;
    for (Vec3 pixel : pixels) {
        // A floor point 3 m away along the pixel's true ray: its depth value is the true one.
        const Matrix4 inv = inverseOf(eye);
        const double nx = pixel[0] * 2.0 - 1.0;
        const double ny = 1.0 - pixel[1] * 2.0;
        const double vx = inv[0] * nx + inv[3] * 1.0;
        const double vy = inv[5] * ny + inv[7] * 1.0;
        const double vz = inv[11] * 1.0;
        const double scale = 3.0 / std::sqrt(vx * vx + vy * vy + vz * vz);
        const Vec3 truePoint = {vx * scale, vy * scale, vz * scale};
        pixel[2] = trueScreen(truePoint, eye)[2];
        const Vec3 start = reconstruct(pixel, packed);
        for (const Vec3& world : rays) {
            const Vec3 trueDir = turn(inverseViewRows(r), world);
            const Vec3 marchDir = turn(rowsForShader, world);
            for (double t = 0.5; t <= 4.0; t += 0.5) {
                const Vec3 marched = {start[0] + marchDir[0] * t, start[1] + marchDir[1] * t,
                                      start[2] + marchDir[2] * t};
                const Vec3 real = {truePoint[0] + trueDir[0] * t, truePoint[1] + trueDir[1] * t,
                                   truePoint[2] + trueDir[2] * t};
                if (real[2] > -0.05 || marched[2] > -0.05) {
                    continue; // behind the eye: the march stops at the screen's edge anyway
                }
                const Vec3 a = project(marched, packed);
                const Vec3 b = trueScreen(real, eye);
                worst = std::fmax(worst, std::hypot((a[0] - b[0]) * 2048.0, (a[1] - b[1]) * 2208.0));
            }
        }
    }
    return worst;
}

} // namespace

TEST_CASE("screen-space view: a pixel's reconstructed position projects back to the pixel") {
    for (const Matrix4& m : {eyeL(), eyeR(), centred()}) {
        const Packed p = packedOf(m);
        const Vec3 pixel = {0.3, 0.7, 0.01};
        const Vec3 back = project(reconstruct(pixel, p), p);
        CHECK(back[0] == doctest::Approx(pixel[0]).epsilon(1e-5));
        CHECK(back[1] == doctest::Approx(pixel[1]).epsilon(1e-5));
        CHECK(back[2] == doctest::Approx(pixel[2]).epsilon(1e-5));
    }
}

TEST_CASE("screen-space view: the SSR march follows the reflected ray's true screen line in each eye") {
    const auto rows = inverseViewRows(rotation(25.0, -20.0));
    for (const Matrix4& eye : {eyeL(), eyeR()}) {
        const auto shear = screenSpaceShear(eye);
        REQUIRE(shear);
        // The engine's rotation: the march runs off the reflected ray by hundreds of pixels.
        CHECK(marchError(eye, rows) > 100.0);
        // Sheared for the eye's projection: on the ray to well under a pixel.
        CHECK(marchError(eye, shearRotationRows(rows, *shear)) < 0.05);
    }
}

TEST_CASE("screen-space view: the eyes shear sideways in opposite directions, both the same way vertically") {
    const auto l = screenSpaceShear(eyeL());
    const auto r = screenSpaceShear(eyeR());
    REQUIRE(l);
    REQUIRE(r);
    CHECK(l->x == doctest::Approx(-r->x));
    CHECK(std::fabs(l->x) > 0.2f);
    CHECK(l->y == doctest::Approx(r->y));
    CHECK(std::fabs(l->y) > 0.15f);
}

TEST_CASE("screen-space view: a centred projection changes nothing") {
    const auto shear = screenSpaceShear(centred());
    REQUIRE(shear);
    CHECK(shear->x == doctest::Approx(0.0f));
    CHECK(shear->y == doctest::Approx(0.0f));
    const auto rows = inverseViewRows(rotation(25.0, -20.0));
    const auto same = shearRotationRows(rows, *shear);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            CHECK(same[i][j] == rows[i][j]);
        }
    }
    CHECK(marchError(centred(), rows) < 0.05);
}

TEST_CASE("screen-space view: the translation terms stay") {
    std::array<ParamRow, 3> rows = {{{1, 0, 0, 5}, {0, 1, 0, -7}, {0, 0, 1, 9}}};
    const auto out = shearRotationRows(rows, ViewShear{0.25f, -0.2f});
    CHECK(out[0][3] == 5.0f);
    CHECK(out[1][3] == -7.0f);
    CHECK(out[2][3] == 9.0f);
    CHECK(out[2][0] == doctest::Approx(0.25f));
    CHECK(out[2][1] == doctest::Approx(-0.2f));
    CHECK(out[0][0] == 1.0f);
}

TEST_CASE("screen-space view: other matrices are refused") {
    Matrix4 m = eyeL();
    m[1] = 0.1f; // a skewed x row
    CHECK_FALSE(screenSpaceShear(m));
    m = eyeL();
    m[15] = 1.0f; // an orthographic-style row 3
    CHECK_FALSE(screenSpaceShear(m));
    m = eyeL();
    m[0] = 0.0f;
    CHECK_FALSE(screenSpaceShear(m));
    m = eyeL();
    m[6] = std::nanf("");
    CHECK_FALSE(screenSpaceShear(m));
}
