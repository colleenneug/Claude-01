#pragma once
#include "Gl.h"
#include "Level.h"
#include <algorithm>

// The physical player: gravity, jump, WASD relative to wherever the camera
// is looking, and collision against the level. Distinct from Camera, which
// owns only the *view* (look direction, FOV, the aim blend) — Game syncs
// Camera::position to Player::eyePosition() once per frame. Splitting them
// is what makes a physical, collidable player possible without rewriting
// the free-fly demo camera those files also serve.
// Inputs a headless run has no keyboard to produce. Each one is OR-ed with
// the real key, so they substitute for held keys rather than faking
// OS-level key events. Driven by EREBUS_FORCE_FORWARD and the tutorial
// driver (EREBUS_TUTORIAL_AUTO) in main.cpp.
//
// At namespace scope rather than nested in Player, because a default argument
// of Scripted{} inside Player's own declaration would name the type before
// its enclosing class is complete.
struct ScriptedInput {
  bool forward = false;
  bool sprint = false;
  bool jump = false;
  bool crouch = false;
};

class Player {
public:
  glm::vec3 position{0.0f, 0.0f, 12.0f};   // feet, at the level's floor
  glm::vec3 velocity{0.0f};
  float radius = 0.4f;
  float height = 1.8f;
  // Where the camera sits. Not a constant any more: crouching and sliding
  // drop it, and it eases between the two so the transition reads as the
  // body moving rather than the view teleporting.
  float eyeHeight = 1.68f;

  // Crouch and slide, ported from src/js/fps/player.js. Crouch on its own is
  // slow and short; crouch *at speed* is a slide, which is the one movement
  // trick the browser build is built around — you come out of it faster than
  // you went in, and jumping out of it keeps that speed.
  bool crouching = false;
  bool sliding = false;

  // The wall run, also from player.js. Leave the ground at speed with a wall
  // beside you and you run along it. Gravity is turned down rather than off,
  // so a wall run is always a descent: it buys distance, not flight. You
  // cannot re-attach to the same wall until you have touched something else.
  bool wallRunning = false;
  float wallSide = 0.0f;                    // -1 left, +1 right, 0 not attached
  glm::vec3 wallNormal{0.0f};

  float maxHp = 100.0f;
  float hp = 100.0f;
  float damageReduction = 0.0f;   // 0..1 fraction shaved off incoming hits, from doctrine + armour
  // The Bulwark's Aegis Barrier: a pool of temporary health that takes
  // hits before `hp` does and does not regenerate. Kept separate rather
  // than added to hp so the health bar can show it as its own tier and so
  // it cannot heal you past your maximum.
  float overshield = 0.0f;
  float overshieldMax = 0.0f;

  // Applies `amount` to the overshield first and the rest to health.
  void takeDamage(float amount) {
    if (overshield > 0.0f) {
      float soaked = std::min(overshield, amount);
      overshield -= soaked;
      amount -= soaked;
    }
    hp -= amount;
  }
  bool grounded = true;

  // yawRadians comes from the camera's look direction: the player walks
  // relative to wherever you're facing, not relative to a fixed axis.
  void update(GLFWwindow* window, float dt, float yawRadians, bool sprint, const Level& level,
              const ScriptedInput& scripted = ScriptedInput{});

  // Ground speed, ignoring any fall or jump. What decides whether a crouch
  // becomes a slide, and what the HUD reads to show how fast you're moving.
  float planarSpeed() const { return glm::length(glm::vec2(velocity.x, velocity.z)); }

  glm::vec3 eyePosition() const { return position + glm::vec3(0, eyeHeight, 0); }
  bool alive() const { return hp > 0.0f; }

private:
  float slideT_ = 0.0f;       // time left in the current slide
  float slideCool_ = 0.0f;    // stops crouch-spamming into a permanent slide
  bool crouchWasHeld_ = false;   // a slide starts on the press, not on the hold
  float wallT_ = 0.0f;        // time left on the current wall
  float wallCool_ = 0.0f;
  // Which collider the last wall run was on, so releasing and re-probing the
  // same surface does not give you an unlimited climb. Compared by the face
  // normal and the side, which is as much identity as a box has here.
  glm::vec3 wallLast_{0.0f};
};
