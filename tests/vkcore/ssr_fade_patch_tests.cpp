#include "vkcore/ssr_fade_patch.hpp"

#include "stereo_seq/screen_space_view.hpp"
#include "vkcore/dump_format.hpp"
#include "vkcore/ssr_fade_fixture.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

using evr::stereo_seq::Matrix4;
using evr::stereo_seq::ParamRow;
using evr::vkcore::ssr_fade::hasFadeSignature;
using evr::vkcore::ssr_fade::knownModules;
using evr::vkcore::ssr_fade::patchFade;
using evr::vkcore::ssr_fade::test::kFixture;
using evr::vkcore::ssr_fade::test::kFixturePackedMember;
using evr::vkcore::ssr_fade::test::kFixtureRowsMember;

namespace {

using Words = std::vector<std::uint32_t>;
using Value = std::vector<float>;

float tanDeg(float d) {
    return std::tan(d * 0.017453292f);
}

// A projection in xr_math::engineProjection's layout (row 3 = (0, 0, -1, 0)), as screen_space_view_tests
// builds it.
Matrix4 projection(float left, float right, float up, float down) {
    const float w = right - left;
    const float h = up - down;
    return {2.0f / w, 0, (right + left) / w, 0, 0, 2.0f / h, (up + down) / h, 0, 0, 0, 0, 1, 0, 0, -1, 0};
}

// Quest 3 as the layer renders it: 54 degrees out, 40 in, 43.98 up, 54.27 down.
Matrix4 eyeL() {
    return projection(-tanDeg(54.0f), tanDeg(40.0f), tanDeg(43.98f), -tanDeg(54.27f));
}

Matrix4 eyeR() {
    return projection(-tanDeg(40.0f), tanDeg(54.0f), tanDeg(43.98f), -tanDeg(54.27f));
}

// inverseViewMatrixX/Y/Z for a view turned by yaw then pitch (the columns of the world-to-view rotation).
std::array<ParamRow, 3> rotationRows(double yawDeg, double pitchDeg) {
    const double y = yawDeg * 0.017453292519943;
    const double p = pitchDeg * 0.017453292519943;
    const double yaw[3][3] = {{std::cos(y), 0, -std::sin(y)}, {0, 1, 0}, {std::sin(y), 0, std::cos(y)}};
    const double pitch[3][3] = {{1, 0, 0}, {0, std::cos(p), std::sin(p)}, {0, -std::sin(p), std::cos(p)}};
    std::array<ParamRow, 3> rows{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double v = 0.0;
            for (int k = 0; k < 3; ++k) {
                v += pitch[i][k] * yaw[k][j];
            }
            rows[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] = static_cast<float>(v);
        }
    }
    return rows;
}

// Runs the expression that feeds the fade's OpDot: the uniform block's members and the loaded ndc are given.
class Evaluator {
public:
    Evaluator(const Words& code, std::map<std::uint32_t, Value> members, Value ndc)
        : m_code(code), m_members(std::move(members)), m_ndc(std::move(ndc)) {
        for (std::size_t at = 5; at < code.size();) {
            const std::uint32_t count = code[at] >> 16;
            const std::uint32_t op = code[at] & 0xFFFF;
            m_order.push_back(at);
            if (op == 43 || op == 59 || op == 61 || op == 65 || op == 81 || op == 82 || op == 127 ||
                op == 129 || op == 131 || op == 133 || op == 136 || op == 148 || op == 12) {
                m_defs[code[at + 2]] = at;
            }
            at += count == 0 ? 1 : count;
        }
    }

    // The fade's dot: an OpDot followed by OpFSub; its first operand evaluated.
    std::optional<Value> fadeInput() {
        for (std::size_t i = 0; i + 1 < m_order.size(); ++i) {
            const std::size_t at = m_order[i];
            if ((m_code[at] & 0xFFFF) == 148 && (m_code[m_order[i + 1]] & 0xFFFF) == 131) {
                return eval(m_code[at + 3]);
            }
        }
        return std::nullopt;
    }

private:
    Value eval(std::uint32_t id) {
        const auto it = m_defs.find(id);
        if (it == m_defs.end()) {
            return {};
        }
        const std::size_t at = it->second;
        const std::uint32_t op = m_code[at] & 0xFFFF;
        const auto w = [&](std::size_t i) {
            return m_code[at + i];
        };
        const auto scalar = [&](std::size_t i) {
            const Value v = eval(w(i));
            return v.empty() ? NAN : v[0];
        };
        switch (op) {
        case 43: {
            float f = 0.0f;
            const std::uint32_t bits = w(3);
            std::memcpy(&f, &bits, sizeof(f));
            return {f};
        }
        case 61: {
            const auto chain = m_defs.find(w(3));
            if (chain != m_defs.end() && (m_code[chain->second] & 0xFFFF) == 65) {
                const auto member = m_defs.find(m_code[chain->second + 4]);
                const std::uint32_t index = member == m_defs.end() ? ~0u : m_code[member->second + 3];
                const auto value = m_members.find(index);
                return value == m_members.end() ? Value{} : value->second;
            }
            return m_ndc; // a Function variable: the fade's ndc
        }
        case 81: {
            const Value v = eval(w(3));
            return w(4) < v.size() ? Value{v[w(4)]} : Value{};
        }
        case 82: {
            Value v = eval(w(4));
            if (w(5) < v.size()) {
                v[w(5)] = scalar(3);
            }
            return v;
        }
        case 127:
            return {-scalar(3)};
        case 129:
            return {scalar(3) + scalar(4)};
        case 131:
            return {scalar(3) - scalar(4)};
        case 133:
            return {scalar(3) * scalar(4)};
        case 136:
            return {scalar(3) / scalar(4)};
        case 12:
            return w(4) == 4 ? Value{std::fabs(scalar(5))} : Value{};
        default:
            return {};
        }
    }

    const Words& m_code;
    std::map<std::uint32_t, Value> m_members;
    Value m_ndc;
    std::map<std::uint32_t, std::size_t> m_defs;
    std::vector<std::size_t> m_order;
};

Words patched() {
    std::string why;
    const auto out = patchFade(kFixture, kFixtureRowsMember, kFixturePackedMember, why);
    REQUIRE_MESSAGE(out, why);
    return *out;
}

// The fade's input in a patched module for one eye: rotation rows as the shear hook sets them for `eye` (or
// the engine's when `sheared` is false), the packed projection as the SSR setup packs `eye`, and the pixel's
// ndc.
Value fadeInputFor(const Words& code, const Matrix4& eye, bool sheared, Value ndc) {
    std::array<ParamRow, 3> rows = rotationRows(25.0, -20.0);
    if (sheared) {
        const auto shear = evr::stereo_seq::screenSpaceShear(eye);
        REQUIRE(shear);
        rows = evr::stereo_seq::shearRotationRows(rows, *shear);
    }
    std::map<std::uint32_t, Value> members;
    for (std::uint32_t k = 0; k < 3; ++k) {
        members[kFixtureRowsMember + k] = Value(rows[k].begin(), rows[k].end());
    }
    members[kFixturePackedMember] = {0.5f * eye[0] / eye[14], -0.5f * eye[5] / eye[14], eye[10] / eye[14],
                                     eye[11] / eye[14]};
    Evaluator e(code, members, std::move(ndc));
    const auto v = e.fadeInput();
    REQUIRE(v);
    return *v;
}

std::optional<std::string> envValue(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || !value) {
        return std::nullopt;
    }
    std::string text(value);
    std::free(value);
    return text;
#else
    const char* value = std::getenv(name);
    return value ? std::optional<std::string>(value) : std::nullopt;
#endif
}

// A pixel's ndc x in `eye` for a direction `degrees` from straight ahead (ndc = m0 tan - m2).
float ndcAt(const Matrix4& eye, float degrees) {
    return eye[0] * tanDeg(degrees) - eye[2];
}

} // namespace

TEST_CASE("ssr fade: both eyes fade a direction at the same place, in the frame of their inner edges") {
    const Words code = patched();
    for (const float degrees : {-38.0f, -30.7f, -15.0f, 0.0f, 12.0f, 27.2f, 39.0f}) {
        const Value l = fadeInputFor(code, eyeL(), true, {ndcAt(eyeL(), degrees), 0.3f});
        const Value r = fadeInputFor(code, eyeR(), true, {ndcAt(eyeR(), degrees), 0.3f});
        REQUIRE(l.size() == 2);
        REQUIRE(r.size() == 2);
        CHECK(l[0] == doctest::Approx(tanDeg(degrees) / tanDeg(40.0f)).epsilon(1e-4));
        CHECK(r[0] == doctest::Approx(l[0]).epsilon(1e-4));
        CHECK(l[1] == doctest::Approx(0.3f));
        CHECK(r[1] == doctest::Approx(0.3f));
    }
    // Each eye's inner edge is the shared frame's edge; its outer edge lies beyond it.
    CHECK(fadeInputFor(code, eyeL(), true, {1.0f, 0.0f})[0] == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(fadeInputFor(code, eyeR(), true, {-1.0f, 0.0f})[0] == doctest::Approx(-1.0f).epsilon(1e-4));
    CHECK(fadeInputFor(code, eyeL(), true, {-1.0f, 0.0f})[0] < -1.5f);
}

TEST_CASE("ssr fade: an unsheared rotation keeps the game's fade") {
    const Words code = patched();
    for (const float x : {-0.9f, -0.2f, 0.0f, 0.4f, 0.95f}) {
        const Value v = fadeInputFor(code, eyeL(), false, {x, -0.6f});
        REQUIRE(v.size() == 2);
        CHECK(v[0] == doctest::Approx(x).epsilon(1e-5));
        CHECK(v[1] == doctest::Approx(-0.6f));
    }
}

TEST_CASE("ssr fade: the patch changes only the fade's input and grows the id bound") {
    const Words code = patched();
    CHECK(code.size() > kFixture.size());
    CHECK(code[3] > kFixture[3]);
    for (const std::size_t i : {0u, 1u, 2u, 4u}) {
        CHECK(code[i] == kFixture[i]); // the header except the bound
    }
    CHECK(hasFadeSignature(kFixture));
    CHECK_FALSE(hasFadeSignature(code)); // the patched dot no longer reads a loaded vec2: never patched twice
}

TEST_CASE("ssr fade: modules of another shape are refused") {
    std::string why;
    CHECK_FALSE(patchFade(kFixture, kFixtureRowsMember + 1, kFixturePackedMember, why));
    CHECK_FALSE(why.empty());
    CHECK_FALSE(patchFade(kFixture, kFixtureRowsMember, 9, why));
    // smoothstep(0, 0.25, ...) is not the fade.
    Words other(kFixture.begin(), kFixture.end());
    std::size_t changed = 0;
    for (std::size_t at = 5; at < other.size();) {
        const std::uint32_t count = other[at] >> 16;
        if ((other[at] & 0xFFFF) == 43 && count == 4 && other[at + 3] == 0x3F000000) {
            other[at + 3] = 0x3E800000;
            ++changed;
        }
        at += count;
    }
    REQUIRE(changed == 1);
    CHECK_FALSE(hasFadeSignature(other));
    CHECK_FALSE(patchFade(other, kFixtureRowsMember, kFixturePackedMember, why));
    CHECK_FALSE(patchFade(std::span<const std::uint32_t>(kFixture.data(), 3), 1, 4, why));
}

TEST_CASE("ssr fade: the known modules are distinct and each has a validated output") {
    std::set<std::uint64_t> inputs;
    for (const auto& m : knownModules()) {
        inputs.insert(m.inputHash);
        CHECK(m.outputHash != 0);
        CHECK(m.outputHash != m.inputHash);
    }
    CHECK(inputs.size() == knownModules().size());
}

// Re-validating the table on a shader dump (not run by default): EVR_TEST_SSR_FADE_DIR=<dump>\modules patches
// each known module found there, writes it to <dir>\patched\<input hash>.spv for spirv-val and prints the
// output hashes.
TEST_CASE("ssr fade: patch a shader dump's modules") {
    const std::optional<std::string> dir = envValue("EVR_TEST_SSR_FADE_DIR");
    if (!dir) {
        return;
    }
    const std::filesystem::path root(*dir);
    std::filesystem::create_directories(root / "patched");
    for (const auto& m : knownModules()) {
        char name[32];
        std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(m.inputHash));
        std::ifstream in(root / (std::string(name) + ".spv"), std::ios::binary);
        REQUIRE_MESSAGE(in, name);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Words words(bytes.size() / 4);
        std::memcpy(words.data(), bytes.data(), words.size() * 4);
        CHECK(evr::vkcore::dump::fnv1a64(words.data(), words.size() * 4) == m.inputHash);
        std::string why;
        const auto out = patchFade(words, m.rowsMember, m.packedMember, why);
        REQUIRE_MESSAGE(out, name, ": ", why);
        std::ofstream(root / "patched" / (std::string(name) + ".spv"), std::ios::binary)
            .write(reinterpret_cast<const char*>(out->data()), static_cast<std::streamsize>(out->size() * 4));
        char hash[32];
        std::snprintf(
            hash, sizeof(hash), "0x%016llxULL",
            static_cast<unsigned long long>(evr::vkcore::dump::fnv1a64(out->data(), out->size() * 4)));
        MESSAGE(name, " -> ", hash);
        CHECK(evr::vkcore::dump::fnv1a64(out->data(), out->size() * 4) == m.outputHash);
    }
}
