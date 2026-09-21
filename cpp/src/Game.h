#pragma once
#include "Gl.h"
#include "Content.h"
#include "Level.h"
#include "Player.h"
#include "Weapon.h"
#include "Hostile.h"
#include "Camera.h"
#include "Profile.h"
#include "Site.h"
#include "Cutscene.h"
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
enum class PickupKind { Ammo, Health, Weapon };

struct Pickup {
  glm::vec3 pos{0.0f};
  PickupKind kind = PickupKind::Ammo;
  float bob = 0.0f;
  bool taken = false;
  // Weapon pickups only: which weapon it is, and what the HUD says when you
  // take it. A weapon lying where somebody left it is how you are armed on
  // the site — you wake up with nothing.
  std::string weaponId;
  std::string note;
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
              const ScriptedInput& scripted = ScriptedInput{});

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
    // A mission with a roof on it says how lit it is; everywhere else the sky
    // above it does, which is the physical story anyway.
    if (mission_.ambientSet) return mission_.ambient;
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

  // ---------- the tutorial ----------
  // A brand-new record starts planetside and is walked through the controls
  // one at a time. Each step watches for the thing it is teaching and only
  // moves on once it has actually happened — a prompt you can skip by
  // waiting is a prompt nobody reads.
  enum class TutorialStep {
    Move, Sprint, Jump, Slide, Fire, Reload, Ability, Clear, Done
  };
  bool isTutorial() const { return mission_.tutorial; }

  // ---------- objectives ----------
  // What you are doing right now, and the control hint under it. These come
  // from the site (Site::Objective) and change as you cross it — quietly,
  // with no banner and no pause, because a wall of AREA COMPLETE every ten
  // metres turns a place into a corridor of checkpoints.
  const std::string& objectiveText() const { return objectiveText_; }
  const std::string& objectiveHint() const { return objectiveHint_; }

  // ---------- cutscenes ----------
  bool cutscenePlaying() const { return cutscene_.playing(); }
  const std::string& cutsceneCaption() const { return cutscene_.caption(); }
  float cutsceneFade() const { return cutscene_.fade(); }
  void skipCutscene() { cutscene_.stop(); }

  // ---------- what you are holding ----------
  // False until you have picked something up. An empty-handed player has no
  // ammo counter, no viewmodel and nothing to fire, which is the whole point
  // of waking up in a bunk.
  bool armed() const { return armed_; }
  // What to put on screen right now, and what it is asking for. Empty once
  // the tutorial is over — or if this mission is not one.
  const std::string& tutorialPrompt() const { return tutorialPrompt_; }
  const std::string& tutorialHint() const { return tutorialHint_; }
  float tutorialProgress() const { return tutorialProgress_; }
  // Which step, for the headless driver — it has to know what input to
  // substitute, and matching on the prompt text would be matching on a
  // string written for a player to read.
  TutorialStep tutorialStep() const { return tutorialStep_; }

  // ---------- the trauma harness ----------
  // Going down is not the end of a mission. The harness gets you back on
  // your feet where you fell, a few times, which is how the browser build
  // does it too (src/js/fps/campaign.js: three charges, none for the boss).
  // Running out of charges is the only thing that fails a mission.
  // Career experience earned in this mission so far, and how much of it has
  // already been handed to the record. The split exists because a mission
  // can be left at any point — you walk out of a debrief, or the harness
  // runs out — and the experience for what you actually killed should not
  // depend on which of those happened.
  int xpEarned() const { return xpEarned_; }
  int xpUnbanked() const { return xpEarned_ - xpBanked_; }
  void bankXp() { xpBanked_ = xpEarned_; }

  int harnessLeft() const { return harnessLeft_; }
  int harnessMax() const { return harnessMax_; }
  // Seconds left on the ground before you are back up, or 0 when you are up.
  float downedFor() const { return downT_; }
  // How long a charge takes to stand you back up. The HUD draws the
  // countdown against it, so it lives here rather than as the same literal
  // typed into two files.
  static constexpr float kDownSeconds = 2.6f;
  bool downed() const { return downT_ > 0.0f; }

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

  // ---------------------------------------------------------------- holsters
  // Two slots: a primary and a sidearm. Which slot a weapon goes into is its
  // `shape` — a pistol is a sidearm, everything else is a primary — so a
  // content drop that adds a second pistol needs no code and no new key.
  //
  // The live weapon is still `weapon_`, and switching copies its state out
  // and the other slot's state in. Keeping one live Weapon rather than two
  // means every system that fires, reloads or reads ammo carries on reading
  // exactly one object.
  enum Slot : int { SlotPrimary = 0, SlotSidearm = 1 };

  struct Holster {
    const WeaponDef* def = nullptr;
    std::string name;
    Weapon state;          // ammo and reload timer, parked while it is stowed
    bool filled = false;
  };

  int slot() const { return slot_; }
  const Holster& holster(int s) const { return slots_[s == 1 ? 1 : 0]; }
  // True if the other slot has anything in it — the HUD dims the second line
  // rather than hiding it, so an empty holster is visibly a thing you could
  // fill rather than a feature you do not have.
  bool hasOtherWeapon() const { return slots_[slot_ ^ 1].filled; }
  // Swaps to the other slot if it is filled. Returns true if anything
  // happened, so the caller can play the sound it does not have yet.
  bool switchWeapon();
  bool selectSlot(int s);
  // 0 while a swap is in progress, rising to 1: the viewmodel drops out of
  // frame and the new one comes up, and you cannot fire through it.
  float swapProgress() const { return swapT_ <= 0.0f ? 1.0f : 1.0f - swapT_ / kSwapSeconds; }
  static constexpr float kSwapSeconds = 0.42f;
  bool abilityReady() const { return abilityCool_ <= 0.0f; }

private:
  static constexpr float kAbilityCooldown = 9.0f;
  static constexpr float kPhaseDistance = 6.5f;

  void spawnBossIfReady();
  void fireComms(CommsTrigger trigger);
  void updateComms(float dt);
  void dropPickup(const glm::vec3& at, int killIndex);
  void updatePickups(float dt);
  void updateTutorial(float dt);
  void updateSite(float dt);
  void reviveAtFallPoint();
  void fireTrigger(const std::string& id);
  void equipWeaponById(const std::string& id);
  void setTutorialStep(TutorialStep step);

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
  // Comms run on their own clock, which stops dead while a cutscene plays.
  // A cutscene owns the whole frame, so a line that comes due behind the
  // letterbox is a line nobody ever sees — and the block's opening scene is
  // nine seconds long, which was swallowing the first two things Division
  // says to you.
  float commsClock_ = 0.0f;
  bool triggerFired_[6] = {};  // one per CommsTrigger
  std::string bossName_;

  std::vector<Pickup> pickups_;
  int killCount_ = 0;          // drives the deterministic drop pattern
  std::string pickupNote_;
  float pickupNoteT_ = 0.0f;
  float abilityCool_ = 0.0f;
  int xpEarned_ = 0, xpBanked_ = 0;
  int harnessMax_ = 3, harnessLeft_ = 3;
  float downT_ = 0.0f;              // counts down while you are on the ground
  glm::vec3 fellAt_{0.0f};          // where you went down
  TutorialStep tutorialStep_ = TutorialStep::Move;
  std::string tutorialPrompt_, tutorialHint_;
  float tutorialProgress_ = 0.0f;
  float tutorialWalked_ = 0.0f;      // metres covered on the Move step
  float tutorialSprinted_ = 0.0f;
  float tutorialHold_ = 0.0f;        // beat between steps, so they do not blur past
  int tutorialKills_ = 0;
  glm::vec3 tutorialLastPos_{0.0f};
  bool tutorialFired_ = false, tutorialReloaded_ = false;
  bool tutorialJumped_ = false, tutorialSlid_ = false, tutorialUsedAbility_ = false;
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
  // Decays fast — a muzzle flash you can still see a tenth of a second
  // later reads as a lamp on the end of the barrel.
  float muzzleFlash_ = 0.0f;

  // ---------- what a shot feels like ----------
  // Recoil is applied to the *view*, not just to the gun: a rifle that kicks
  // the model but leaves the crosshair nailed to the target is a rifle you
  // are watching rather than firing. The kick goes on instantly and is
  // pulled back toward zero, so the sight settles roughly where it started
  // instead of walking up the screen forever.
  float viewKickPitch_ = 0.0f, viewKickYaw_ = 0.0f;
  float viewKickRecoverPitch_ = 0.0f, viewKickRecoverYaw_ = 0.0f;
  // Screen shake, from taking a hit or from something big going off nearby.
  float shake_ = 0.0f;
  float shakeT_ = 0.0f;

  // A spark where a round landed. Short-lived and drawn as world geometry,
  // so it is lit and occluded like everything else rather than being a
  // sprite pasted over the frame.
  struct Impact {
    glm::vec3 pos{0.0f};
    glm::vec3 tint{1.0f, 0.82f, 0.55f};
    float life = 0.0f;
    float seed = 0.0f;
  };
  std::vector<Impact> impacts_;
  void addImpact(const glm::vec3& at, const glm::vec3& tint);
  float swayX_ = 0.0f, swayY_ = 0.0f;
  glm::vec3 prevFwd_{0.0f, 0.0f, -1.0f};
  float bobT_ = 0.0f;
  const WeaponDef* weaponDef_ = nullptr;
  bool armed_ = true;

  Holster slots_[2];
  int slot_ = SlotPrimary;
  float swapT_ = 0.0f;          // counts down while the hands are busy
  int swapTo_ = -1;             // the slot to become live at the halfway point
  // Which slot a def belongs in. One rule, used by the hub, by the floor
  // pickups and by init, so the three can never disagree about where a
  // weapon lives.
  static int slotFor(const WeaponDef* def);

  // Hand-built levels (MissionDef::layout). Empty `site_.parts` means
  // this mission is a procedural arena and none of this is in play.
  Site site_;
  bool usingSite_ = false;
  size_t objectiveIndex_ = 0;
  std::string objectiveText_, objectiveHint_;
  std::vector<bool> triggerFiredById_;
  // Hostiles asleep until their room's trigger fires, indexed alongside
  // hostiles_ so waking one is a flag rather than a spawn.
  std::vector<std::string> hostileWakeOn_;
  Cutscene cutscene_;

  void collectViewmodel(std::vector<DrawItem>& out) const;
  glm::vec3 lookDir_{0.0f, 0.0f, -1.0f};   // last frame's aim, for the dash direction

  glm::vec3 sunDirection_{0.0f};

  GLuint moteVao_ = 0, moteVbo_ = 0;
  int moteCount_ = 2400;
  float moteBox_ = 30.0f;

  bool loaded_ = false;
};
