#include "Game.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>

// Loads a def into the live Weapon. Split out because init, the floor
// pickups and the slot switch all need exactly this and had grown three
// nearly-identical copies of it.
static void applyDef(Weapon& w, const WeaponDef* def) {
  if (!def) return;
  w.magSize = def->magSize;
  w.ammoInMag = def->magSize;
  w.reserveAmmo = def->reserveAmmo;
  w.damage = def->damage;
  w.headshotMultiplier = def->headshotMultiplier;
  w.fireInterval = def->fireInterval;
  w.reloadTime = def->reloadTime;
  w.pellets = def->pellets;
  w.spread = def->spread;
  w.pierce = def->pierce;
  w.range = def->range;
  w.reloading = false;
  w.reloadT = 0.0f;
  w.cooldown = 0.0f;
  w.primed = false;
}

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
  commsClock_ = 0.0f;
  xpEarned_ = 0;
  xpBanked_ = 0;
  for (bool& fired : triggerFired_) fired = false;
  bossName_.clear();
  pickups_.clear();
  killCount_ = 0;
  pickupNote_.clear();
  pickupNoteT_ = 0.0f;
  tutorialWalked_ = tutorialSprinted_ = 0.0f;
  tutorialKills_ = 0;
  tutorialFired_ = tutorialReloaded_ = tutorialJumped_ = false;
  tutorialSlid_ = tutorialUsedAbility_ = false;
  tutorialPrompt_.clear();
  tutorialHint_.clear();
  tutorialProgress_ = 0.0f;

  usingSite_ = false;
  site_ = Site{};
  objectiveIndex_ = 0;
  objectiveText_.clear();
  objectiveHint_.clear();
  triggerFiredById_.clear();
  hostileWakeOn_.clear();
  cutscene_.stop();

  if (!mission_.layout.empty()) {
    if (!buildSite(mission_.layout, site_)) {
      std::fprintf(stderr, "[Game] mission '%s' wants layout '%s', which does not exist\n",
                   missionId.c_str(), mission_.layout.c_str());
      return false;
    }
    usingSite_ = true;
    level_.buildFromParts(site_.parts, site_.floorY);
    triggerFiredById_.assign(site_.triggers.size(), false);
  } else {
    level_.build(mission_.arenaSize, mission_.floorColour, mission_.coverDensity);
  }
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

  // Three charges, and none at all in a boss fight — the browser build's
  // rule, and the reason its last mission reads differently from the
  // fifteen before it.
  harnessMax_ = mission_.bossId.empty() ? 3 : 0;
  harnessLeft_ = harnessMax_;
  downT_ = 0.0f;
  abilityCool_ = 0.0f;

  // Whatever is equipped — for a new record that is the service sidearm and
  // nothing else. The doctrine's weapon is only a fallback for a save with
  // no equipped weapon at all.
  // Two holsters. They are filled from the record's two equipped slots, and
  // the one you deploy holding is the primary if there is one.
  slots_[SlotPrimary] = Holster{};
  slots_[SlotSidearm] = Holster{};
  swapT_ = 0.0f;
  swapTo_ = -1;

  auto fill = [&](int s, const std::string& id) {
    const WeaponDef* d = content_.weapon(id);
    if (!d) return;
    // A weapon whose shape puts it in the other holster is filed where it
    // belongs rather than where it was asked for: a record that somehow has
    // a pistol in its primary slot should still carry it as a sidearm.
    const int actual = slotFor(d);
    (void)s;
    slots_[actual].def = d;
    slots_[actual].name = d->name;
    applyDef(slots_[actual].state, d);
    slots_[actual].filled = true;
  };

  std::string primaryId = profile.equippedWeapon;
  if (primaryId.empty() && class_) primaryId = class_->weaponId;
  fill(SlotPrimary, primaryId);
  fill(SlotSidearm, profile.equippedSidearm);

  slot_ = slots_[SlotPrimary].filled ? SlotPrimary : SlotSidearm;
  const WeaponDef* wdef = slots_[slot_].def;
  if (wdef) {
    weapon_ = slots_[slot_].state;
    weaponName_ = slots_[slot_].name;
  } else {
    weaponName_ = primaryId;
  }
  weaponDef_ = wdef;
  armed_ = !mission_.startUnarmed && wdef != nullptr;
  if (!armed_) {
    // Empty hands: no rounds, nothing to fire, no viewmodel. What you would
    // have been issued is in the armoury on the other side of the building.
    weapon_.ammoInMag = 0;
    weapon_.reserveAmmo = 0;
    weaponName_.clear();
    weaponDef_ = nullptr;
    slots_[SlotPrimary] = Holster{};
    slots_[SlotSidearm] = Holster{};
    slot_ = SlotPrimary;
  }
  recoil_ = 0.0f;
  swayX_ = swayY_ = 0.0f;
  bobT_ = 0.0f;
  muzzleFlash_ = 0.0f;
  shake_ = 0.0f;
  shakeT_ = 0.0f;
  viewKickPitch_ = viewKickYaw_ = 0.0f;
  viewKickRecoverPitch_ = viewKickRecoverYaw_ = 0.0f;
  impacts_.clear();

  const CosmeticDef* cosmetic = content_.cosmetic(profile.equippedCosmetic);
  hudAccent_ = cosmetic ? cosmetic->accent : glm::vec3(0.85f, 0.95f, 1.0f);

  player_.position = usingSite_ ? site_.playerSpawn
                                : glm::vec3(0.0f, level_.floorY(), 0.0f);
  player_.hp = player_.maxHp;

  // A site places its own hostiles, by hand, in the rooms they are standing
  // in. Most of them are asleep until the room's trigger fires — the
  // building should not empty itself into the corridor behind you while you
  // are still looking for a weapon.
  if (usingSite_) {
    for (const Site::Spawn& sp : site_.hostiles) {
      const EnemyType* t = content_.enemy(sp.enemyId);
      if (!t) {
        std::fprintf(stderr, "[Game] site '%s' references unknown enemy '%s', skipped\n",
                     mission_.layout.c_str(), sp.enemyId.c_str());
        continue;
      }
      Hostile h;
      glm::vec3 at = sp.pos;
      at.y = site_.floorY;
      level_.resolve(at, t->radius, t->height);
      h.spawn(t, at);
      // Asleep: stunned indefinitely until woken, which costs nothing and
      // reuses the one mechanism that already means "stands there and does
      // not shoot".
      if (!sp.wakeOn.empty()) h.stunT = 1e9f;
      hostiles_.push_back(h);
      hostileWakeOn_.push_back(sp.wakeOn);
    }
    for (const Site::WeaponDrop& d : site_.drops) {
      Pickup p;
      p.kind = PickupKind::Weapon;
      p.pos = d.pos;
      // "@issued" is the doctrine's own weapon, so the rack in the armoury
      // hands a Bulwark a shotgun and a Wraith a carbine rather than everyone
      // the same rifle. The doctrine's, specifically, and not whatever is
      // equipped: with the sidearm as the starter, what is equipped when you
      // walk into the armoury is the pistol you are there to replace.
      p.weaponId = d.weaponId == "@issued"
                      ? (class_ ? class_->weaponId : primaryId)
                      : d.weaponId;
      p.note = d.note;
      pickups_.push_back(p);
    }
    if (!site_.objectives.empty()) {
      objectiveText_ = site_.objectives[0].text;
      objectiveHint_ = site_.objectives[0].hint;
    }
  }

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
  tutorialLastPos_ = player_.position;
  // A site runs on objectives; the old step-by-step prompt sequence is for
  // the arena tutorials that have no rooms to put an objective in.
  if (mission_.tutorial && !usingSite_) setTutorialStep(TutorialStep::Move);
  else tutorialStep_ = TutorialStep::Done;
  // "wake" plays on arrival, which is the one scene with no box to walk
  // into: you are already in it.
  //
  // EREBUS_SCENE=<name> rolls a different one instead. Writing a cutscene
  // otherwise means playing to the trigger box that fires it — twenty
  // minutes of walking to look at four seconds of camera — and a headless
  // check of the closing shots would have to clear the whole block first.
  const char* forceScene = std::getenv("EREBUS_SCENE");
  cutscene_.play(mission_.scenes,
                 forceScene && *forceScene ? std::string(forceScene) : std::string("wake"));
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
    commsQueue_.push_back({&beat, commsClock_ + beat.delay});
  }
}

void Game::updateComms(float dt) {
  // Frozen for the duration of a cutscene: see commsClock_ in Game.h.
  if (cutscene_.playing()) return;
  commsClock_ += dt;
  commsT_ += dt;

  // A line that's had its time on screen clears, so the next queued beat
  // isn't stuck waiting behind it forever.
  if (!commsLine_.empty() && commsT_ > commsHold_ + 0.6f) {
    commsLine_.clear();
    commsSpeaker_.clear();
  }

  if (!commsLine_.empty()) return;   // one voice on the channel at a time

  for (size_t i = 0; i < commsQueue_.size(); i++) {
    if (commsQueue_[i].at > commsClock_) continue;
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

// ------------------------------------------------------------------ site
//
// A hand-built place runs on triggers rather than on a wave counter: named
// boxes you walk into, which wake the room's hostiles, fire the scene of the
// same name, and tick the objective waiting on them. Objectives change
// quietly — no banner, no pause — because a wall of AREA COMPLETE every ten
// metres turns a place into a corridor of checkpoints.

int Game::slotFor(const WeaponDef* def) {
  // A pistol is a sidearm and everything else is a primary. Read off the
  // weapon's own `shape`, which already exists for the viewmodel, so a drop
  // that adds a second pistol lands in the right holster with no code.
  return (def && def->shape == "pistol") ? SlotSidearm : SlotPrimary;
}

bool Game::selectSlot(int s) {
  const int want = (s == SlotSidearm) ? SlotSidearm : SlotPrimary;
  if (want == slot_ || swapT_ > 0.0f) return false;
  if (!slots_[want].filled) return false;
  // The swap is not instant. The hands are busy for kSwapSeconds and the
  // change of weapon happens at the halfway point, under the bottom of the
  // frame — an instant switch is a free reload and removes the only cost a
  // second weapon has.
  swapT_ = kSwapSeconds;
  swapTo_ = want;
  return true;
}

bool Game::switchWeapon() { return selectSlot(slot_ ^ 1); }

void Game::equipWeaponById(const std::string& id) {
  const WeaponDef* def = content_.weapon(id);
  if (!def) return;
  weapon_.magSize = def->magSize;
  weapon_.ammoInMag = def->magSize;
  weapon_.reserveAmmo = def->reserveAmmo;
  weapon_.damage = def->damage;
  weapon_.headshotMultiplier = def->headshotMultiplier;
  weapon_.fireInterval = def->fireInterval;
  weapon_.reloadTime = def->reloadTime;
  weapon_.pellets = def->pellets;
  weapon_.spread = def->spread;
  weapon_.pierce = def->pierce;
  weapon_.range = def->range;
  weapon_.reloading = false;
  weapon_.reloadT = 0.0f;
  weapon_.cooldown = 0.0f;
  weapon_.primed = false;
  weaponDef_ = def;
  weaponName_ = def->name;
  armed_ = true;

  // File it in the holster it belongs to, and make that holster live. Picking
  // up a rifle while holding a pistol should leave you holding the rifle and
  // still carrying the pistol, which is the whole point of two slots.
  slot_ = slotFor(def);
  slots_[slot_].def = def;
  slots_[slot_].name = def->name;
  slots_[slot_].state = weapon_;
  slots_[slot_].filled = true;
  swapT_ = 0.0f;
  swapTo_ = -1;

  // Found is owned. Picking your doctrine's weapon off the armoury bench is
  // how you come to have it at all — there is no desk that issues it.
  if (profile_ && !profile_->ownsWeapon(id)) {
    profile_->ownedWeapons.push_back(id);
    profile_->equippedWeapon = id;
  }
}

void Game::fireTrigger(const std::string& id) {
  // Wake whatever was waiting on it.
  for (size_t i = 0; i < hostiles_.size() && i < hostileWakeOn_.size(); i++) {
    if (hostileWakeOn_[i] != id) continue;
    hostileWakeOn_[i].clear();
    hostiles_[i].stunT = 0.0f;
  }
  // ...and roll the scene of the same name, if the mission wrote one.
  cutscene_.play(mission_.scenes, id);
}

void Game::updateSite(float dt) {
  (void)dt;
  if (!usingSite_) return;

  const glm::vec3 p = player_.position;
  for (size_t i = 0; i < site_.triggers.size(); i++) {
    if (triggerFiredById_[i]) continue;
    const Site::Trigger& t = site_.triggers[i];
    if (p.x < t.min.x || p.x > t.max.x) continue;
    if (p.z < t.min.z || p.z > t.max.z) continue;
    if (p.y + player_.height < t.min.y || p.y > t.max.y) continue;
    triggerFiredById_[i] = true;
    fireTrigger(t.id);
  }

  // The objective ends when its own trigger has fired — or, for the last
  // one, when everything that is awake is down.
  while (objectiveIndex_ < site_.objectives.size()) {
    const Site::Objective& o = site_.objectives[objectiveIndex_];
    bool done = false;
    if (o.needsClear) {
      done = std::all_of(hostiles_.begin(), hostiles_.end(), [](const Hostile& h) {
        return h.state == HostileState::Gone;
      });
    } else if (!o.trigger.empty()) {
      for (size_t i = 0; i < site_.triggers.size(); i++) {
        if (site_.triggers[i].id == o.trigger && triggerFiredById_[i]) { done = true; break; }
      }
    }
    if (!done) break;
    objectiveIndex_++;
    if (objectiveIndex_ < site_.objectives.size()) {
      objectiveText_ = site_.objectives[objectiveIndex_].text;
      objectiveHint_ = site_.objectives[objectiveIndex_].hint;
    } else {
      objectiveText_.clear();
      objectiveHint_.clear();
    }
  }
}

// -------------------------------------------------------------- tutorial
//
// A brand-new record starts planetside, on Recovery Division's ground site,
// and is walked through the controls one at a time. Every step watches for
// the thing it teaches and will not advance until it has happened: a prompt
// you can clear by waiting is a prompt nobody reads.

void Game::setTutorialStep(TutorialStep step) {
  tutorialStep_ = step;
  tutorialProgress_ = 0.0f;
  // A short beat on each change, so two steps completed in quick succession
  // do not flash past as one.
  tutorialHold_ = 0.9f;

  switch (step) {
    case TutorialStep::Move:
      tutorialPrompt_ = "WALK";
      tutorialHint_ = "W A S D, OR THE ARROW KEYS";
      break;
    case TutorialStep::Sprint:
      tutorialPrompt_ = "SPRINT";
      tutorialHint_ = "HOLD LEFT SHIFT AND KEEP MOVING FORWARD";
      break;
    case TutorialStep::Jump:
      tutorialPrompt_ = "JUMP";
      tutorialHint_ = "SPACE";
      break;
    case TutorialStep::Slide:
      tutorialPrompt_ = "SLIDE";
      tutorialHint_ = "SPRINT, THEN CROUCH - LEFT CTRL OR C. JUMP OUT OF IT TO KEEP THE SPEED.";
      break;
    case TutorialStep::Fire:
      tutorialPrompt_ = "FIRE";
      tutorialHint_ = "LEFT MOUSE. HOLD RIGHT MOUSE TO AIM.";
      break;
    case TutorialStep::Reload:
      tutorialPrompt_ = "RELOAD";
      tutorialHint_ = "R";
      break;
    case TutorialStep::Ability:
      tutorialPrompt_ = "FIELD ABILITY";
      tutorialHint_ = class_ ? "Q OR E - " + class_->abilityName : "Q OR E";
      break;
    case TutorialStep::Clear:
      tutorialPrompt_ = "CLEAR THE RANGE";
      tutorialHint_ = "PUT DOWN EVERY TARGET ON THE FIELD";
      break;
    case TutorialStep::Done:
      tutorialPrompt_.clear();
      tutorialHint_.clear();
      break;
  }
}

void Game::updateTutorial(float dt) {
  if (!mission_.tutorial || tutorialStep_ == TutorialStep::Done) return;

  if (tutorialHold_ > 0.0f) {
    tutorialHold_ -= dt;
    return;
  }

  glm::vec3 now = player_.position;
  float moved = glm::length(glm::vec2(now.x - tutorialLastPos_.x, now.z - tutorialLastPos_.z));
  tutorialLastPos_ = now;

  switch (tutorialStep_) {
    case TutorialStep::Move:
      tutorialWalked_ += moved;
      tutorialProgress_ = std::min(1.0f, tutorialWalked_ / 12.0f);
      if (tutorialProgress_ >= 1.0f) setTutorialStep(TutorialStep::Sprint);
      break;

    case TutorialStep::Sprint:
      // Sprinting is a speed, not a key: holding shift while standing still
      // is not sprinting, and the step should not accept it.
      if (player_.planarSpeed() > 7.0f) tutorialSprinted_ += dt;
      tutorialProgress_ = std::min(1.0f, tutorialSprinted_ / 1.2f);
      if (tutorialProgress_ >= 1.0f) setTutorialStep(TutorialStep::Jump);
      break;

    case TutorialStep::Jump:
      if (!player_.grounded && player_.velocity.y > 0.5f) tutorialJumped_ = true;
      tutorialProgress_ = tutorialJumped_ ? 1.0f : 0.0f;
      if (tutorialJumped_ && player_.grounded) setTutorialStep(TutorialStep::Slide);
      break;

    case TutorialStep::Slide:
      if (player_.sliding) tutorialSlid_ = true;
      tutorialProgress_ = tutorialSlid_ ? 1.0f : 0.0f;
      if (tutorialSlid_ && !player_.sliding) setTutorialStep(TutorialStep::Fire);
      break;

    case TutorialStep::Fire:
      tutorialProgress_ = tutorialFired_ ? 1.0f : 0.0f;
      if (tutorialFired_) setTutorialStep(TutorialStep::Reload);
      break;

    case TutorialStep::Reload:
      tutorialProgress_ = tutorialReloaded_ ? 1.0f : 0.0f;
      if (tutorialReloaded_ && !weapon_.reloading) setTutorialStep(TutorialStep::Ability);
      break;

    case TutorialStep::Ability:
      tutorialProgress_ = tutorialUsedAbility_ ? 1.0f : 0.0f;
      if (tutorialUsedAbility_) setTutorialStep(TutorialStep::Clear);
      break;

    case TutorialStep::Clear: {
      tutorialProgress_ = waveProgress();
      break;   // the ordinary win condition finishes it
    }

    case TutorialStep::Done:
      break;
  }
}

void Game::addImpact(const glm::vec3& at, const glm::vec3& tint) {
  // Capped: a full-auto weapon into a wall would otherwise grow this without
  // bound, and thirty sparks in the same square metre look like one light.
  if (impacts_.size() >= 48) impacts_.erase(impacts_.begin());
  Impact im;
  im.pos = at;
  im.tint = tint;
  im.life = 0.22f;
  im.seed = std::fmod((float)impacts_.size() * 37.31f + at.x * 13.7f + at.z * 7.1f, 6.2831853f);
  impacts_.push_back(im);
}

void Game::reviveAtFallPoint() {
  // Back up where you fell. Not at the start of the level: crossing a
  // building again because the last room went badly is a punishment for
  // having got that far, and the browser build's harness does not do it
  // either.
  player_.hp = player_.maxHp * 0.6f;
  player_.overshield = 0.0f;
  player_.velocity = glm::vec3(0.0f);
  player_.position = fellAt_;
  player_.eyeHeight = 1.68f;
  level_.resolve(player_.position, player_.radius, player_.height);

  // Push whatever was standing over you off, and put its attack timer back.
  // Standing up inside a thrall's swing is not a second chance.
  const float clearRadius = 4.5f;
  for (Hostile& h : hostiles_) {
    if (!h.alive() || h.state == HostileState::Dying) continue;
    glm::vec3 d = h.pos - player_.position;
    d.y = 0.0f;
    float dist = glm::length(d);
    if (dist > clearRadius) continue;
    glm::vec3 away = dist > 1e-3f ? d / dist : glm::vec3(1.0f, 0.0f, 0.0f);
    h.pos = player_.position + away * clearRadius;
    level_.resolve(h.pos, h.type->radius, h.type->height);
    h.cooldown = std::max(h.cooldown, h.type->attackRate * 0.8f);
  }

  // A magazine, so you are not back on your feet with an empty weapon in a
  // room that just killed you.
  if (armed_ && weapon_.ammoInMag == 0) {
    int take = std::min(weapon_.magSize, weapon_.reserveAmmo);
    weapon_.ammoInMag += take;
    weapon_.reserveAmmo -= take;
    weapon_.reloading = false;
    weapon_.reloadT = 0.0f;
  }

  damageFlashT = 0.6f;
  pickupNote_ = "HARNESS ENGAGED";
  pickupNoteT_ = 1.6f;
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
  tutorialUsedAbility_ = true;
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
  // Things keep turning during a cutscene, but nothing is collected: the
  // camera is somewhere else, and picking a weapon up off a table you are
  // not standing at would read as the game taking it for you.
  const bool inScene = cutscene_.playing();
  for (Pickup& p : pickups_) {
    if (p.taken) continue;
    p.bob += dt * 2.2f;
    if (inScene) continue;

    glm::vec3 d = p.pos - (player_.position + glm::vec3(0.0f, 0.9f, 0.0f));
    if (glm::length(d) > reach) continue;

    // A pickup that would add nothing (full reserve, full health) is left on
    // the ground for later rather than silently consumed, so the gain is
    // worked out before anything is applied.
    char note[48];
    if (p.kind == PickupKind::Weapon) {
      // A weapon on the floor is taken whatever you are already holding: the
      // armoury rack is meant to replace the sidearm you found in a bunk.
      equipWeaponById(p.weaponId);
      std::snprintf(note, sizeof(note), "%s", p.note.empty() ? "WEAPON RECOVERED" : p.note.c_str());
    } else if (p.kind == PickupKind::Ammo) {
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
                   const ScriptedInput& scripted) {
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

  // A cutscene drives the camera and eats the input. The world keeps
  // simulating underneath it — the alarm keeps sounding, and nothing walks
  // into a frozen room — but you cannot walk out of your own establishing
  // shot.
  const bool inScene = cutscene_.playing();
  if (inScene) {
    cutscene_.update(dt, camera);
    updateComms(dt);
    updatePickups(dt);
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

  const bool onTheGround = downed();
  if (!onTheGround) {
    bool sprint = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    player_.update(window, dt, glm::radians(camera.yaw), sprint, level_, scripted);
  } else {
    // Down: the camera drops to the floor and stays where you fell. You can
    // still look around, which is the difference between being downed and
    // being at a menu.
    player_.velocity = glm::vec3(0.0f);
    player_.eyeHeight += (0.42f - player_.eyeHeight) * std::min(1.0f, 6.0f * dt);
  }
  camera.position = player_.eyePosition();

  weapon_.update(dt);

  // The swap. The change of weapon happens at the halfway point, while the
  // viewmodel is below the bottom of the frame, so the two guns are never
  // both on screen and the swap costs the time it looks like it costs.
  if (swapT_ > 0.0f) {
    const float was = swapT_;
    swapT_ = std::max(0.0f, swapT_ - dt);
    if (swapTo_ >= 0 && was > kSwapSeconds * 0.5f && swapT_ <= kSwapSeconds * 0.5f) {
      slots_[slot_].state = weapon_;       // park what was in hand
      slot_ = swapTo_;
      swapTo_ = -1;
      weapon_ = slots_[slot_].state;
      weaponDef_ = slots_[slot_].def;
      weaponName_ = slots_[slot_].name;
      // A swap interrupts a reload rather than carrying it across: the
      // magazine you were part-way through is still part-way through when
      // you come back to it.
      weapon_.reloading = false;
      weapon_.reloadT = 0.0f;
      recoil_ = 0.6f;
    }
  }
  bool wasReloading = weapon_.reloading;
  if (armed_ && swapT_ <= 0.0f && reloadHeld) weapon_.startReload();
  if (!wasReloading && weapon_.reloading) tutorialReloaded_ = true;

  if (armed_ && !onTheGround && swapT_ <= 0.0f && firePressed && weapon_.canFire()) {
    // One trigger pull can strike several hostiles — a shotgun's cone across
    // a pair of thralls, or an induction bolt through the front rank into the
    // one behind it — so this is a list, not a single hit.
    weapon_.fire(camera.position, camera.forward(), level_, hostiles_, shotBuffer_);

    // The flash, and the kick the view takes. Scaled by the round's weight
    // against a rifle's, so a breaching shell throws the sights off the
    // target and a suppressed carbine barely moves them.
    const float weight = weapon_.damage * (float)weapon_.pellets / 22.0f;
    muzzleFlash_ = std::min(1.4f, 0.85f + weight * 0.10f);
    float kick = 0.22f + weight * 0.16f;
    // A deterministic sideways component per shot, so a burst walks rather
    // than climbing dead straight — and walks the same way twice.
    float side = (std::fmod((float)weapon_.ammoInMag * 7.13f, 2.0f) - 1.0f) * kick * 0.45f;
    viewKickPitch_ -= kick;
    viewKickYaw_ += side;
    viewKickRecoverPitch_ -= kick;
    viewKickRecoverYaw_ += side;
    shake_ = std::max(shake_, 0.10f + weight * 0.05f);

    for (const ShotResult& shot : shotBuffer_) {
      if (shot.hitSomething) {
        addImpact(shot.point, shot.hitHostile ? glm::vec3(1.0f, 0.36f, 0.30f)
                                              : glm::vec3(1.0f, 0.84f, 0.55f));
      }
      if (!shot.hitHostile) continue;
      Hostile& h = hostiles_[shot.hostileIndex];
      bool killed = h.takeDamage(shot.damage);
      hitMarkerT = 0.14f;
      // Chits are paid out once per mission clear (see the reward-payout
      // block below), not per kill — a per-kill bounty economy is a
      // reasonable future addition but wasn't asked for. A kill does drop
      // resupply, without which a long mission is unwinnable on the fixed
      // starting ammo (see dropPickup).
      if (killed) {
        // ...and the experience. Every enemy archetype has carried an `xp`
        // figure in its content file since they were written; this is what
        // reads it. Banked in the mission and handed to the record when the
        // mission ends, so a run you walk out of pays what you actually did
        // rather than nothing.
        xpEarned_ += (int)h.type->xp;
        dropPickup(h.pos, killCount_++);
      }
    }
    // Kick the viewmodel back on every trigger pull, whether or not it hit.
    // Scaled by the round's damage against a rifle's, so a breaching shotgun
    // throws the gun and a suppressed carbine barely moves it.
    recoil_ = std::min(1.0f, recoil_ + 0.35f + weapon_.damage * weapon_.pellets * 0.0016f);
    tutorialFired_ = true;
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
    muzzleFlash_ = std::max(0.0f, muzzleFlash_ - dt * 22.0f);
    bobT_ += dt * player_.planarSpeed() * 1.5f;
  }

  // ---- the view kick. Applied to the camera, then pulled back toward where
  // the sights started: the recovery is what stops a magazine from walking
  // the crosshair off the top of the screen and never bringing it down.
  {
    const float settle = std::min(1.0f, 9.0f * dt);
    float takePitch = viewKickPitch_ * settle;
    float takeYaw = viewKickYaw_ * settle;
    viewKickPitch_ -= takePitch;
    viewKickYaw_ -= takeYaw;
    camera.pitch = std::clamp(camera.pitch + takePitch, -89.0f, 89.0f);
    camera.yaw += takeYaw;

    // ...and the other half: give back what was taken, more slowly, so the
    // sights drift down to roughly where they were rather than snapping.
    const float recover = std::min(1.0f, 3.2f * dt);
    float givePitch = viewKickRecoverPitch_ * recover;
    float giveYaw = viewKickRecoverYaw_ * recover;
    viewKickRecoverPitch_ -= givePitch;
    viewKickRecoverYaw_ -= giveYaw;
    camera.pitch = std::clamp(camera.pitch - givePitch, -89.0f, 89.0f);
    camera.yaw -= giveYaw;
  }

  // ---- screen shake: a small, fast wobble on the view, decaying. Applied
  // after the kick so a hit landing mid-burst reads as one event.
  if (shake_ > 0.001f) {
    shakeT_ += dt * 47.0f;
    float k = shake_;
    camera.pitch = std::clamp(camera.pitch + std::sin(shakeT_ * 1.7f) * k * 0.9f, -89.0f, 89.0f);
    camera.yaw += std::sin(shakeT_ * 2.3f + 1.1f) * k * 0.9f;
    shake_ = std::max(0.0f, shake_ - dt * 2.4f);
  }

  // ---- sparks age out.
  for (Impact& im : impacts_) im.life -= dt;
  impacts_.erase(std::remove_if(impacts_.begin(), impacts_.end(),
                                [](const Impact& im) { return im.life <= 0.0f; }),
                 impacts_.end());

  // Hostiles close in from the first frame, but nothing lands a hit for
  // the first few seconds after the drop: you arrive facing an arbitrary
  // direction, with no idea where the squad is, and taking fire before the
  // opening comms beat has even finished reads as dying for no reason
  // rather than as a fight. They still advance and wind up during it.
  const float kDeployGrace = 3.0f;
  bool graced = missionT_ < kDeployGrace;
  // On the range, nothing shoots back until you have been taught to shoot.
  if (mission_.tutorial && tutorialStep_ < TutorialStep::Fire) graced = true;

  for (auto& h : hostiles_) {
    if (!h.alive()) continue;
    bool didAttack = h.update(dt, player_.position, level_);
    // Nothing lands while a cutscene is running either. The world keeps
    // simulating through one on purpose — the alarm keeps sounding, nothing
    // walks into a frozen room — but you cannot move, and thirteen seconds
    // of being shot at by a warden you are not allowed to answer is not a
    // cutscene, it is a punishment for having reached one.
    if (didAttack && !graced && !onTheGround && !cutscene_.playing()) {
      float dmg = h.type->damage * (1.0f - player_.damageReduction);
      player_.takeDamage(dmg);
      player_.hp = std::max(0.0f, player_.hp);
      damageFlashT = 0.4f;
      // Being hit moves the camera. A red vignette on its own reads as a
      // notification; the shake is what makes it a hit.
      shake_ = std::max(shake_, std::min(0.9f, 0.22f + dmg * 0.012f));
    }
  }
  updateSite(dt);
  updateTutorial(dt);
  spawnBossIfReady();

  hitMarkerT = std::max(0.0f, hitMarkerT - dt * 2.5f);
  damageFlashT = std::max(0.0f, damageFlashT - dt * 1.6f);

  updatePickups(dt);

  if (waveProgress() >= 0.5f) fireComms(CommsTrigger::HalfCleared);

  if (!player_.alive() && !downed()) {
    // Down, not out — if the harness has a charge left. A boss is issued
    // none (see init), so dying to one means taking the fight from the top.
    if (harnessLeft_ > 0) {
      harnessLeft_--;
      downT_ = kDownSeconds;
      fellAt_ = player_.position;
    } else {
      missionState_ = MissionState::Failed;
      fireComms(CommsTrigger::Failed);
    }
  }

  if (downed()) {
    downT_ -= dt;
    if (downT_ <= 0.0f) {
      downT_ = 0.0f;
      reviveAtFallPoint();
    }
  }

  if (player_.alive() && !downed()) {
    bool wavesClear = std::all_of(hostiles_.begin(), hostiles_.begin() + waveTotal_,
                                  [](const Hostile& h) { return h.state == HostileState::Gone; });
    bool bossClear = bossIndex_ < 0 || hostiles_[bossIndex_].state == HostileState::Gone;
    if (wavesClear) fireComms(CommsTrigger::WavesCleared);
    // A tutorial is not over when the targets are down, it is over when the
    // lesson is: shooting the range dry during the FIRE step would otherwise
    // end it before it had taught the reload or the ability. A site says the
    // same thing with its objective list — the last one is the one that ends
    // when the place is clear.
    bool lessonDone = !mission_.tutorial ||
                      (usingSite_ ? objectiveIndex_ + 1 >= site_.objectives.size()
                                  : tutorialStep_ == TutorialStep::Clear);
    if (wavesClear && lessonDone && !bossPending_ && bossClear) {
      if (profile_ && !rewardApplied_) {
        profile_->recordMissionComplete(mission_.id, mission_.rewardChits);
        // The clearance bonus, on top of what the fight itself paid. Paid
        // every time, unlike the chits: replaying a mission you have already
        // cleared is still work, and rank is the record of work done.
        xpEarned_ += mission_.rewardXp;
        rewardApplied_ = true;
      }
      missionState_ = MissionState::Complete;
      fireComms(CommsTrigger::Complete);
      // ...and the mission's closing scene, if it wrote one. Block D's is
      // the lift: you do not own a ship, so the way off Earth is somebody
      // coming down to collect you.
      cutscene_.play(mission_.scenes, "complete");
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
  if (!loaded_ || !armed_) return;

  const Mesh& box = HostileGeometry::unitBox();
  const Mesh& cyl = HostileGeometry::unitCylinder();
  const Mesh& tpr = HostileGeometry::unitTaper();
  const Mesh& sph = HostileGeometry::unitSphere();

  // Which silhouette to build. The weapon's own `shape` decides; left unset,
  // it is derived from the ballistics, so a content drop that adds a fourth
  // shotgun puts a shotgun in your hands without touching this file.
  enum class Shape { Pistol, Shotgun, Induction, Smg, Marksman, Carbine, Rifle };
  Shape shape = Shape::Rifle;
  if (weaponDef_) {
    const std::string& sh = weaponDef_->shape;
    if (sh == "pistol") shape = Shape::Pistol;
    else if (sh == "shotgun") shape = Shape::Shotgun;
    else if (sh == "induction") shape = Shape::Induction;
    else if (sh == "smg") shape = Shape::Smg;
    else if (sh == "marksman") shape = Shape::Marksman;
    else if (sh == "carbine") shape = Shape::Carbine;
    else if (sh == "rifle") shape = Shape::Rifle;
    else if (weaponDef_->pellets > 1) shape = Shape::Shotgun;
    else if (weaponDef_->pierce) shape = Shape::Induction;
    else if (weaponDef_->magSize <= 14) shape = Shape::Pistol;
    else if (weaponDef_->fireInterval < 0.085f) shape = Shape::Smg;
    else if (weaponDef_->range > 80.0f) shape = Shape::Marksman;
  }
  const bool isPistol = shape == Shape::Pistol;

  // Where it sits, in the camera's own frame: right of centre, below the
  // crosshair, far enough forward to clear the near plane. Aiming pulls it
  // to the middle and closer to the eye, which is what "down the sights"
  // means when the sights are geometry rather than an overlay. A sidearm
  // rides higher and closer in, the way a pistol is actually held.
  const float aim = camAim_;
  float right = glm::mix(isPistol ? 0.115f : 0.135f, 0.0f, aim);
  float down = glm::mix(isPistol ? -0.145f : -0.112f, isPistol ? -0.085f : -0.066f, aim);
  float fwd = glm::mix(isPistol ? 0.50f : 0.62f, isPistol ? 0.60f : 0.74f, aim);

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
  // rather than thirty. A sidearm is a smaller object, not a smaller rifle.
  const float S = isPistol ? 0.92f : 1.18f;
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

  if (isPistol) {
    // A sidearm is a slide, a grip and almost nothing else: short, blunt,
    // no stock and no rail. Held higher and closer than a rifle, which is
    // most of why it reads as a pistol before you have looked at its shape.
    piece(base, box, {0.0f, 0.0f, -0.030f}, {0.050f, 0.062f, 0.185f});      // slide
    piece(base, box, {0.0f, -0.030f, -0.030f}, {0.044f, 0.030f, 0.160f});   // frame
    piece(base, box, {0.0f, -0.082f, 0.052f}, {0.042f, 0.110f, 0.055f}, {18, 0, 0});  // grip
    piece(base, box, {0.0f, -0.075f, 0.052f}, {0.032f, 0.096f, 0.040f}, {18, 0, 0});  // magazine
    piece(base, box, {0.0f, -0.024f, 0.010f}, {0.030f, 0.022f, 0.048f});    // trigger guard
    piece(base, cyl, {0.0f, -0.004f, -0.128f}, {0.024f, 0.060f, 0.024f}, {90, 0, 0});  // muzzle
    piece(lit, box, {0.0f, 0.034f, 0.058f}, {0.016f, 0.006f, 0.012f});      // rear sight
    piece(base, box, {0.0f, 0.034f, -0.100f}, {0.008f, 0.020f, 0.008f});    // front sight
  } else {
    // Shared across the long guns: a receiver, a pistol grip, a magazine and
    // a stock. What changes is the barrel and what hangs off it.
    piece(base, box, {0.0f, 0.0f, 0.0f}, {0.062f, 0.070f, 0.230f});                 // receiver
    piece(base, box, {0.0f, -0.058f, 0.072f}, {0.044f, 0.088f, 0.050f}, {14, 0, 0});// grip
    piece(base, box, {0.0f, -0.052f, -0.012f}, {0.040f, 0.090f, 0.062f}, {-8, 0, 0});// magazine
    piece(base, box, {0.0f, -0.004f, 0.138f}, {0.040f, 0.052f, 0.086f});            // stock
    piece(base, box, {0.0f, 0.046f, -0.020f}, {0.026f, 0.016f, 0.150f});            // top rail

    switch (shape) {
      case Shape::Shotgun:
        // A fat bore, a second tube under it, and a pump you can see.
        piece(base, cyl, {0.0f, 0.004f, -0.230f}, {0.052f, 0.240f, 0.052f}, {90, 0, 0});
        piece(base, cyl, {0.0f, -0.040f, -0.200f}, {0.036f, 0.180f, 0.036f}, {90, 0, 0});
        piece(base, box, {0.0f, -0.040f, -0.150f}, {0.056f, 0.052f, 0.070f});
        piece(lit, cyl, {0.0f, 0.004f, -0.352f}, {0.034f, 0.008f, 0.034f}, {90, 0, 0});
        break;

      case Shape::Induction:
        // A long thin barrel through a stack of induction rings, lit between
        // them.
        piece(base, tpr, {0.0f, 0.008f, -0.300f}, {0.030f, 0.380f, 0.030f}, {90, 0, 0});
        for (int i = 0; i < 3; i++) {
          float z = -0.190f - (float)i * 0.085f;
          piece(base, cyl, {0.0f, 0.008f, z}, {0.070f, 0.018f, 0.070f}, {90, 0, 0});
          piece(lit, cyl, {0.0f, 0.008f, z - 0.030f}, {0.050f, 0.010f, 0.050f}, {90, 0, 0});
        }
        piece(lit, box, {0.0f, 0.046f, 0.040f}, {0.020f, 0.008f, 0.090f});
        break;

      case Shape::Smg:
        // Short and stubby, with the magazine through the grip and a folding
        // stock that is barely there. Nothing in front of the hand.
        piece(base, cyl, {0.0f, 0.004f, -0.150f}, {0.034f, 0.150f, 0.034f}, {90, 0, 0});
        piece(base, box, {0.0f, -0.070f, 0.066f}, {0.038f, 0.130f, 0.046f}, {12, 0, 0});
        piece(base, box, {0.0f, 0.010f, 0.160f}, {0.020f, 0.030f, 0.070f});
        piece(base, box, {0.0f, -0.028f, -0.120f}, {0.044f, 0.038f, 0.090f});   // fore grip
        piece(lit, box, {0.0f, 0.046f, 0.030f}, {0.016f, 0.008f, 0.050f});
        break;

      case Shape::Marksman:
        // Long barrel, a bipod folded under it, and a scope you can see the
        // tube of — the one gun whose silhouette says "range" on its own.
        piece(base, tpr, {0.0f, 0.006f, -0.330f}, {0.026f, 0.420f, 0.026f}, {90, 0, 0});
        piece(base, box, {0.0f, -0.034f, -0.250f}, {0.028f, 0.020f, 0.150f});
        piece(base, cyl, {0.0f, 0.086f, -0.040f}, {0.058f, 0.230f, 0.058f}, {90, 0, 0});  // scope
        piece(base, cyl, {0.0f, 0.086f, -0.160f}, {0.070f, 0.040f, 0.070f}, {90, 0, 0});  // objective
        piece(lit, cyl, {0.0f, 0.086f, 0.078f}, {0.040f, 0.008f, 0.040f}, {90, 0, 0});    // eyepiece
        break;

      case Shape::Carbine:
      case Shape::Rifle:
      default:
        // A slim barrel inside a suppressor can for the quiet one; a plain
        // barrel and a gas block for the service rifle.
        piece(base, cyl, {0.0f, 0.006f, -0.190f}, {0.026f, 0.190f, 0.026f}, {90, 0, 0});
        if (shape == Shape::Carbine) {
          piece(base, cyl, {0.0f, 0.006f, -0.300f}, {0.048f, 0.150f, 0.048f}, {90, 0, 0});
        } else {
          piece(base, cyl, {0.0f, 0.006f, -0.300f}, {0.030f, 0.160f, 0.030f}, {90, 0, 0});
          piece(base, box, {0.0f, 0.030f, -0.210f}, {0.030f, 0.034f, 0.055f});
        }
        piece(base, box, {0.0f, -0.030f, -0.170f}, {0.034f, 0.030f, 0.120f});
        piece(lit, box, {0.0f, 0.046f, 0.030f}, {0.016f, 0.008f, 0.060f});
        break;
    }
  }

  // Front and rear sights, so aiming has something to line up. The pistol
  // and the marksman rifle carry their own, so they are excluded here rather
  // than ending up with two sets.
  if (!isPistol && shape != Shape::Marksman) {
    piece(base, box, {0.0f, 0.070f, -0.120f}, {0.010f, 0.030f, 0.010f});
    piece(lit, box, {0.0f, 0.072f, 0.058f}, {0.026f, 0.008f, 0.010f});
  }

  // ---- muzzle flash. Bright, brief, and in front of the barrel: a shot you
  // can see leaving the gun rather than a number changing in the corner.
  // Three pieces, because a single quad reads as a sticker — a hot core, a
  // wider petal, and a short streak along the bore.
  if (muzzleFlash_ > 0.001f) {
    float k = std::clamp(muzzleFlash_, 0.0f, 1.0f);
    float muzzleZ = isPistol ? -0.155f
                  : shape == Shape::Shotgun ? -0.360f
                  : shape == Shape::Induction ? -0.500f
                  : shape == Shape::Smg ? -0.230f
                  : shape == Shape::Marksman ? -0.545f
                  : -0.380f;
    DrawItem flash = lit;
    flash.emissive = glm::vec3(1.0f, 0.86f, 0.62f);
    flash.emissiveIntensity = 14.0f * k;
    piece(flash, sph, {0.0f, 0.006f, muzzleZ}, glm::vec3(0.075f, 0.075f, 0.075f) * k);
    flash.emissiveIntensity = 7.0f * k;
    piece(flash, box, {0.0f, 0.006f, muzzleZ - 0.02f},
          glm::vec3(0.19f * k, 0.028f * k, 0.028f * k), {0, 0, 45.0f * k});
    flash.emissiveIntensity = 5.0f * k;
    piece(flash, box, {0.0f, 0.006f, muzzleZ - 0.09f},
          glm::vec3(0.030f * k, 0.030f * k, 0.22f * k));
  }
}

void Game::collect(float time, std::vector<DrawItem>& out) const {
  (void)time;   // hostiles animate off their own accumulated bob, not wall time
  level_.collect(out);
  for (auto& h : hostiles_) h.collect(out);
  collectViewmodel(out);

  // Sparks where rounds landed. World geometry rather than a screen sprite,
  // so they are occluded by the thing they hit like everything else.
  for (const Impact& im : impacts_) {
    float k = std::clamp(im.life / 0.22f, 0.0f, 1.0f);
    DrawItem it;
    it.mesh = &HostileGeometry::unitBox();
    it.material = MaterialType::Emissive;
    it.tint = im.tint;
    it.emissive = im.tint;
    it.emissiveIntensity = 9.0f * k;
    it.castShadow = false;
    // Three shards on different axes, shrinking as they fade: one cube reads
    // as a pixel, three read as something breaking.
    for (int i = 0; i < 3; i++) {
      float a = im.seed + (float)i * 2.094f;
      glm::vec3 dir(std::cos(a), std::sin(a * 1.7f) * 0.6f, std::sin(a));
      glm::mat4 m = glm::translate(glm::mat4(1.0f), im.pos + dir * (1.0f - k) * 0.22f);
      m = glm::rotate(m, a, glm::vec3(0.3f, 1.0f, 0.2f));
      it.model = glm::scale(m, glm::vec3(0.055f, 0.012f, 0.012f) * k);
      out.push_back(it);
    }
  }

  // Pickups: a small emissive box, bobbing and slowly spinning so it reads
  // as an item rather than scenery, on the shared unit box every hostile
  // part also uses.
  for (const Pickup& p : pickups_) {
    if (p.taken) continue;
    DrawItem it;
    it.mesh = &HostileGeometry::unitBox();
    it.material = MaterialType::Emissive;
    glm::vec3 at = p.pos + glm::vec3(0.0f, std::sin(p.bob) * 0.12f, 0.0f);
    it.model = glm::translate(glm::mat4(1.0f), at);
    it.model = glm::rotate(it.model, p.bob * 0.8f, glm::vec3(0.2f, 1.0f, 0.1f));

    if (p.kind == PickupKind::Weapon) {
      // A weapon reads as a weapon: a long flat slab rather than the little
      // cube resupply uses, so you can tell across a room whether the thing
      // in the rack is a gun or a box of rounds.
      it.model = glm::scale(it.model, glm::vec3(0.16f, 0.13f, 0.72f));
      it.tint = glm::vec3(0.62f, 0.86f, 1.0f);
      it.emissiveIntensity = 2.4f;
    } else {
      bool ammo = p.kind == PickupKind::Ammo;
      it.model = glm::scale(it.model, glm::vec3(0.34f, 0.34f, 0.34f));
      it.tint = ammo ? glm::vec3(0.95f, 0.8f, 0.35f) : glm::vec3(0.4f, 0.95f, 0.55f);
      it.emissiveIntensity = 3.2f;
    }
    it.emissive = it.tint;
    it.castShadow = false;
    out.push_back(it);
  }
}
