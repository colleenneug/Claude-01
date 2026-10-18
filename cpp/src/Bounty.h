#pragma once
#include "Content.h"
#include "Profile.h"
#include <map>
#include <string>
#include <vector>

// Everything about a crew member's post that is not a conversation: their
// bounties, their upgrades, and the Rook's appraisal. All of it is rules over
// a Profile and the content tree, kept here rather than on Profile because
// Profile is deliberately a plain record that knows nothing about content,
// and rather than in the post screen because the mission-exit path needs the
// same rules without a screen anywhere in sight.

// What one mission did, as far as a bounty cares. Game fills it in as the
// mission runs; the mission-exit path hands it to applyTally once, at the
// same point experience is banked, so walking out of a debrief counts
// exactly what finishing it would have.
struct MissionTally {
  std::string missionId;
  bool cleared = false;
  int downs = 0;            // harness charges spent, plus the final one
  int kills = 0;
  int headshots = 0;        // kills whose killing round was a headshot
  int abilityUses = 0;
  std::map<std::string, int> killsOf;     // by enemy archetype id
  std::map<std::string, int> killsWith;   // by weapon shape (pistol, shotgun...)
};

// Where a bounty stands for a record, in the order a board shows them.
enum class BountyStatus {
  Ready,       // taken and the count is met: go and get paid
  Active,      // taken, still counting
  Available,   // on the board, yours to take
  RankLocked,  // on the board, but not at your rank
  Done,        // a one-off that has been paid
};
BountyStatus bountyStatus(const Profile& p, const Content& c, const std::string& id);

// Adds a mission's work to every bounty the record has taken. Returns the
// ids that crossed their count on this mission — not the ones that were
// already ready, which would announce the same thing after every mission.
std::vector<std::string> applyTally(Profile& p, const Content& c, const MissionTally& t);

// How much of a tally one bounty would count. Exposed for the board, which
// says what counts in the same words the tally uses.
int tallyCounts(const BountyDef& b, const MissionTally& t);

// One short line saying what the bounty counts: "THRALL KILLS", "HEADSHOTS",
// "CLEAR THE RANGE WITHOUT GOING DOWN".
std::string bountyGoalLabel(const BountyDef& b, const Content& c);

bool takeBounty(Profile& p, const Content& c, const std::string& id, std::string* note = nullptr);
// Pays a ready bounty and takes it off the record. Returns false, with the
// reason in `note`, if it is not ready.
bool turnInBounty(Profile& p, const Content& c, const std::string& id, std::string* note = nullptr);

// Whether anyone at this post has something for the record right now.
bool giverHasReady(const Profile& p, const Content& c, const std::string& giver);
bool giverHasNew(const Profile& p, const Content& c, const std::string& giver);

// The sum of every upgrade a record owns, as the multipliers and offsets
// Game applies. A record with none gets the identity, so a save written
// before upgrades existed plays exactly as it did.
struct UpgradeTotals {
  float reloadMul = 1.0f;    // reload time
  float reserveMul = 1.0f;   // reserve ammunition carried
  float spreadMul = 1.0f;    // the cone
  float hpAdd = 0.0f;
  int harnessAdd = 0;
  float xpMul = 1.0f;
  float abilityMul = 1.0f;   // field ability cooldown
};
UpgradeTotals upgradeTotals(const Profile& p, const Content& c);

enum class UpgradeStatus { Owned, Affordable, TooPoor, RankLocked, NeedsPrior };
UpgradeStatus upgradeStatus(const Profile& p, const Content& c, const std::string& id);
bool buyUpgrade(Profile& p, const Content& c, const std::string& id, std::string* note = nullptr);
// "+8% RELOAD SPEED", "+1 HARNESS CHARGE" — what the upgrade does, in the
// units a player thinks in rather than the ones the file is written in.
std::string upgradeEffectLabel(const UpgradeDef& u);

// THE ROOK. Anything you own, are not wearing or holding, and paid for, is
// worth half what you paid. Issue gear (cost 0) is not worth anything to
// anybody and is not on the list — selling your service sidearm for nothing
// is a mistake the screen should not let you make.
struct Appraisal {
  enum class Kind { Weapon, Armour, Shader } kind = Kind::Weapon;
  std::string id, name;
  int value = 0;
};
std::vector<Appraisal> appraisable(const Profile& p, const Content& c);
bool sellToAppraiser(Profile& p, const Content& c, const Appraisal& a, std::string* note = nullptr);

// THE SHIPWRIGHT. Nothing is sold to a record that has not finished the
// campaign — the first ship is issued, not bought, and the yard will not
// put a candidate in a hull ahead of that. After it, ships are bought like
// gear: rank first, then chits. Picking an owned one makes it the one you fly.
enum class ShipStatus { Flying, Owned, Affordable, TooPoor, RankLocked, NotRated };
ShipStatus shipStatus(const Profile& p, const Content& c, const std::string& id);
bool buyOrFlyShip(Profile& p, const Content& c, const std::string& id, std::string* note = nullptr);
