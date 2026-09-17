#pragma once
#include "Gl.h"
#include "Camera.h"
#include "Content.h"
#include "Mesh.h"
#include "Scene.h"
#include <string>
#include <vector>

// Open space: the ship you fly between the Cradle and the worlds you drop
// onto. This replaces the menu-shaped hub as the place you are *between*
// missions — the hub still exists, but you now fly to the station to open
// it rather than starting inside it.
//
// The flight model is deliberately arcade rather than Newtonian. True
// frictionless space means every nudge is permanent and a ten-year-old
// spends the session tumbling; velocity here is damped toward the thrust
// direction, so letting go coasts to a stop and the ship goes where it is
// pointed. Scale is in the thousands of units, so Renderer's far plane and
// shadow decisions come from SceneSource, not from the mission's numbers.
class Space : public SceneSource {
public:
  // A place you can fly to. `missionId` empty means the Cradle itself,
  // which opens the hub instead of launching a mission.
  struct Body {
    std::string id, name;
    glm::vec3 pos{0.0f};
    float radius = 200.0f;
    glm::vec3 tint{0.5f, 0.5f, 0.55f};
    std::string missionId;
    bool isStation = false;
  };

  bool init(const Content& content);
  void destroy();

  // Flies the ship. `camera` is written every frame (the view rides the
  // ship's nose). Returns nothing — what the player is near, and whether
  // they asked to engage it, is read back through the accessors below.
  // forceThrust exists only for headless verification (EREBUS_FORCE_FORWARD
  // in main.cpp): a run with no keyboard has no way to hold W, and without
  // it a test can never prove the ship moves at all.
  void update(GLFWwindow* window, Camera& camera, float dt, bool boost,
              bool forceThrust = false);

  // ---------- SceneSource ----------
  void collect(float time, std::vector<DrawItem>& out) const override;
  glm::vec3 sunDirection() const override { return sunDirection_; }
  glm::vec3 sunColour() const override { return glm::vec3(1.0f, 0.95f, 0.88f); }
  float sunIntensity() const override { return 3.4f; }
  GLuint moteVao() const override { return starVao_; }
  float moteBoxSize() const override { return starBox_; }
  int moteCount() const override { return starCount_; }
  glm::vec3 moteColour() const override { return glm::vec3(0.86f, 0.90f, 1.0f); }
  float moteSize() const override { return 1.3f; }       // fixed pixels, not metres
  float moteOpacity() const override { return 0.75f; }
  bool moteDistanceScaled() const override { return false; }
  glm::vec3 clearColour() const override { return glm::vec3(0.0016f, 0.0018f, 0.0035f); }
  float viewDistance() const override { return 60000.0f; }
  bool wantsShadows() const override { return false; }
  // Vacuum: no haze, no inscatter, and genuinely zero rather than a trace.
  // The fog is an integral along the whole view ray, and a background pixel
  // has no geometry, so its ray runs to the 60,000-unit far plane: even
  // 0.00004 per unit integrates to 91% opacity out there, which painted the
  // entire sky fog-coloured and drowned the starfield.
  float fogDensity() const override { return 0.0f; }
  float fogFalloff() const override { return 0.0f; }
  float fogBase() const override { return 0.0f; }
  glm::vec3 fogColour() const override { return glm::vec3(0.05f, 0.05f, 0.09f); }
  float fogInscatter() const override { return 0.0f; }

  // ---------- what the HUD and main.cpp read ----------
  const std::vector<Body>& bodies() const { return bodies_; }
  // Nearest body within engage range, or -1. "Engage" means land (a world)
  // or dock (the station).
  int nearestBody() const { return nearest_; }
  float nearestDistance() const { return nearestDist_; }
  // Range scales with the body: a 520-unit world and a 180-unit station
  // can't share one absolute distance and both feel like "close enough".
  float engageRangeFor(const Body& b) const { return b.radius * 0.5f; }
  bool inEngageRange() const {
    return nearest_ >= 0 && nearestDist_ <= engageRangeFor(bodies_[nearest_]);
  }
  const Body* engageTarget() const { return inEngageRange() ? &bodies_[nearest_] : nullptr; }

  float speed() const { return glm::length(velocity_); }
  float maxSpeed() const { return maxSpeed_; }
  const glm::vec3& shipPosition() const { return position_; }

  // Puts the ship just off a body — used when a mission ends, so you come
  // back to space where you left rather than at the origin.
  void placeNear(const std::string& bodyId);

private:
  void buildStars();

  std::vector<Body> bodies_;
  Mesh planetMesh_, stationMesh_;

  glm::vec3 position_{0.0f, 0.0f, 2600.0f};
  glm::vec3 velocity_{0.0f};
  glm::vec3 sunDirection_{glm::normalize(glm::vec3(-0.45f, -0.25f, -0.86f))};

  float maxSpeed_ = 900.0f;
  int nearest_ = -1;
  float nearestDist_ = 0.0f;

  GLuint starVao_ = 0, starVbo_ = 0;
  int starCount_ = 3000;
  float starBox_ = 40000.0f;

  bool loaded_ = false;
};
