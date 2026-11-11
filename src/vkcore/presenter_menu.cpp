// Menus in VR (docs/VR_MENUS.md): while the game shows its menu cursor, the menu goes onto a world-locked
// panel in front of the player (the UI quad for menus over the game, such as pause; the game's whole frame
// for the title screen and the main menu), a laser from the pointing hand meets the panel, and the router
// (features/menu/menu_router.hpp) drives the game's own cursor, clicks, back and scrolling through its raw
// input. The controllers' gameplay actions are held back meanwhile.

#include "vkcore/presenter_impl.hpp"
#include "vkcore/presenter_menu_release.hpp"

#include "vkcore/controllers.hpp"
#include "vkcore/dossier_screen_hook.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/menu_cursor.hpp"
#include "vkcore/menu_cursor_hide.hpp"
#include "vkcore/menu_input.hpp"
#include "vkcore/menu_model_hook.hpp"
#include "vkcore/prompt_hint_keys.hpp"
#include "vkcore/prompt_load_tips.hpp"
#include "xr_math/cinema_quad.hpp"

namespace evr::vkcore {

namespace {

// The pointer's drawing (presenter_menu_update.cpp has the beam's length off the panel and the dot's size):
// the beam's thickness, the beam's image and the dot's (the reticle's).
constexpr float kBeamThicknessMetres = 0.005f;
constexpr std::uint32_t kBeamWidthPixels = 16;
constexpr std::uint32_t kBeamHeightPixels = 128;
constexpr std::uint32_t kDotPixels = 64; // the reticle image, reused

XrPosef toXr(const Pose& p) {
    return {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
            {p.position.x, p.position.y, p.position.z}};
}

} // namespace

void XrPresenter::Impl::startMenu() {
    if (!settings.ui.menuPointer) {
        EVR_LOG("menu: pointer off (ETERNALVR_MENU_POINTER=0)");
        return;
    }
    menuInstalled = menu_cursor::install();
    if (menuInstalled) {
        menu_cursor_hide::install();
        menu_model::install();     // a menu's 3D model on the panel (menu_model_hook.hpp)
        dossier_screen::install(); // the Dossier screen the game shows (dossier_screen_hook.hpp)
    }
    EVR_LOG("menu: pointer %s; panel %.2f m wide, %.2f m ahead, world-locked while a menu is up%s",
            menuInstalled ? "on" : "OFF (reason above)", settings.ui.menuWidthMetres,
            settings.ui.menuDistanceMetres, settings.ui.menuBeam ? "" : "; no beam");
}

std::optional<Pose> XrPresenter::Impl::locateHead(XrTime time) const {
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    constexpr XrSpaceLocationFlags needed =
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if (XR_FAILED(xr.xrLocateSpace(viewSpace, localSpace, time, &location)) ||
        (location.locationFlags & needed) != needed) {
        return std::nullopt;
    }
    Pose head;
    head.orientation = {location.pose.orientation.x, location.pose.orientation.y, location.pose.orientation.z,
                        location.pose.orientation.w};
    head.position = {location.pose.position.x, location.pose.position.y, location.pose.position.z};
    return head;
}

bool XrPresenter::Impl::placeMenuPanel(XrTime time) {
    const std::optional<Pose> head = locateHead(time);
    if (!head) {
        return false;
    }
    menuPanel.pose = xr_math::cinemaQuadPose(*head, settings.ui.menuDistanceMetres);
    menuPanel.pose.position.y += settings.ui.offsetYMetres;
    menuPanelPlaced = true;
    menuFollow.reset();
    return true;
}

void releaseMenuInput(XrPresenter::Impl& p, const char* why) {
    controllers::releaseLoadTipsKeys();
    controllers::releaseHintKeys();
    if (!p.menuRouter || !p.menuRouter->holdsInput()) {
        return;
    }
    const menu::RouterOutput out = p.menuRouter->releaseHeld(qpcSeconds(qpcNow()));
    menu_input::send(out.events);
    menu_cursor_hide::setHidden(false);
    menu_input::setSuppressGameplay(false);
    EVR_LOG("menu: %s: the menu's held input is let go (%zu release(s))", why, out.events.size());
}

ui_layer::PixelRect XrPresenter::Impl::menuContent() const {
    const VkExtent2D image = gameExtent.width ? gameExtent : eyeExtent;
    return settings.ui.wideCrop ? ui_layer::wideContentRect(image.width, image.height)
                                : ui_layer::PixelRect{0, 0, image.width, image.height};
}

void XrPresenter::Impl::placeOnMenuPanel(XrCompositionLayerQuad& quad) const {
    if (settings.ui.wideCrop) {
        // The quad's image cut to its 16:9 band, as the panel's size is (a band already cut stays as it is).
        XrRect2Di& rect = quad.subImage.imageRect;
        const ui_layer::PixelRect band = ui_layer::wideContentRectOf(
            {rect.offset.x, rect.offset.y, static_cast<std::uint32_t>(rect.extent.width),
             static_cast<std::uint32_t>(rect.extent.height)});
        rect = {{band.x, band.y},
                {static_cast<std::int32_t>(band.width), static_cast<std::int32_t>(band.height)}};
    }
    quad.space = localSpace;
    quad.pose = toXr(menuPanel.pose);
    quad.size = {menuPanel.width, menuPanel.height};
}

std::uint32_t XrPresenter::Impl::fillPointerQuads(XrCompositionLayerQuad* first) {
    if (!menuOn || !menuPointer.visible) {
        return 0;
    }
    std::uint32_t count = 0;
    if (settings.ui.menuBeam && !menuPointerKept.beamFailed) {
        if (!menuPointerKept.beamSwapchain &&
            !createStaticImage(menuPointerKept.beamSwapchain,
                               menu::beamImage(kBeamWidthPixels, kBeamHeightPixels), kBeamWidthPixels,
                               kBeamHeightPixels, "menu beam")) {
            menuPointerKept.beamFailed = true;
        }
        const auto beam =
            menu::beamQuad(menuPointer.from, menuPointer.to, menuPointer.eye, kBeamThicknessMetres);
        if (!menuPointerKept.beamFailed && beam) {
            XrCompositionLayerQuad& quad = first[count++];
            quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            quad.space = localSpace;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = menuPointerKept.beamSwapchain;
            quad.subImage.imageRect = {
                {0, 0},
                {static_cast<std::int32_t>(kBeamWidthPixels), static_cast<std::int32_t>(kBeamHeightPixels)}};
            quad.subImage.imageArrayIndex = 0;
            quad.pose = toXr(beam->pose);
            quad.size = {beam->width, beam->length};
        }
    }
    if (menuPointer.hit && !reticleFailed) {
        if (!reticleSwapchain && !createReticle()) {
            reticleFailed = true;
        } else {
            XrCompositionLayerQuad& quad = first[count++];
            quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            quad.space = localSpace;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = reticleSwapchain;
            quad.subImage.imageRect = {
                {0, 0}, {static_cast<std::int32_t>(kDotPixels), static_cast<std::int32_t>(kDotPixels)}};
            quad.subImage.imageArrayIndex = 0;
            quad.pose = toXr(menuPointer.dot);
            quad.size = {menuPointer.dotSide, menuPointer.dotSide};
        }
    }
    return count;
}

void XrPresenter::Impl::logMenuStats() {
    const ULONGLONG now = GetTickCount64();
    if (!menuInstalled || now - lastMenuStatsTicks < 10000) {
        return;
    }
    lastMenuStatsTicks = now;
    const menu_input::Counters c = menu_input::counters();
    const auto cursor = menu_cursor::read();
    const DesktopCursorCounters desktop = desktopCursorCounters();
    EVR_LOG("menu: %llu frame(s) with the panel up; %llu move(s), %llu click(s), %llu wheel(s), %llu key(s), "
            "%llu not delivered; cursor %s at %d, %d; gameplay input %s; the game's desktop cursor moves "
            "%llu and clips %llu kept off the desktop, %llu desktop input record(s) neutralised",
            static_cast<unsigned long long>(menuFrames), static_cast<unsigned long long>(c.moves),
            static_cast<unsigned long long>(c.clicks), static_cast<unsigned long long>(c.wheels),
            static_cast<unsigned long long>(c.keys), static_cast<unsigned long long>(c.failed),
            cursor ? (cursor->active ? "shown" : "hidden") : "unreadable", cursor ? cursor->x : 0,
            cursor ? cursor->y : 0, menu_input::suppressGameplay() ? "held back" : "on",
            static_cast<unsigned long long>(desktop.moves), static_cast<unsigned long long>(desktop.clips),
            static_cast<unsigned long long>(desktop.input));
}

void XrPresenter::Impl::destroyMenuXrObjects() {
    xrSwapchains.destroy(xr, menuPointerKept.beamSwapchain);
    menuPointerKept.beamFailed = false;
}

} // namespace evr::vkcore
