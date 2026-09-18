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
  const ArmorDef* armor = content_.armor(profile.equippedArmor);
  player_.maxHp = 100.0f + (armor ? armor->hpBonus : 0.0f);
  player_.damageReduction = armor ? armor->damageReduction : 0.0f;

  const WeaponDef* wdef = content_.weapon(profile.equippedWeapon);
  if (wdef) {
    weapon_.magSize = wdef->magSize;
    weapon_.ammoInMag = wdef->magSize;
    weapon_.reserveAmmo = wdef->reserveAmmo;
    weapon_.damage = wdef->damage;
    weapon_.headshotMultiplier = wdef->headshotMultiplier;
    weapon_.fireInterval = wdef->fireInterval;
    weapon_.reloadTime = wdef->reloadTime;
  }

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
  abilityCool_ = kAbilityCooldown;

  // Dash along the look direction, flattened: a phase step is a reposition,
  // not a jump, and letting it follow the pitch would fire you into the sky
  // or the floor depending on where you happened to be aiming.
  glm::vec3 flat(lookDir_.x, 0.0f, lookDir_.z);
  if (glm::length(flat) < 1e-4f) flat = glm::vec3(0.0f, 0.0f, -1.0f);
  flat = glm::normalize(flat);

  // Step there in pieces, resolving collision at each one, so the dash stops
  // against a crate rather than through it — the whole point of it is to get
  // behind cover, which does not work if it can also get you inside cover.
  const int steps = 8;
  glm::vec3 probe = player_.position;
  for (int i = 0; i < steps; i++) {
    glm::vec3 next = probe + flat * (kPhaseDistance / (float)steps);
    level_.resolve(next, player_.radius, player_.height);
    // Resolution pushed it back roughly where it started: something solid is
    // there, so this is as far as the step goes.
    if (glm::length(glm::vec2(next.x - probe.x, next.z - probe.z)) < 0.02f) break;
    probe = next;
  }
  player_.position = probe;

  pickupNote_ = "PHASE STEP";
  pickupNoteT_ = 1.2f;
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
  // Kept for useAbility(), which is called from main.cpp's key handling and
  // has no camera of its own to ask.
  lookDir_ = camera.forward();

  bool sprint = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
  player_.update(window, dt, glm::radians(camera.yaw), sprint, level_, forceForward);
  camera.position = player_.eyePosition();

  weapon_.update(dt);
  if (reloadHeld) weapon_.startReload();

  if (firePressed && weapon_.canFire()) {
    ShotResult shot = weapon_.fire(camera.position, camera.forward(), level_, hostiles_);
    if (shot.hitHostile) {
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
      player_.hp = std::max(0.0f, player_.hp - dmg);
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

void Game::collect(float time, std::vector<DrawItem>& out) const {
  (void)time;   // hostiles animate off their own accumulated bob, not wall time
  level_.collect(out);
  for (auto& h : hostiles_) h.collect(out);

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
