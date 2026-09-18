#pragma once
#include <algorithm>
#include <string>
#include <vector>

// A player's persistent progress: currency, what gear is owned vs merely
// available in content/, what's currently equipped, and which missions
// have been completed at least once (so a repeat completion doesn't pay
// out the reward chits again). Saved to and loaded from a plain text file
// — the same "key = value" format Content.cpp reads, not JSON, for the
// same reason: nothing here needs a library this project has no offline
// way to fetch.
struct Profile {
  std::string name = "Operative";
  int chits = 100;

  // The doctrine this record was created under (content/classes/<id>.cfg).
  // Fixed for the record's life: it decides the issued weapon, the field
  // ability and the passive, which is how the browser build draws the line
  // too. Empty on a save written before classes existed — Game falls back to
  // the plain starter loadout when it cannot resolve one, so an old save
  // still loads and plays.
  std::string classId;

  std::vector<std::string> ownedWeapons;
  std::vector<std::string> ownedArmor;
  std::vector<std::string> ownedCosmetics;

  std::string equippedWeapon;
  std::string equippedArmor;
  std::string equippedCosmetic;

  std::vector<std::string> completedMissions;

  static bool contains(const std::vector<std::string>& v, const std::string& id) {
    return std::find(v.begin(), v.end(), id) != v.end();
  }
  bool ownsWeapon(const std::string& id) const { return contains(ownedWeapons, id); }
  bool ownsArmor(const std::string& id) const { return contains(ownedArmor, id); }
  bool ownsCosmetic(const std::string& id) const { return contains(ownedCosmetics, id); }
  bool hasCompleted(const std::string& missionId) const { return contains(completedMissions, missionId); }

  // Grants the starter gear (patrol_vest / default, plus a rifle) if not
  // already owned, and equips it if nothing is currently equipped — so a
  // brand-new profile is playable without a shop trip, and an old save
  // that predates a new starter item still gets it.
  //
  // `issuedWeapon` is the doctrine's own weapon (ClassDef::weaponId), granted
  // free and equipped with the record. Empty falls back to the service rifle,
  // which is what a save written before classes existed has.
  void ensureStarterGear(const std::string& issuedWeapon = "");

  // Records that `missionId` finished successfully and pays out `reward`
  // chits — but only the first time; replaying a cleared mission doesn't
  // re-pay it. Returns the amount actually paid (0 on a repeat).
  int recordMissionComplete(const std::string& missionId, int reward);
};

class ProfileStore {
public:
  // Loads from `path`; if the file doesn't exist (a first run), returns a
  // fresh Profile with starter gear already granted rather than failing —
  // there is no "no save file" error state a player should ever see.
  static Profile load(const std::string& path);
  static bool save(const Profile& p, const std::string& path);

  // Whether a save file is actually there. load() deliberately can't tell
  // you this — it hands back a playable profile either way — but the slot
  // select screen has to show "EMPTY" rather than a fabricated one.
  static bool exists(const std::string& path);

  // Deletes a slot's file. Returns false if there was nothing to delete.
  static bool erase(const std::string& path);
};
