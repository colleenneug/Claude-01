#include "Bounty.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace {

std::string upper(std::string s) {
  for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
  return s;
}

std::string shapeLabel(const std::string& shape) {
  if (shape == "pistol") return "SIDEARM";
  if (shape == "smg") return "SMG";
  return upper(shape);
}

void addUnique(std::vector<std::string>& v, const std::string& id) {
  if (!Profile::contains(v, id)) v.push_back(id);
}

int percent(float f) { return (int)std::lround(f * 100.0f); }

}  // namespace

// ---------------------------------------------------------------- bounties

BountyStatus bountyStatus(const Profile& p, const Content& c, const std::string& id) {
  const BountyDef* b = c.bounty(id);
  if (!b) return BountyStatus::Done;
  if (const Profile::ActiveBounty* a = p.activeBounty(id)) {
    return a->progress >= b->count ? BountyStatus::Ready : BountyStatus::Active;
  }
  if (!b->repeatable && p.bountyDone(id)) return BountyStatus::Done;
  if (!c.rankReached(b->rankRequired, p.xp)) return BountyStatus::RankLocked;
  return BountyStatus::Available;
}

int tallyCounts(const BountyDef& b, const MissionTally& t) {
  auto lookup = [](const std::map<std::string, int>& m, const std::string& k) {
    auto it = m.find(k);
    return it == m.end() ? 0 : it->second;
  };
  const bool thisMission = b.target.empty() || b.target == t.missionId;
  switch (b.goal) {
    case BountyGoal::Kills:       return t.kills;
    case BountyGoal::KillsOf:     return lookup(t.killsOf, b.target);
    case BountyGoal::Headshots:   return t.headshots;
    case BountyGoal::WeaponKills: return lookup(t.killsWith, b.target);
    case BountyGoal::Ability:     return t.abilityUses;
    case BountyGoal::Clear:       return (t.cleared && thisMission) ? 1 : 0;
    case BountyGoal::Flawless:    return (t.cleared && thisMission && t.downs == 0) ? 1 : 0;
  }
  return 0;
}

std::vector<std::string> applyTally(Profile& p, const Content& c, const MissionTally& t) {
  std::vector<std::string> ready;
  for (Profile::ActiveBounty& a : p.bounties) {
    const BountyDef* b = c.bounty(a.id);
    if (!b) continue;
    if (a.progress >= b->count) continue;   // already waiting to be paid
    const int add = tallyCounts(*b, t);
    if (add <= 0) continue;
    a.progress = std::min(b->count, a.progress + add);
    if (a.progress >= b->count) ready.push_back(a.id);
  }
  return ready;
}

std::string bountyGoalLabel(const BountyDef& b, const Content& c) {
  auto missionName = [&](const std::string& id) {
    const MissionDef* m = c.mission(id);
    return m ? upper(m->name) : upper(id);
  };
  switch (b.goal) {
    case BountyGoal::Kills: return "KILLS, ANY HOSTILE";
    case BountyGoal::KillsOf: {
      const EnemyType* e = c.enemy(b.target);
      return (e ? upper(e->name) : upper(b.target)) + " KILLS";
    }
    case BountyGoal::Headshots: return "HEADSHOT KILLS";
    case BountyGoal::WeaponKills: return "KILLS WITH A " + shapeLabel(b.target);
    case BountyGoal::Ability: return "FIELD ABILITY USES";
    case BountyGoal::Clear:
      return b.target.empty() ? "MISSIONS CLEARED" : "CLEAR " + missionName(b.target);
    case BountyGoal::Flawless:
      return b.target.empty() ? "CLEARS WITHOUT GOING DOWN"
                              : "CLEAR " + missionName(b.target) + " WITHOUT GOING DOWN";
  }
  return "";
}

bool takeBounty(Profile& p, const Content& c, const std::string& id, std::string* note) {
  const BountyDef* b = c.bounty(id);
  if (!b) return false;
  switch (bountyStatus(p, c, id)) {
    case BountyStatus::Available: break;
    case BountyStatus::Active:
    case BountyStatus::Ready:
      if (note) *note = "ALREADY ON YOUR RECORD";
      return false;
    case BountyStatus::RankLocked: {
      const RankDef* r = c.rank(b->rankRequired);
      if (note) *note = "NOT AT YOUR RANK - " + (r ? r->name : std::string("LOCKED"));
      return false;
    }
    case BountyStatus::Done:
      if (note) *note = "ALREADY PAID";
      return false;
  }
  p.bounties.push_back({id, 0});
  if (note) *note = upper(b->name) + " - TAKEN";
  return true;
}

bool turnInBounty(Profile& p, const Content& c, const std::string& id, std::string* note) {
  const BountyDef* b = c.bounty(id);
  if (!b || bountyStatus(p, c, id) != BountyStatus::Ready) {
    if (note) *note = "NOT DONE YET";
    return false;
  }
  p.bounties.erase(std::remove_if(p.bounties.begin(), p.bounties.end(),
                                  [&](const Profile::ActiveBounty& a) { return a.id == id; }),
                   p.bounties.end());
  addUnique(p.bountiesDone, id);
  p.chits += b->rewardChits;
  creditXp(p, c, b->rewardXp);

  std::string paid = "+" + std::to_string(b->rewardChits) + " CHITS  +" +
                     std::to_string(b->rewardXp) + " XP";
  // Gear paid as a reward is owned outright, rank gate or not: earning a
  // rifle early is the whole reason a bounty pays in one.
  const std::string& item = b->rewardItem;
  const size_t colon = item.find(':');
  if (colon != std::string::npos) {
    const std::string kind = item.substr(0, colon), what = item.substr(colon + 1);
    if (kind == "weapon" && c.weapon(what)) {
      addUnique(p.ownedWeapons, what);
      paid += "  +" + upper(c.weapon(what)->name);
    } else if (kind == "armor" && c.armor(what)) {
      addUnique(p.ownedArmor, what);
      paid += "  +" + upper(c.armor(what)->name);
    } else if (kind == "cosmetic" && c.cosmetic(what)) {
      addUnique(p.ownedCosmetics, what);
      paid += "  +" + upper(c.cosmetic(what)->name);
    }
  }
  if (note) *note = "PAID: " + paid;
  return true;
}

bool giverHasReady(const Profile& p, const Content& c, const std::string& giver) {
  for (const std::string& id : c.bountyIds(giver)) {
    if (bountyStatus(p, c, id) == BountyStatus::Ready) return true;
  }
  return false;
}

bool giverHasNew(const Profile& p, const Content& c, const std::string& giver) {
  for (const std::string& id : c.bountyIds(giver)) {
    // "New" means never taken: a repeatable one you have done before is
    // still on the board, but it is not news.
    if (bountyStatus(p, c, id) == BountyStatus::Available && !p.bountyDone(id)) return true;
  }
  return false;
}

// ---------------------------------------------------------------- upgrades

UpgradeTotals upgradeTotals(const Profile& p, const Content& c) {
  UpgradeTotals t;
  for (const std::string& id : p.upgrades) {
    const UpgradeDef* u = c.upgrade(id);
    if (!u) continue;   // an upgrade removed from content stops applying
    switch (u->stat) {
      // Faster reloads and a tighter cone are reductions, and stack
      // multiplicatively so that no number of drills reaches zero.
      case UpgradeStat::Reload: t.reloadMul *= std::max(0.1f, 1.0f - u->amount); break;
      case UpgradeStat::Spread: t.spreadMul *= std::max(0.1f, 1.0f - u->amount); break;
      case UpgradeStat::AbilityCooldown: t.abilityMul *= std::max(0.1f, 1.0f - u->amount); break;
      case UpgradeStat::Reserve: t.reserveMul += u->amount; break;
      case UpgradeStat::Xp: t.xpMul += u->amount; break;
      case UpgradeStat::Hp: t.hpAdd += u->amount; break;
      case UpgradeStat::Harness: t.harnessAdd += (int)std::lround(u->amount); break;
    }
  }
  return t;
}

UpgradeStatus upgradeStatus(const Profile& p, const Content& c, const std::string& id) {
  const UpgradeDef* u = c.upgrade(id);
  if (!u || p.hasUpgrade(id)) return UpgradeStatus::Owned;
  if (!u->prerequisite.empty() && !p.hasUpgrade(u->prerequisite)) return UpgradeStatus::NeedsPrior;
  if (!c.rankReached(u->rankRequired, p.xp)) return UpgradeStatus::RankLocked;
  if (p.chits < u->cost) return UpgradeStatus::TooPoor;
  return UpgradeStatus::Affordable;
}

bool buyUpgrade(Profile& p, const Content& c, const std::string& id, std::string* note) {
  const UpgradeDef* u = c.upgrade(id);
  if (!u) return false;
  switch (upgradeStatus(p, c, id)) {
    case UpgradeStatus::Owned:
      if (note) *note = "ALREADY ON YOUR RECORD";
      return false;
    case UpgradeStatus::NeedsPrior: {
      const UpgradeDef* prior = c.upgrade(u->prerequisite);
      if (note) *note = "FINISH " + (prior ? upper(prior->name) : upper(u->prerequisite)) + " FIRST";
      return false;
    }
    case UpgradeStatus::RankLocked: {
      const RankDef* r = c.rank(u->rankRequired);
      if (note) *note = "NOT AT YOUR RANK - " + (r ? r->name : std::string("LOCKED"));
      return false;
    }
    case UpgradeStatus::TooPoor:
      if (note) *note = "NOT ENOUGH CHITS";
      return false;
    case UpgradeStatus::Affordable: break;
  }
  p.chits -= u->cost;
  p.upgrades.push_back(id);
  if (note) *note = upper(u->name) + " - " + upgradeEffectLabel(*u);
  return true;
}

std::string upgradeEffectLabel(const UpgradeDef& u) {
  const std::string pc = std::to_string(percent(u.amount)) + "%";
  const int flat = (int)std::lround(u.amount);
  switch (u.stat) {
    case UpgradeStat::Reload: return pc + " FASTER RELOADS";
    case UpgradeStat::Reserve: return "+" + pc + " RESERVE AMMO";
    case UpgradeStat::Spread: return pc + " TIGHTER CONE";
    case UpgradeStat::Hp: return "+" + std::to_string(flat) + " MAX HEALTH";
    case UpgradeStat::Harness:
      return "+" + std::to_string(flat) + (flat == 1 ? " HARNESS CHARGE" : " HARNESS CHARGES");
    case UpgradeStat::Xp: return "+" + pc + " EXPERIENCE";
    case UpgradeStat::AbilityCooldown: return pc + " SHORTER ABILITY COOLDOWN";
  }
  return "";
}

// ---------------------------------------------------------------- appraisal

std::vector<Appraisal> appraisable(const Profile& p, const Content& c) {
  std::vector<Appraisal> out;
  auto worth = [](int cost) { return cost / 2; };

  std::vector<std::string> ids = p.ownedWeapons;
  std::sort(ids.begin(), ids.end());
  for (const std::string& id : ids) {
    const WeaponDef* d = c.weapon(id);
    if (!d || d->cost <= 0) continue;
    if (id == p.equippedWeapon || id == p.equippedSidearm) continue;
    out.push_back({Appraisal::Kind::Weapon, id, d->name, worth(d->cost)});
  }
  ids = p.ownedArmor;
  std::sort(ids.begin(), ids.end());
  for (const std::string& id : ids) {
    const ArmorDef* d = c.armor(id);
    if (!d || d->cost <= 0 || id == p.equippedArmor) continue;
    out.push_back({Appraisal::Kind::Armour, id, d->name, worth(d->cost)});
  }
  ids = p.ownedCosmetics;
  std::sort(ids.begin(), ids.end());
  for (const std::string& id : ids) {
    const CosmeticDef* d = c.cosmetic(id);
    if (!d || d->cost <= 0 || id == p.equippedCosmetic) continue;
    out.push_back({Appraisal::Kind::Shader, id, d->name, worth(d->cost)});
  }
  return out;
}

bool sellToAppraiser(Profile& p, const Content& c, const Appraisal& a, std::string* note) {
  // Re-derive rather than trust the caller's copy: the list on screen can be
  // a frame stale, and selling the rifle you just equipped would be a bug
  // with a price on it.
  for (const Appraisal& now : appraisable(p, c)) {
    if (now.kind != a.kind || now.id != a.id) continue;
    std::vector<std::string>& owned = a.kind == Appraisal::Kind::Weapon ? p.ownedWeapons
                                      : a.kind == Appraisal::Kind::Armour ? p.ownedArmor
                                                                          : p.ownedCosmetics;
    owned.erase(std::remove(owned.begin(), owned.end(), a.id), owned.end());
    p.chits += now.value;
    if (note) *note = upper(now.name) + " - SOLD FOR " + std::to_string(now.value) + " CHITS";
    return true;
  }
  if (note) *note = "NOT SOMETHING I WILL TAKE";
  return false;
}

// ---------------------------------------------------------------- shipyard

ShipStatus shipStatus(const Profile& p, const Content& c, const std::string& id) {
  const ShipDef* d = c.ship(id);
  if (!d) return ShipStatus::NotRated;
  if (p.equippedShip == id && p.ownsShip(id)) return ShipStatus::Flying;
  if (p.ownsShip(id)) return ShipStatus::Owned;
  if (!campaignComplete(p, c)) return ShipStatus::NotRated;
  if (!c.rankReached(d->rankRequired, p.xp)) return ShipStatus::RankLocked;
  if (p.chits < d->cost) return ShipStatus::TooPoor;
  return ShipStatus::Affordable;
}

bool buyOrFlyShip(Profile& p, const Content& c, const std::string& id, std::string* note) {
  const ShipDef* d = c.ship(id);
  if (!d) return false;
  switch (shipStatus(p, c, id)) {
    case ShipStatus::Flying:
      if (note) *note = "ALREADY YOUR SHIP";
      return false;
    case ShipStatus::Owned:
      p.equippedShip = id;
      if (note) *note = upper(d->name) + " - FUELLED AND ON THE PAD";
      return true;
    case ShipStatus::NotRated:
      if (note) *note = "FINISH THE CAMPAIGN - THE FIRST HULL IS ISSUED, NOT SOLD";
      return false;
    case ShipStatus::RankLocked: {
      const RankDef* r = c.rank(d->rankRequired);
      if (note) *note = "NOT AT YOUR RANK - " + (r ? r->name : std::string("LOCKED"));
      return false;
    }
    case ShipStatus::TooPoor:
      if (note) *note = "NOT ENOUGH CHITS";
      return false;
    case ShipStatus::Affordable: break;
  }
  p.chits -= d->cost;
  p.ownedShips.push_back(id);
  p.equippedShip = id;
  if (note) *note = upper(d->name) + " - BOUGHT, AND YOURS TO FLY";
  return true;
}
