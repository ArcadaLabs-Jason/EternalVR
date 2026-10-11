#include "features/input/family_touch_settings.hpp"

#include "features/input/rest_touch_bindings.hpp"
#include "features/input/touch_inputs.hpp"

#include <array>
#include <cstddef>

namespace evr::input {

void applyFamilyTouchSettings(MapperSettings& settings,
                              const ControllerData& data,
                              bool faceTouch,
                              bool scripted) {
    settings.restFaceTouch = faceTouchRests(data, faceTouch);
    const std::array<bool, 2> rests = wheelRestHands(data, faceTouch);
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        const std::size_t h = static_cast<std::size_t>(hand);
        // A thumb rest the map binds like a button is not the wheel's: the mapper leaves it out.
        settings.restSensors[h] = rests[h] || scripted;
        // Beside a thumb rest the face buttons' touch says the thumb is elsewhere, never that it rests.
        settings.restIsThumbRest[h] = data.find(XrActionId::ThumbRest, hand) != nullptr;
        for (std::size_t t = 0; t < kControlTouchCount; ++t) {
            settings.touchOwnControls[h][t] = ownControlsOf(data, hand, kControlTouches[t]);
            settings.touchLandingControls[h][t] = landingControlsOf(data, hand, kControlTouches[t]);
        }
    }
}

} // namespace evr::input
