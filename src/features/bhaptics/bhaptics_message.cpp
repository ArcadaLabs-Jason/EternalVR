#include "features/bhaptics/bhaptics_message.hpp"

#include <algorithm>

namespace evr::bhaptics {

namespace {

std::string percentEncoded(std::string_view text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                           c == '-' || c == '.' || c == '_' || c == '~';
        if (plain) {
            out += c;
        } else {
            out += '%';
            out += kHex[u >> 4];
            out += kHex[u & 0x0F];
        }
    }
    return out;
}

void appendFrame(std::string& out, const Frame& frame) {
    const char* position = positionName(frame.device);
    out += R"({"Type":"frame","Key":")";
    out += frameKey(frame.effect, frame.device);
    out += R"(","Frame":{"position":")";
    out += position;
    out += R"(","dotPoints":[)";
    bool first = true;
    const int motors = motorCount(frame.device);
    for (const Dot& dot : frame.dots) {
        if (dot.index >= motors) {
            continue;
        }
        out += first ? "" : ",";
        first = false;
        out += R"({"index":)" + std::to_string(dot.index) + R"(,"intensity":)" +
               std::to_string(std::min<int>(dot.intensity, 100)) + "}";
    }
    out +=
        R"(],"pathPoints":[],"durationMillis":)" + std::to_string(std::max(1, frame.durationMillis)) + "}}";
}

} // namespace

std::string feedbackPath(std::string_view appId, std::string_view appName) {
    return "/v2/feedbacks?app_id=" + percentEncoded(appId) + "&app_name=" + percentEncoded(appName);
}

std::string frameKey(Effect effect, Device device) {
    return std::string("evr_") + effectName(effect) + "_" + positionName(device);
}

std::string submitMessage(const std::vector<Frame>& frames) {
    if (frames.empty()) {
        return {};
    }
    std::string out = R"({"Submit":[)";
    bool first = true;
    for (const Frame& frame : frames) {
        out += first ? "" : ",";
        first = false;
        appendFrame(out, frame);
    }
    out += "]}";
    return out;
}

std::string turnOffAllMessage() {
    return R"({"Submit":[{"Type":"turnOffAll","Key":""}]})";
}

} // namespace evr::bhaptics
