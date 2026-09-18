#include "Player.h"
#include <algorithm>
#include <cmath>

namespace {
// Movement numbers taken from the browser build (src/js/fps/player.js) so
// the two play the same. They are not arbitrary: the slide's entry threshold
// is a fraction of WALK_SPEED, and its floor and ceiling are set relative to
// SPRINT_SPEED, so changing one of these without the others changes whether
// a slide is worth doing at all.
constexpr float GRAVITY = 18.0f;
constexpr float JUMP_SPEED = 7.2f;
constexpr float WALK_SPEED = 4.6f;
constexpr float SPRINT_SPEED = 9.4f;
constexpr float CROUCH_SPEED = 2.3f;
constexpr float GROUND_ACCEL = 52.0f;
// Air control is a nudge, not a steering wheel. Closing most of the gap to
// the target every frame in mid-air throws away whatever speed you jumped
// with, which turns a slide hop into a full stop.
constexpr float AIR_ACCEL = 3.5f;
constexpr float GROUND_FRICTION = 14.0f;

constexpr float EYE_STAND = 1.68f;
constexpr float EYE_CROUCH = 0.95f;

constexpr float SLIDE_KICK = 1.7f;      // multiplier on the speed you came in with
constexpr float SLIDE_FLOOR = 10.0f;    // ...but never slower than this
constexpr float SLIDE_CEILING = 17.0f;  // ...and never faster than this
// Drag is what decides how long a slide lasts, and it is exponential: from a
// 14 m/s entry, 1.5 takes about three quarters of a second to reach the exit
// speed. Much above that and the slide is over before the camera has finished
// dropping.
constexpr float SLIDE_DRAG = 1.5f;
constexpr float SLIDE_STEER = 3.4f;     // steering authority, against 52 on foot
constexpr float SLIDE_TIME = 1.15f;     // longest a slide can last
constexpr float SLIDE_EXIT = 4.6f;      // ...or until it has slowed to this
constexpr float SLIDE_COOLDOWN = 0.45f;

// The wall run. The probe runs from the player's centre, so WALL_REACH is
// added to their radius: 0.75 lets you catch a wall from about a metre off
// it, which is the difference between a move you can aim and one you have to
// scrape along.
constexpr float WALL_REACH = 0.75f;
constexpr float WALL_MIN_SPEED = 5.2f;
constexpr float WALL_GRAVITY = 4.5f;     // instead of 18, while attached
constexpr float WALL_STICK = 7.0f;       // pull toward the wall, so you don't drift off
constexpr float WALL_ALONG = 1.04f;      // a little speed gain per second along it
constexpr float WALL_TIME = 1.8f;
constexpr float WALL_JUMP_UP = 7.4f;
constexpr float WALL_JUMP_OUT = 6.6f;
constexpr float WALL_COOLDOWN = 0.25f;
}  // namespace

void Player::update(GLFWwindow* window, float dt, float yawRadians, bool sprint, const Level& level,
                     const ScriptedInput& scripted) {
  glm::vec3 fwd(std::cos(yawRadians), 0.0f, std::sin(yawRadians));
  glm::vec3 right(-fwd.z, 0.0f, fwd.x);

  auto held = [&](int key) { return glfwGetKey(window, key) == GLFW_PRESS; };
  // Crouch is on either Ctrl or C, and the arrow keys stand in for W and S,
  // exactly as the browser build binds them.
  const bool crouchHeld = scripted.crouch || held(GLFW_KEY_LEFT_CONTROL) ||
                          held(GLFW_KEY_RIGHT_CONTROL) || held(GLFW_KEY_C);
  const bool forwardHeld = scripted.forward || held(GLFW_KEY_W) || held(GLFW_KEY_UP);
  const bool jumpHeld = scripted.jump || held(GLFW_KEY_SPACE);

  glm::vec3 wish(0.0f);
  if (forwardHeld) wish += fwd;
  if (held(GLFW_KEY_S) || held(GLFW_KEY_DOWN)) wish -= fwd;
  if (held(GLFW_KEY_D) || held(GLFW_KEY_RIGHT)) wish += right;
  if (held(GLFW_KEY_A) || held(GLFW_KEY_LEFT)) wish -= right;
  const bool moving = glm::length(wish) > 1e-4f;
  if (moving) wish = glm::normalize(wish);

  // Sprint needs to be going forwards, and crouch outranks it: you cannot
  // sprint out of a crouch, you have to stand up first.
  const bool sprinting = (sprint || scripted.sprint) && !crouchHeld && !sliding && moving && forwardHeld;

  // ---- starting a slide: crouch, at speed, on the ground, cooldown clear.
  // Anything else and crouch is just crouch. It triggers on the press rather
  // than the hold, so holding crouch through a slide does not restart it.
  slideCool_ = std::max(0.0f, slideCool_ - dt);
  const float speedNow = planarSpeed();
  if (!sliding && crouchHeld && !crouchWasHeld_ && grounded && slideCool_ <= 0.0f &&
      speedNow > WALK_SPEED * 0.85f) {
    sliding = true;
    slideT_ = SLIDE_TIME;
    float boosted = std::clamp(speedNow * SLIDE_KICK, SLIDE_FLOOR, SLIDE_CEILING);
    float scale = speedNow > 0.01f ? boosted / speedNow : 0.0f;
    velocity.x *= scale;
    velocity.z *= scale;
  }
  crouchWasHeld_ = crouchHeld;

  if (sliding) {
    slideT_ -= dt;
    // Bleed off, steer a little, and end when it is over.
    float drag = std::max(0.0f, 1.0f - SLIDE_DRAG * dt);
    velocity.x *= drag;
    velocity.z *= drag;
    if (moving) {
      velocity.x += wish.x * SLIDE_STEER * dt;
      velocity.z += wish.z * SLIDE_STEER * dt;
    }
    if (slideT_ <= 0.0f || planarSpeed() < SLIDE_EXIT || !grounded || !crouchHeld) {
      sliding = false;
      slideCool_ = SLIDE_COOLDOWN;
    }
  } else {
    // Crouch only slows you on the ground — ducking in mid-air should not
    // brake you.
    float targetSpeed = (crouchHeld && grounded) ? CROUCH_SPEED
                      : sprinting                ? SPRINT_SPEED
                                                 : WALK_SPEED;
    glm::vec3 targetVel = wish * targetSpeed;
    float accel = grounded ? GROUND_ACCEL : AIR_ACCEL;
    glm::vec3 horizVel(velocity.x, 0.0f, velocity.z);
    glm::vec3 delta = targetVel - horizVel;
    float deltaLen = glm::length(delta);
    float step = accel * dt;
    if (deltaLen > 1e-5f) {
      glm::vec3 add = delta * std::min(1.0f, step / deltaLen);
      velocity.x += add.x;
      velocity.z += add.z;
    }
    if (grounded && !moving) {
      float friction = std::max(0.0f, 1.0f - GROUND_FRICTION * dt);
      velocity.x *= friction;
      velocity.z *= friction;
    }
  }

  crouching = crouchHeld || sliding;

  if (grounded && jumpHeld) {
    // Jumping out of a slide keeps the speed you built — that is the trick.
    if (sliding) {
      sliding = false;
      slideCool_ = SLIDE_COOLDOWN;
    }
    velocity.y = JUMP_SPEED;
    grounded = false;
  }
  // ---- the wall run. Off the ground, moving, with something solid beside
  // you: probe both sides at shoulder height and take whichever is there.
  wallCool_ = std::max(0.0f, wallCool_ - dt);
  if (grounded) {
    wallLast_ = glm::vec3(0.0f);
    wallRunning = false;
    wallSide = 0.0f;
  }

  const float flatSpeed = planarSpeed();
  if (!grounded && wallCool_ <= 0.0f && flatSpeed > WALL_MIN_SPEED) {
    const float out = radius + WALL_REACH;
    // Once attached, only re-probe the side already held — checking both
    // would let a corridor hand you a new wall every frame forever.
    const float sides[2] = {wallRunning ? wallSide : 1.0f, -1.0f};
    const int sideCount = wallRunning ? 1 : 2;
    for (int i = 0; i < sideCount; i++) {
      float side = sides[i];
      glm::vec3 probe = position + right * out * side + glm::vec3(0.0f, height * 0.6f, 0.0f);
      glm::vec3 normal;
      if (!level.wallAt(probe, normal)) continue;
      if (!wallRunning && normal == wallLast_ && side == wallSide) continue;
      if (!wallRunning) {
        wallRunning = true;
        wallT_ = WALL_TIME;
        wallSide = side;
        wallLast_ = normal;
        velocity.y = std::max(velocity.y, 0.0f);   // catch the fall on contact
      }
      wallNormal = normal;
      break;
    }
    if (wallRunning) {
      glm::vec3 probe = position + right * out * wallSide + glm::vec3(0.0f, height * 0.6f, 0.0f);
      glm::vec3 normal;
      if (!level.wallAt(probe, normal)) {
        wallRunning = false;
        wallCool_ = WALL_COOLDOWN;
      }
    }
  } else if (wallRunning) {
    wallRunning = false;
    wallCool_ = WALL_COOLDOWN;
  }

  if (wallRunning) {
    wallT_ -= dt;
    if (wallT_ <= 0.0f) {
      wallRunning = false;
      wallCool_ = WALL_COOLDOWN;
    }
  }

  if (wallRunning) {
    const glm::vec3& n = wallNormal;
    // Kill the component going into the wall, keep the one along it, and lean
    // on it so you track the surface instead of drifting off.
    float into = velocity.x * n.x + velocity.z * n.z;
    if (into < 0.0f) {
      velocity.x -= n.x * into;
      velocity.z -= n.z * into;
    }
    velocity.x -= n.x * WALL_STICK * dt;
    velocity.z -= n.z * WALL_STICK * dt;
    float along = planarSpeed();
    if (along > 0.01f) {
      float want = along * (1.0f + (WALL_ALONG - 1.0f) * dt);
      velocity.x *= want / along;
      velocity.z *= want / along;
    }
    // Kicking off: away from the wall as well as up.
    if (jumpHeld) {
      velocity.x += n.x * WALL_JUMP_OUT;
      velocity.z += n.z * WALL_JUMP_OUT;
      velocity.y = WALL_JUMP_UP;
      wallRunning = false;
      wallCool_ = WALL_COOLDOWN;
    }
  }

  velocity.y -= (wallRunning ? WALL_GRAVITY : GRAVITY) * dt;

  // Ease the camera between standing and crouched rather than snapping it.
  float targetEye = crouching ? EYE_CROUCH : EYE_STAND;
  eyeHeight += (targetEye - eyeHeight) * std::min(1.0f, 12.0f * dt);

  // Substepped, the same reason the browser build substeps at boost speed:
  // a single large step at sprint velocity can skip clean over a thin
  // collider in one frame.
  const int SUBSTEPS = 4;
  glm::vec3 stepVel = velocity * (dt / SUBSTEPS);
  for (int i = 0; i < SUBSTEPS; i++) {
    position += stepVel;
    grounded = level.resolve(position, radius, height);
    if (grounded && velocity.y < 0.0f) velocity.y = 0.0f;
  }
}
