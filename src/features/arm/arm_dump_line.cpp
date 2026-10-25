#include "features/arm/arm_dump_line.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

namespace evr::arm {

namespace {

void number(std::string& out, double value) {
    if (!std::isfinite(value)) {
        out += "null";
        return;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.5f", value);
    out += buffer;
}

void floats(std::string& out, const float* v, std::size_t n) {
    out += '[';
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0) {
            out += ',';
        }
        number(out, v[i]);
    }
    out += ']';
}

void vec(std::string& out, Vec3 v) {
    out += '[';
    number(out, v.x);
    out += ',';
    number(out, v.y);
    out += ',';
    number(out, v.z);
    out += ']';
}

void axisNumbers(std::string& out, const xr_math::IdViewAxis& a) {
    const Vec3 rows[3] = {a.forward, a.left, a.up};
    for (std::size_t r = 0; r < 3; ++r) {
        if (r > 0) {
            out += ',';
        }
        number(out, rows[r].x);
        out += ',';
        number(out, rows[r].y);
        out += ',';
        number(out, rows[r].z);
    }
}

void axis(std::string& out, const xr_math::IdViewAxis& a) {
    out += '[';
    axisNumbers(out, a);
    out += ']';
}

void pose(std::string& out, const xr_math::ModelPose& p) {
    out += '[';
    number(out, p.position.x);
    out += ',';
    number(out, p.position.y);
    out += ',';
    number(out, p.position.z);
    out += ',';
    axisNumbers(out, p.axis);
    out += ']';
}

void poseSet(std::string& out, const std::optional<ArmPoses>& poses) {
    if (!poses) {
        out += "null";
        return;
    }
    out += '{';
    for (std::size_t i = 0; i < kArmJointCount; ++i) {
        if (i > 0) {
            out += ',';
        }
        out += '"';
        out += kDumpJointKeys[i];
        out += "\":";
        pose(out, (*poses)[i]);
    }
    out += '}';
}

void text(std::string& out, std::string_view s) {
    out += '"';
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                          static_cast<unsigned>(static_cast<unsigned char>(c)));
            out += buffer;
        } else {
            out += c;
        }
    }
    out += '"';
}

void handInput(std::string& out, const std::optional<DumpHandInput>& hand) {
    if (!hand) {
        out += "null";
        return;
    }
    out += "{\"yaw\":";
    number(out, hand->yawDegrees);
    out += ",\"pitch\":";
    number(out, hand->pitchDegrees);
    out += ",\"roll\":";
    number(out, hand->rollDegrees);
    out += ",\"pos\":";
    if (hand->position) {
        vec(out, *hand->position);
    } else {
        out += "null";
    }
    out += '}';
}

void armRecord(std::string& out, const DumpArm& arm) {
    out += "{\"posing\":";
    text(out, arm.layer ? "layer" : "game");
    out += ",\"weight\":";
    number(out, arm.weight);
    out += ",\"twist\":";
    number(out, arm.twistDegrees);
    out += ",\"landed\":";
    number(out, arm.landed);
    out += ",\"final\":";
    poseSet(out, arm.finalPoses);
    out += ",\"read\":";
    poseSet(out, arm.readPoses);
    out += ",\"animated\":";
    poseSet(out, arm.animated);
    out += ",\"target\":";
    if (arm.target) {
        pose(out, *arm.target);
    } else {
        out += "null";
    }
    out += ",\"root\":";
    if (arm.root) {
        pose(out, *arm.root);
    } else {
        out += "null";
    }
    out += ",\"zero\":";
    out += std::to_string(arm.zeroScale);
    out += ",\"fingers\":";
    if (arm.fingers) {
        out += "{\"curl\":";
        floats(out, arm.fingers->curl.data(), arm.fingers->curl.size());
        out += ",\"want\":";
        floats(out, arm.fingers->want.data(), arm.fingers->want.size());
        out += ",\"read\":";
        if (arm.fingers->read) {
            floats(out, arm.fingers->read->data(), arm.fingers->read->size());
        } else {
            out += "null";
        }
        out += '}';
    } else {
        out += "null";
    }
    out += '}';
}

void optionalVec(std::string& out, const std::optional<Vec3>& v) {
    if (v) {
        vec(out, *v);
    } else {
        out += "null";
    }
}

} // namespace

std::string armDumpLine(const ArmDumpRecord& r) {
    std::string out;
    out.reserve(8192);
    out += "{\"t\":";
    number(out, r.seconds);
    out += ",\"upm\":";
    number(out, r.unitsPerMetre);
    out += ",\"scale\":";
    vec(out, r.scale);
    out += ",\"fault\":";
    text(out, r.fault ? r.fault : "none");
    out += ",\"input\":{\"L\":";
    handInput(out, r.input[0]);
    out += ",\"R\":";
    handInput(out, r.input[1]);
    out += "},\"arms\":{\"L\":";
    armRecord(out, r.arms[0]);
    out += ",\"R\":";
    armRecord(out, r.arms[1]);
    out += "},\"model\":";
    if (r.modelValid) {
        out += "{\"eye\":";
        vec(out, r.eye);
        out += ",\"offset\":";
        vec(out, r.model.offset);
        out += ",\"axis\":";
        axis(out, r.model.axis);
        out += '}';
    } else {
        out += "null";
    }
    out += ",\"head\":";
    if (r.head) {
        axis(out, *r.head);
    } else {
        out += "null";
    }
    out += ",\"item\":";
    text(out, r.item);
    out += ",\"weapon\":";
    if (r.weapon) {
        pose(out, *r.weapon);
    } else {
        out += "null";
    }
    out += ",\"aim\":";
    if (r.aimValid) {
        out += "{\"origin\":";
        vec(out, r.aim.offset);
        out += ",\"dir\":";
        vec(out, r.aim.axis.forward);
        out += '}';
    } else {
        out += "null";
    }
    out += ",\"grip\":";
    optionalVec(out, r.grip);
    out += ",\"offgrip\":";
    optionalVec(out, r.offGrip);
    out += ",\"arms_item\":";
    text(out, r.armsItem);
    out += ",\"hstate\":";
    out += r.handsState ? std::to_string(*r.handsState) : std::string("null");
    out += ",\"hswim\":";
    out += !r.handsSwim ? "null" : *r.handsSwim ? "true" : "false";
    out += ",\"mods\":[";
    text(out, r.mods[0]);
    out += ',';
    text(out, r.mods[1]);
    out += "],\"sight\":";
    text(out, r.sightRow);
    out += "}\n";
    return out;
}

} // namespace evr::arm
