#include "features/menu/model_camera.hpp"

#include "common/quat.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <utility>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::menu::ModelCamera;
using evr::menu::modelOnPanel;
using evr::menu::panelCamera;
using evr::menu::PanelImage;
using evr::menu::WorldHead;
using evr::menu::worldPoint;

namespace {

constexpr float kPi = 3.14159265358979f;

Quat yaw(float degrees) {
    return Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, degrees * kPi / 180.0f);
}

Quat pitch(float degrees) {
    return Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, degrees * kPi / 180.0f);
}

// A 1920 x 1440 GUI image whose 16:9 band (1920 x 1080, rows 180..1259) is on a 2 m panel 1.5 m ahead.
PanelImage bandPanel() {
    PanelImage image;
    image.panel.pose.position = {0.0f, 1.7f, -1.5f};
    image.panel.width = 2.0f;
    image.panel.height = 1.125f;
    image.imageWidth = 1920;
    image.imageHeight = 1440;
    image.rectX = 0.0f;
    image.rectY = 180.0f;
    image.rectWidth = 1920.0f;
    image.rectHeight = 1080.0f;
    return image;
}

// The head at the panel's height looking straight at it; the game draws it from (0, 0, 1.66) facing +X.
WorldHead straightHead() {
    WorldHead head;
    head.local.position = {0.0f, 1.7f, 0.0f};
    head.origin = {0.0f, 0.0f, 1.66f};
    return head;
}

// The game's world as a fixed transform of LOCAL (a body yaw, a place and a scale), and a head seen in it.
struct World {
    float bodyYawDegrees = 30.0f;
    Vec3 place{12.0f, -4.0f, 1.0f};
    float unitsPerMetre = 2.0f;

    [[nodiscard]] Vec3 direction(Vec3 local) const {
        const float c = std::cos(bodyYawDegrees * kPi / 180.0f);
        const float s = std::sin(bodyYawDegrees * kPi / 180.0f);
        const Vec3 id{-local.z, -local.x, local.y}; // OpenXR to id Tech axes
        return {c * id.x - s * id.y, s * id.x + c * id.y, id.z};
    }
    [[nodiscard]] Vec3 point(Vec3 local) const { return place + direction(local) * unitsPerMetre; }
    [[nodiscard]] WorldHead head(const Pose& local) const {
        WorldHead h;
        h.local = local;
        h.origin = point(local.position);
        h.forward = direction(rotate(local.orientation, Vec3{0.0f, 0.0f, -1.0f}));
        h.left = direction(rotate(local.orientation, Vec3{-1.0f, 0.0f, 0.0f}));
        h.up = direction(rotate(local.orientation, Vec3{0.0f, 1.0f, 0.0f}));
        h.unitsPerMetre = unitsPerMetre;
        return h;
    }
};

// Where the game puts a GUI element at (u, v) of the image (0..1, row 0 at the top) for a camera: the ray
// of idMenuWidget_3D_Stand (RVA 0x15A7510), from the camera's fov_x / fov_y, `depth` ahead.
Vec3 gameRay(const ModelCamera& c, float u, float v, float depth) {
    const float tx = std::tan(c.fovX * 0.5f * kPi / 180.0f);
    const float ty = std::tan(c.fovY * 0.5f * kPi / 180.0f);
    return c.origin +
           (c.forward + c.left * (-tx * (2.0f * u - 1.0f)) + c.up * (ty * (1.0f - 2.0f * v))) * depth;
}

// The LOCAL point of the panel that shows image pixel (u, v) (0..1 of the whole image).
Vec3 panelPoint(const PanelImage& image, float u, float v) {
    const float perX = image.panel.width / image.rectWidth;
    const float perY = image.panel.height / image.rectHeight;
    const float x =
        (u * static_cast<float>(image.imageWidth) - (image.rectX + image.rectWidth * 0.5f)) * perX;
    const float y =
        -(v * static_cast<float>(image.imageHeight) - (image.rectY + image.rectHeight * 0.5f)) * perY;
    return transformPoint(image.panel.pose, Vec3{x, y, 0.0f});
}

void checkNear(Vec3 a, Vec3 b, float tolerance = 1e-3f) {
    CHECK(a.x == doctest::Approx(b.x).epsilon(tolerance));
    CHECK(a.y == doctest::Approx(b.y).epsilon(tolerance));
    CHECK(a.z == doctest::Approx(b.z).epsilon(tolerance));
}

} // namespace

TEST_CASE("the camera faces the panel from where the image fills the flat game's field of view") {
    const auto camera = panelCamera(bandPanel(), straightHead(), 90.0f);
    REQUIRE(camera);
    // tan(45) = 1: the 2 m wide image fills 90 degrees 1 m in front of the panel, 0.5 m ahead of the head.
    checkNear(camera->origin, {0.5f, 0.0f, 1.66f});
    checkNear(camera->forward, {1.0f, 0.0f, 0.0f});
    checkNear(camera->left, {0.0f, 1.0f, 0.0f});
    checkNear(camera->up, {0.0f, 0.0f, 1.0f});
    CHECK(camera->panelDistance == doctest::Approx(1.0f));
    // The whole image is 1.5 m tall on the panel's plane, its 16:9 band 2 x 1.125 m.
    CHECK(camera->fovY == doctest::Approx(2.0f * std::atan(0.75f) * 180.0f / kPi));
}

TEST_CASE("a GUI element's ray from the camera meets the panel where the panel shows that element") {
    const PanelImage image = bandPanel();
    const WorldHead head = straightHead();
    const auto camera = panelCamera(image, head, 90.0f);
    REQUIRE(camera);
    for (const auto& [u, v] : {std::pair{0.5f, 0.5f}, std::pair{0.1f, 0.2f}, std::pair{0.8f, 0.7f},
                               std::pair{1.0f, 0.125f}, std::pair{0.0f, 0.875f}}) {
        CAPTURE(u);
        CAPTURE(v);
        checkNear(gameRay(*camera, u, v, camera->panelDistance), worldPoint(head, panelPoint(image, u, v)));
    }
}

TEST_CASE("a turned panel, a turned head and a scaled world keep the element on the panel") {
    PanelImage image = bandPanel();
    image.panel.pose = {yaw(40.0f), {-1.2f, 1.5f, -0.9f}};
    image.panel.width = 1.6f;
    image.panel.height = 0.9f;
    const World world;
    const WorldHead head = world.head({yaw(25.0f) * pitch(-10.0f), {0.1f, 1.6f, 0.2f}});
    const auto camera = panelCamera(image, head, 75.0f);
    REQUIRE(camera);
    CHECK(camera->panelDistance == doctest::Approx(0.8f / std::tan(37.5f * kPi / 180.0f) * 2.0f));
    for (const auto& [u, v] : {std::pair{0.5f, 0.5f}, std::pair{0.2f, 0.3f}, std::pair{0.9f, 0.8f}}) {
        CAPTURE(u);
        CAPTURE(v);
        checkNear(gameRay(*camera, u, v, camera->panelDistance), world.point(panelPoint(image, u, v)));
    }
}

TEST_CASE("the camera stays put in the world while the head moves: the model is world-locked") {
    const PanelImage image = bandPanel();
    const World world;
    const auto still = panelCamera(image, world.head({Quat::identity(), {0.0f, 1.7f, 0.0f}}), 90.0f);
    const auto swayed =
        panelCamera(image, world.head({yaw(25.0f) * pitch(6.0f), {0.05f, 1.68f, 0.1f}}), 90.0f);
    REQUIRE(still);
    REQUIRE(swayed);
    checkNear(still->origin, swayed->origin);
    checkNear(still->forward, swayed->forward);
    checkNear(still->up, swayed->up);
}

TEST_CASE("an image shown whole and off the panel's centre still lines up") {
    PanelImage image = bandPanel();
    // The panel shows the image's left 960 columns and all its rows: the image's centre is the panel's right
    // edge.
    image.rectY = 0.0f;
    image.rectWidth = 960.0f;
    image.rectHeight = 1440.0f;
    image.panel.width = 1.0f;
    image.panel.height = 1.5f;
    const WorldHead head = straightHead();
    const auto camera = panelCamera(image, head, 90.0f);
    REQUIRE(camera);
    checkNear(camera->origin, {0.5f, -0.5f, 1.66f}); // 0.5 m to the right (id Tech left is -right)
    checkNear(gameRay(*camera, 0.25f, 0.4f, camera->panelDistance),
              worldPoint(head, panelPoint(image, 0.25f, 0.4f)));
}

TEST_CASE("the model is magnified about the camera onto the panel's plane, unchanged as the camera sees it") {
    const auto camera = panelCamera(bandPanel(), straightHead(), 90.0f);
    REQUIRE(camera);
    // The game's placement: r_znear deep along an element's ray, plus the model's size.
    const Vec3 placed = gameRay(*camera, 0.7f, 0.4f, 0.1f);
    const auto on = modelOnPanel(*camera, placed, {0.5f, 0.5f, 0.5f});
    REQUIRE(on);
    CHECK(on->factor == doctest::Approx(10.0f));
    checkNear(on->scale, {5.0f, 5.0f, 5.0f});
    checkNear(on->position, gameRay(*camera, 0.7f, 0.4f, 1.0f));
    CHECK(dot(on->position - camera->origin, camera->forward) == doctest::Approx(camera->panelDistance));
}

TEST_CASE("nothing is placed from a bad panel, field of view, head or model") {
    const WorldHead head = straightHead();
    PanelImage empty = bandPanel();
    empty.panel.width = 0.0f;
    CHECK_FALSE(panelCamera(empty, head, 90.0f));
    PanelImage noImage = bandPanel();
    noImage.imageWidth = 0;
    CHECK_FALSE(panelCamera(noImage, head, 90.0f));
    CHECK_FALSE(panelCamera(bandPanel(), head, 0.5f));
    CHECK_FALSE(panelCamera(bandPanel(), head, 175.0f));
    CHECK_FALSE(panelCamera(bandPanel(), head, std::nanf("")));
    WorldHead badScale = head;
    badScale.unitsPerMetre = 0.0f;
    CHECK_FALSE(panelCamera(bandPanel(), badScale, 90.0f));
    WorldHead badAxis = head;
    badAxis.forward = {0.0f, 0.0f, 0.0f};
    CHECK_FALSE(panelCamera(bandPanel(), badAxis, 90.0f));
    WorldHead nanHead = head;
    nanHead.local.position.x = std::nanf("");
    CHECK_FALSE(panelCamera(bandPanel(), nanHead, 90.0f));

    const auto camera = panelCamera(bandPanel(), head, 90.0f);
    REQUIRE(camera);
    const Vec3 one{1.0f, 1.0f, 1.0f};
    CHECK_FALSE(modelOnPanel(*camera, camera->origin, one));                           // at the camera
    CHECK_FALSE(modelOnPanel(*camera, camera->origin - camera->forward * 0.2f, one));  // behind it
    CHECK_FALSE(modelOnPanel(*camera, camera->origin + camera->forward * 1e-5f, one)); // factor too large
    CHECK_FALSE(modelOnPanel(*camera, camera->origin + camera->forward * 0.1f, {std::nanf(""), 1.0f, 1.0f}));
    CHECK(modelOnPanel(*camera, camera->origin + camera->forward * 2.0f, one)); // beyond the panel: drawn in
}
