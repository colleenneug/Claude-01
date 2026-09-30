#include "Profile.h"
#include "Content.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace {

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

std::string stripComment(const std::string& line) {
  size_t h = line.find('#');
  return trim(h == std::string::npos ? line : line.substr(0, h));
}

bool keyValue(const std::string& line, std::string& key, std::string& value) {
  size_t eq = line.find('=');
  if (eq == std::string::npos) return false;
  key = trim(line.substr(0, eq));
  value = trim(line.substr(eq + 1));
  return !key.empty();
}

void addUnique(std::vector<std::string>& v, const std::string& id) {
  if (!Profile::contains(v, id)) v.push_back(id);
}

}  // namespace

void Profile::ensureStarterGear() {
  // A new record owns a sidearm and nothing else. Your doctrine's weapon is
  // not issued at a desk — it is on the armoury bench in Block D, and walking
  // over it is what puts it in your inventory (see Game::equipWeaponById).
  //
  // An older save keeps whatever it already owns: its own owned_weapon lines
  // are read before this runs, so nothing here takes a rifle away from a
  // record that earned one before the sidearm became the starter.
  addUnique(ownedWeapons, "sidearm");
  addUnique(ownedArmor, "patrol_vest");
  addUnique(ownedCosmetics, "default");
  // A new record carries the sidearm and nothing in the primary holster.
  // The primary is what Block D's armoury bench is for.
  if (equippedSidearm.empty()) equippedSidearm = "sidearm";
  if (equippedWeapon == "sidearm") equippedWeapon.clear();
  if (equippedArmor.empty()) equippedArmor = "patrol_vest";
  if (equippedCosmetic.empty()) equippedCosmetic = "default";
}

int Profile::addXp(int amount) {
  if (amount <= 0) return 0;
  xp += amount;
  return amount;
}

int Profile::recordMissionComplete(const std::string& missionId, int reward) {
  if (hasCompleted(missionId)) return 0;
  completedMissions.push_back(missionId);
  chits += reward;
  return reward;
}

int settleRank(Profile& p, const Content& content, std::string* promotedTo,
               int* stipendPaid) {
  if (promotedTo) promotedTo->clear();
  if (stipendPaid) *stipendPaid = 0;

  const std::vector<RankDef>& ladder = content.ranks();
  if (ladder.empty()) return -1;

  int now = content.rankIndexForXp(p.xp);
  if (now < 0) return -1;
  // Clamp: a save written against a longer ladder must not index past the
  // end of a shorter one, and must not be paid a second time for rungs that
  // no longer exist either.
  if (p.rankPaid > (int)ladder.size() - 1) p.rankPaid = (int)ladder.size() - 1;
  if (p.rankPaid < 0) p.rankPaid = 0;

  int paid = 0;
  for (int i = p.rankPaid + 1; i <= now; i++) {
    p.chits += ladder[i].stipend;
    paid += ladder[i].stipend;
  }
  if (now > p.rankPaid) {
    if (promotedTo) *promotedTo = ladder[now].name;
    p.rankPaid = now;
  }
  if (stipendPaid) *stipendPaid = paid;
  return now;
}

bool trackCleared(const Profile& p, const Content& content, const std::string& track) {
  const std::vector<std::string> ids = content.campaignIds(track);
  if (ids.empty()) return true;   // no such track in this content tree

  bool all = true;
  for (const std::string& id : ids) {
    if (!p.hasCompleted(id)) { all = false; break; }
  }
  if (all) return true;

  // The grandfather clause. Anything cleared on a track that comes after this
  // one means the record predates this track being in front of it.
  const std::vector<std::string> tracks = content.campaignTracks();
  size_t here = 0;
  while (here < tracks.size() && tracks[here] != track) here++;
  for (size_t t = here + 1; t < tracks.size(); t++) {
    for (const std::string& id : content.campaignIds(tracks[t])) {
      if (p.hasCompleted(id)) return true;
    }
  }
  return false;
}

Profile ProfileStore::load(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    Profile p;
    p.ensureStarterGear();
    return p;
  }

  Profile p;
  p.ownedWeapons.clear();
  p.ownedArmor.clear();
  p.ownedCosmetics.clear();

  std::string raw;
  while (std::getline(f, raw)) {
    std::string line = stripComment(raw);
    if (line.empty()) continue;

    std::istringstream ls(line);
    std::string first;
    ls >> first;
    if (first == "owned_weapon" || first == "owned_armor" ||
        first == "owned_cosmetic" || first == "completed") {
      std::string id;
      ls >> id;
      if (id.empty()) continue;
      if (first == "owned_weapon") addUnique(p.ownedWeapons, id);
      else if (first == "owned_armor") addUnique(p.ownedArmor, id);
      else if (first == "owned_cosmetic") addUnique(p.ownedCosmetics, id);
      else addUnique(p.completedMissions, id);
      continue;
    }

    std::string k, v;
    if (!keyValue(line, k, v)) continue;
    try {
      if (k == "name") p.name = v;
      else if (k == "chits") p.chits = std::stoi(v);
      else if (k == "xp") p.xp = std::stoi(v);
      else if (k == "rank_paid") p.rankPaid = std::stoi(v);
      else if (k == "class") p.classId = v;
      else if (k == "equipped_weapon") p.equippedWeapon = v;
      else if (k == "equipped_sidearm") p.equippedSidearm = v;
      else if (k == "equipped_armor") p.equippedArmor = v;
      else if (k == "equipped_cosmetic") p.equippedCosmetic = v;
    } catch (...) {
      std::fprintf(stderr, "[Profile] %s: bad value for '%s' = '%s', ignored\n",
                   path.c_str(), k.c_str(), v.c_str());
    }
  }

  p.ensureStarterGear();
  return p;
}

bool ProfileStore::exists(const std::string& path) {
  std::error_code ec;   // the throwing overload would turn a permissions
                        // hiccup into a crash on the slot select screen
  return std::filesystem::exists(path, ec) && !ec;
}

std::string ProfileStore::dataDir() {
  namespace fs = std::filesystem;
  fs::path base;
  if (const char* o = std::getenv("EREBUS_DATA_DIR"); o && *o) {
    base = fs::path(o);
  } else {
#if defined(_WIN32)
    if (const char* a = std::getenv("APPDATA"); a && *a) base = fs::path(a) / "ErebusCradle";
#elif defined(__APPLE__)
    if (const char* h = std::getenv("HOME"); h && *h)
      base = fs::path(h) / "Library" / "Application Support" / "ErebusCradle";
#else
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) {
      base = fs::path(x) / "erebus-cradle";
    } else if (const char* h = std::getenv("HOME"); h && *h) {
      base = fs::path(h) / ".local" / "share" / "erebus-cradle";
    }
#endif
  }
  if (base.empty()) return "";
  std::error_code ec;
  fs::create_directories(base, ec);
  if (ec) {
    std::fprintf(stderr, "[Profile] cannot create save folder '%s' (%s); saving next to the game\n",
                 base.string().c_str(), ec.message().c_str());
    return "";
  }
  return base.string();
}

std::string ProfileStore::slotPath(int slot) {
  const std::string name = "save" + std::to_string(slot + 1) + ".dat";
  const std::string dir = dataDir();
  if (dir.empty()) return name;
  return (std::filesystem::path(dir) / name).string();
}

std::string ProfileStore::migrateLegacySlot(int slot, const std::string& legacyDir) {
  namespace fs = std::filesystem;
  const std::string target = slotPath(slot);
  const fs::path legacy = fs::path(legacyDir) / ("save" + std::to_string(slot + 1) + ".dat");

  std::error_code ec;
  // Nothing to do if the new slot already has a record in it — including the
  // case where the per-user folder could not be created and slotPath fell
  // back to the very file we would be copying.
  if (fs::exists(target, ec)) return "";
  if (!fs::exists(legacy, ec)) return "";
  if (fs::equivalent(legacy, target, ec)) return "";

  fs::copy_file(legacy, target, fs::copy_options::skip_existing, ec);
  if (ec) {
    std::fprintf(stderr, "[Profile] could not carry %s over to %s (%s)\n",
                 legacy.string().c_str(), target.c_str(), ec.message().c_str());
    return "";
  }
  return legacy.string();
}

bool ProfileStore::erase(const std::string& path) {
  std::error_code ec;
  return std::filesystem::remove(path, ec) && !ec;
}

bool ProfileStore::save(const Profile& p, const std::string& path) {
  std::ofstream f(path, std::ios::trunc);
  if (!f) {
    std::fprintf(stderr, "[Profile] failed to open '%s' for writing\n", path.c_str());
    return false;
  }

  f << "name = " << p.name << "\n";
  f << "chits = " << p.chits << "\n";
  f << "xp = " << p.xp << "\n";
  f << "rank_paid = " << p.rankPaid << "\n";
  f << "class = " << p.classId << "\n";
  f << "equipped_weapon = " << p.equippedWeapon << "\n";
  f << "equipped_sidearm = " << p.equippedSidearm << "\n";
  f << "equipped_armor = " << p.equippedArmor << "\n";
  f << "equipped_cosmetic = " << p.equippedCosmetic << "\n";
  for (auto& id : p.ownedWeapons) f << "owned_weapon " << id << "\n";
  for (auto& id : p.ownedArmor) f << "owned_armor " << id << "\n";
  for (auto& id : p.ownedCosmetics) f << "owned_cosmetic " << id << "\n";
  for (auto& id : p.completedMissions) f << "completed " << id << "\n";

  return true;
}
