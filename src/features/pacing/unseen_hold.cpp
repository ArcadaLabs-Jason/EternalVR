#include "features/pacing/unseen_hold.hpp"

namespace evr::pacing {

const char* holdReasonText(HoldReason reason) {
    switch (reason) {
    case HoldReason::Menu:
        return "the session is VISIBLE: a runtime's menu is over the game";
    case HoldReason::Hidden:
        return "the session is SYNCHRONIZED: the headset shows none of the game's frames (asleep, off, "
               "or the runtime's home or another app in front)";
    case HoldReason::Away:
        return "the runtime reports the user not present (XR_EXT_user_presence): the headset is off";
    case HoldReason::RigVisible:
        return "the session is VISIBLE, taken as nobody looking "
               "(ETERNALVR_TEST_UNSEEN_VISIBLE=1, a rig test)";
    case HoldReason::None:
        break;
    }
    return "the session has focus";
}

void ViewerWatch::ready() {
    const bool senses = senses_;
    *this = ViewerWatch{};
    senses_ = senses;
}

void ViewerWatch::state(SessionView view) {
    view_ = view;
    if (view == SessionView::Focused) {
        hadFocus_ = true;
    }
}

void ViewerWatch::presence(bool present) {
    if (!senses_) {
        return;
    }
    presence_ = present ? Presence::Present : Presence::Absent;
    if (present) {
        seenPresent_ = true;
    }
}

UnseenHold ViewerWatch::hold() const {
    if (view_ == SessionView::NotRunning || !hadFocus_) {
        return {};
    }
    if (presence_ == Presence::Absent && seenPresent_) {
        return {HoldLevel::Low, HoldReason::Away};
    }
    switch (view_) {
    case SessionView::Synchronized:
        return {HoldLevel::Low, HoldReason::Hidden};
    case SessionView::Visible:
        return {HoldLevel::DisplayRate, HoldReason::Menu};
    case SessionView::Focused:
    case SessionView::NotRunning:
        break;
    }
    return {};
}

} // namespace evr::pacing
