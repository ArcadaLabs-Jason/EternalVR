#include "xr_math/cinema_eyes.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace evr::xr_math {

namespace {

constexpr float kDegreesToRadians = 0.017453292519943295f;

bool validFov(float degrees) {
    return std::isfinite(degrees) && degrees > 0.0f && degrees < 170.0f;
}

} // namespace

float cinemaDisparity(float interaxialMetres, float tanHalfX, float shift, float depthMetres) {
    return shift - interaxialMetres / (2.0f * tanHalfX * depthMetres);
}

float cinemaZeroParallax(float interaxialMetres, float tanHalfX, float shift) {
    return interaxialMetres / (2.0f * tanHalfX * shift);
}

float cinemaLensFovY(float fovXDegrees, float fovYDegrees, double screenAspect) {
    if (!validFov(fovXDegrees) || !validFov(fovYDegrees) || !(screenAspect > 0.0)) {
        return fovYDegrees;
    }
    const double tanX = std::tan(static_cast<double>(fovXDegrees) * kDegreesToRadians * 0.5);
    const double tanY = std::tan(static_cast<double>(fovYDegrees) * kDegreesToRadians * 0.5);
    // A band narrower than the image's own height: its half-height is the half-width over the aspect.
    if (tanX / tanY >= screenAspect) {
        return fovYDegrees;
    }
    return static_cast<float>(2.0 * std::atan(tanX / screenAspect) / kDegreesToRadians);
}

float cinemaNearestMetres(float traceNearestMetres) {
    if (!(traceNearestMetres > 0.0f) || !std::isfinite(traceNearestMetres)) {
        return kCinemaNearestMetres;
    }
    return std::min(kCinemaNearestMetres, std::max(kCinemaMinTraceMetres, traceNearestMetres));
}

float cinemaComfortInteraxial(float tanHalfX, float traceNearestMetres) {
    return 2.0f * tanHalfX * cinemaNearestMetres(traceNearestMetres) *
           (kCinemaInfinityShift + kCinemaMaxCrossed);
}

std::array<CinemaRay, 15> cinemaRayFan(float tanHalfX, float tanHalfLens) {
    constexpr float kColumns[5] = {-0.8f, -0.4f, 0.0f, 0.4f, 0.8f};
    constexpr float kRows[3] = {-0.7f, 0.0f, 0.7f};
    std::array<CinemaRay, 15> fan{};
    std::size_t i = 0;
    for (const float row : kRows) {
        for (const float column : kColumns) {
            fan[i++] = {column * tanHalfX, row * tanHalfLens};
        }
    }
    return fan;
}

Vec3 cinemaRayDirection(const IdViewAxis& axis, const CinemaRay& ray) {
    return normalize(axis.forward + axis.left * -ray.right + axis.up * ray.up);
}

float cinemaSmoothLimit(float current, float target, float seconds, bool cut) {
    if (!(target > 0.0f)) {
        return current;
    }
    if (cut || !(current > 0.0f) || target <= current) {
        return target;
    }
    const float dt = std::clamp(std::isfinite(seconds) ? seconds : 0.0f, 0.0f, 0.1f);
    const float k = 1.0f - std::exp(-dt / kCinemaLimitRiseSeconds);
    return current + (target - current) * k;
}

std::optional<CinemaStereo> cinemaStereo(const CinemaStereoInput& input) {
    if (!validFov(input.fovXDegrees) || !validFov(input.fovYDegrees) || !std::isfinite(input.unitsPerMetre) ||
        !(input.unitsPerMetre > 0.0f) || !std::isfinite(input.interaxialMetres) ||
        !(input.interaxialMetres > 0.0f)) {
        return std::nullopt;
    }
    const float tanX = std::tan(input.fovXDegrees * kDegreesToRadians * 0.5f);
    const float tanY = std::tan(input.fovYDegrees * kDegreesToRadians * 0.5f);
    CinemaStereo out;
    out.comfortMetres = cinemaComfortInteraxial(tanX, input.traceNearestMetres);
    const float comfort = input.limitMetres > 0.0f ? input.limitMetres : out.comfortMetres;
    out.clamped = input.interaxialMetres > comfort;
    out.interaxialMetres = std::min(input.interaxialMetres, comfort);
    out.zeroParallaxMetres = cinemaZeroParallax(out.interaxialMetres, tanX, kCinemaInfinityShift);
    out.nearestMetres = cinemaNearestMetres(input.traceNearestMetres);
    out.nearestDisparity =
        cinemaDisparity(out.interaxialMetres, tanX, kCinemaInfinityShift, out.nearestMetres);
    // Parallel cameras; each eye's window moves by `shift` of the half-width (its image of infinity ends up
    // `shift` of the screen width from the other eye's, behind the screen): the left eye's window moves
    // right.
    const float shift = kCinemaInfinityShift;
    const float half = 0.5f * out.interaxialMetres * input.unitsPerMetre;
    out.eyes[0].leftUnits = half;
    out.eyes[0].fov = fromTangents({tanX * (-1.0f + shift), tanX * (1.0f + shift), tanY, -tanY});
    out.eyes[1].leftUnits = -half;
    out.eyes[1].fov = fromTangents({tanX * (-1.0f - shift), tanX * (1.0f - shift), tanY, -tanY});
    return out;
}

} // namespace evr::xr_math
