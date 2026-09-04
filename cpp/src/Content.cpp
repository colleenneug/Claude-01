#include "Content.h"
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
        else if (k == "arena") { try { m.arenaSize = std::stof(v); } catch (...) {} }
      }
    }
  }
  return m;
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

  std::printf("[Content] loaded %zu enemy type(s), %zu mission(s) from %s\n",
              enemies_.size(), missions_.size(), dir.c_str());
  return true;
}

const EnemyType* Content::enemy(const std::string& id) const {
  auto it = enemies_.find(id);
  return it == enemies_.end() ? nullptr : &it->second;
}

const MissionDef* Content::mission(const std::string& id) const {
  auto it = missions_.find(id);
  return it == missions_.end() ? nullptr : &it->second;
}

std::vector<std::string> Content::missionIds() const {
  std::vector<std::string> out;
  out.reserve(missions_.size());
  for (auto& kv : missions_) out.push_back(kv.first);
  return out;
}
