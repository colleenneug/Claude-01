#pragma once
#include "Gl.h"
#include "Content.h"
#include "Level.h"
#include "Player.h"
#include "Weapon.h"
#include "Hostile.h"
#include "Camera.h"
#include <string>
#include <vector>

enum class MissionState { InProgress, Complete, Failed };

// Owns everything the C++ game actually simulates — content, the level, the
// player, the weapon, every hostile — and satisfies the same interface
// Renderer was built against (collect(), sun*, mote*), so Renderer doesn't
// need to know or care that "the world" is now a live mission rather than
// the static demo scene it was first verified against.
class Game {
public:
  // Loads content/, finds `missionId` in it, builds the level and spawns
  // its waves. Returns false (and logs why) if the mission or any enemy
  // type it references can't be found — a bad content file should refuse
  // to start rather than silently spawn nothing.
  bool init(const std::string& contentDir, const std::string& missionId);
  void destroy();

  // window/dt drive the player; camera is both read (for aim direction)
  // and written (position synced to the player's eye every frame).
  void update(GLFWwindow* window, Camera& camera, float dt, bool firePressed, bool reloadHeld,
              bool forceForward = false);

  void collect(float time, std::vector<DrawItem>& out) const;

  // ---------- world state Renderer expects ----------
  glm::vec3 sunDirection{0.0f};
  glm::vec3 sunColour{1.0f, 0.94f, 0.82f};
  float sunIntensityLux = 4.0f;
  GLuint moteVao() const { return moteVao_; }
  float moteBoxSize() const { return moteBox_; }
  int moteCount() const { return moteCount_; }

  // ---------- state main.cpp/Hud read ----------
  const Player& player() const { return player_; }
  const Weapon& weapon() const { return weapon_; }
  MissionState missionState() const { return missionState_; }
  const std::string& missionName() const { return mission_.name; }
  float waveProgress() const;
  bool bossAlive() const;
  float bossHpFraction() const;
  float hitMarkerT = 0.0f;
  float damageFlashT = 0.0f;

private:
  void spawnBossIfReady();

  Content content_;
  Level level_;
  Player player_;
  Weapon weapon_;
  MissionDef mission_;
  std::vector<Hostile> hostiles_;
  int waveTotal_ = 0;       // hostiles_[0..waveTotal_) — everything but the boss
  bool bossPending_ = false;
  int bossIndex_ = -1;
  MissionState missionState_ = MissionState::InProgress;

  GLuint moteVao_ = 0, moteVbo_ = 0;
  int moteCount_ = 2400;
  float moteBox_ = 30.0f;

  bool loaded_ = false;
};
