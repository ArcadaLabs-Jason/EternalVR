#include "stereo_seq/dlss_menu.hpp"

namespace evr::stereo_seq {

namespace {

constexpr int kMenuOff = 0;
constexpr int kMenuPerformance = 1;
constexpr int kMenuQuality = 3;

} // namespace

int dlssMenuIndexForQuality(int quality) {
    if (quality == 0) {
        return kMenuPerformance; // Ultra Performance: the nearest entry the menu has
    }
    if (quality >= kMenuPerformance && quality <= kMenuQuality) {
        return quality;
    }
    return kMenuQuality;
}

bool dlssMenuFollowsGame(const DlssMenuHold& hold) {
    return hold.perEyeTaa && !hold.dlssOption && hold.dlssPerEye;
}

int dlssMenuShown(const DlssMenuHold& hold, int profileIndex) {
    if (!hold.perEyeTaa) {
        return kMenuOff; // the v1 set or the launcher's Off: r_antialiasing 0
    }
    if (hold.dlssOption) {
        return hold.dlssPerEye ? dlssMenuIndexForQuality(hold.dlssQuality) : kMenuOff;
    }
    // The launcher's TAA keeps the profile's DLSS, per eye when eye R can have a feature, else TAA.
    return profileIndex != kMenuOff && !hold.dlssPerEye ? kMenuOff : profileIndex;
}

DlssMenuApply dlssMenuApply(const DlssMenuHold& hold, int chosen, int shown) {
    if (shown < 0) {
        return DlssMenuApply::Apply;
    }
    if (chosen == shown) {
        return DlssMenuApply::Keep;
    }
    return dlssMenuFollowsGame(hold) ? DlssMenuApply::Apply : DlssMenuApply::Ignore;
}

const char* dlssMenuApplyName(DlssMenuApply apply) {
    switch (apply) {
    case DlssMenuApply::Keep:
        return "unchanged: the profile's own index kept";
    case DlssMenuApply::Apply:
        return "applied as in the flat game";
    case DlssMenuApply::Ignore:
        return "not used in VR (the launcher's Anti-aliasing decides): the profile's own index kept";
    }
    return "?";
}

} // namespace evr::stereo_seq
