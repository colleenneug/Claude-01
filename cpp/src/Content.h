#pragma once
#include "Gl.h"
#include <string>
#include <unordered_map>
#include <vector>

// Data-driven content: enemy archetypes, missions, weapons, armour and
// cosmetics, all loaded from plain text files under content/ at startup,
// not compiled in. This is the whole point of the exercise — a new
// monthly mission, boss, gun or skin is a text file dropped into the
// right content/ subdirectory, not a code change and a rebuild.
//
// The format is deliberately small rather than pulling in a JSON library
// this project has no offline way to fetch: "key = value" lines, blank
// lines and #-comments ignored, one item per file, with a couple of
// small file-specific extensions (mission waves, a boss line) documented
// on the structs below.
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

// When a comms beat fires. The browser build stages its story the same way
// (see src/js/fps/game.js): each objective fires its own beats as you reach
// it, rather than stopping the game for a dialogue screen.
enum class CommsTrigger { Deploy, HalfCleared, WavesCleared, BossSpawn, Complete, Failed };

struct CommsBeat {
  CommsTrigger trigger = CommsTrigger::Deploy;
  std::string speaker;   // who's on the channel
  std::string line;      // what they say
  float delay = 0.0f;    // seconds after the trigger before it goes out
};

struct MissionDef {
  std::string id, name;
  float arenaSize = 80.0f;
  std::vector<WaveSpawn> waves;
  std::string bossId;       // empty = no boss
  float bossHpMultiplier = 1.0f;
  int rewardChits = 40;     // paid out once, on first completion — see Profile
  std::vector<CommsBeat> comms;
};

// content/weapons/<id>.cfg — equipping one (see Game::equipWeapon) sets
// these directly onto the live Weapon instance.
struct WeaponDef {
  std::string id, name;
  float damage = 22.0f, headshotMultiplier = 2.0f;
  float fireInterval = 0.11f, reloadTime = 1.6f;
  int magSize = 24;
  int reserveAmmo = 96;   // rounds carried beyond the loaded magazine
  int cost = 0;           // chits; 0 = starter gear, owned from a fresh profile
};

// content/armor/<id>.cfg — equipping one changes Player::maxHp and the
// fraction of incoming damage it absorbs.
struct ArmorDef {
  std::string id, name;
  float hpBonus = 0.0f;
  float damageReduction = 0.0f;   // 0..~0.5, fraction of incoming damage absorbed
  int cost = 0;
};

// content/cosmetics/<id>.cfg — purely visual: recolours the HUD accent
// (health-full colour, ammo pip colour, crosshair colour). No mechanical
// effect, same as a browser-game cosmetic ought to have none.
struct CosmeticDef {
  std::string id, name;
  glm::vec3 accent{0.85f, 0.95f, 1.0f};
  int cost = 0;
};

// content/planets/<id>.cfg — somewhere to fly to in open space. A planet
// with a `mission` drops you into that mission when you land; the one
// marked `station = true` is the Cradle, and opens the hub instead.
struct PlanetDef {
  std::string id, name;
  glm::vec3 position{0.0f};
  float radius = 200.0f;
  glm::vec3 colour{0.55f, 0.5f, 0.45f};
  std::string missionId;
  bool station = false;
};

class Content {
public:
  // Scans <dir>/{enemies,missions,weapons,armor,cosmetics}/*.cfg. Returns
  // false (and logs why) if the directory itself is missing; a missing or
  // malformed individual file is logged and skipped rather than aborting
  // the load — one bad mission file shouldn't take the other eleven down
  // with it.
  bool loadAll(const std::string& dir);

  const EnemyType* enemy(const std::string& id) const;
  const MissionDef* mission(const std::string& id) const;
  const WeaponDef* weapon(const std::string& id) const;
  const ArmorDef* armor(const std::string& id) const;
  const CosmeticDef* cosmetic(const std::string& id) const;
  const PlanetDef* planet(const std::string& id) const;

  std::vector<std::string> planetIds() const;
  std::vector<std::string> missionIds() const;
  std::vector<std::string> weaponIds() const;
  std::vector<std::string> armorIds() const;
  std::vector<std::string> cosmeticIds() const;

private:
  std::unordered_map<std::string, EnemyType> enemies_;
  std::unordered_map<std::string, MissionDef> missions_;
  std::unordered_map<std::string, WeaponDef> weapons_;
  std::unordered_map<std::string, ArmorDef> armor_;
  std::unordered_map<std::string, CosmeticDef> cosmetics_;
  std::unordered_map<std::string, PlanetDef> planets_;
};
