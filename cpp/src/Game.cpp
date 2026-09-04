#include "Game.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>

bool Game::init(const std::string& contentDir, const std::string& missionId) {
  if (!content_.loadAll(contentDir)) return false;
  const MissionDef* def = content_.mission(missionId);
  if (!def) {
    std::fprintf(stderr, "[Game] mission '%s' not found in %s/missions\n",
                 missionId.c_str(), contentDir.c_str());
    return false;
  }
  mission_ = *def;

  level_.build(mission_.arenaSize);
  HostileGeometry::ensure();

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
  sunDirection = glm::normalize(glm::vec3(-0.62f, -0.34f, -0.32f));

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

  std::printf("[Game] mission '%s' loaded: %d hostile(s), boss=%s\n",
              mission_.name.c_str(), waveTotal_, bossPending_ ? mission_.bossId.c_str() : "none");
  loaded_ = true;
  return true;
}

void Game::destroy() {
  level_.destroy();
  HostileGeometry::destroyShared();
  if (moteVbo_) glDeleteBuffers(1, &moteVbo_);
  if (moteVao_) glDeleteVertexArrays(1, &moteVao_);
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
}

void Game::update(GLFWwindow* window, Camera& camera, float dt, bool firePressed, bool reloadHeld,
                   bool forceForward) {
  if (!loaded_ || missionState_ != MissionState::InProgress) return;

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
      (void)killed;   // xp/currency rewards belong to Phase 3's economy system
    }
  }

  for (auto& h : hostiles_) {
    if (!h.alive()) continue;
    bool didAttack = h.update(dt, player_.position, level_);
    if (didAttack) {
      player_.hp = std::max(0.0f, player_.hp - h.type->damage);
      damageFlashT = 0.4f;
    }
  }
  spawnBossIfReady();

  hitMarkerT = std::max(0.0f, hitMarkerT - dt * 2.5f);
  damageFlashT = std::max(0.0f, damageFlashT - dt * 1.6f);

  if (!player_.alive()) {
    missionState_ = MissionState::Failed;
  } else {
    bool wavesClear = std::all_of(hostiles_.begin(), hostiles_.begin() + waveTotal_,
                                  [](const Hostile& h) { return h.state == HostileState::Gone; });
    bool bossClear = bossIndex_ < 0 || hostiles_[bossIndex_].state == HostileState::Gone;
    if (wavesClear && !bossPending_ && bossClear) missionState_ = MissionState::Complete;
  }
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
}
