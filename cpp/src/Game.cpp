#include "Game.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>

bool Game::init(const std::string& contentDir, const std::string& missionId, Profile& profile) {
  if (!content_.loadAll(contentDir)) return false;
  const MissionDef* def = content_.mission(missionId);
  if (!def) {
    std::fprintf(stderr, "[Game] mission '%s' not found in %s/missions\n",
                 missionId.c_str(), contentDir.c_str());
    return false;
  }
  mission_ = *def;
  profile_ = &profile;
  rewardApplied_ = false;

  // Reset per-mission run state — the Hub lets a player launch more than
  // one mission per process, and without this a second init() on the same
  // Game would keep the first mission's dead hostiles, boss index and
  // Complete/Failed state around instead of starting clean.
  hostiles_.clear();
  waveTotal_ = 0;
  bossPending_ = false;
  bossIndex_ = -1;
  missionState_ = MissionState::InProgress;
  hitMarkerT = 0.0f;
  damageFlashT = 0.0f;
  commsQueue_.clear();
  commsSpeaker_.clear();
  commsLine_.clear();
  commsT_ = 0.0f;
  commsHold_ = 0.0f;
  missionT_ = 0.0f;
  for (bool& fired : triggerFired_) fired = false;
  bossName_.clear();
  pickups_.clear();
  killCount_ = 0;
  pickupNote_.clear();
  pickupNoteT_ = 0.0f;

  level_.build(mission_.arenaSize, mission_.floorColour);
  HostileGeometry::ensure();

  // Gear: looked up by the ids the profile has equipped, in the same
  // content_ this mission itself came from, so a monthly content drop that
  // adds a new weapon/armor/cosmetic file just works — nothing here is
  // hardcoded to the three starter items.
  // The doctrine comes first: it sets the baseline vitals and the passive,
  // and armour adds to both on top. A record written before classes existed
  // has no doctrine, and falls back to the old flat baseline rather than
  // refusing to load.
  class_ = content_.playerClass(profile.classId);
  const float baseHp = class_ ? class_->hp : 100.0f;
  const float baseDr = class_ ? class_->damageReduction : 0.0f;

  const ArmorDef* armor = content_.armor(profile.equippedArmor);
  player_.maxHp = baseHp + (armor ? armor->hpBonus : 0.0f);
  // Two reductions stack multiplicatively, not additively: 22% doctrine plus
  // an eventual 80% armour piece would otherwise reach immunity, and each
  // layer should shave a share of what got through the last one anyway.
  const float armourDr = armor ? armor->damageReduction : 0.0f;
  player_.damageReduction = 1.0f - (1.0f - baseDr) * (1.0f - armourDr);
  player_.overshield = 0.0f;
  player_.overshieldMax = 0.0f;

  abilityCool_ = 0.0f;

  // The issued weapon is the doctrine's, unless the player has equipped
  // something else in the armoury.
  std::string weaponId = profile.equippedWeapon;
  if (weaponId.empty() && class_) weaponId = class_->weaponId;
  const WeaponDef* wdef = content_.weapon(weaponId);
  if (wdef) {
    weapon_.magSize = wdef->magSize;
    weapon_.ammoInMag = wdef->magSize;
    weapon_.reserveAmmo = wdef->reserveAmmo;
    weapon_.damage = wdef->damage;
    weapon_.headshotMultiplier = wdef->headshotMultiplier;
    weapon_.fireInterval = wdef->fireInterval;
    weapon_.reloadTime = wdef->reloadTime;
    weapon_.pellets = wdef->pellets;
    weapon_.spread = wdef->spread;
    weapon_.pierce = wdef->pierce;
    weapon_.range = wdef->range;
    weapon_.primed = false;
    weaponName_ = wdef->name;
  } else {
    weaponName_ = weaponId;
  }
  weaponDef_ = wdef;
  recoil_ = 0.0f;
  swayX_ = swayY_ = 0.0f;
  bobT_ = 0.0f;

  const CosmeticDef* cosmetic = content_.cosmetic(profile.equippedCosmetic);
  hudAccent_ = cosmetic ? cosmetic->accent : glm::vec3(0.85f, 0.95f, 1.0f);

  player_.position = glm::vec3(0.0f, level_.floorY(), 0.0f);
  player_.hp = player_.maxHp;

  // Spawn every non-boss wave immediately, on rings scaled to each wave's
  // own radius; the boss (if this mission has one) waits until every
  // regular hostile is Gone — see spawnBossIfReady().
  for (auto& w : mission_.waves) {
    const EnemyType* t = content_.enemy(w.enemyId);
    if (!t) {
      std::fprintf(stderr, "[Game] mission '%s' references unknown enemy '%s', skipping wave\n",
                   missionId.c_str(), w.enemyId.c_str());
      continue;
    }
    for (int i = 0; i < w.count; i++) {
      Hostile h;
      glm::vec3 p = level_.spawnPoint((int)hostiles_.size(), w.count, w.radius);
      p.y = level_.floorY();
      // Crate scatter (Level::build) only keeps clear of the arena centre —
      // it has no idea what radius a mission will actually spawn waves at,
      // so a spawn point can land inside a crate's collider. Resolving it
      // against the level once, right here, pushes it clear before the
      // hostile ever exists; without this a hostile that spawned embedded
      // in a crate would re-enter that same crate every single frame it
      // tried to close on the player and never make net progress — not
      // "slow pathing around cover," just permanently wedged.
      level_.resolve(p, t->radius, t->height);
      h.spawn(t, p);
      hostiles_.push_back(h);
    }
  }
  waveTotal_ = (int)hostiles_.size();
  bossPending_ = !mission_.bossId.empty() && content_.enemy(mission_.bossId) != nullptr;
  if (!mission_.bossId.empty() && !content_.enemy(mission_.bossId)) {
    std::fprintf(stderr, "[Game] mission '%s' boss '%s' not found, mission has no boss\n",
                 missionId.c_str(), mission_.bossId.c_str());
  }

  // Twenty degrees: low enough for long dramatic shadows, high enough that
  // upward-facing surfaces still catch real light. Same reasoning as the
  // renderer demo scene this replaces.
  sunDirection_ = glm::normalize(glm::vec3(-0.62f, -0.34f, -0.32f));

  // The dust motes don't depend on anything mission-specific (fixed box,
  // fixed count, fixed seed), so they're built once and reused across
  // however many missions the Hub launches in one process rather than
  // leaking a VAO/VBO pair every relaunch.
  if (moteVao_ == 0) {
    srand(99);
    auto rnd = [](float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; };
    std::vector<float> data;
    data.reserve(moteCount_ * 5);
    for (int i = 0; i < moteCount_; i++) {
      data.push_back(rnd(-1.0f, 1.0f) * moteBox_ * 0.5f);
      data.push_back(rnd(-1.0f, 1.0f) * moteBox_ * 0.5f);
      data.push_back(rnd(-1.0f, 1.0f) * moteBox_ * 0.5f);
      data.push_back(rnd(0.0f, 1.0f));
      data.push_back(0.35f + std::pow(rnd(0.0f, 1.0f), 3.0f) * 1.9f);
    }
    glGenVertexArrays(1, &moteVao_);
    glGenBuffers(1, &moteVbo_);
    glBindVertexArray(moteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, moteVbo_);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(4 * sizeof(float)));
    glBindVertexArray(0);
  }

  if (const EnemyType* bt = content_.enemy(mission_.bossId)) bossName_ = bt->name;

  std::printf("[Game] mission '%s' loaded: %d hostile(s), boss=%s, %zu comms beat(s)\n",
              mission_.name.c_str(), waveTotal_, bossPending_ ? mission_.bossId.c_str() : "none",
              mission_.comms.size());
  loaded_ = true;
  fireComms(CommsTrigger::Deploy);
  return true;
}

// ------------------------------------------------------------------ comms

void Game::fireComms(CommsTrigger trigger) {
  int idx = (int)trigger;
  if (idx < 0 || idx >= 6 || triggerFired_[idx]) return;
  triggerFired_[idx] = true;
  for (const CommsBeat& beat : mission_.comms) {
    if (beat.trigger != trigger) continue;
    commsQueue_.push_back({&beat, missionT_ + beat.delay});
  }
}

void Game::updateComms(float dt) {
  commsT_ += dt;

  // A line that's had its time on screen clears, so the next queued beat
  // isn't stuck waiting behind it forever.
  if (!commsLine_.empty() && commsT_ > commsHold_ + 0.6f) {
    commsLine_.clear();
    commsSpeaker_.clear();
  }

  if (!commsLine_.empty()) return;   // one voice on the channel at a time

  for (size_t i = 0; i < commsQueue_.size(); i++) {
    if (commsQueue_[i].at > missionT_) continue;
    const CommsBeat* beat = commsQueue_[i].beat;
    commsSpeaker_ = beat->speaker;
    commsLine_ = beat->line;
    commsT_ = 0.0f;
    // Long lines stay up longer — roughly reading speed, with a floor so a
    // two-word callout doesn't blink past.
    commsHold_ = std::max(2.4f, 0.055f * (float)beat->line.size());
    commsQueue_.erase(commsQueue_.begin() + (long)i);
    return;
  }
}

// ---------------------------------------------------------- field ability

bool Game::useAbility() {
  if (!loaded_ || missionState_ != MissionState::InProgress) return false;
  if (abilityCool_ > 0.0f) return false;

  const AbilityKind kind = class_ ? class_->ability : AbilityKind::Phase;
  abilityCool_ = abilityCooldownMax();

  switch (kind) {
    case AbilityKind::Barrier: {
      // AEGIS BARRIER: a pool of temporary health that takes the next wave of
      // fire before your own does. It replaces rather than adds to whatever is
      // left of the last one, so holding the button through a fight does not
      // stack barriers into permanent immunity.
      player_.overshieldMax = 60.0f;
      player_.overshield = 60.0f;
      pickupNote_ = "AEGIS BARRIER";
      break;
    }
    case AbilityKind::Breach: {
      // SYSTEMS BREACH: an EMP pulse. Everything within reach stops where it
      // is for a few seconds. It deals no damage — its job is to buy a window,
      // not to clear a room.
      const float radius = 13.0f;
      int caught = 0;
      for (Hostile& h : hostiles_) {
        if (!h.alive() || h.state == HostileState::Dying) continue;
        if (glm::length(h.pos - player_.position) > radius) continue;
        // A boss shrugs most of it off; a drone is out of the fight.
        h.stunT = h.isBoss ? 1.2f : 3.2f;
        caught++;
      }
      char buf[48];
      std::snprintf(buf, sizeof(buf), "SYSTEMS BREACH - %d STUNNED", caught);
      pickupNote_ = buf;
      break;
    }
    case AbilityKind::Phase: {
      // PHASE STEP: a dash along the look direction, flattened. A phase step
      // is a reposition, not a jump, and letting it follow the pitch would
      // fire you into the sky or the floor depending on where you were aiming.
      glm::vec3 flat(lookDir_.x, 0.0f, lookDir_.z);
      if (glm::length(flat) < 1e-4f) flat = glm::vec3(0.0f, 0.0f, -1.0f);
      flat = glm::normalize(flat);

      // Step there in pieces, resolving collision at each one, so the dash
      // stops against a crate rather than through it — the whole point of it
      // is to get behind cover, which does not work if it can also get you
      // inside cover.
      const int steps = 8;
      glm::vec3 probe = player_.position;
      for (int i = 0; i < steps; i++) {
        glm::vec3 next = probe + flat * (kPhaseDistance / (float)steps);
        level_.resolve(next, player_.radius, player_.height);
        // Resolution pushed it back roughly where it started: something solid
        // is there, so this is as far as the step goes.
        if (glm::length(glm::vec2(next.x - probe.x, next.z - probe.z)) < 0.02f) break;
        probe = next;
      }
      player_.position = probe;
      weapon_.primed = true;      // ...and the next round lands as a headshot
      pickupNote_ = "PHASE STEP - NEXT SHOT PRIMED";
      break;
    }
  }

  pickupNoteT_ = 1.4f;
  return true;
}

// ---------------------------------------------------------------- pickups

void Game::dropPickup(const glm::vec3& at, int killIndex) {
  // A fixed rotation rather than a random roll: every third kill drops
  // health, the rest drop ammo, so a run can't be starved by bad luck and a
  // mission's total resupply is a known quantity when tuning it.
  Pickup p;
  p.pos = at;
  p.pos.y = level_.floorY() + 0.55f;
  p.kind = (killIndex % 3 == 2) ? PickupKind::Health : PickupKind::Ammo;
  pickups_.push_back(p);
}

void Game::updatePickups(float dt) {
  pickupNoteT_ = std::max(0.0f, pickupNoteT_ - dt);
  if (pickupNoteT_ <= 0.0f) pickupNote_.clear();

  const float reach = 1.8f;
  for (Pickup& p : pickups_) {
    if (p.taken) continue;
    p.bob += dt * 2.2f;

    glm::vec3 d = p.pos - (player_.position + glm::vec3(0.0f, 0.9f, 0.0f));
    if (glm::length(d) > reach) continue;

    // A pickup that would add nothing (full reserve, full health) is left on
    // the ground for later rather than silently consumed, so the gain is
    // worked out before anything is applied.
    char note[48];
    if (p.kind == PickupKind::Ammo) {
      int gain = std::min(weapon_.reserveAmmo + weapon_.magSize, weapon_.magSize * 8) -
                 weapon_.reserveAmmo;
      if (gain <= 0) continue;
      weapon_.reserveAmmo += gain;
      std::snprintf(note, sizeof(note), "+%d AMMO", gain);
    } else {
      float gain = std::min(player_.maxHp, player_.hp + 25.0f) - player_.hp;
      if (gain <= 0.5f) continue;
      player_.hp += gain;
      std::snprintf(note, sizeof(note), "+%d INTEGRITY", (int)std::lround(gain));
    }

    p.taken = true;
    pickupNote_ = note;
    pickupNoteT_ = 1.6f;
  }

  pickups_.erase(std::remove_if(pickups_.begin(), pickups_.end(),
                                [](const Pickup& p) { return p.taken; }),
                 pickups_.end());
}

float Game::commsAlpha() const {
  if (commsLine_.empty()) return 0.0f;
  const float fade = 0.35f;
  if (commsT_ < fade) return commsT_ / fade;                       // in
  if (commsT_ < commsHold_) return 1.0f;                           // hold
  return std::max(0.0f, 1.0f - (commsT_ - commsHold_) / 0.6f);     // out
}

void Game::destroy() {
  level_.destroy();
  HostileGeometry::destroyShared();
  if (moteVbo_) glDeleteBuffers(1, &moteVbo_);
  if (moteVao_) glDeleteVertexArrays(1, &moteVao_);
  moteVbo_ = moteVao_ = 0;   // so a later init() (Hub -> another mission) rebuilds them
  loaded_ = false;
}

void Game::spawnBossIfReady() {
  if (!bossPending_) return;
  bool clear = std::all_of(hostiles_.begin(), hostiles_.begin() + waveTotal_,
                           [](const Hostile& h) { return h.state == HostileState::Gone; });
  if (!clear) return;

  const EnemyType* t = content_.enemy(mission_.bossId);
  Hostile boss;
  boss.spawn(t, glm::vec3(0.0f, level_.floorY(), -level_.arenaHalf() * 0.5f),
             /*isBoss=*/true, mission_.bossHpMultiplier);
  bossIndex_ = (int)hostiles_.size();
  hostiles_.push_back(boss);
  bossPending_ = false;
  fireComms(CommsTrigger::BossSpawn);
}

void Game::update(GLFWwindow* window, Camera& camera, float dt, bool firePressed, bool reloadHeld,
                   bool forceForward) {
  if (!loaded_) return;
  // Advanced here, once per frame, rather than inside updateComms: both the
  // comms schedule and the post-drop grace period below read it.
  missionT_ += dt;
  if (missionState_ != MissionState::InProgress) {
    // The mission is over, but its closing comms beat still has to play
    // out — the HUD sits on the end-of-mission banner until the player
    // chooses to head back, so there's time for it.
    updateComms(dt);
    return;
  }

  abilityCool_ = std::max(0.0f, abilityCool_ - dt);
  // Kept for useAbility() and the viewmodel, both of which are called from
  // places that have no camera of their own to ask.
  lookDir_ = camera.forward();
  camPos_ = camera.position;
  camFwd_ = camera.forward();
  camRight_ = camera.right();
  camUp_ = glm::normalize(glm::cross(camRight_, camFwd_));
  camAim_ = camera.aim;

  bool sprint = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
  player_.update(window, dt, glm::radians(camera.yaw), sprint, level_, forceForward);
  camera.position = player_.eyePosition();

  weapon_.update(dt);
  if (reloadHeld) weapon_.startReload();

  if (firePressed && weapon_.canFire()) {
    // One trigger pull can strike several hostiles — a shotgun's cone across
    // a pair of thralls, or an induction bolt through the front rank into the
    // one behind it — so this is a list, not a single hit.
    weapon_.fire(camera.position, camera.forward(), level_, hostiles_, shotBuffer_);
    for (const ShotResult& shot : shotBuffer_) {
      if (!shot.hitHostile) continue;
      Hostile& h = hostiles_[shot.hostileIndex];
      bool killed = h.takeDamage(shot.damage);
      hitMarkerT = 0.14f;
      // Chits are paid out once per mission clear (see the reward-payout
      // block below), not per kill — a per-kill bounty economy is a
      // reasonable future addition but wasn't asked for. A kill does drop
      // resupply, without which a long mission is unwinnable on the fixed
      // starting ammo (see dropPickup).
      if (killed) dropPickup(h.pos, killCount_++);
    }
    // Kick the viewmodel back on every trigger pull, whether or not it hit.
    // Scaled by the round's damage against a rifle's, so a breaching shotgun
    // throws the gun and a suppressed carbine barely moves it.
    recoil_ = std::min(1.0f, recoil_ + 0.35f + weapon_.damage * weapon_.pellets * 0.0016f);
  }

  // Sway: the gun lags the view. The impulse is how far the aim moved this
  // frame, decayed back to zero — so it swings out when you whip round and
  // settles when you stop, rather than oscillating the way a spring on the
  // angle itself would.
  {
    glm::vec3 turn = camFwd_ - prevFwd_;
    prevFwd_ = camFwd_;
    const float kLag = 3.2f, kEase = 9.0f;
    swayX_ += -glm::dot(turn, camRight_) * kLag;
    swayY_ += -glm::dot(turn, camUp_) * kLag;
    float ease = std::min(1.0f, kEase * dt);
    swayX_ -= swayX_ * ease;
    swayY_ -= swayY_ * ease;
    swayX_ = std::clamp(swayX_, -0.06f, 0.06f);
    swayY_ = std::clamp(swayY_, -0.06f, 0.06f);

    recoil_ = std::max(0.0f, recoil_ - dt * 4.5f);
    bobT_ += dt * player_.planarSpeed() * 1.5f;
  }

  // Hostiles close in from the first frame, but nothing lands a hit for
  // the first few seconds after the drop: you arrive facing an arbitrary
  // direction, with no idea where the squad is, and taking fire before the
  // opening comms beat has even finished reads as dying for no reason
  // rather than as a fight. They still advance and wind up during it.
  const float kDeployGrace = 3.0f;
  bool graced = missionT_ < kDeployGrace;

  for (auto& h : hostiles_) {
    if (!h.alive()) continue;
    bool didAttack = h.update(dt, player_.position, level_);
    if (didAttack && !graced) {
      float dmg = h.type->damage * (1.0f - player_.damageReduction);
      player_.takeDamage(dmg);
      player_.hp = std::max(0.0f, player_.hp);
      damageFlashT = 0.4f;
    }
  }
  spawnBossIfReady();

  hitMarkerT = std::max(0.0f, hitMarkerT - dt * 2.5f);
  damageFlashT = std::max(0.0f, damageFlashT - dt * 1.6f);

  updatePickups(dt);

  if (waveProgress() >= 0.5f) fireComms(CommsTrigger::HalfCleared);

  if (!player_.alive()) {
    missionState_ = MissionState::Failed;
    fireComms(CommsTrigger::Failed);
  } else {
    bool wavesClear = std::all_of(hostiles_.begin(), hostiles_.begin() + waveTotal_,
                                  [](const Hostile& h) { return h.state == HostileState::Gone; });
    bool bossClear = bossIndex_ < 0 || hostiles_[bossIndex_].state == HostileState::Gone;
    if (wavesClear) fireComms(CommsTrigger::WavesCleared);
    if (wavesClear && !bossPending_ && bossClear) {
      if (profile_ && !rewardApplied_) {
        profile_->recordMissionComplete(mission_.id, mission_.rewardChits);
        rewardApplied_ = true;
      }
      missionState_ = MissionState::Complete;
      fireComms(CommsTrigger::Complete);
    }
  }

  updateComms(dt);
}

float Game::waveProgress() const {
  if (waveTotal_ == 0) return 1.0f;
  int gone = 0;
  for (int i = 0; i < waveTotal_; i++) if (hostiles_[i].state == HostileState::Gone) gone++;
  return (float)gone / (float)waveTotal_;
}

bool Game::bossAlive() const {
  return bossIndex_ >= 0 && hostiles_[bossIndex_].state != HostileState::Gone;
}

float Game::bossHpFraction() const {
  if (!bossAlive()) return 0.0f;
  const Hostile& b = hostiles_[bossIndex_];
  return b.maxHp > 0.0f ? b.hp / b.maxHp : 0.0f;
}

// ------------------------------------------------------------- viewmodel

void Game::collectViewmodel(std::vector<DrawItem>& out) const {
  if (!loaded_) return;

  const Mesh& box = HostileGeometry::unitBox();
  const Mesh& cyl = HostileGeometry::unitCylinder();
  const Mesh& tpr = HostileGeometry::unitTaper();

  // Which of the three silhouettes to build. Read off the ballistics rather
  // than off the weapon's id, so a content drop that adds a fourth shotgun
  // gets a shotgun in your hands without touching this file.
  const bool shotgun = weaponDef_ && weaponDef_->pellets > 1;
  const bool induction = weaponDef_ && weaponDef_->pierce;

  // Where it sits, in the camera's own frame: right of centre, below the
  // crosshair, far enough forward to clear the near plane. Aiming pulls it
  // to the middle and closer to the eye, which is what "down the sights"
  // means when the sights are geometry rather than an overlay.
  const float aim = camAim_;
  float right = glm::mix(0.135f, 0.0f, aim);
  float down = glm::mix(-0.112f, -0.066f, aim);
  // Far enough out that the perspective is not extreme. A gun two hand-spans
  // from a 68-degree lens is mostly foreshortening: pushing it out and
  // scaling it up keeps the same size on screen with a readable shape.
  float fwd = glm::mix(0.62f, 0.74f, aim);

  // Walk bob, halved while aiming; sway; and the recoil kick, which pushes
  // the gun back towards the eye and tips its muzzle up.
  float bobAmp = (1.0f - aim * 0.6f) * std::min(1.0f, player_.planarSpeed() / 6.0f);
  right += std::sin(bobT_) * 0.012f * bobAmp + swayX_;
  down += std::fabs(std::cos(bobT_)) * 0.010f * bobAmp + swayY_;
  fwd -= recoil_ * 0.055f;

  // The reload: the gun drops out of frame and tips over while it happens,
  // and comes back as it finishes. One curve, so it leaves and returns.
  if (weapon_.reloading && weapon_.reloadTime > 0.0f) {
    float t = 1.0f - std::clamp(weapon_.reloadT / weapon_.reloadTime, 0.0f, 1.0f);
    float drop = std::sin(t * 3.14159265f);
    down -= drop * 0.16f;
    right += drop * 0.05f;
  }

  const glm::vec3 origin = camPos_ + camRight_ * right + camUp_ * down + camFwd_ * fwd;

  // A basis with the recoil tip already in it, so every part of the gun
  // inherits it rather than each one having to apply it.
  // Yawed a few degrees inward when hipfired, so what you see is the gun's
  // inboard side rather than its right flank; aiming straightens it.
  float yaw = glm::mix(0.085f, 0.0f, aim);
  glm::vec3 f = glm::normalize(camFwd_ + camUp_ * (recoil_ * 0.10f) - camRight_ * yaw);
  glm::vec3 r = glm::normalize(glm::cross(f, camUp_));
  glm::vec3 u = glm::cross(r, f);

  // Column-major: x = right, y = up, z = *backwards*, because a part placed
  // at +z in this frame should sit nearer the eye, matching how the rest of
  // the project treats -z as forward.
  glm::mat4 rig(1.0f);
  rig[0] = glm::vec4(r, 0.0f);
  rig[1] = glm::vec4(u, 0.0f);
  rig[2] = glm::vec4(-f, 0.0f);
  rig[3] = glm::vec4(origin, 1.0f);

  DrawItem base;
  base.material = MaterialType::Armour;
  base.tint = glm::vec3(0.14f, 0.155f, 0.185f);
  base.metallic = 0.9f;
  base.roughness = 0.42f;
  base.wear = 0.8f;
  base.anisoStrength = 0.3f;
  // Nothing in your hands should cast a shadow across the world: it is two
  // hand-spans from the eye, so its shadow would be a black wall over half
  // the frame.
  base.castShadow = false;

  DrawItem lit = base;
  lit.material = MaterialType::Emissive;
  lit.emissive = class_ ? class_->accent : hudAccent_;
  lit.emissiveIntensity = weapon_.primed ? 3.4f : 1.5f;

  // One scale for the whole gun, so its size is a single number to tune
  // rather than thirty.
  const float S = 1.18f;
  auto piece = [&](const DrawItem& src, const Mesh& mesh, glm::vec3 at,
                   glm::vec3 size, glm::vec3 eulerDeg = glm::vec3(0.0f)) {
    glm::mat4 m = glm::translate(rig, at * S);
    if (eulerDeg.y != 0.0f) m = glm::rotate(m, glm::radians(eulerDeg.y), glm::vec3(0, 1, 0));
    if (eulerDeg.x != 0.0f) m = glm::rotate(m, glm::radians(eulerDeg.x), glm::vec3(1, 0, 0));
    if (eulerDeg.z != 0.0f) m = glm::rotate(m, glm::radians(eulerDeg.z), glm::vec3(0, 0, 1));
    DrawItem it = src;
    it.mesh = &mesh;
    it.model = glm::scale(m, size * S);
    out.push_back(it);
  };

  // Shared across all three: a receiver, a pistol grip, a magazine and a
  // stock. What changes is the barrel and what hangs off it.
  piece(base, box, {0.0f, 0.0f, 0.0f}, {0.062f, 0.070f, 0.230f});                 // receiver
  piece(base, box, {0.0f, -0.058f, 0.072f}, {0.044f, 0.088f, 0.050f}, {14, 0, 0});// grip
  piece(base, box, {0.0f, -0.052f, -0.012f}, {0.040f, 0.090f, 0.062f}, {-8, 0, 0});// magazine
  piece(base, box, {0.0f, -0.004f, 0.138f}, {0.040f, 0.052f, 0.086f});            // stock
  piece(base, box, {0.0f, 0.046f, -0.020f}, {0.026f, 0.016f, 0.150f});            // top rail

  if (shotgun) {
    // MAUL-12: a fat bore, a second tube under it, and a pump you can see.
    piece(base, cyl, {0.0f, 0.004f, -0.230f}, {0.052f, 0.240f, 0.052f}, {90, 0, 0});
    piece(base, cyl, {0.0f, -0.040f, -0.200f}, {0.036f, 0.180f, 0.036f}, {90, 0, 0});
    piece(base, box, {0.0f, -0.040f, -0.150f}, {0.056f, 0.052f, 0.070f});
    piece(lit, cyl, {0.0f, 0.004f, -0.352f}, {0.034f, 0.008f, 0.034f}, {90, 0, 0});
  } else if (induction) {
    // ARC LANCE: a long thin barrel through a pair of induction rings, lit
    // between them.
    piece(base, tpr, {0.0f, 0.008f, -0.300f}, {0.030f, 0.380f, 0.030f}, {90, 0, 0});
    for (int i = 0; i < 3; i++) {
      float z = -0.190f - (float)i * 0.085f;
      piece(base, cyl, {0.0f, 0.008f, z}, {0.070f, 0.018f, 0.070f}, {90, 0, 0});
      piece(lit, cyl, {0.0f, 0.008f, z - 0.030f}, {0.050f, 0.010f, 0.050f}, {90, 0, 0});
    }
    piece(lit, box, {0.0f, 0.046f, 0.040f}, {0.020f, 0.008f, 0.090f});
  } else {
    // WHISPER and anything else: a slim barrel inside a suppressor can.
    piece(base, cyl, {0.0f, 0.006f, -0.190f}, {0.026f, 0.190f, 0.026f}, {90, 0, 0});
    piece(base, cyl, {0.0f, 0.006f, -0.300f}, {0.048f, 0.150f, 0.048f}, {90, 0, 0});
    piece(base, box, {0.0f, -0.030f, -0.170f}, {0.034f, 0.030f, 0.120f});
    piece(lit, box, {0.0f, 0.046f, 0.030f}, {0.016f, 0.008f, 0.060f});
  }

  // Front and rear sights, so aiming has something to line up.
  piece(base, box, {0.0f, 0.070f, -0.120f}, {0.010f, 0.030f, 0.010f});
  piece(lit, box, {0.0f, 0.072f, 0.058f}, {0.026f, 0.008f, 0.010f});
}

void Game::collect(float time, std::vector<DrawItem>& out) const {
  (void)time;   // hostiles animate off their own accumulated bob, not wall time
  level_.collect(out);
  for (auto& h : hostiles_) h.collect(out);
  collectViewmodel(out);

  // Pickups: a small emissive box, bobbing and slowly spinning so it reads
  // as an item rather than scenery, on the shared unit box every hostile
  // part also uses.
  for (const Pickup& p : pickups_) {
    if (p.taken) continue;
    bool ammo = p.kind == PickupKind::Ammo;
    DrawItem it;
    it.mesh = &HostileGeometry::unitBox();
    it.material = MaterialType::Emissive;
    glm::vec3 at = p.pos + glm::vec3(0.0f, std::sin(p.bob) * 0.12f, 0.0f);
    it.model = glm::translate(glm::mat4(1.0f), at);
    it.model = glm::rotate(it.model, p.bob * 0.8f, glm::vec3(0.2f, 1.0f, 0.1f));
    it.model = glm::scale(it.model, glm::vec3(0.34f, 0.34f, 0.34f));
    it.tint = ammo ? glm::vec3(0.95f, 0.8f, 0.35f) : glm::vec3(0.4f, 0.95f, 0.55f);
    it.emissive = it.tint;
    it.emissiveIntensity = 3.2f;
    it.castShadow = false;
    out.push_back(it);
  }
}
