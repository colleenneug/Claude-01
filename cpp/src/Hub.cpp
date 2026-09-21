#include "Hub.h"
#include <algorithm>
#include <cstring>

namespace {
int indexOf(const std::vector<std::string>& ids, const std::string& id) {
  auto it = std::find(ids.begin(), ids.end(), id);
  return it == ids.end() ? 0 : (int)(it - ids.begin());
}
}  // namespace

void Hub::init(const Content& content, Profile& profile) {
  content_ = &content;
  profile_ = &profile;
  weaponIds_ = content.weaponIds();
  armorIds_ = content.armorIds();
  cosmeticIds_ = content.cosmeticIds();
  std::sort(weaponIds_.begin(), weaponIds_.end());
  std::sort(armorIds_.begin(), armorIds_.end());
  std::sort(cosmeticIds_.begin(), cosmeticIds_.end());

  // The campaign first, in story order, then everything else alphabetically.
  // Sorting the whole list by id instead would scatter the route through the
  // side content and leave WARD SIX sitting above HARD DOCK.
  missionIds_ = content.campaignIds();
  campaignCount_ = (int)missionIds_.size();
  std::vector<std::string> side;
  for (const std::string& id : content.missionIds()) {
    const MissionDef* m = content.mission(id);
    if (!m || m->campaignIndex <= 0) side.push_back(id);
  }
  std::sort(side.begin(), side.end());
  missionIds_.insert(missionIds_.end(), side.begin(), side.end());

  // Open on the furthest sector actually reachable rather than on mission one
  // again every time you dock. Across two tracks that means the furthest
  // *unfinished* one: walking in and finding the list parked on a mission you
  // cleared six deployments ago is a list that has stopped being useful.
  missionIndex_ = 0;
  for (int i = 0; i < campaignCount_; i++) {
    if (missionLocked(i)) continue;
    missionIndex_ = i;
    if (!profile.hasCompleted(missionIds_[(size_t)i])) break;
  }

  // Open the hub on whatever's actually equipped/last-played rather than
  // always the alphabetically-first item.
  weaponIndex_ = indexOf(weaponIds_, profile.equippedWeapon);
  armorIndex_ = indexOf(armorIds_, profile.equippedArmor);
  cosmeticIndex_ = indexOf(cosmeticIds_, profile.equippedCosmetic);
}

void Hub::preselectMission(const std::string& id) {
  auto it = std::find(missionIds_.begin(), missionIds_.end(), id);
  if (it == missionIds_.end()) return;
  int index = (int)(it - missionIds_.begin());
  if (missionLocked(index)) return;   // --mission does not skip the route
  missionIndex_ = index;
}

void Hub::preselectFirstSideContract() {
  if (campaignCount_ < (int)missionIds_.size()) missionIndex_ = campaignCount_;
}

void Hub::cycleWeapon() {
  if (weaponIds_.empty()) return;
  weaponIndex_ = (weaponIndex_ + 1) % (int)weaponIds_.size();
  const std::string& id = weaponIds_[weaponIndex_];
  // Which slot it lands in is its shape — a pistol is a sidearm, everything
  // else is a primary. That means one key still cycles the whole rack and
  // there is no second list to navigate: picking a pistol changes what is in
  // your second holster and picking a rifle changes what is in your first.
  const WeaponDef* picked = content_->weapon(id);
  const bool isSidearm = picked && picked->shape == "pistol";
  auto equip = [&](const std::string& weaponId) {
    if (isSidearm) profile_->equippedSidearm = weaponId;
    else profile_->equippedWeapon = weaponId;
  };

  if (profile_->ownsWeapon(id)) {
    equip(id);
    return;
  }
  const WeaponDef* def = content_->weapon(id);
  // Rank first, then price. Something released above your rank is shown but
  // not sold: the ladder is supposed to be visible from the counter.
  const bool rightDoctrine =
      !def || def->classRequired.empty() || def->classRequired == profile_->classId;
  if (def && rightDoctrine && content_->rankReached(def->rankRequired, profile_->xp) &&
      profile_->chits >= def->cost) {
    profile_->chits -= def->cost;
    profile_->ownedWeapons.push_back(id);
    equip(id);
  }
  // else: can't afford it yet — the selection still moves so an unaffordable
  // item is visible (Hud::drawHub dims it red), it just doesn't equip.
}

void Hub::cycleArmor() {
  if (armorIds_.empty()) return;
  armorIndex_ = (armorIndex_ + 1) % (int)armorIds_.size();
  const std::string& id = armorIds_[armorIndex_];
  if (profile_->ownsArmor(id)) {
    profile_->equippedArmor = id;
    return;
  }
  const ArmorDef* def = content_->armor(id);
  if (def && content_->rankReached(def->rankRequired, profile_->xp) &&
      profile_->chits >= def->cost) {
    profile_->chits -= def->cost;
    profile_->ownedArmor.push_back(id);
    profile_->equippedArmor = id;
  }
}

void Hub::cycleCosmetic() {
  if (cosmeticIds_.empty()) return;
  cosmeticIndex_ = (cosmeticIndex_ + 1) % (int)cosmeticIds_.size();
  const std::string& id = cosmeticIds_[cosmeticIndex_];
  if (profile_->ownsCosmetic(id)) {
    profile_->equippedCosmetic = id;
    return;
  }
  const CosmeticDef* def = content_->cosmetic(id);
  if (def && content_->rankReached(def->rankRequired, profile_->xp) &&
      profile_->chits >= def->cost) {
    profile_->chits -= def->cost;
    profile_->ownedCosmetics.push_back(id);
    profile_->equippedCosmetic = id;
  }
}

bool Hub::missionLocked(int index) const {
  if (!content_ || !profile_) return false;
  if (index < 0 || index >= campaignCount_) return false;   // side content
  const MissionDef* m = content_->mission(missionIds_[(size_t)index]);
  if (!m) return false;

  // Within a track, a mission waits on the one before it. Across tracks, a
  // whole track waits on the one before *it*: the ark does not open to a
  // record that has not finished the Strider programme, and HARD DOCK
  // showing OPEN to a candidate was the one thing that made the two
  // campaigns read as one long list with a gap in it.
  const bool firstOfTrack =
      index == 0 || content_->mission(missionIds_[(size_t)index - 1])->campaign != m->campaign;

  if (firstOfTrack) {
    const std::vector<std::string> tracks = content_->campaignTracks();
    size_t here = 0;
    while (here < tracks.size() && tracks[here] != m->campaign) here++;
    if (here == 0 || here >= tracks.size()) return false;    // the first track is always open
    for (const std::string& id : content_->campaignIds(tracks[here - 1])) {
      if (!profile_->hasCompleted(id)) return true;
    }
    return false;
  }

  return !profile_->hasCompleted(missionIds_[(size_t)index - 1]);
}

void Hub::cycleMission() {
  if (missionIds_.empty()) return;
  // Step over locked sectors rather than stopping on them: a list you can
  // land on but not launch from is a list that looks broken.
  const int n = (int)missionIds_.size();
  for (int step = 1; step <= n; step++) {
    int next = (missionIndex_ + step) % n;
    if (!missionLocked(next)) { missionIndex_ = next; return; }
  }
}

bool Hub::update(GLFWwindow* window, const char* scriptedKey) {
  if (scriptedKey) {
    if (std::strcmp(scriptedKey, "1") == 0) cycleWeapon();
    else if (std::strcmp(scriptedKey, "2") == 0) cycleArmor();
    else if (std::strcmp(scriptedKey, "3") == 0) cycleCosmetic();
    else if (std::strcmp(scriptedKey, "mission") == 0) cycleMission();
    else if (std::strcmp(scriptedKey, "launch") == 0) return true;
    return false;
  }

  auto edge = [&](int key, bool& prev) {
    bool now = glfwGetKey(window, key) == GLFW_PRESS;
    bool pressedNow = now && !prev;
    prev = now;
    return pressedNow;
  };

  if (edge(GLFW_KEY_1, prev1_)) cycleWeapon();
  if (edge(GLFW_KEY_2, prev2_)) cycleArmor();
  if (edge(GLFW_KEY_3, prev3_)) cycleCosmetic();
  if (edge(GLFW_KEY_TAB, prevTab_)) cycleMission();
  bool enter = edge(GLFW_KEY_ENTER, prevEnter_);
  bool space = edge(GLFW_KEY_SPACE, prevSpace_);
  return enter || space;
}
