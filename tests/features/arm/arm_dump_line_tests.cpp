#include "features/arm/arm_dump_line.hpp"

#include "features/arm/arm_fixture.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <string>

using evr::Vec3;
using evr::arm::armDumpLine;
using evr::arm::ArmDumpRecord;
using evr::arm::ArmJoint;
using evr::arm::DumpArm;
using evr::arm::DumpHandInput;
using evr::arm::index;
using evr::test::bindArm;

namespace {

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// Braces and brackets outside strings close in order and nothing follows the object but the newline.
bool balanced(const std::string& line) {
    std::string open;
    bool inString = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inString) {
            if (c == '\\') {
                ++i;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            open += c;
        } else if (c == '}' || c == ']') {
            if (open.empty() || (c == '}') != (open.back() == '{')) {
                return false;
            }
            open.pop_back();
            if (open.empty() && i + 2 != line.size()) {
                return false;
            }
        }
    }
    return open.empty() && !inString;
}

ArmDumpRecord sample() {
    ArmDumpRecord r;
    r.seconds = 1234.5;
    r.unitsPerMetre = 1.0f;
    r.fault = "twist";
    r.input[0] = DumpHandInput{10.0f, -5.0f, 90.0f, Vec3{-0.2f, -0.3f, -0.4f}};
    DumpArm left;
    left.layer = true;
    left.weight = 1.0f;
    left.twistDegrees = 45.0f;
    left.landed = 0.002f;
    left.finalPoses = bindArm();
    left.readPoses = bindArm();
    left.animated = bindArm();
    left.target = bindArm()[index(ArmJoint::Hand)];
    left.root = evr::xr_math::ModelPose{{0.0f, 0.0f, -1.6f}, {}};
    r.arms[0] = left;
    r.arms[1].weight = 0.25f;
    r.modelValid = true;
    r.eye = {100.0f, 200.0f, 30.0f};
    r.model.offset = {0.3f, -0.1f, -0.2f};
    r.item = "weapon/player/shotgun";
    r.weapon = bindArm()[index(ArmJoint::Attach)];
    r.aimValid = true;
    r.aim.offset = {0.3f, -0.15f, -0.25f};
    r.grip = Vec3{0.3f, -0.15f, -0.25f};
    r.armsItem = "weapon/player/fists_swim";
    r.handsState = 12;
    r.handsSwim = true;
    r.mods = {"", "weapon/player/heavy_cannon_bolt_action"};
    r.sightRow = "weapon/player/heavy_cannon_bolt_action";
    return r;
}

} // namespace

TEST_CASE("a dump line is one balanced JSON object with every key in order") {
    const std::string line = armDumpLine(sample());
    REQUIRE(line.size() > 2);
    CHECK(line.back() == '\n');
    CHECK(line.find('\n') == line.size() - 1);
    CHECK(balanced(line));
    CHECK(line.rfind(
              "{\"t\":1234.50000,\"upm\":1.00000,\"scale\":[1.00000,1.00000,1.00000],\"fault\":\"twist\",",
              0) == 0);
    std::size_t at = 0;
    for (const char* key : {"\"input\":", "\"arms\":", "\"model\":", "\"head\":", "\"item\":", "\"weapon\":",
                            "\"aim\":", "\"grip\":", "\"offgrip\":", "\"arms_item\":", "\"hstate\":",
                            "\"hswim\":", "\"mods\":", "\"sight\":"}) {
        const std::size_t found = line.find(key, at);
        CHECK_MESSAGE(found != std::string::npos, key);
        at = found == std::string::npos ? at : found;
    }
}

TEST_CASE("a dump line gives the scripted input, the arms and the view as documented") {
    const std::string line = armDumpLine(sample());
    CHECK(contains(line, "\"input\":{\"L\":{\"yaw\":10.00000,\"pitch\":-5.00000,\"roll\":90.00000,"
                         "\"pos\":[-0.20000,-0.30000,-0.40000]},\"R\":null}"));
    CHECK(contains(line, "\"arms\":{\"L\":{\"posing\":\"layer\",\"weight\":1.00000,\"twist\":45.00000,"
                         "\"landed\":0.00200,\"final\":{\"attach\":[0.58500,0.00000,1.26750,1.00000,0.00000,"
                         "0.00000,0.00000,1.00000,0.00000,0.00000,0.00000,1.00000],\"hand\":[0.37829,"));
    CHECK(contains(line, "\"root\":[0.00000,0.00000,-1.60000,1.00000,0.00000,0.00000,0.00000,1.00000,0.00000,"
                         "0.00000,0.00000,1.00000],\"zero\":0,\"fingers\":null},\"R\":"));
    for (const char* key : {"\"roll3\":[", "\"roll2\":[", "\"roll1\":[", "\"forearmroll\":[", "\"forearm\":[",
                            "\"upperarm\":[-0.03531,0.22438,1.45470,"}) {
        CHECK_MESSAGE(contains(line, key), key);
    }
    // The arm the game poses has its share and no poses.
    CHECK(contains(line,
                   "\"R\":{\"posing\":\"game\",\"weight\":0.25000,\"twist\":0.00000,\"landed\":-1.00000,"
                   "\"final\":null,\"read\":null,\"animated\":null,\"target\":null,\"root\":null,"
                   "\"zero\":0,\"fingers\":null}"));
    CHECK(contains(line,
                   "\"model\":{\"eye\":[100.00000,200.00000,30.00000],\"offset\":[0.30000,-0.10000,"
                   "-0.20000],\"axis\":[1.00000,0.00000,0.00000,0.00000,1.00000,0.00000,0.00000,0.00000,"
                   "1.00000]},\"head\":null,\"item\":\"weapon/player/shotgun\",\"weapon\":[0.58500,"));
    CHECK(contains(line, "\"aim\":{\"origin\":[0.30000,-0.15000,-0.25000],\"dir\":[1.00000,0.00000,0.00000]},"
                         "\"grip\":[0.30000,-0.15000,-0.25000],\"offgrip\":null,"
                         "\"arms_item\":\"weapon/player/fists_swim\",\"hstate\":12,\"hswim\":true,"
                         "\"mods\":[\"\",\"weapon/player/heavy_cannon_bolt_action\"],"
                         "\"sight\":\"weapon/player/heavy_cannon_bolt_action\"}\n"));
}

TEST_CASE("a dump line gives the joints hands only writes scaled to nothing, and the forearm fault") {
    ArmDumpRecord r = sample();
    r.fault = "forearm";
    r.arms[0].zeroScale = 0xFCu; // the roll joints, the elbow and the shoulder (ArmJoint 2 to 7)
    r.arms[1].layer = true;
    r.arms[1].zeroScale = 0x80u; // the shoulder alone
    const std::string line = armDumpLine(r);
    CHECK(balanced(line));
    CHECK(contains(line, "\"fault\":\"forearm\","));
    CHECK(contains(line, "1.00000],\"zero\":252,\"fingers\":null},\"R\":{\"posing\":\"layer\","));
    CHECK(contains(line, "\"root\":null,\"zero\":128,\"fingers\":null}},\"model\":"));
}

TEST_CASE("a dump line gives the free off hand's fingers: curls, each bone's turn written and read") {
    ArmDumpRecord r = sample();
    evr::arm::DumpFingers f;
    f.curl = {0.0f, 0.3f, 1.0f, 1.0f, 0.5f};
    f.want[0] = 12.5f;
    f.want[14] = 40.0f;
    r.arms[0].fingers = f;
    std::string line = armDumpLine(r);
    CHECK(balanced(line));
    CHECK(contains(line, "\"zero\":0,\"fingers\":{\"curl\":[0.00000,0.30000,1.00000,1.00000,0.50000],"
                         "\"want\":[12.50000,0.00000,"));
    CHECK(contains(line, ",40.00000],\"read\":null}},\"R\":"));
    f.read = std::array<float, 15>{};
    (*f.read)[3] = 7.0f;
    r.arms[0].fingers = f;
    line = armDumpLine(r);
    CHECK(balanced(line));
    CHECK(contains(line, "\"read\":[0.00000,0.00000,0.00000,7.00000,0.00000,"));
}

TEST_CASE("a dump line escapes the item's name and writes null for what is not known") {
    ArmDumpRecord r;
    r.item = "odd\"name\\x\n";
    r.unitsPerMetre = std::numeric_limits<float>::quiet_NaN();
    const std::string line = armDumpLine(r);
    CHECK(balanced(line));
    CHECK(contains(line, "\"item\":\"odd\\\"name\\\\x\\u000a\""));
    CHECK(contains(line, "\"upm\":null"));
    CHECK(contains(line, "\"fault\":\"none\""));
    CHECK(contains(line, "\"input\":{\"L\":null,\"R\":null}"));
    CHECK(contains(line, "\"model\":null,\"head\":null"));
    CHECK(contains(line, "\"weapon\":null,\"aim\":null,\"grip\":null,\"offgrip\":null,\"arms_item\":\"\","
                         "\"hstate\":null,\"hswim\":null,\"mods\":[\"\",\"\"],\"sight\":\"\"}"));
}
