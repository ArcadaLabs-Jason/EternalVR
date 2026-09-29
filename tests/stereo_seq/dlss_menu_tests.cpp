#include "stereo_seq/dlss_menu.hpp"

#include <doctest/doctest.h>

using evr::stereo_seq::DlssMenuApply;
using evr::stereo_seq::dlssMenuApply;
using evr::stereo_seq::dlssMenuFollowsGame;
using evr::stereo_seq::DlssMenuHold;
using evr::stereo_seq::dlssMenuIndexForQuality;
using evr::stereo_seq::dlssMenuShown;

namespace {

DlssMenuHold launcherDlss(int quality) {
    DlssMenuHold h;
    h.perEyeTaa = true;
    h.dlssOption = true;
    h.dlssPerEye = true;
    h.dlssQuality = quality;
    return h;
}

DlssMenuHold launcherTaa(bool dlssPerEye = true) {
    DlssMenuHold h;
    h.perEyeTaa = true;
    h.dlssPerEye = dlssPerEye;
    return h;
}

} // namespace

TEST_CASE("DLSS menu: r_dlssQuality maps to the menu's four entries") {
    CHECK(dlssMenuIndexForQuality(1) == 1);
    CHECK(dlssMenuIndexForQuality(2) == 2);
    CHECK(dlssMenuIndexForQuality(3) == 3);
    CHECK(dlssMenuIndexForQuality(0) == 1);  // Ultra Performance: shown as Performance
    CHECK(dlssMenuIndexForQuality(5) == 3);  // above the menu's range
    CHECK(dlssMenuIndexForQuality(-1) == 3); // unknown
}

TEST_CASE("DLSS menu: the launcher's DLSS is shown whatever the profile holds") {
    CHECK(dlssMenuShown(launcherDlss(3), 0) == 3);
    CHECK(dlssMenuShown(launcherDlss(2), 3) == 2);
    CHECK(dlssMenuShown(launcherDlss(1), 0) == 1);
    CHECK(dlssMenuShown(launcherDlss(0), 0) == 1);
    CHECK_FALSE(dlssMenuFollowsGame(launcherDlss(3)));
    // No DLSS feature for eye R: the layer runs TAA, so the menu shows Off.
    DlssMenuHold noTwin = launcherDlss(3);
    noTwin.dlssPerEye = false;
    CHECK(dlssMenuShown(noTwin, 3) == 0);
}

TEST_CASE("DLSS menu: the launcher's Off and a failed-closed session show Off") {
    DlssMenuHold off; // per-eye TAA not requested (ETERNALVR_STEREO_TAA=0) or failed closed
    CHECK(dlssMenuShown(off, 3) == 0);
    CHECK(dlssMenuShown(off, 0) == 0);
    off.dlssOption = true;
    off.dlssPerEye = true;
    CHECK(dlssMenuShown(off, 2) == 0);
    CHECK_FALSE(dlssMenuFollowsGame(off));
}

TEST_CASE("DLSS menu: the launcher's TAA shows the profile's own choice") {
    CHECK(dlssMenuFollowsGame(launcherTaa()));
    CHECK(dlssMenuShown(launcherTaa(), 0) == 0);
    CHECK(dlssMenuShown(launcherTaa(), 2) == 2); // the profile's DLSS runs per eye
    // Eye R cannot have a DLSS feature: the layer holds TAA, the profile's DLSS does not run.
    CHECK_FALSE(dlssMenuFollowsGame(launcherTaa(false)));
    CHECK(dlssMenuShown(launcherTaa(false), 2) == 0);
    CHECK(dlssMenuShown(launcherTaa(false), 0) == 0);
}

TEST_CASE("DLSS menu: an entry the player did not change keeps the profile's index") {
    CHECK(dlssMenuApply(launcherDlss(3), 3, 3) == DlssMenuApply::Keep);
    CHECK(dlssMenuApply(DlssMenuHold{}, 0, 0) == DlssMenuApply::Keep);
    CHECK(dlssMenuApply(launcherTaa(), 2, 2) == DlssMenuApply::Keep);
}

TEST_CASE("DLSS menu: a change is applied only when the layer follows the game") {
    CHECK(dlssMenuApply(launcherTaa(), 3, 0) == DlssMenuApply::Apply);
    CHECK(dlssMenuApply(launcherTaa(), 0, 2) == DlssMenuApply::Apply);
    CHECK(dlssMenuApply(launcherDlss(3), 0, 3) == DlssMenuApply::Ignore);
    CHECK(dlssMenuApply(launcherDlss(3), 1, 3) == DlssMenuApply::Ignore);
    CHECK(dlssMenuApply(DlssMenuHold{}, 3, 0) == DlssMenuApply::Ignore);
    CHECK(dlssMenuApply(launcherTaa(false), 3, 0) == DlssMenuApply::Ignore);
}

TEST_CASE("DLSS menu: nothing shown by the layer keeps the game's behaviour") {
    CHECK(dlssMenuApply(launcherDlss(3), 0, -1) == DlssMenuApply::Apply);
    CHECK(dlssMenuApply(DlssMenuHold{}, 2, -1) == DlssMenuApply::Apply);
}
