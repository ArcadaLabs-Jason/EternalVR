// Menus in VR (docs/VR_MENUS.md): while the game shows its menu cursor, the menu goes onto a world-locked
// panel in front of the player (the UI quad for menus over the game, such as pause; the game's whole frame
// for the title screen and the main menu), a laser from the pointing hand meets the panel, and the router
// (features/menu/menu_router.hpp) drives the game's own cursor, clicks, back and scrolling through its raw
// input. The controllers' gameplay actions are held back meanwhile.

#include "vkcore/presenter_impl.hpp"

#include "features/menu/menu_kind.hpp"
#include "game/eternal/usercmd_buttons.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/menu_cursor.hpp"
#include "vkcore/menu_input.hpp"
#include "vkcore/mp_guard.hpp"
#include "xr_math/cinema_quad.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace evr::vkcore {

namespace {

// The pointer's drawing: the beam's thickness, its length when it misses the panel, the dot's angular size.
constexpr float kBeamThicknessMetres = 0.005f;
constexpr float kBeamMissMetres = 1.0f;
constexpr float kDotDegrees = 0.9f;
constexpr float kDotOffsetMetres = 0.004f; // in front of the panel
constexpr std::uint32_t kBeamWidthPixels = 16;
constexpr std::uint32_t kBeamHeightPixels = 128;
constexpr std::uint32_t kDotPixels = 64; // the reticle image, reused
constexpr double kPanelGraceSeconds = 0.5;
constexpr double kMenuHoldSeconds = 1.0;
// A menu over the game that comes up this long after the controllers asked for one (the pause key, the
// Dossier) is taken for a popup the game raised by itself. The rig saw the pause menu's cursor 0.05 s after
// the key and the Dossier's 0.03 s after its button.
constexpr double kMenuRequestSeconds = 1.5;
// The strip at the top of the menu's 16:9 band where tabbed screens (the Dossier, settings) have their tabs.
constexpr float kTabStripV = 0.08f;
// The room transform moved by more than this while a panel is up: a re-anchor (a recenter, the runtime's
// or ours, or the height after standing up or sitting down); the panel is placed in front of the head again.
constexpr float kReanchorMetres = 0.05f;
constexpr float kReanchorDegrees = 3.0f;

bool reanchored(const Pose& a, const Pose& b) {
    const float dot = std::fabs(a.orientation.x * b.orientation.x + a.orientation.y * b.orientation.y +
                                a.orientation.z * b.orientation.z + a.orientation.w * b.orientation.w);
    const float degrees = 2.0f * std::acos(std::min(1.0f, dot)) * 57.29578f;
    return length(a.position - b.position) > kReanchorMetres || degrees > kReanchorDegrees;
}

XrPosef toXr(const Pose& p) {
    return {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
            {p.position.x, p.position.y, p.position.z}};
}

menu::RouterHand routerHand(const input::HandState& h) {
    menu::RouterHand r;
    r.trigger = h.trigger;
    r.grip = h.grip;
    r.stick = h.stick;
    r.primary = h.primaryButton;
    r.secondary = h.secondaryButton;
    r.stickClick = h.stickClick;
    return r;
}

} // namespace

void XrPresenter::Impl::startMenu() {
    if (!settings.ui.menuPointer) {
        EVR_LOG("menu: pointer off (ETERNALVR_MENU_POINTER=0)");
        return;
    }
    menuInstalled = menu_cursor::install();
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

void XrPresenter::Impl::updateMenu(XrTime time, bool panelContent) {
    const std::optional<menu_cursor::CursorState> cursor =
        menuInstalled ? menu_cursor::read() : std::optional<menu_cursor::CursorState>{};
    const bool allowed = menuInstalled && mp_guard::allowsGameTouch();
    // A frame or two without a panel (a GUI capture a little late) does not end menu mode: the panel
    // keeps its place and the router its state for up to kPanelGraceSeconds.
    const double now = qpcSeconds(qpcNow());
    if (panelContent) {
        menuPanelSeen = now;
    }
    const bool showing = panelContent || ((menuOn || menuHeld) && now - menuPanelSeen < kPanelGraceSeconds);
    if (!menuRouter) {
        menuRouter.emplace(controllers::dominantHand(), controllers::mapSticks());
    }
    // A cursor the game shows for the weapon wheel is not a menu: the stick selects and gameplay input stays
    // on (wheel_cursor.hpp).
    menu::WheelCursorInput wheelIn;
    wheelIn.seconds = now;
    wheelIn.cursorShown = allowed && cursor && cursor->active;
    wheelIn.menuUp = menuOn || menuHeld;
    wheelIn.wheelHeld = game::contains(controllers::heldActions(), game::GameAction::WeaponWheel);
    const menu::WheelCursorOutput wheel = menuRouter->wheelCursor().update(wheelIn);
    if (wheel.started) {
        EVR_LOG("menu: the weapon wheel is up: the game shows its cursor at %d, %d; no panel or pointer, the "
                "stick selects and gameplay input stays on",
                cursor->x, cursor->y);
    } else if (wheel.ended) {
        EVR_LOG("menu: the weapon wheel's cursor is gone%s",
                wheelIn.cursorShown ? "; the cursor stays: a menu" : "");
    }
    const bool cursorActive = wheelIn.cursorShown && !wheel.owned;
    const bool active = cursorActive && showing;
    const std::optional<input::InputFrame> frame = controllers::latestFrame();
    // The cursor can go while its menu stays up (the pause menu logged "the cursor is gone" 0.12 s after it
    // opened). While the backdrop test (backdrop_probe.hpp) still sees the menu's full-screen backdrop, the
    // panel keeps its place for up to kMenuHoldSeconds; the pointer and the router's hold on gameplay end
    // with the cursor. A cursor back within the hold finds the panel where it was.
    if (!active && menuOn) {
        menuCursorGone = now;
    }
    const bool wasHeld = menuHeld;
    menuHeld = !active && (menuOn || menuHeld) && allowed && showing && uiBackdrop.backdrop() &&
               now - menuCursorGone < kMenuHoldSeconds;
    if (wasHeld && !menuHeld && !active) {
        EVR_LOG("menu: the panel is down (held %.2f s)", now - menuCursorGone);
    }

    if (active && !menuOn && wasHeld) {
        EVR_LOG("menu: the cursor is back; the panel stays where it was");
    } else if (active && !menuOn) {
        // Tutorial and lore popups show the cursor over the game unasked; they wait for Space or E. The
        // Dossier the controllers asked for opens on its map page (the router follows its tabs). A cursor
        // back within moments of the last one going is the same menu changing screens and keeps its kind
        // (menu_kind.hpp).
        menu::MenuKindInput kindIn;
        kindIn.overGame = panelContent && shownHasView;
        kindIn.menuRequested = controllers::menuRequestedWithin(kMenuRequestSeconds);
        kindIn.dossierRequested = controllers::dossierRequestedWithin(kMenuRequestSeconds);
        kindIn.hadMenu = menuCursorGone > 0.0;
        kindIn.previous = menuPopup     ? menu::MenuKind::Popup
                          : menuDossier ? menu::MenuKind::Dossier
                                        : menu::MenuKind::Screen;
        kindIn.sinceCursorGone = now - menuCursorGone;
        const menu::MenuKind kind = menu::menuKindOnCursor(kindIn);
        if (kindIn.hadMenu && kindIn.sinceCursorGone < menu::kMenuContinueSeconds) {
            EVR_LOG("menu: the cursor is back after %.2f s: the same menu (%s) on another screen",
                    kindIn.sinceCursorGone, menu::menuKindName(kind));
        }
        menuPopup = kind == menu::MenuKind::Popup;
        menuDossier = kind == menu::MenuKind::Dossier;
        // A menu came up: its panel goes in front of the head (yaw only) at the UI quad's height.
        placeMenuPanel(time);
        menuReplace.store(false, std::memory_order_relaxed);
        menuRoom.reset();
        const ui_layer::PixelRect content = menuContent();
        if (const auto size =
                ui_layer::panelSize(settings.ui.menuWidthMetres, content.width, content.height)) {
            menuPanel.width = size->width;
            menuPanel.height = size->height;
        }
        EVR_LOG("menu: the game shows its cursor (%s): panel %.2f x %.2f m at (%.2f, %.2f, %.2f)%s",
                menuPopup     ? "a popup over the game, not asked for: A / X sends Space, a stick click E, Y "
                                "Left Alt, the gameplay buttons their actions' keys"
                : menuDossier ? "the Dossier, on its map: the sticks move the map"
                : panelContent && shownHasView ? "a menu over the game"
                                               : "a menu screen",
                menuPanel.width, menuPanel.height, menuPanel.pose.position.x, menuPanel.pose.position.y,
                menuPanel.pose.position.z, frame ? "" : "; no controllers");
    } else if (!active && menuOn) {
        EVR_LOG("menu: the cursor is gone; %s",
                menuHeld ? "the pointer is down, the panel stays while the menu's backdrop shows"
                         : "the panel and the pointer are down");
    }
    menuOn = active && menuPanelPlaced && menuPanel.width > 0.0f;
    menuHeld = menuHeld && menuPanelPlaced && menuPanel.width > 0.0f;
    menuUp.store(cursorActive || menuOn || menuHeld, std::memory_order_relaxed);

    menu::RouterInput in;
    in.seconds = now;
    in.menuActive = menuOn;
    in.popup = menuOn && menuPopup;
    in.dossier = menuOn && menuDossier;
    in.actions = controllers::heldActions();
    in.width = gameExtent.width;
    in.height = gameExtent.height;
    if (cursor) {
        in.gameCursor = menu::CursorPixel{cursor->x, cursor->y};
    }
    // A re-anchor while the panel is up (the runtime moved LOCAL, or the room transform jumped: a recenter or
    // the height after standing up or sitting down): the panel goes in front of the head again. The panel
    // and the rays are both in LOCAL, so they agree either way; this keeps the panel where the player is.
    // So does turning away from the panel for a while (ETERNALVR_MENU_FOLLOW, panel_follow.hpp).
    if ((menuOn || menuHeld) && frame) {
        const bool runtimeMoved = menuReplace.exchange(false, std::memory_order_relaxed);
        bool turnedAway = false;
        if (settings.ui.menuFollow) {
            if (const std::optional<Pose> head = locateHead(time)) {
                turnedAway = menuFollow.update(menu::horizontalAngleTo(*head, menuPanel.pose.position), now);
            }
        }
        if (runtimeMoved || turnedAway || (menuRoom && reanchored(*menuRoom, frame->roomFromLocal))) {
            if (placeMenuPanel(time)) {
                EVR_LOG("menu: %s; the panel is placed again in front of the head at (%.2f, %.2f, %.2f)",
                        runtimeMoved ? "the runtime moved LOCAL"
                        : turnedAway ? "the head turned away from the panel"
                                     : "the room was re-anchored",
                        menuPanel.pose.position.x, menuPanel.pose.position.y, menuPanel.pose.position.z);
            }
        }
        menuRoom = frame->roomFromLocal;
    } else {
        menuRoom.reset();
    }

    if (frame) {
        // Left Menu held + a trigger is the in-headset capture (the mapper asks for it): no click meanwhile.
        const input::CaptureChordOutput chord = menuChord.update(*frame);
        for (const input::Hand hand : {input::Hand::Left, input::Hand::Right}) {
            const input::HandState& h = frame->hand(hand);
            menu::RouterHand& r = in.hands[static_cast<std::size_t>(hand)];
            r = routerHand(h);
            if (chord.triggerHeldBack[static_cast<std::size_t>(hand)]) {
                r.trigger = 0.0f;
            }
            if (menuOn && h.poseValid) {
                // The hand is located in room space; the panel lives in LOCAL.
                r.hit = menu::intersectPanel(menuPanel, menu::localFromRoom(frame->roomFromLocal, h.aimPose));
                r.onTabStrip = r.hit && r.hit->v < kTabStripV;
            }
        }
    }
    // The panel shows the GUI's 16:9 band (menuContent): the router takes the hits on the whole image.
    menu::RouterInput routed = in;
    const ui_layer::PixelRect content = menuContent();
    for (menu::RouterHand& r : routed.hands) {
        if (r.hit) {
            const ui_layer::ImageUv uv =
                ui_layer::contentToImage(r.hit->u, r.hit->v, content, in.width, in.height);
            r.hit->u = uv.u;
            r.hit->v = uv.v;
        }
    }
    const menu::RouterOutput out = menuRouter->update(routed);
    // Vibration: a light tick on the pointing hand as its ray comes onto the panel (enter_tick.hpp: a frame
    // without controller data is not a miss), and on a click.
    for (const input::Hand hand : {input::Hand::Left, input::Hand::Right}) {
        menu::EnterTick& enter = menuPointerKept.enterTicks[static_cast<std::size_t>(hand)];
        if (!menuOn) {
            // While the panel is only held (the cursor went a moment), the ray is off it: a cursor that
            // flickers back does not tick again. A closed menu starts afresh.
            if (menuHeld) {
                enter.update(false, now);
            } else {
                enter.reset();
            }
            continue;
        }
        std::optional<bool> hit;
        if (frame && frame->hand(hand).poseValid) {
            hit = in.hands[static_cast<std::size_t>(hand)].hit.has_value();
        }
        if (enter.update(hit, now) && hand == out.pointerHand) {
            controllers::noteMenuHaptic(hand, input::MenuTick::Enter);
        }
    }
    for (const menu::RouterEvent& e : out.events) {
        if (e.kind == menu::RouterEvent::Kind::ButtonDown) {
            controllers::noteMenuHaptic(out.pointerHand, input::MenuTick::Click);
        }
    }
    for (const menu::PopupActionKey& k : out.popupKeys) {
        const std::string_view action = game::gameActionName(k.action);
        const std::string_view key = game::keyName(k.key);
        EVR_LOG("menu: popup: %.*s sends %.*s (0x%02x)", static_cast<int>(action.size()), action.data(),
                static_cast<int>(key.size()), key.data(), k.key);
    }
    menu_input::send(out.events);
    menu_input::setSuppressGameplay(out.suppressGameplay);
    if (out.mapPage != menuMapPage) {
        menuMapPage = out.mapPage;
        EVR_LOG("menu: %s", menuMapPage ? "the Dossier's map page: the sticks pan, zoom and rotate the map"
                                        : "not the Dossier's map page (or not known): the sticks scroll and "
                                          "change tabs");
    }

    // What the pointer looks like this frame.
    menuPointer = {};
    if (menuOn && out.pointerVisible && frame) {
        const input::HandState& h = frame->hand(out.pointerHand);
        const std::optional<menu::PanelHit>& hit = in.hands[static_cast<std::size_t>(out.pointerHand)].hit;
        if (h.poseValid) {
            const Pose aim = menu::localFromRoom(frame->roomFromLocal, h.aimPose);
            menuPointer.visible = true;
            menuPointer.from = aim.position;
            // The beam turns to face the head (without a head pose: the panel's viewing position).
            menuPointer.eye =
                frame->head.poseValid
                    ? menu::localFromRoom(frame->roomFromLocal, frame->head.pose).position
                    : transformPoint(menuPanel.pose, Vec3{0.0f, 0.0f, settings.ui.menuDistanceMetres});
            if (hit) {
                menuPointer.hit = true;
                menuPointer.to = hit->point;
                menuPointer.dot = menu::dotPose(menuPanel, hit->u, hit->v, kDotOffsetMetres);
                menuPointer.dotSide =
                    ui_layer::reticleSideMetres(length(hit->point - menuPointer.eye), kDotDegrees);
            } else {
                menuPointer.to = aim.position + rotate(aim.orientation, Vec3{0.0f, 0.0f, -kBeamMissMetres});
            }
        }
    }
    if (menuOn) {
        ++menuFrames;
    }
    logMenuStats();
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
        const ui_layer::PixelRect band = ui_layer::wideContentRect(
            static_cast<std::uint32_t>(rect.extent.width), static_cast<std::uint32_t>(rect.extent.height));
        rect.offset.y += band.y;
        rect.extent.height = static_cast<std::int32_t>(band.height);
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
    if (menuPointerKept.beamSwapchain) {
        xr.xrDestroySwapchain(menuPointerKept.beamSwapchain);
        menuPointerKept.beamSwapchain = XR_NULL_HANDLE;
    }
    menuPointerKept.beamFailed = false;
}

} // namespace evr::vkcore
