#pragma once
#include "Gl.h"
#include "Content.h"
#include "Level.h"
#include "Player.h"
#include "Weapon.h"
#include "Hostile.h"
#include "Camera.h"
#include "Profile.h"
#include "Scene.h"
#include <algorithm>
#include <string>
#include <vector>

enum class MissionState { InProgress, Complete, Failed };

// Dropped by a killed hostile and collected by walking over it — the
// browser build's pickups.js in miniature. Without these a mission is
// winnable only if your shooting is efficient enough to clear every wave
// *and* a 900-HP boss out of one magazine and a fixed reserve, which the
// dig site measurably is not.
enum class PickupKind { Ammo, Health };

struct Pickup {
  glm::vec3 pos{0.0f};
  PickupKind kind = PickupKind::Ammo;
  float bob = 0.0f;
  bool taken = false;
};

// Owns everything the C++ game actually simulates — content, the level, the
// player, the weapon, every hostile — and satisfies the same interface
// Renderer was built against (collect(), sun*, mote*), so Renderer doesn't
// need to know or care that "the world" is now a live mission rather than
// the static demo scene it was first verified against.
class Game : public SceneSource {
public:
  // Loads content/, finds `missionId` in it, builds the level and spawns
  // its waves. Returns false (and logs why) if the mission or any enemy
  // type it references can't be found — a bad content file should refuse
  // to start rather than silently spawn nothing. `profile`'s equipped
  // weapon/armour/cosmetic are looked up in the same content_ and applied
  // to the live Weapon/Player/Hud accent before the mission starts; Game
  // keeps a pointer to it so a mission completion can pay out chits and
  // mark the mission completed directly, once, without main.cpp having to
  // poll missionState() itself.
  bool init(const std::string& contentDir, const std::string& missionId, Profile& profile);
  void destroy();

  // window/dt drive the player; camera is both read (for aim direction)
  // and written (position synced to the player's eye every frame).
  void update(GLFWwindow* window, Camera& camera, float dt, bool firePressed, bool reloadHeld,
              bool forceForward = false);

  // ---------- SceneSource ----------
  void collect(float time, std::vector<DrawItem>& out) const override;
  glm::vec3 sunDirection() const override { return sunDirection_; }
  glm::vec3 sunColour() const override { return sunColour_; }
  float sunIntensity() const override { return sunIntensityLux_; }
  GLuint moteVao() const override { return moteVao_; }
  float moteBoxSize() const override { return moteBox_; }
  int moteCount() const override { return moteCount_; }

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
  glm::vec3 hudAccent() const { return hudAccent_; }

  // ---------- story ----------
  // The comms line currently on the channel, if any, and how strongly to
  // show it (fades in, holds, fades out). Beats are declared per mission in
  // content/missions/*.cfg and fire off mission progress — see
  // CommsTrigger in Content.h.
  const std::string& commsSpeaker() const { return commsSpeaker_; }
  const std::string& commsLine() const { return commsLine_; }
  float commsAlpha() const;
  const std::string& bossName() const { return bossName_; }

  // Short-lived "+24 AMMO" style note, shown near the crosshair after a
  // pickup. Empty when nothing was collected recently.
  const std::string& pickupNote() const { return pickupNote_; }
  float pickupNoteAlpha() const { return std::max(0.0f, std::min(1.0f, pickupNoteT_ * 1.4f)); }

private:
  void spawnBossIfReady();
  void fireComms(CommsTrigger trigger);
  void updateComms(float dt);
  void dropPickup(const glm::vec3& at, int killIndex);
  void updatePickups(float dt);

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

  Profile* profile_ = nullptr;
  bool rewardApplied_ = false;
  glm::vec3 hudAccent_{0.85f, 0.95f, 1.0f};

  // Comms: beats waiting on their delay, the one currently on the channel,
  // and which triggers have already fired (a beat never repeats within a
  // mission run).
  struct PendingBeat { const CommsBeat* beat; float at; };
  std::vector<PendingBeat> commsQueue_;
  std::string commsSpeaker_, commsLine_;
  float commsT_ = 0.0f;        // seconds the current line has been up
  float commsHold_ = 0.0f;     // how long it stays up before fading
  float missionT_ = 0.0f;      // seconds since the mission started
  bool triggerFired_[6] = {};  // one per CommsTrigger
  std::string bossName_;

  std::vector<Pickup> pickups_;
  int killCount_ = 0;          // drives the deterministic drop pattern
  std::string pickupNote_;
  float pickupNoteT_ = 0.0f;

  glm::vec3 sunDirection_{0.0f};
  glm::vec3 sunColour_{1.0f, 0.94f, 0.82f};
  float sunIntensityLux_ = 4.0f;

  GLuint moteVao_ = 0, moteVbo_ = 0;
  int moteCount_ = 2400;
  float moteBox_ = 30.0f;

  bool loaded_ = false;
};
