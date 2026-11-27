// Tests for engine::Camera: projections, reverse-Z depth, and the
// screen <-> world conversions used for aiming. Pure maths: no window, no GPU.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/engine/Camera.h"
#include "playground/engine/CameraController.h"
#include "playground/platform/Input.h"

#include <glm/vec4.hpp>

using namespace playground;
using engine::Camera;
using engine::Projection;

namespace {

// Where a world point lands in normalised device coordinates (x, y in -1..1,
// z = depth in 0..1).
glm::vec3 ndc(const Camera& camera, glm::vec3 world, glm::vec2 viewport) {
  const glm::vec4 clip = camera.viewProjection(viewport) * glm::vec4(world, 1.0f);
  return glm::vec3(clip) / clip.w;
}

Camera tiltedPerspective() {
  Camera camera;
  camera.projection = Projection::Perspective;
  camera.position = {0.0f, -900.0f, 900.0f};  // in front of and above the plane, looking down at it
  camera.target = {0.0f, 0.0f, 0.0f};
  camera.up = {0.0f, 1.0f, 0.0f};
  return camera;
}

constexpr glm::vec2 kViewport{1600.0f, 900.0f};

}  // namespace

// --- Orthographic ----------------------------------------------------------------

TEST_CASE("orthographic: the required area fills a same-shaped window exactly") {
  Camera camera;  // looks down -z at the origin, must show +-800 x +-450
  const glm::vec3 centre = ndc(camera, {0.0f, 0.0f, 0.0f}, kViewport);
  CHECK(centre.x == doctest::Approx(0.0f));
  CHECK(centre.y == doctest::Approx(0.0f));

  const glm::vec3 corner = ndc(camera, {800.0f, 450.0f, 0.0f}, kViewport);
  CHECK(corner.x == doctest::Approx(1.0f));
  CHECK(corner.y == doctest::Approx(1.0f));  // +y is up
}

TEST_CASE("orthographic: a square window shows the whole width and extra height, unstretched") {
  Camera camera;
  const glm::vec3 right = ndc(camera, {800.0f, 0.0f, 0.0f}, {900.0f, 900.0f});
  const glm::vec3 top = ndc(camera, {0.0f, 800.0f, 0.0f}, {900.0f, 900.0f});
  CHECK(right.x == doctest::Approx(1.0f));  // the width just fits
  CHECK(top.y == doctest::Approx(1.0f));    // same scale vertically: 800 units up is the top edge
}

TEST_CASE("orthographic: distance does not change size") {
  Camera camera;
  const glm::vec3 nearPoint = ndc(camera, {400.0f, 0.0f, 50.0f}, kViewport);
  const glm::vec3 farPoint = ndc(camera, {400.0f, 0.0f, -50.0f}, kViewport);
  CHECK(nearPoint.x == doctest::Approx(farPoint.x));
}

// --- Reverse-Z ---------------------------------------------------------------------

TEST_CASE("reverse-Z: the near plane is depth 1, the far plane depth 0, nearer is larger") {
  for (const Camera& camera : {Camera{}, tiltedPerspective()}) {
    const glm::vec3 eye = camera.position;
    const glm::vec3 forward = glm::normalize(camera.target - eye);
    CHECK(ndc(camera, eye + forward * camera.nearPlane, kViewport).z == doctest::Approx(1.0f));
    CHECK(ndc(camera, eye + forward * camera.farPlane, kViewport).z == doctest::Approx(0.0f).epsilon(1e-4));
    // Sprite layers: a sprite at z = 1 is nearer the camera than one at z = 0,
    // so it must have the greater depth (the depth test keeps GREATER).
    const float layer0 = ndc(camera, {0.0f, 0.0f, 0.0f}, kViewport).z;
    const float layer1 = ndc(camera, {0.0f, 0.0f, 1.0f}, kViewport).z;
    CHECK(layer1 > layer0);
    CHECK(layer0 > 0.0f);
    CHECK(layer0 < 1.0f);
  }
}

// --- Perspective ---------------------------------------------------------------------

TEST_CASE("perspective: the target is at the centre, and farther things look smaller") {
  const Camera camera = tiltedPerspective();
  const glm::vec3 centre = ndc(camera, camera.target, kViewport);
  CHECK(centre.x == doctest::Approx(0.0f).epsilon(1e-5));
  CHECK(centre.y == doctest::Approx(0.0f).epsilon(1e-5));

  // The same 100-unit step sideways, near the bottom of the plane (closer to
  // the camera) and near the top (farther): the far one covers less screen.
  const float nearWidth = ndc(camera, {100.0f, -400.0f, 0.0f}, kViewport).x - ndc(camera, {0.0f, -400.0f, 0.0f}, kViewport).x;
  const float farWidth = ndc(camera, {100.0f, 400.0f, 0.0f}, kViewport).x - ndc(camera, {0.0f, 400.0f, 0.0f}, kViewport).x;
  CHECK(nearWidth > farWidth);
}

// --- Screen <-> world ----------------------------------------------------------------

TEST_CASE("screenToWorld (orthographic): centre, corners, and y pointing down on screen") {
  Camera camera;
  const auto centre = camera.screenToWorld({800.0f, 450.0f}, kViewport);
  REQUIRE(centre.has_value());
  CHECK(centre->x == doctest::Approx(0.0f).epsilon(1e-4));
  CHECK(centre->y == doctest::Approx(0.0f).epsilon(1e-4));
  CHECK(centre->z == doctest::Approx(0.0f).epsilon(1e-4));

  const auto topLeft = camera.screenToWorld({0.0f, 0.0f}, kViewport);
  REQUIRE(topLeft.has_value());
  CHECK(topLeft->x == doctest::Approx(-800.0f));
  CHECK(topLeft->y == doctest::Approx(450.0f));  // screen top = world +y

  const auto bottomRight = camera.screenToWorld({1600.0f, 900.0f}, kViewport);
  REQUIRE(bottomRight.has_value());
  CHECK(bottomRight->x == doctest::Approx(800.0f));
  CHECK(bottomRight->y == doctest::Approx(-450.0f));
}

TEST_CASE("screenToWorld follows the camera when it moves") {
  Camera camera;
  camera.position = {300.0f, -200.0f, 1000.0f};
  camera.target = {300.0f, -200.0f, 0.0f};
  const auto centre = camera.screenToWorld({800.0f, 450.0f}, kViewport);
  REQUIRE(centre.has_value());
  CHECK(centre->x == doctest::Approx(300.0f));
  CHECK(centre->y == doctest::Approx(-200.0f));
}

TEST_CASE("screenToWorld (perspective): the screen centre hits the target on the plane") {
  const Camera camera = tiltedPerspective();
  const auto centre = camera.screenToWorld({800.0f, 450.0f}, kViewport);
  REQUIRE(centre.has_value());
  CHECK(centre->x == doctest::Approx(0.0f).epsilon(1e-3));
  CHECK(centre->y == doctest::Approx(0.0f).epsilon(1e-3));
  CHECK(centre->z == doctest::Approx(0.0f).epsilon(1e-3));
}

TEST_CASE("world -> screen -> world returns the same point, in both projections") {
  const glm::vec3 points[] = {{0.0f, 0.0f, 0.0f}, {350.0f, 120.0f, 0.0f}, {-600.0f, -300.0f, 0.0f}, {123.0f, 321.0f, 0.0f}};
  for (const Camera& camera : {Camera{}, tiltedPerspective()}) {
    for (const glm::vec3 world : points) {
      const auto screen = camera.worldToScreen(world, kViewport);
      REQUIRE(screen.has_value());
      const auto back = camera.screenToWorld(*screen, kViewport);
      REQUIRE(back.has_value());
      CHECK(back->x == doctest::Approx(world.x).epsilon(1e-3));
      CHECK(back->y == doctest::Approx(world.y).epsilon(1e-3));
    }
  }
}

TEST_CASE("screenToWorld: no hit when the plane is behind the camera or edge-on") {
  Camera edgeOn;  // looking along the plane: every ray is parallel to it
  edgeOn.projection = Projection::Orthographic;
  edgeOn.position = {0.0f, -1000.0f, 0.0f};
  edgeOn.target = {0.0f, 0.0f, 0.0f};
  edgeOn.up = {0.0f, 0.0f, 1.0f};
  CHECK_FALSE(edgeOn.screenToWorld({800.0f, 450.0f}, kViewport).has_value());

  Camera underneath;  // below the plane, looking away from it
  underneath.position = {0.0f, 0.0f, -10.0f};
  underneath.target = {0.0f, 0.0f, -100.0f};
  CHECK_FALSE(underneath.screenToWorld({800.0f, 450.0f}, kViewport).has_value());
}

TEST_CASE("worldToScreen: a point behind the camera has no screen position") {
  const Camera camera = tiltedPerspective();
  CHECK_FALSE(camera.worldToScreen({0.0f, -2000.0f, 2000.0f}, kViewport).has_value());
}

// --- CameraController ---------------------------------------------------------------

namespace {

using engine::CameraController;
using engine::CameraMode;

// One frame of input: what the collector gathered since the last snapshot.
struct InputScript {
  platform::InputCollector collector;
  platform::InputState frame() { return collector.takeSnapshot(); }
};

bool close(glm::vec3 a, glm::vec3 b, f32 tolerance = 1e-3f) { return glm::length(a - b) <= tolerance; }

}  // namespace

TEST_CASE("orbit: the eye sits `distance` from the target, behind it along the view direction") {
  CameraController controller;
  controller.orbit({1.0f, 2.0f, 3.0f}, 10.0f);  // yaw 0, pitch 0: looking along -z
  engine::Camera camera;
  InputScript input;
  controller.update(input.frame(), 0.016f, camera);
  CHECK(camera.projection == engine::Projection::Perspective);
  CHECK(close(camera.position, {1.0f, 2.0f, 13.0f}));
  CHECK(close(glm::normalize(camera.target - camera.position), {0.0f, 0.0f, -1.0f}));

  controller.orbit({0.0f, 0.0f, 0.0f}, 10.0f, glm::radians(90.0f), 0.0f);  // turned to look along +x
  controller.update(input.frame(), 0.016f, camera);
  CHECK(close(camera.position, {-10.0f, 0.0f, 0.0f}));
}

TEST_CASE("orbit: left-drag spins and tilts, the wheel zooms, and the distance is kept within limits") {
  CameraController controller({.dragRadiansPerPixel = 0.01f, .zoomPerClick = 0.5f, .minDistance = 1.0f});
  controller.orbit({0.0f, 0.0f, 0.0f}, 8.0f);
  engine::Camera camera;
  InputScript input;
  input.collector.cursorMoved(100.0, 100.0);
  controller.update(input.frame(), 0.016f, camera);  // first frame: no jump

  input.collector.mouseButtonEvent(platform::MouseButton::Left, true);
  input.collector.cursorMoved(150.0, 80.0);  // 50 right, 20 up
  controller.update(input.frame(), 0.016f, camera);
  CHECK(controller.yaw() == doctest::Approx(0.5f));
  CHECK(controller.pitch() == doctest::Approx(0.2f));
  CHECK(glm::length(camera.position) == doctest::Approx(8.0f));  // still on the sphere around the target

  input.collector.scrolled(1.0);  // one click towards you: half the distance
  controller.update(input.frame(), 0.016f, camera);
  CHECK(controller.distance() == doctest::Approx(4.0f));
  input.collector.scrolled(10.0);
  controller.update(input.frame(), 0.016f, camera);
  CHECK(controller.distance() == doctest::Approx(1.0f));  // the minimum

  input.collector.cursorMoved(150.0, -5000.0);  // a huge drag up: pitch stops short of straight down
  controller.update(input.frame(), 0.016f, camera);
  CHECK(controller.pitch() < 1.571f);
  CHECK(controller.pitch() > 1.4f);
}

TEST_CASE("fly: W moves where you look at flySpeed; Shift is faster; diagonals are not") {
  CameraController controller({.flySpeed = 2.0f, .fastMultiplier = 4.0f});
  controller.orbit({0.0f, 0.0f, 0.0f}, 5.0f);
  controller.setMode(CameraMode::Fly);
  engine::Camera camera;
  InputScript input;
  controller.update(input.frame(), 0.0f, camera);
  const glm::vec3 start = camera.position;
  CHECK(close(start, {0.0f, 0.0f, 5.0f}));  // the switch kept the view

  input.collector.keyEvent(platform::Key::W, true);
  for (int i = 0; i < 10; ++i) controller.update(input.frame(), 0.1f, camera);  // 1 s
  CHECK(close(camera.position, start + glm::vec3(0.0f, 0.0f, -2.0f)));

  input.collector.keyEvent(platform::Key::LeftShift, true);
  controller.update(input.frame(), 0.5f, camera);
  CHECK(close(camera.position, start + glm::vec3(0.0f, 0.0f, -6.0f)));  // 2 x 4 x 0.5 more
  input.collector.keyEvent(platform::Key::LeftShift, false);

  input.collector.keyEvent(platform::Key::D, true);  // forward and right together
  const glm::vec3 before = camera.position;
  controller.update(input.frame(), 1.0f, camera);
  CHECK(glm::length(camera.position - before) == doctest::Approx(2.0f));
}

TEST_CASE("fly: right-drag looks around; switching back to orbit keeps the view") {
  CameraController controller({.dragRadiansPerPixel = 0.01f});
  controller.orbit({0.0f, 0.0f, 0.0f}, 5.0f);
  controller.setMode(CameraMode::Fly);
  engine::Camera camera;
  InputScript input;
  controller.update(input.frame(), 0.016f, camera);
  input.collector.mouseButtonEvent(platform::MouseButton::Right, true);
  input.collector.cursorMoved(157.0796, 0.0);  // turn 90 degrees right
  controller.update(input.frame(), 0.016f, camera);
  CHECK(close(controller.forward(), {1.0f, 0.0f, 0.0f}));

  const glm::vec3 eye = camera.position;
  const glm::vec3 looking = controller.forward();
  controller.setMode(CameraMode::Orbit);
  CHECK(controller.mode() == CameraMode::Orbit);
  CHECK(close(controller.eye(), eye));
  CHECK(close(controller.forward(), looking));
  CHECK(close(controller.target(), eye + looking * 5.0f));
}

TEST_CASE("frame(): a sphere fits in the view from the distance chosen") {
  CameraController controller;
  const f32 fovY = glm::radians(60.0f);
  controller.frame({0.0f, 1.0f, 0.0f}, 2.0f, fovY, 0.0f, 0.0f);
  // sin(30 degrees) = 0.5, so radius 2 needs distance 4, plus 10%.
  CHECK(controller.distance() == doctest::Approx(4.4f));
  CHECK(close(controller.target(), {0.0f, 1.0f, 0.0f}));
}
