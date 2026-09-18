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
  // Flyers get a different body entirely (see Hostile::collect): a core in
  // a cowl with a spinning ring and a thruster, rather than legs. They also
  // hover, so the rig sits at a fraction of `height` instead of standing on
  // the ground at it.
  bool flying = false;
  // Elites and bosses get the wider, more heavily plated build and carry a
  // shoulder cannon instead of a forearm one. It is a content decision, not
  // one to infer from a health number that also moves for balance reasons.
  bool elite = false;
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
  // One line of what you are here to do, and a few of why. The browser build
  // shows both on the mission card before you launch (see src/js/fps/hub.js);
  // this is the same text, so the two builds tell the same story.
  std::string objective;
  std::string brief;
  float arenaSize = 80.0f;
  std::vector<WaveSpawn> waves;
  std::string bossId;       // empty = no boss
  float bossHpMultiplier = 1.0f;
  int rewardChits = 40;     // paid out once, on first completion — see Profile
  std::vector<CommsBeat> comms;
  // Position in the campaign route down the ark, 1..N, or 0 for a mission
  // that is not part of it (a planet's patrol, a debug fixture). The route is
  // ordered by this number and unlocks one step at a time, so a mission's
  // place in the story lives in the mission's own file rather than in a list
  // somewhere else that has to be kept in step with it.
  int campaignIndex = 0;
  // Where aboard the ark it happens. Missions that share a zone share a look.
  std::string zone;

  // The look of the place. Defaults are the dusty-planet grade the renderer
  // was built around; a mission overrides whichever of them it cares about.
  // This is what makes the route down the ark read as a route rather than
  // sixteen fights in the same room — THE FALSE SKY is a held sunset, the
  // reactor runs hot and orange, Deck Zero is nearly black.
  glm::vec3 skyZenith{0.055f, 0.070f, 0.115f};
  glm::vec3 skyHorizon{0.28f, 0.20f, 0.20f};
  glm::vec3 fogColour{0.42f, 0.30f, 0.34f};
  glm::vec3 sunColour{1.0f, 0.94f, 0.82f};
  glm::vec3 floorColour{0.31f, 0.26f, 0.21f};
  float fogDensity = 0.011f;
  float sunIntensity = 3.4f;
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

  // What makes the three doctrines' weapons play differently rather than
  // just hit for different numbers.
  //   pellets  — rays per trigger pull. A shotgun's damage is per pellet, so
  //              MAUL-12 does 8 x 17 at point blank and a fraction of that
  //              once the cone has opened past its target.
  //   spread   — the cone, in radians at one metre. Applied per pellet.
  //   pierce   — the ray carries on through a hostile into whatever stood
  //              behind it, instead of stopping at the first thing it hits.
  //   range    — beyond this the shot simply misses. A shotgun that reaches
  //              as far as a rifle is a rifle.
  int pellets = 1;
  float spread = 0.0f;
  bool pierce = false;
  float range = 200.0f;
};

// content/classes/<id>.cfg — a doctrine. Each one is its issued weapon, its
// field ability and its passive, which is how the browser build draws the
// line too (src/js/classes.js). Picked when a record is created and fixed
// for that record's life.
enum class AbilityKind { Barrier, Breach, Phase };

struct ClassDef {
  std::string id, name, role, tagline;
  glm::vec3 accent{0.85f, 0.95f, 1.0f};

  std::string weaponId;          // issued free with the record

  AbilityKind ability = AbilityKind::Phase;
  std::string abilityName = "PHASE STEP";
  std::string abilityDesc;
  float abilityCooldown = 10.0f;

  std::string perkName, perk;
  float hp = 100.0f;
  float damageReduction = 0.0f;  // before any armour piece adds to it
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
  // Two surface colours the continents mix between, and how far the polar
  // caps reach (0 = none, ~0.45 = a properly iced world).
  glm::vec3 colour{0.55f, 0.5f, 0.45f};
  glm::vec3 colour2{0.35f, 0.33f, 0.30f};
  float capExtent = 0.0f;
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
  const ClassDef* playerClass(const std::string& id) const;

  std::vector<std::string> planetIds() const;
  std::vector<std::string> classIds() const;
  std::vector<std::string> missionIds() const;
  // The campaign route in story order — every mission with a campaign
  // number, sorted by it. Side content (a planet's patrol, a debug
  // fixture) has no number and does not appear here.
  std::vector<std::string> campaignIds() const;
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
  std::unordered_map<std::string, ClassDef> classes_;
};
