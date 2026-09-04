#pragma once
#include "Gl.h"
#include <string>
#include <unordered_map>
#include <vector>

// Data-driven content: enemy archetypes and mission definitions loaded from
// plain text files under content/ at startup, not compiled in. This is the
// whole point of the exercise — a new monthly mission or boss is a text
// file dropped into content/missions/, not a code change and a rebuild.
//
// The format is deliberately small rather than pulling in a JSON library
// this project has no offline way to fetch: "key = value" lines, blank
// lines and #-comments ignored, one enemy per content/enemies/*.cfg file,
// one mission per content/missions/*.cfg file with repeatable `wave` lines.
struct EnemyType {
  std::string id, name;
  float hp = 60.0f, speed = 3.0f, damage = 10.0f;
  float attackRange = 2.0f, attackRate = 1.2f;   // seconds between attacks
  float radius = 0.45f, height = 1.8f;
  glm::vec3 colour{0.55f, 0.58f, 0.5f};
  glm::vec3 glow{0.6f, 0.85f, 1.0f};
  bool ranged = false;
  float xp = 20.0f;
};

struct WaveSpawn { std::string enemyId; int count = 1; float radius = 20.0f; };

struct MissionDef {
  std::string id, name;
  float arenaSize = 80.0f;
  std::vector<WaveSpawn> waves;
  std::string bossId;       // empty = no boss
  float bossHpMultiplier = 1.0f;
};

class Content {
public:
  // Scans <dir>/enemies/*.cfg and <dir>/missions/*.cfg. Returns false (and
  // logs why) if the directory itself is missing; a missing or malformed
  // individual file is logged and skipped rather than aborting the load —
  // one bad mission file shouldn't take the other eleven down with it.
  bool loadAll(const std::string& dir);

  const EnemyType* enemy(const std::string& id) const;
  const MissionDef* mission(const std::string& id) const;
  std::vector<std::string> missionIds() const;

private:
  std::unordered_map<std::string, EnemyType> enemies_;
  std::unordered_map<std::string, MissionDef> missions_;
};
