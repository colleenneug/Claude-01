#pragma once
#include "Gl.h"
#include "Scene.h"
#include "Level.h"
#include "Player.h"
#include "Camera.h"
#include "Content.h"
#include <string>
#include <vector>

// THE CRADLE — the station you come back to, and walk around in.
//
// Not a mission: nothing here shoots at you. It is the place the Division
// kept flying — the last assembled station in orbit, modules bolted end to
// end and still holding pressure, with the ark going past overhead. Ported
// from the browser build's src/js/fps/station.js, which is where the layout,
// the module names and the deck heights come from.
//
// Three decks around an open concourse, joined by four stair flights. The
// concourse is open through all three, so from the floor you can see the
// cupola and from the cupola you can see the floor:
//
//   DECK A  y  0   arrivals, concourse floor, the airlock out
//   DECK B  y  7   gallery ring, the two side labs
//   DECK C  y 14   upper ring and the cupola
//
// The browser build's two lifts are not here. Every deck is reachable on
// foot by the stairs, and a lift is a collider that moves, which this
// project's collision — built once per level, queried flat — would need
// rebuilding for.
class Station : public SceneSource {
public:
  // What you can walk up to and use. The browser build puts these apart on
  // purpose: a hub you cross is a hub.
  struct Terminal {
    std::string id, name, line;
    glm::vec3 pos{0.0f};
    glm::vec3 colour{0.6f, 0.9f, 1.0f};
  };

  bool init();
  void destroy();

  // Walks the player. `scriptedForward` stands in for holding W, the same
  // hook Player::update takes, so a headless run can cross the concourse.
  void update(GLFWwindow* window, Camera& camera, float dt, bool scriptedForward = false);

  // Puts the player at the arrivals end, facing down the concourse. Called
  // every time you dock.
  void enter(Camera& camera);

  // Puts the player somewhere specific, facing a given heading. Only the
  // headless hooks use it (EREBUS_STATION_AT / EREBUS_STATION_YAW): a run
  // with no mouse cannot steer, so without this the only thing a test can
  // prove is that you can walk in a straight line down the spine.
  void placeAt(Camera& camera, glm::vec3 at, float yawDegrees);

  // The terminal within reach, or null. Reach is generous — you are meant to
  // walk up and press a key, not stand on a mark.
  const Terminal* nearestTerminal() const;
  const std::vector<Terminal>& terminals() const { return terminals_; }
  const Player& player() const { return player_; }

  // ---------- SceneSource ----------
  void collect(float time, std::vector<DrawItem>& out) const override;
  glm::vec3 sunDirection() const override { return sunDir_; }
  glm::vec3 sunColour() const override { return glm::vec3(0.86f, 0.93f, 1.0f); }
  float sunIntensity() const override { return 2.1f; }
  GLuint moteVao() const override { return 0; }
  float moteBoxSize() const override { return 0.0f; }
  int moteCount() const override { return 0; }
  // Pressurised and clean: no haze indoors, and the volume is lit by its own
  // strips plus the probe rather than by depth cueing.
  float fogDensity() const override { return 0.0f; }
  glm::vec3 clearColour() const override { return glm::vec3(0.004f, 0.006f, 0.012f); }
  float viewDistance() const override { return 400.0f; }
  float skyIntensity() const override { return 0.0f; }
  float iblIntensity() const override { return 0.85f; }
  // A station you walk around has to be navigable, and this renderer has one
  // directional light and a probe — the emissive strips read as sources but
  // do not illuminate anything. So the fill carries the room, the way the
  // bounced light off a hundred metres of white panel would in a real one.
  // Cool, because everything in here is lit by the same cold strips.
  glm::vec3 ambientFill() const override { return glm::vec3(0.150f, 0.168f, 0.205f); }

private:
  Level level_;
  Player player_;
  std::vector<Terminal> terminals_;
  glm::vec3 sunDir_{-0.42f, -0.36f, -0.83f};
  float reach_ = 3.2f;
};
