// The hands rig's dump (arm_dump.hpp).

#include "vkcore/arm_dump.hpp"

#include "vkcore/log.hpp"

#include <array>
#include <cstdio>
#include <numbers>
#include <optional>
#include <share.h>
#include <string>

namespace evr::vkcore::controllers::arm_dump {

namespace {

constexpr const char* kTag = "arm dump";
constexpr double kIntervalSeconds = 0.25;
constexpr float kDegreesPerRadian = 180.0f / std::numbers::pi_v<float>;

// Type info (build 25216728), as the viewmodel and fire hooks read them.
constexpr std::size_t kHandsOwner = 0x358;                // idHands::owner
constexpr std::size_t kPlayerFirstPersonOrigin = 0x16580; // idPlayer::firstPersonViewOrigin

// Off-hand hook thread only.
std::FILE* g_file = nullptr;
bool g_failed = false;
LONGLONG g_lastLine = 0;
bool g_viewNoted = false;
WorldHand g_world;
ModelPlacement g_model;
std::array<arm::DumpArm, 2> g_arms;
const std::byte* g_itemDecl = nullptr;
std::string g_itemName;

std::wstring widen(const std::string& text) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

double qpcSeconds(LONGLONG qpc) {
    static const double frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    return static_cast<double>(qpc) / frequency;
}

std::size_t slot(arm::ArmSide side) {
    return side == arm::ArmSide::Right ? 1 : 0;
}

bool open() {
    if (g_file || g_failed) {
        return g_file != nullptr;
    }
    const std::string& path = settings().testArmDumpPath;
    // Shared for reading, so a script can follow the file while the game runs.
    g_file = _wfsopen(widen(path).c_str(), L"ab", _SH_DENYNO);
    if (!g_file) {
        g_failed = true;
        EVR_LOG("%s: '%s' cannot be opened for writing; no dump", kTag, path.c_str());
        return false;
    }
    EVR_LOG("%s: both arms every %.2f s to '%s'%s (rig test)", kTag, kIntervalSeconds, path.c_str(),
            settings().testArmTwistFault ? ", planted twist fault ON (ETERNALVR_TEST_ARM_FAULT)" : "");
    return true;
}

std::optional<arm::DumpHandInput> handInput(const std::optional<input::TestInput>& test, input::Hand hand) {
    if (!test) {
        return std::nullopt;
    }
    const input::TestHand& h = test->hand(hand);
    if (!h.aimYawDegrees || !h.aimPitchDegrees) {
        return std::nullopt;
    }
    return arm::DumpHandInput{*h.aimYawDegrees, *h.aimPitchDegrees, h.aimRollDegrees, h.position};
}

void forgetTick() {
    g_viewNoted = false;
    g_arms = {};
}

} // namespace

bool on() {
    return !settings().testArmDumpPath.empty();
}

void noteView(const WorldHand& world, const ModelPlacement& model) {
    if (!on()) {
        return;
    }
    g_world = world;
    g_model = model;
    g_viewNoted = true;
}

void noteGameArm(arm::ArmSide side, float weight) {
    if (on()) {
        g_arms[slot(side)] = {};
        g_arms[slot(side)].weight = weight;
    }
}

void noteLayerArm(arm::ArmSide side,
                  float weight,
                  float twist,
                  const game_arm::ArmRead& read,
                  const arm::ArmPoses& written,
                  const xr_math::ModelPose* target) {
    if (!on()) {
        return;
    }
    arm::DumpArm& a = g_arms[slot(side)];
    a.layer = true;
    a.weight = weight;
    a.twistDegrees = twist * kDegreesPerRadian;
    a.landed = read.landed;
    a.scale = read.scale;
    a.finalPoses = written;
    a.readPoses = read.read;
    a.animated = read.animated;
    a.target = target ? std::optional<xr_math::ModelPose>(*target) : std::nullopt;
    a.root = read.root;
}

void tick(const std::byte* hands) {
    if (!on() || !g_viewNoted) {
        forgetTick();
        return;
    }
    const LONGLONG now = nowQpc();
    if (g_lastLine != 0 && qpcSeconds(now - g_lastLine) < kIntervalSeconds) {
        forgetTick();
        return;
    }
    const std::byte* owner = nullptr;
    float eye[3] = {};
    if (!safeRead(hands + kHandsOwner, owner) || !isPlayerSafe(owner) ||
        !safeCopy(eye, owner + kPlayerFirstPersonOrigin, sizeof(eye)) || !open()) {
        forgetTick();
        return;
    }
    g_lastLine = now;
    arm::ArmDumpRecord r;
    r.seconds = qpcSeconds(now);
    r.unitsPerMetre = g_world.unitsPerMetre;
    r.fault = settings().testArmTwistFault ? "twist" : "none";
    const std::optional<input::TestInput> test = testInput();
    r.input = {handInput(test, input::Hand::Left), handInput(test, input::Hand::Right)};
    r.arms = g_arms;
    r.modelValid = g_model.valid;
    r.eye = {eye[0], eye[1], eye[2]};
    r.model = g_model.pose;
    if (g_world.valid) {
        r.head = g_world.head.axis;
        r.aimValid = true;
        r.aim = g_world.aim;
        r.grip = g_world.grip.offset;
        if (g_world.offValid) {
            r.offGrip = g_world.offGrip.offset;
        }
    }
    const std::byte* decl = heldItemDecl(hands);
    if (decl != g_itemDecl) {
        g_itemDecl = decl;
        g_itemName = decl ? itemDeclName(decl) : std::string{};
    }
    r.item = g_itemName;
    const arm::DumpArm& right = g_arms[slot(arm::ArmSide::Right)];
    if (right.readPoses) {
        r.weapon = (*right.readPoses)[arm::index(arm::ArmJoint::Attach)];
    }
    for (const arm::DumpArm& a : g_arms) {
        if (a.layer) {
            r.scale = a.scale;
        }
    }
    const std::string line = arm::armDumpLine(r);
    if (std::fwrite(line.data(), 1, line.size(), g_file) != line.size() || std::fflush(g_file) != 0) {
        EVR_LOG("%s: a write failed; no more lines", kTag);
        std::fclose(g_file);
        g_file = nullptr;
        g_failed = true;
    }
    forgetTick();
}

} // namespace evr::vkcore::controllers::arm_dump
