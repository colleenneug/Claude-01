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
class Content;

struct Profile {
  std::string name = "Operative";
  int chits = 100;

  // Career experience. The one number on a record that only ever goes up:
  // chits are spent and gear is swapped, but what you have actually done is
  // this. Rank is derived from it (Content::rankIndexForXp) rather than
  // stored, so re-balancing the ladder re-ranks every existing save instead
  // of leaving old records stranded on a rung that no longer exists.
  int xp = 0;
  // The highest rung this record has been paid the stipend for. Stored,
  // because a promotion pays out once and "has xp past this rung" would pay
  // again every time the file was loaded.
  int rankPaid = 0;

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

  // Two carried weapons. `equippedWeapon` is the primary and keeps its name
  // so saves written before there were two slots still load; the sidearm is
  // the second holster, and a record that has one is never completely out of
  // ammunition.
  std::string equippedWeapon;
  std::string equippedSidearm;
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

  // Grants the starter gear — a service sidearm, patrol_vest, default — if
  // not already owned, and equips it if nothing is currently equipped, so a
  // brand-new profile is playable without a shop trip and an old save that
  // predates a new starter item still gets it. A new record owns the sidearm
  // and nothing else: the weapon its doctrine carries is on the armoury
  // bench in Block D, not handed over at a desk.
  void ensureStarterGear();

  // Records that `missionId` finished successfully and pays out `reward`
  // chits — but only the first time; replaying a cleared mission doesn't
  // re-pay it. Returns the amount actually paid (0 on a repeat).
  int recordMissionComplete(const std::string& missionId, int reward);

  // Adds career experience. Returns the amount added, which is never
  // negative — nothing in this game takes experience away.
  int addXp(int amount);
};

// Brings `p`'s rank up to date with its experience and pays out the stipend
// for every rung reached since the last time this ran — several at once, if a
// long mission carried the record past two of them.
//
// Lives here rather than on Profile because it needs the ladder, and Profile
// is deliberately a plain record that knows nothing about content. Returns
// the rank index now held, or -1 when the content tree has no ladder at all.
// `promotedTo`, when given, is filled with the name of the highest rank newly
// reached — empty if nothing changed — so the caller can say so.
int settleRank(Profile& p, const Content& content, std::string* promotedTo = nullptr,
               int* stipendPaid = nullptr);

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
