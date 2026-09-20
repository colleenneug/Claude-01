#include "Content.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// Strips a trailing "# comment" (but not a '#' inside the value — none of
// our values need one) and returns the trimmed remainder.
std::string stripComment(const std::string& line) {
  size_t h = line.find('#');
  return trim(h == std::string::npos ? line : line.substr(0, h));
}

std::vector<std::string> splitWs(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream ss(s);
  std::string tok;
  while (ss >> tok) out.push_back(tok);
  return out;
}

glm::vec3 parseVec3(const std::string& s, glm::vec3 fallback) {
  auto parts = splitWs(s);
  // also accept comma-separated "r,g,b"
  if (parts.size() == 1 && parts[0].find(',') != std::string::npos) {
    std::vector<std::string> csv;
    std::istringstream ss(parts[0]);
    std::string tok;
    while (std::getline(ss, tok, ',')) csv.push_back(tok);
    parts = csv;
  }
  if (parts.size() != 3) return fallback;
  try {
    return glm::vec3(std::stof(parts[0]), std::stof(parts[1]), std::stof(parts[2]));
  } catch (...) {
    return fallback;
  }
}

bool keyValue(const std::string& line, std::string& key, std::string& value) {
  size_t eq = line.find('=');
  if (eq == std::string::npos) return false;
  key = trim(line.substr(0, eq));
  value = trim(line.substr(eq + 1));
  return !key.empty();
}

EnemyType parseEnemy(const std::string& id, const fs::path& path) {
  EnemyType e;
  e.id = id;
  e.name = id;
  std::ifstream f(path);
  std::string raw;
  while (std::getline(f, raw)) {
    std::string line = stripComment(raw);
    if (line.empty()) continue;
    std::string k, v;
    if (!keyValue(line, k, v)) continue;
    try {
      if (k == "name") e.name = v;
      else if (k == "hp") e.hp = std::stof(v);
      else if (k == "speed") e.speed = std::stof(v);
      else if (k == "damage") e.damage = std::stof(v);
      else if (k == "attack_range") e.attackRange = std::stof(v);
      else if (k == "attack_rate") e.attackRate = std::stof(v);
      else if (k == "radius") e.radius = std::stof(v);
      else if (k == "height") e.height = std::stof(v);
      else if (k == "colour" || k == "color") e.colour = parseVec3(v, e.colour);
      else if (k == "glow") e.glow = parseVec3(v, e.glow);
      else if (k == "ranged") e.ranged = (v == "true" || v == "1");
      else if (k == "flying") e.flying = (v == "true" || v == "1");
      else if (k == "elite") e.elite = (v == "true" || v == "1");
      else if (k == "xp") e.xp = std::stof(v);
    } catch (...) {
      std::fprintf(stderr, "[Content] %s: bad value for '%s' = '%s', ignored\n",
                   path.string().c_str(), k.c_str(), v.c_str());
    }
  }
  return e;
}

MissionDef parseMission(const std::string& id, const fs::path& path) {
  MissionDef m;
  m.id = id;
  m.name = id;
  std::ifstream f(path);
  std::string raw;
  while (std::getline(f, raw)) {
    std::string line = stripComment(raw);
    if (line.empty()) continue;
    auto tokens = splitWs(line);
    if (tokens.empty()) continue;

    // comms <trigger> <delay> <speaker> | <line>
    // e.g.  comms deploy 0.5 VANGUARD | DROP CONFIRMED. THE SHELF IS YOURS.
    // The pipe keeps the speaker's name from having to be quoted, and lets
    // the line itself contain spaces and punctuation without escaping.
    if (tokens[0] == "comms") {
      size_t bar = line.find('|');
      if (bar == std::string::npos || tokens.size() < 4) {
        std::fprintf(stderr, "[Content] %s: malformed comms line '%s', skipped\n",
                     path.string().c_str(), line.c_str());
        continue;
      }
      CommsBeat beat;
      const std::string& trig = tokens[1];
      if (trig == "deploy") beat.trigger = CommsTrigger::Deploy;
      else if (trig == "half") beat.trigger = CommsTrigger::HalfCleared;
      else if (trig == "cleared") beat.trigger = CommsTrigger::WavesCleared;
      else if (trig == "boss") beat.trigger = CommsTrigger::BossSpawn;
      else if (trig == "complete") beat.trigger = CommsTrigger::Complete;
      else if (trig == "failed") beat.trigger = CommsTrigger::Failed;
      else {
        std::fprintf(stderr, "[Content] %s: unknown comms trigger '%s', skipped\n",
                     path.string().c_str(), trig.c_str());
        continue;
      }
      try { beat.delay = std::stof(tokens[2]); } catch (...) { beat.delay = 0.0f; }

      // Everything between the delay and the pipe is the speaker. Walk past
      // the first three tokens by position rather than searching for the
      // delay's text, which would find the wrong occurrence if the speaker
      // or the line happened to contain the same digits.
      size_t pos = 0;
      for (int skipped = 0; skipped < 3 && pos < line.size(); skipped++) {
        while (pos < line.size() && std::isspace((unsigned char)line[pos])) pos++;
        while (pos < line.size() && !std::isspace((unsigned char)line[pos])) pos++;
      }
      beat.speaker = bar > pos ? trim(line.substr(pos, bar - pos)) : "";
      beat.line = trim(line.substr(bar + 1));
      if (beat.line.empty()) {
        std::fprintf(stderr, "[Content] %s: comms line with no text, skipped\n",
                     path.string().c_str());
        continue;
      }
      m.comms.push_back(beat);
      continue;
    }

    // scene <id> <seconds> | <from x,y,z> | <lookAt x,y,z> | <caption>
    if (tokens[0] == "scene" && tokens.size() >= 3) {
      std::vector<std::string> fields;
      size_t start = 0;
      while (true) {
        size_t bar = line.find('|', start);
        fields.push_back(line.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
      }
      if (fields.size() < 4) {
        std::fprintf(stderr, "[Content] %s: malformed scene line '%s', skipped\n",
                     path.string().c_str(), line.c_str());
        continue;
      }
      CutsceneShot shot;
      shot.scene = tokens[1];
      try { shot.seconds = std::stof(tokens[2]); } catch (...) { shot.seconds = 3.0f; }
      shot.from = parseVec3(fields[1], shot.from);
      shot.lookAt = parseVec3(fields[2], shot.lookAt);
      std::string caption = fields[3];
      size_t a = caption.find_first_not_of(" \t");
      if (a != std::string::npos) shot.caption = caption.substr(a);
      m.scenes.push_back(shot);
      continue;
    }

    if (tokens[0] == "wave" && tokens.size() >= 3) {
      WaveSpawn w;
      w.enemyId = tokens[1];
      try {
        w.count = std::stoi(tokens[2]);
        if (tokens.size() >= 4) w.radius = std::stof(tokens[3]);
      } catch (...) {
        std::fprintf(stderr, "[Content] %s: malformed wave line '%s', skipped\n",
                     path.string().c_str(), line.c_str());
        continue;
      }
      m.waves.push_back(w);
    } else if (tokens[0] == "boss" && tokens.size() >= 2) {
      m.bossId = tokens[1];
      if (tokens.size() >= 3) {
        try { m.bossHpMultiplier = std::stof(tokens[2]); } catch (...) {}
      }
    } else {
      std::string k, v;
      if (keyValue(line, k, v)) {
        if (k == "name") m.name = v;
        else if (k == "objective") m.objective = v;
        else if (k == "brief") m.brief = v;
        else if (k == "zone") m.zone = v;
        else if (k == "tutorial") m.tutorial = (v == "true" || v == "1");
        else if (k == "layout") m.layout = v;
        else if (k == "start_unarmed") m.startUnarmed = (v == "true" || v == "1");
        else if (k == "campaign") { try { m.campaignIndex = std::stoi(v); } catch (...) {} }
        else if (k == "sky_zenith") m.skyZenith = parseVec3(v, m.skyZenith);
        else if (k == "floor_colour" || k == "floor_color") m.floorColour = parseVec3(v, m.floorColour);
        else if (k == "ambient") { m.ambient = parseVec3(v, m.ambient); m.ambientSet = true; }
        else if (k == "sky_horizon") m.skyHorizon = parseVec3(v, m.skyHorizon);
        else if (k == "fog_colour" || k == "fog_color") m.fogColour = parseVec3(v, m.fogColour);
        else if (k == "sun_colour" || k == "sun_color") m.sunColour = parseVec3(v, m.sunColour);
        else if (k == "fog_density") { try { m.fogDensity = std::stof(v); } catch (...) {} }
        else if (k == "cover") { try { m.coverDensity = std::stof(v); } catch (...) {} }
        else if (k == "sun_intensity") { try { m.sunIntensity = std::stof(v); } catch (...) {} }
        else if (k == "arena") { try { m.arenaSize = std::stof(v); } catch (...) {} }
        else if (k == "reward") { try { m.rewardChits = std::stoi(v); } catch (...) {} }
        else if (k == "reward_xp") { try { m.rewardXp = std::stoi(v); } catch (...) {} }
      }
    }
  }
  return m;
}

WeaponDef parseWeapon(const std::string& id, const fs::path& path) {
  WeaponDef w;
  w.id = id;
  w.name = id;
  std::ifstream f(path);
  std::string raw;
  while (std::getline(f, raw)) {
    std::string line = stripComment(raw);
    if (line.empty()) continue;
    std::string k, v;
    if (!keyValue(line, k, v)) continue;
    try {
      if (k == "name") w.name = v;
      else if (k == "damage") w.damage = std::stof(v);
      else if (k == "headshot_multiplier") w.headshotMultiplier = std::stof(v);
      else if (k == "fire_interval") w.fireInterval = std::stof(v);
      else if (k == "reload_time") w.reloadTime = std::stof(v);
      else if (k == "mag_size") w.magSize = std::stoi(v);
      else if (k == "reserve") w.reserveAmmo = std::stoi(v);
      else if (k == "cost") w.cost = std::stoi(v);
      else if (k == "pellets") w.pellets = std::max(1, std::stoi(v));
      else if (k == "spread") w.spread = std::stof(v);
      else if (k == "range") w.range = std::stof(v);
      else if (k == "pierce") w.pierce = (v == "true" || v == "1");
      else if (k == "shape") w.shape = v;
      else if (k == "rank_required") w.rankRequired = v;
      else if (k == "class_required") w.classRequired = v;
    } catch (...) {
      std::fprintf(stderr, "[Content] %s: bad value for '%s' = '%s', ignored\n",
                   path.string().c_str(), k.c_str(), v.c_str());
    }
  }
  return w;
}

ArmorDef parseArmor(const std::string& id, const fs::path& path) {
  ArmorDef a;
  a.id = id;
  a.name = id;
  std::ifstream f(path);
  std::string raw;
  while (std::getline(f, raw)) {
    std::string line = stripComment(raw);
    if (line.empty()) continue;
    std::string k, v;
    if (!keyValue(line, k, v)) continue;
    try {
      if (k == "name") a.name = v;
      else if (k == "hp_bonus") a.hpBonus = std::stof(v);
      else if (k == "damage_reduction") a.damageReduction = std::stof(v);
      else if (k == "cost") a.cost = std::stoi(v);
      else if (k == "rank_required") a.rankRequired = v;
    } catch (...) {
      std::fprintf(stderr, "[Content] %s: bad value for '%s' = '%s', ignored\n",
                   path.string().c_str(), k.c_str(), v.c_str());
    }
  }
  return a;
}

CosmeticDef parseCosmetic(const std::string& id, const fs::path& path) {
  CosmeticDef c;
  c.id = id;
  c.name = id;
  std::ifstream f(path);
  std::string raw;
  while (std::getline(f, raw)) {
    std::string line = stripComment(raw);
    if (line.empty()) continue;
    std::string k, v;
    if (!keyValue(line, k, v)) continue;
    try {
      if (k == "name") c.name = v;
      else if (k == "accent") c.accent = parseVec3(v, c.accent);
      else if (k == "cost") c.cost = std::stoi(v);
      else if (k == "rank_required") c.rankRequired = v;
    } catch (...) {
      std::fprintf(stderr, "[Content] %s: bad value for '%s' = '%s', ignored\n",
                   path.string().c_str(), k.c_str(), v.c_str());
    }
  }
  return c;
}

PlanetDef parsePlanet(const std::string& id, const fs::path& path) {
  PlanetDef p;
  p.id = id;
  p.name = id;
  std::ifstream f(path);
  std::string raw;
  while (std::getline(f, raw)) {
    std::string line = stripComment(raw);
    if (line.empty()) continue;
    std::string k, v;
    if (!keyValue(line, k, v)) continue;
    try {
      if (k == "name") p.name = v;
      else if (k == "position") p.position = parseVec3(v, p.position);
      else if (k == "radius") p.radius = std::stof(v);
      else if (k == "colour" || k == "color") p.colour = parseVec3(v, p.colour);
      else if (k == "colour2" || k == "color2") p.colour2 = parseVec3(v, p.colour2);
      else if (k == "cap") p.capExtent = std::stof(v);
      else if (k == "mission") p.missionId = v;
      else if (k == "station") p.station = (v == "true" || v == "1");
    } catch (...) {
      std::fprintf(stderr, "[Content] %s: bad value for '%s' = '%s', ignored\n",
                   path.string().c_str(), k.c_str(), v.c_str());
    }
  }
  return p;
}

ClassDef parseClass(const std::string& id, const fs::path& path) {
  ClassDef c;
  c.id = id;
  c.name = id;
  std::ifstream f(path);
  std::string raw;
  while (std::getline(f, raw)) {
    std::string line = stripComment(raw);
    if (line.empty()) continue;
    std::string k, v;
    if (!keyValue(line, k, v)) continue;
    try {
      if (k == "name") c.name = v;
      else if (k == "role") c.role = v;
      else if (k == "tagline") c.tagline = v;
      else if (k == "accent") c.accent = parseVec3(v, c.accent);
      else if (k == "weapon") c.weaponId = v;
      else if (k == "ability") {
        if (v == "barrier") c.ability = AbilityKind::Barrier;
        else if (v == "breach") c.ability = AbilityKind::Breach;
        else if (v == "phase") c.ability = AbilityKind::Phase;
        else {
          std::fprintf(stderr, "[Content] %s: unknown ability '%s', defaulting to phase\n",
                       path.string().c_str(), v.c_str());
        }
      }
      else if (k == "ability_name") c.abilityName = v;
      else if (k == "ability_desc") c.abilityDesc = v;
      else if (k == "ability_cooldown") c.abilityCooldown = std::stof(v);
      else if (k == "perk_name") c.perkName = v;
      else if (k == "perk") c.perk = v;
      else if (k == "hp") c.hp = std::stof(v);
      else if (k == "damage_reduction") c.damageReduction = std::stof(v);
    } catch (...) {
      std::fprintf(stderr, "[Content] %s: bad value for '%s' = '%s', ignored\n",
                   path.string().c_str(), k.c_str(), v.c_str());
    }
  }
  return c;
}

CrewDef parseCrew(const std::string& id, const fs::path& path) {
  CrewDef c;
  c.id = id;
  c.name = id;
  std::ifstream f(path);
  std::string raw;
  while (std::getline(f, raw)) {
    std::string line = stripComment(raw);
    if (line.empty()) continue;

    // say | <one line of dialogue>
    // The pipe keeps the line itself free to contain anything but a pipe,
    // which is the same trick the comms beats use.
    if (line.rfind("say", 0) == 0) {
      size_t bar = line.find('|');
      if (bar == std::string::npos) {
        std::fprintf(stderr, "[Content] %s: malformed say line '%s', skipped\n",
                     path.string().c_str(), line.c_str());
        continue;
      }
      std::string spoken = line.substr(bar + 1);
      size_t a = spoken.find_first_not_of(" \t");
      if (a != std::string::npos) c.say.push_back(spoken.substr(a));
      continue;
    }

    std::string k, v;
    if (!keyValue(line, k, v)) continue;
    try {
      if (k == "name") c.name = v;
      else if (k == "title") c.title = v;
      else if (k == "line") c.line = v;
      else if (k == "colour" || k == "color") c.colour = parseVec3(v, c.colour);
      else if (k == "position") c.position = parseVec3(v, c.position);
      else if (k == "facing") c.facingDegrees = std::stof(v);
      else if (k == "desk") c.desk = (v == "true" || v == "1");
      else if (k == "board") c.board = (v == "true" || v == "1");
      else if (k == "shop") {
        if (v == "gear") c.shop = CrewShop::Gear;
        else if (v == "route") c.shop = CrewShop::Route;
        else if (v == "contracts") c.shop = CrewShop::Contracts;
        else if (v == "none") c.shop = CrewShop::None;
        else {
          std::fprintf(stderr, "[Content] %s: unknown shop '%s', treated as none\n",
                       path.string().c_str(), v.c_str());
        }
      }
    } catch (...) {
      std::fprintf(stderr, "[Content] %s: bad value for '%s' = '%s', ignored\n",
                   path.string().c_str(), k.c_str(), v.c_str());
    }
  }
  return c;
}

RankDef parseRank(const std::string& id, const fs::path& path) {
  RankDef r;
  r.id = id;
  r.name = id;
  std::ifstream f(path);
  if (!f) {
    std::fprintf(stderr, "[Content] cannot open %s\n", path.string().c_str());
    return r;
  }
  std::string raw;
  while (std::getline(f, raw)) {
    std::string line = stripComment(raw);
    if (line.empty()) continue;
    std::string k, v;
    if (!keyValue(line, k, v)) continue;
    try {
      if (k == "name") r.name = v;
      else if (k == "xp") r.xp = std::stoi(v);
      else if (k == "stipend") r.stipend = std::stoi(v);
      else if (k == "blurb") r.blurb = v;
      else if (k == "unlock") r.unlock = v;
    } catch (...) {
      std::fprintf(stderr, "[Content] %s: bad value for '%s' = '%s', ignored\n",
                   path.string().c_str(), k.c_str(), v.c_str());
    }
  }
  return r;
}

}  // namespace

bool Content::loadAll(const std::string& dir) {
  fs::path root(dir);
  if (!fs::exists(root)) {
    std::fprintf(stderr, "[Content] content directory not found: %s\n", dir.c_str());
    return false;
  }

  fs::path enemyDir = root / "enemies";
  if (fs::exists(enemyDir)) {
    for (auto& entry : fs::directory_iterator(enemyDir)) {
      if (entry.path().extension() != ".cfg") continue;
      std::string id = entry.path().stem().string();
      enemies_[id] = parseEnemy(id, entry.path());
    }
  }

  fs::path missionDir = root / "missions";
  if (fs::exists(missionDir)) {
    for (auto& entry : fs::directory_iterator(missionDir)) {
      if (entry.path().extension() != ".cfg") continue;
      std::string id = entry.path().stem().string();
      missions_[id] = parseMission(id, entry.path());
    }
  }

  fs::path weaponDir = root / "weapons";
  if (fs::exists(weaponDir)) {
    for (auto& entry : fs::directory_iterator(weaponDir)) {
      if (entry.path().extension() != ".cfg") continue;
      std::string id = entry.path().stem().string();
      weapons_[id] = parseWeapon(id, entry.path());
    }
  }

  fs::path armorDir = root / "armor";
  if (fs::exists(armorDir)) {
    for (auto& entry : fs::directory_iterator(armorDir)) {
      if (entry.path().extension() != ".cfg") continue;
      std::string id = entry.path().stem().string();
      armor_[id] = parseArmor(id, entry.path());
    }
  }

  fs::path cosmeticDir = root / "cosmetics";
  if (fs::exists(cosmeticDir)) {
    for (auto& entry : fs::directory_iterator(cosmeticDir)) {
      if (entry.path().extension() != ".cfg") continue;
      std::string id = entry.path().stem().string();
      cosmetics_[id] = parseCosmetic(id, entry.path());
    }
  }

  fs::path planetDir = root / "planets";
  if (fs::exists(planetDir)) {
    for (auto& entry : fs::directory_iterator(planetDir)) {
      if (entry.path().extension() != ".cfg") continue;
      std::string id = entry.path().stem().string();
      planets_[id] = parsePlanet(id, entry.path());
    }
  }

  fs::path classDir = root / "classes";
  if (fs::exists(classDir)) {
    for (auto& entry : fs::directory_iterator(classDir)) {
      if (entry.path().extension() != ".cfg") continue;
      std::string id = entry.path().stem().string();
      classes_[id] = parseClass(id, entry.path());
    }
  }

  fs::path crewDir = root / "crew";
  if (fs::exists(crewDir)) {
    for (auto& entry : fs::directory_iterator(crewDir)) {
      if (entry.path().extension() != ".cfg") continue;
      std::string id = entry.path().stem().string();
      crew_[id] = parseCrew(id, entry.path());
    }
  }

  fs::path rankDir = root / "ranks";
  if (fs::exists(rankDir)) {
    rankLadder_.clear();
    for (auto& entry : fs::directory_iterator(rankDir)) {
      if (entry.path().extension() != ".cfg") continue;
      rankLadder_.push_back(parseRank(entry.path().stem().string(), entry.path()));
    }
    // Ordered by the experience each begins at, not by filename: the ladder
    // is a property of the content, and a directory listing is alphabetical.
    std::sort(rankLadder_.begin(), rankLadder_.end(),
              [](const RankDef& a, const RankDef& b) { return a.xp < b.xp; });
  }

  std::printf("[Content] loaded %zu enemy type(s), %zu mission(s), %zu weapon(s), "
              "%zu armor piece(s), %zu cosmetic(s), %zu planet(s), %zu class(es), "
              "%zu crew, %zu rank(s) from %s\n",
              enemies_.size(), missions_.size(), weapons_.size(), armor_.size(),
              cosmetics_.size(), planets_.size(), classes_.size(), crew_.size(),
              rankLadder_.size(), dir.c_str());
  return true;
}

const RankDef* Content::rank(const std::string& id) const {
  for (const RankDef& r : rankLadder_) if (r.id == id) return &r;
  return nullptr;
}

int Content::rankIndexForXp(int xp) const {
  if (rankLadder_.empty()) return -1;
  int best = 0;
  for (size_t i = 0; i < rankLadder_.size(); i++) {
    if (xp >= rankLadder_[i].xp) best = (int)i;
  }
  return best;
}

bool Content::rankReached(const std::string& rankId, int xp) const {
  if (rankId.empty()) return true;
  const RankDef* r = rank(rankId);
  // A gear file naming a rank that no longer exists stays buyable. The
  // alternative is an item that is permanently unobtainable because a rank
  // was renamed, which is a worse failure than one released slightly early.
  if (!r) return true;
  return xp >= r->xp;
}

const ClassDef* Content::playerClass(const std::string& id) const {
  auto it = classes_.find(id);
  return it == classes_.end() ? nullptr : &it->second;
}

std::vector<std::string> Content::classIds() const {
  std::vector<std::string> out;
  out.reserve(classes_.size());
  for (auto& kv : classes_) out.push_back(kv.first);
  std::sort(out.begin(), out.end());
  return out;
}

const CrewDef* Content::crew(const std::string& id) const {
  auto it = crew_.find(id);
  return it == crew_.end() ? nullptr : &it->second;
}

std::vector<std::string> Content::crewIds() const {
  std::vector<std::string> out;
  out.reserve(crew_.size());
  for (auto& kv : crew_) out.push_back(kv.first);
  std::sort(out.begin(), out.end());
  return out;
}

const EnemyType* Content::enemy(const std::string& id) const {
  auto it = enemies_.find(id);
  return it == enemies_.end() ? nullptr : &it->second;
}

const MissionDef* Content::mission(const std::string& id) const {
  auto it = missions_.find(id);
  return it == missions_.end() ? nullptr : &it->second;
}

const WeaponDef* Content::weapon(const std::string& id) const {
  auto it = weapons_.find(id);
  return it == weapons_.end() ? nullptr : &it->second;
}

const ArmorDef* Content::armor(const std::string& id) const {
  auto it = armor_.find(id);
  return it == armor_.end() ? nullptr : &it->second;
}

const CosmeticDef* Content::cosmetic(const std::string& id) const {
  auto it = cosmetics_.find(id);
  return it == cosmetics_.end() ? nullptr : &it->second;
}

const PlanetDef* Content::planet(const std::string& id) const {
  auto it = planets_.find(id);
  return it == planets_.end() ? nullptr : &it->second;
}

std::vector<std::string> Content::planetIds() const {
  std::vector<std::string> out;
  out.reserve(planets_.size());
  for (auto& kv : planets_) out.push_back(kv.first);
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<std::string> Content::missionIds() const {
  std::vector<std::string> out;
  out.reserve(missions_.size());
  for (auto& kv : missions_) out.push_back(kv.first);
  // The map is unordered, so without this the hub's mission list reorders
  // itself between runs on nothing but hash iteration order.
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<std::string> Content::campaignIds() const {
  std::vector<const MissionDef*> route;
  for (auto& kv : missions_) {
    if (kv.second.campaignIndex > 0) route.push_back(&kv.second);
  }
  std::sort(route.begin(), route.end(), [](const MissionDef* a, const MissionDef* b) {
    return a->campaignIndex < b->campaignIndex;
  });
  std::vector<std::string> out;
  out.reserve(route.size());
  for (const MissionDef* m : route) out.push_back(m->id);
  return out;
}

std::vector<std::string> Content::weaponIds() const {
  std::vector<std::string> out;
  out.reserve(weapons_.size());
  for (auto& kv : weapons_) out.push_back(kv.first);
  return out;
}

std::vector<std::string> Content::armorIds() const {
  std::vector<std::string> out;
  out.reserve(armor_.size());
  for (auto& kv : armor_) out.push_back(kv.first);
  return out;
}

std::vector<std::string> Content::cosmeticIds() const {
  std::vector<std::string> out;
  out.reserve(cosmetics_.size());
  for (auto& kv : cosmetics_) out.push_back(kv.first);
  return out;
}
