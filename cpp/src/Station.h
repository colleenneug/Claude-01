#pragma once
#include "Gl.h"
#include "Scene.h"
#include "Level.h"
#include "Player.h"
#include "Camera.h"
#include "Content.h"
#include "Crew.h"
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

  // `content` supplies the crew (content/crew/*.cfg): a hub's geometry never
  // changes, but who is standing in it is content.
  //
  // `layout` picks the place. Two exist:
  //   "cradle" — the ark in orbit, described above
  //   "kourou" — the Strider programme's ground station on Earth, which is
  //              where a record spends the whole of its first campaign
  // The two differ in more than geometry: one is a sealed volume with no sky
  // and no haze, the other has weather and a doorway onto a launch pan, so
  // the scene parameters below are fields set by init() rather than the
  // constants they used to be.
  bool init(const Content& content, const std::string& layout = "cradle");

  [[nodiscard]] const std::string& layout() const { return layout_; }
  // What to put at the top of the screen. The hub is not always the Cradle
  // any more, and a heading that says so anyway is the kind of detail that
  // makes a place feel like a reskin.
  [[nodiscard]] const std::string& title() const { return title_; }
  [[nodiscard]] const std::string& subtitle() const { return subtitle_; }
  void destroy();

  // Walks the player. `scriptedForward` stands in for holding W, the same
  // hook Player::update takes, so a headless run can cross the concourse.
  void update(GLFWwindow* window, Camera& camera, float dt,
              const ScriptedInput& scripted = ScriptedInput{});

  // Same as Game::setInputFrozen: the kit screen navigates on W A S D, and
  // walking off across the concourse while reading a rifle's stats is not
  // what anyone meant. The crew keep walking their routes.
  void setInputFrozen(bool frozen) { inputFrozen_ = frozen; }

  // Puts the player at the arrivals end, facing down the concourse. Called
  // every time you dock.
  void enter(Camera& camera);

  // Puts the player somewhere specific, facing a given heading. Only the
  // headless hooks use it (EREBUS_STATION_AT / EREBUS_STATION_YAW): a run
  // with no mouse cannot steer, so without this the only thing a test can
  // prove is that you can walk in a straight line down the spine.
  void placeAt(Camera& camera, glm::vec3 at, float yawDegrees);

  // The crew member with a post within reach, or null. Takes precedence over
  // the terminals: where both exist they are the same station, and being
  // offered "FLIGHT DECK" while standing in front of Kaur is two prompts for
  // one thing.
  const Crew::Person* nearestPerson() const;
  const Crew& crew() const { return crew_; }

  // The terminal within reach, or null. Reach is generous — you are meant to
  // walk up and press a key, not stand on a mark.
  const Terminal* nearestTerminal() const;
  const std::vector<Terminal>& terminals() const { return terminals_; }
  const Player& player() const { return player_; }

  // ---------- SceneSource ----------
  void collect(float time, std::vector<DrawItem>& out) const override;
  glm::vec3 sunDirection() const override { return sunDir_; }
  glm::vec3 sunColour() const override { return sunColour_; }
  float sunIntensity() const override { return sunIntensity_; }
  GLuint moteVao() const override { return 0; }
  float moteBoxSize() const override { return 0.0f; }
  int moteCount() const override { return 0; }
  // The Cradle is pressurised and clean — no haze, lit by its own strips and
  // the probe. Kourou has a doorway onto a launch pan and coastal air coming
  // through it, so it gets both fog and a sky.
  float fogDensity() const override { return fogDensity_; }
  glm::vec3 fogColour() const override { return fogColour_; }
  glm::vec3 clearColour() const override { return clearColour_; }
  float viewDistance() const override { return viewDistance_; }
  glm::vec3 skyZenith() const override { return skyZenith_; }
  glm::vec3 skyHorizon() const override { return skyHorizon_; }
  float skyIntensity() const override { return skyIntensity_; }
  float iblIntensity() const override { return iblIntensity_; }
  // A station you walk around has to be navigable, and this renderer has one
  // directional light and a probe — the emissive strips read as sources but
  // do not illuminate anything. So the fill carries the room, the way the
  // bounced light off a hundred metres of white panel would in a real one.
  // Cool, because everything in here is lit by the same cold strips.
  glm::vec3 ambientFill() const override { return ambientFill_; }

private:
  Level level_;
  Player player_;
  Crew crew_;
  std::vector<Terminal> terminals_;
  std::string layout_ = "cradle";
  std::string title_ = "THE CRADLE";
  std::string subtitle_;

  // Where you arrive, and facing which way. Per layout, because the Cradle's
  // arrivals end and Kourou's pad door are not in the same place.
  glm::vec3 spawn_{0.0f, 0.0f, -48.0f};
  float spawnYaw_ = 90.0f;

  // The grade. Defaults are the Cradle's; buildKourou overwrites them.
  glm::vec3 sunDir_{-0.42f, -0.36f, -0.83f};
  glm::vec3 sunColour_{0.86f, 0.93f, 1.0f};
  float sunIntensity_ = 2.1f;
  float fogDensity_ = 0.0f;
  glm::vec3 fogColour_{0.30f, 0.31f, 0.36f};
  glm::vec3 clearColour_{0.004f, 0.006f, 0.012f};
  float viewDistance_ = 400.0f;
  glm::vec3 skyZenith_{0.0f}, skyHorizon_{0.0f};
  float skyIntensity_ = 0.0f;
  float iblIntensity_ = 0.85f;
  // A station you walk around has to be navigable, and this renderer has one
  // directional light and a probe — the emissive strips read as sources but
  // do not illuminate anything. So the fill carries the room, the way the
  // bounced light off a hundred metres of white panel would in a real one.
  glm::vec3 ambientFill_{0.150f, 0.168f, 0.205f};

  bool inputFrozen_ = false;
  float reach_ = 3.2f;
};
