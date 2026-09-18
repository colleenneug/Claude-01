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
  glm::vec3 sunColour() const override { return mission_.sunColour; }
  float sunIntensity() const override { return mission_.sunIntensity; }
  // The place's own grade, straight off the mission file. See MissionDef.
  glm::vec3 skyZenith() const override { return mission_.skyZenith; }
  glm::vec3 skyHorizon() const override { return mission_.skyHorizon; }
  glm::vec3 fogColour() const override { return mission_.fogColour; }
  float fogDensity() const override { return mission_.fogDensity; }
  glm::vec3 clearColour() const override { return mission_.fogColour * 0.10f; }
  // The light probe is a capture of a lit station interior (see IBL::build).
  // At full strength it is the dominant ambient in every mission, which means
  // every sector is lit by the same room no matter what its own sky says. So
  // it contributes a fraction, and the rest of the fill comes from the
  // mission's own sky — which is the physical story anyway: what fills a
  // shadow outdoors is the sky above it.
  float iblIntensity() const override { return 0.35f; }
  glm::vec3 ambientFill() const override {
    return mission_.skyHorizon * 0.070f + mission_.skyZenith * 0.055f;
  }
  GLuint moteVao() const override { return moteVao_; }
  float moteBoxSize() const override { return moteBox_; }
  int moteCount() const override { return moteCount_; }

  // ---------- state main.cpp/Hud read ----------
  const Player& player() const { return player_; }
  // Read-only, for the headless aiming aid (EREBUS_DEBUG_AUTOAIM in
  // main.cpp), which needs to know where the hostiles are and what is
  // between it and them. It used to infer that from the draw list by
  // finding the nearest emissive item, which stopped meaning "a hostile"
  // the moment rigs grew lit vents and kills started dropping glowing
  // pickups — the aid would happily lock onto an ammo box on the floor.
  const std::vector<Hostile>& hostiles() const { return hostiles_; }
  const Level& level() const { return level_; }
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

  // ---------- the field ability ----------
  // Phase step, on Q or E: a short dash along your look direction, on a
  // cooldown. The browser build gives each class a different ability
  // (src/js/fps/weapons.js); this build has no classes yet, so it carries
  // the one that is purely a movement tool and needs nothing else — and it
  // is the one that changes how a Warden's 26 metres of reach plays, which
  // is what the ability is for.
  bool useAbility();
  float abilityCooldown() const { return abilityCool_; }
  float abilityCooldownMax() const { return class_ ? class_->abilityCooldown : kAbilityCooldown; }
  // What to call it on the HUD, and the doctrine behind it. Null when the
  // record predates classes.
  const ClassDef* playerClass() const { return class_; }
  const std::string& weaponName() const { return weaponName_; }
  bool abilityReady() const { return abilityCool_ <= 0.0f; }

private:
  static constexpr float kAbilityCooldown = 9.0f;
  static constexpr float kPhaseDistance = 6.5f;

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
  float abilityCool_ = 0.0f;
  // Reused every trigger pull rather than allocated per shot.
  std::vector<ShotResult> shotBuffer_;
  const ClassDef* class_ = nullptr;   // owned by content_, valid while loaded
  std::string weaponName_;

  // ---------- the viewmodel ----------
  // The gun in your hands. It is drawn as ordinary world geometry placed on
  // the camera's own basis rather than through a separate view-space pass,
  // because the whole renderer — shadows, fog, tone map — already works in
  // world space, and a second pass with its own projection would need its own
  // copy of all of it.
  //
  // The basis is captured in update() rather than read from the camera in
  // collect(), because collect() is the SceneSource interface the Renderer
  // calls and it has no camera to ask.
  glm::vec3 camPos_{0.0f}, camFwd_{0.0f, 0.0f, -1.0f}, camRight_{1.0f, 0.0f, 0.0f}, camUp_{0.0f, 1.0f, 0.0f};
  float camAim_ = 0.0f;
  // Recoil, sway and the walk bob, all in the viewmodel's own local space.
  float recoil_ = 0.0f;
  float swayX_ = 0.0f, swayY_ = 0.0f;
  glm::vec3 prevFwd_{0.0f, 0.0f, -1.0f};
  float bobT_ = 0.0f;
  const WeaponDef* weaponDef_ = nullptr;

  void collectViewmodel(std::vector<DrawItem>& out) const;
  glm::vec3 lookDir_{0.0f, 0.0f, -1.0f};   // last frame's aim, for the dash direction

  glm::vec3 sunDirection_{0.0f};

  GLuint moteVao_ = 0, moteVbo_ = 0;
  int moteCount_ = 2400;
  float moteBox_ = 30.0f;

  bool loaded_ = false;
};
