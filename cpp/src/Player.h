#pragma once
#include "Gl.h"
#include "Level.h"

// The physical player: gravity, jump, WASD relative to wherever the camera
// is looking, and collision against the level. Distinct from Camera, which
// owns only the *view* (look direction, FOV, the aim blend) — Game syncs
// Camera::position to Player::eyePosition() once per frame. Splitting them
// is what makes a physical, collidable player possible without rewriting
// the free-fly demo camera those files also serve.
class Player {
public:
  glm::vec3 position{0.0f, 0.0f, 12.0f};   // feet, at the level's floor
  glm::vec3 velocity{0.0f};
  float radius = 0.4f;
  float height = 1.8f;
  float eyeHeight = 1.62f;

  float maxHp = 100.0f;
  float hp = 100.0f;
  bool grounded = true;

  // yawRadians comes from the camera's look direction: the player walks
  // relative to wherever you're facing, not relative to a fixed axis.
  // forceForward exists only for headless verification (EREBUS_FORCE_FORWARD
  // in main.cpp) — a run with no real keyboard has no way to hold W, so this
  // substitutes for that one input rather than faking OS-level key events.
  void update(GLFWwindow* window, float dt, float yawRadians, bool sprint, const Level& level,
              bool forceForward = false);

  glm::vec3 eyePosition() const { return position + glm::vec3(0, eyeHeight, 0); }
  bool alive() const { return hp > 0.0f; }
};
