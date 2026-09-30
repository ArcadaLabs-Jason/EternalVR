#pragma once

// The bHaptics Player's local WebSocket messages (docs/BHAPTICS.md): the layer submits raw motor frames
// ("dot" frames), so it needs no pattern files, no app registered with bHaptics and no key. Pure text
// building; the connection is the layer's (src/vkcore/bhaptics_link.hpp).
//
//   ws://127.0.0.1:15881/v2/feedbacks?app_id=<id>&app_name=<name>
//   {"Submit":[{"Type":"frame","Key":"evr_shot_ForearmR","Frame":{"position":"ForearmR",
//     "dotPoints":[{"index":0,"intensity":65}],"pathPoints":[],"durationMillis":90}}]}
//
// A submission replaces one still playing under the same key, so each effect and device has its own key.

#include "features/bhaptics/body_haptics.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace evr::bhaptics {

inline constexpr int kPlayerPort = 15881;
inline constexpr std::string_view kAppId = "com.arcadalabs.eternalvr";
inline constexpr std::string_view kAppName = "EternalVR";

// The request path with the app's id and name (letters, digits and "-._~" are kept; anything else is
// percent-encoded).
std::string feedbackPath(std::string_view appId = kAppId, std::string_view appName = kAppName);

// "evr_<effect>_<position>".
std::string frameKey(Effect effect, Device device);

// One message submitting every frame; empty when there are none.
std::string submitMessage(const std::vector<Frame>& frames);

// Stops everything this app plays.
std::string turnOffAllMessage();

} // namespace evr::bhaptics
