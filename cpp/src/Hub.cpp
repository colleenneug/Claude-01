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
  missionIds_ = content.missionIds();
  std::sort(weaponIds_.begin(), weaponIds_.end());
  std::sort(armorIds_.begin(), armorIds_.end());
  std::sort(cosmeticIds_.begin(), cosmeticIds_.end());
  std::sort(missionIds_.begin(), missionIds_.end());

  // Open the hub on whatever's actually equipped/last-played rather than
  // always the alphabetically-first item.
  weaponIndex_ = indexOf(weaponIds_, profile.equippedWeapon);
  armorIndex_ = indexOf(armorIds_, profile.equippedArmor);
  cosmeticIndex_ = indexOf(cosmeticIds_, profile.equippedCosmetic);
}

void Hub::preselectMission(const std::string& id) {
  auto it = std::find(missionIds_.begin(), missionIds_.end(), id);
  if (it != missionIds_.end()) missionIndex_ = (int)(it - missionIds_.begin());
}

void Hub::cycleWeapon() {
  if (weaponIds_.empty()) return;
  weaponIndex_ = (weaponIndex_ + 1) % (int)weaponIds_.size();
  const std::string& id = weaponIds_[weaponIndex_];
  if (profile_->ownsWeapon(id)) {
    profile_->equippedWeapon = id;
    return;
  }
  const WeaponDef* def = content_->weapon(id);
  if (def && profile_->chits >= def->cost) {
    profile_->chits -= def->cost;
    profile_->ownedWeapons.push_back(id);
    profile_->equippedWeapon = id;
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
  if (def && profile_->chits >= def->cost) {
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
  if (def && profile_->chits >= def->cost) {
    profile_->chits -= def->cost;
    profile_->ownedCosmetics.push_back(id);
    profile_->equippedCosmetic = id;
  }
}

void Hub::cycleMission() {
  if (missionIds_.empty()) return;
  missionIndex_ = (missionIndex_ + 1) % (int)missionIds_.size();
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
