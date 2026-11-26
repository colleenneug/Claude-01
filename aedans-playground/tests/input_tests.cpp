// Tests for input: edges, focus loss, the fixed-step latch, and action
// mapping. No window: events are fed to the collector directly, exactly as
// Window.cpp's GLFW callbacks do.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/engine/Actions.h"
#include "playground/platform/Input.h"

#include <cmath>

using namespace playground;
using platform::InputCollector;
using platform::InputLatch;
using platform::InputState;
using platform::Key;
using platform::MouseButton;

// --- Edges: pressed -> down -> released ---------------------------------------

TEST_CASE("a key goes pressed, then held, then released, one frame each") {
  InputCollector collector;

  collector.keyEvent(Key::W, true);
  const InputState frame1 = collector.takeSnapshot();
  CHECK(frame1.pressed(Key::W));
  CHECK(frame1.down(Key::W));
  CHECK_FALSE(frame1.released(Key::W));

  const InputState frame2 = collector.takeSnapshot();  // still held, no new events
  CHECK_FALSE(frame2.pressed(Key::W));                 // the edge was in frame 1 only
  CHECK(frame2.down(Key::W));

  collector.keyEvent(Key::W, false);
  const InputState frame3 = collector.takeSnapshot();
  CHECK(frame3.released(Key::W));
  CHECK_FALSE(frame3.down(Key::W));

  const InputState frame4 = collector.takeSnapshot();
  CHECK_FALSE(frame4.released(Key::W));
  CHECK_FALSE(frame4.hasEdges());
}

TEST_CASE("a tap inside one frame is not lost: pressed and released, not down") {
  InputCollector collector;
  collector.keyEvent(Key::Space, true);
  collector.keyEvent(Key::Space, false);
  const InputState frame = collector.takeSnapshot();
  CHECK(frame.pressed(Key::Space));
  CHECK(frame.released(Key::Space));
  CHECK_FALSE(frame.down(Key::Space));
}

TEST_CASE("duplicate downs and stray ups do not invent edges") {
  InputCollector collector;
  collector.keyEvent(Key::A, false);  // up for a key never seen down
  CHECK_FALSE(collector.takeSnapshot().released(Key::A));

  collector.keyEvent(Key::A, true);
  (void)collector.takeSnapshot();
  collector.keyEvent(Key::A, true);  // a second "down" while held
  CHECK_FALSE(collector.takeSnapshot().pressed(Key::A));

  collector.keyEvent(Key::Unknown, true);  // keys without a name are ignored
  CHECK_FALSE(collector.takeSnapshot().down(Key::Unknown));
}

TEST_CASE("mouse buttons and the cursor are tracked like keys") {
  InputCollector collector;
  collector.mouseButtonEvent(MouseButton::Left, true);
  collector.cursorMoved(120.5, 64.0);
  const InputState frame = collector.takeSnapshot();
  CHECK(frame.pressed(MouseButton::Left));
  CHECK(frame.down(MouseButton::Left));
  CHECK_FALSE(frame.down(MouseButton::Right));
  CHECK(frame.mouseX() == 120.5);
  CHECK(frame.mouseY() == 64.0);
}

// --- Focus loss -----------------------------------------------------------------

TEST_CASE("losing focus releases every held key and button (no stuck keys)") {
  InputCollector collector;
  collector.keyEvent(Key::D, true);
  collector.keyEvent(Key::LeftShift, true);
  collector.mouseButtonEvent(MouseButton::Right, true);
  (void)collector.takeSnapshot();

  collector.focusLost();  // Alt+Tab while holding them: their "up" never comes
  const InputState frame = collector.takeSnapshot();
  CHECK_FALSE(frame.down(Key::D));
  CHECK_FALSE(frame.down(Key::LeftShift));
  CHECK_FALSE(frame.down(MouseButton::Right));
  CHECK(frame.released(Key::D));  // reported once, so "stop" logic still runs
  CHECK(frame.released(MouseButton::Right));

  const InputState later = collector.takeSnapshot();
  CHECK_FALSE(later.down(Key::D));
  CHECK_FALSE(later.hasEdges());
}

TEST_CASE("after focus loss, pressing the key again works normally") {
  InputCollector collector;
  collector.keyEvent(Key::W, true);
  collector.focusLost();
  (void)collector.takeSnapshot();
  collector.keyEvent(Key::W, true);
  const InputState frame = collector.takeSnapshot();
  CHECK(frame.pressed(Key::W));
  CHECK(frame.down(Key::W));
}

// --- The fixed-step latch ---------------------------------------------------------

TEST_CASE("held keys reach every fixed step; a press reaches exactly one") {
  InputCollector collector;
  InputLatch latch;
  collector.keyEvent(Key::Space, true);
  collector.keyEvent(Key::W, true);

  // A slow frame: four fixed steps.
  latch.beginFrame(collector.takeSnapshot());
  int pressedSteps = 0;
  int downSteps = 0;
  for (int step = 0; step < 4; ++step) {
    if (latch.current().pressed(Key::Space)) ++pressedSteps;
    if (latch.current().down(Key::W)) ++downSteps;
    latch.consumeEdges();
  }
  CHECK(pressedSteps == 1);  // Fire once, not four times
  CHECK(downSteps == 4);     // movement in every step
}

TEST_CASE("a press in a frame with no fixed steps waits for the next step") {
  InputCollector collector;
  InputLatch latch;

  collector.keyEvent(Key::Space, true);
  latch.beginFrame(collector.takeSnapshot());  // frame 1: fast, zero steps run

  latch.beginFrame(collector.takeSnapshot());  // frame 2: no new events, one step
  CHECK(latch.current().pressed(Key::Space));  // still delivered
  latch.consumeEdges();
  CHECK_FALSE(latch.current().pressed(Key::Space));

  latch.beginFrame(collector.takeSnapshot());  // frame 3
  CHECK_FALSE(latch.current().pressed(Key::Space));  // and only once
  CHECK(latch.current().down(Key::Space));
}

TEST_CASE("a tap during zero-step frames still arrives as pressed and released") {
  InputCollector collector;
  InputLatch latch;
  collector.keyEvent(Key::Space, true);
  latch.beginFrame(collector.takeSnapshot());  // zero steps
  collector.keyEvent(Key::Space, false);
  latch.beginFrame(collector.takeSnapshot());  // zero steps
  latch.beginFrame(collector.takeSnapshot());  // the step that finally runs
  CHECK(latch.current().pressed(Key::Space));
  CHECK(latch.current().released(Key::Space));
  CHECK_FALSE(latch.current().down(Key::Space));
}

// --- Actions ----------------------------------------------------------------------

namespace {
InputState holding(std::initializer_list<Key> keys) {
  InputCollector collector;
  for (Key key : keys) collector.keyEvent(key, true);
  return collector.takeSnapshot();
}
}  // namespace

TEST_CASE("Move: WASD and arrows both steer, and diagonals are not faster") {
  const engine::ActionMap actions = engine::ActionMap::defaults();
  using engine::actions::kMove;

  CHECK(actions.axis2D(holding({}), kMove) == glm::vec2{0.0f, 0.0f});
  CHECK(actions.axis2D(holding({Key::W}), kMove) == glm::vec2{0.0f, 1.0f});
  CHECK(actions.axis2D(holding({Key::A}), kMove) == glm::vec2{-1.0f, 0.0f});
  CHECK(actions.axis2D(holding({Key::Down}), kMove) == glm::vec2{0.0f, -1.0f});
  CHECK(actions.axis2D(holding({Key::Right}), kMove) == glm::vec2{1.0f, 0.0f});

  const glm::vec2 diagonal = actions.axis2D(holding({Key::W, Key::D}), kMove);
  CHECK(std::sqrt(diagonal.x * diagonal.x + diagonal.y * diagonal.y) == doctest::Approx(1.0f));
  CHECK(diagonal.x == doctest::Approx(diagonal.y));

  CHECK(actions.axis2D(holding({Key::A, Key::D}), kMove) == glm::vec2{0.0f, 0.0f});      // opposites cancel
  CHECK(actions.axis2D(holding({Key::D, Key::Right}), kMove) == glm::vec2{1.0f, 0.0f});  // not double speed
}

TEST_CASE("Fire: Space or the left mouse button, with pressed/down/released") {
  const engine::ActionMap actions = engine::ActionMap::defaults();
  using engine::actions::kFire;
  InputCollector collector;

  collector.mouseButtonEvent(MouseButton::Left, true);
  const InputState click = collector.takeSnapshot();
  CHECK(actions.pressed(click, kFire));
  CHECK(actions.down(click, kFire));

  collector.mouseButtonEvent(MouseButton::Left, false);
  CHECK(actions.released(collector.takeSnapshot(), kFire));

  collector.keyEvent(Key::Space, true);
  CHECK(actions.pressed(collector.takeSnapshot(), kFire));
  CHECK_FALSE(actions.pressed(holding({Key::Enter}), kFire));  // not bound
}

TEST_CASE("bindings are data: a new map can bind anything") {
  engine::ActionMap actions;
  actions.bindButton("Jump", Key::J);
  actions.bindAxis2D("Look", engine::AxisKeys{.left = Key::J, .right = Key::L, .down = Key::K, .up = Key::I});
  CHECK(actions.hasButton("Jump"));
  CHECK(actions.hasAxis2D("Look"));
  CHECK_FALSE(actions.hasButton("Fire"));
  CHECK(actions.pressed(holding({Key::J}), "Jump"));
  CHECK(actions.axis2D(holding({Key::I}), "Look") == glm::vec2{0.0f, 1.0f});
}

TEST_CASE("keys have readable names") {
  CHECK(platform::describe(Key::W) == "W");
  CHECK(platform::describe(Key::Num7) == "7");
  CHECK(platform::describe(Key::Space) == "Space");
  CHECK(platform::describe(Key::F12) == "F12");
}
