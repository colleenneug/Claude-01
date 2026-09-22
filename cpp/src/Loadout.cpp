#include "Loadout.h"

#include <algorithm>
#include <cstring>

namespace {

// The sidearm column is the weapons whose shape puts them in that holster,
// and the primary column is everything else. One rule, the same one the shop
// and the floor pickups use (Game::slotFor), so the three can never disagree
// about where a weapon lives.
bool isSidearmShape(const WeaponDef* d) { return d && d->shape == "pistol"; }

}  // namespace

void Loadout::open(const Content& content, Profile& profile) {
  content_ = &content;
  profile_ = &profile;
  open_ = true;
  notice_.clear();
  noticeT_ = 99.0f;
  rebuild();

  // Open with the caret already on what is equipped in each column, rather
  // than at the top of every list: the first thing you do on opening your kit
  // is look at what you are carrying.
  auto caretTo = [&](Column c, const std::string& id) {
    const std::vector<std::string>& list = ids_[(int)c];
    auto it = std::find(list.begin(), list.end(), id);
    rows_[(int)c] = it == list.end() ? 0 : (int)(it - list.begin());
  };
  caretTo(Column::Primary, profile.equippedWeapon);
  caretTo(Column::Sidearm, profile.equippedSidearm);
  caretTo(Column::Armour, profile.equippedArmor);
  caretTo(Column::Shader, profile.equippedCosmetic);

  // Reset the key edges from the live state. The key that opened this is
  // still down — G, usually — and an edge seeded false reads it as a fresh
  // press and closes the screen on frame one. The same bug the cutscenes had.
  prevUp_ = prevDown_ = prevLeft_ = prevRight_ = false;
  prevEquip_ = true;
  prevClose_ = true;
}

void Loadout::rebuild() {
  for (auto& v : ids_) v.clear();
  if (!content_) return;

  std::vector<std::string> weapons = content_->weaponIds();
  std::sort(weapons.begin(), weapons.end());
  for (const std::string& id : weapons) {
    const WeaponDef* d = content_->weapon(id);
    ids_[(int)(isSidearmShape(d) ? Column::Sidearm : Column::Primary)].push_back(id);
  }

  ids_[(int)Column::Armour] = content_->armorIds();
  std::sort(ids_[(int)Column::Armour].begin(), ids_[(int)Column::Armour].end());
  ids_[(int)Column::Shader] = content_->cosmeticIds();
  std::sort(ids_[(int)Column::Shader].begin(), ids_[(int)Column::Shader].end());

  for (int c = 0; c < (int)Column::Count; c++) {
    if (rows_[c] >= (int)ids_[c].size()) rows_[c] = 0;
  }
}

Loadout::Availability Loadout::availability(Column c, const std::string& id) const {
  if (!content_ || !profile_ || id.empty()) return Availability::Empty;

  int cost = 0;
  std::string rankReq, classReq;
  bool owned = false, equipped = false;

  switch (c) {
    case Column::Primary:
    case Column::Sidearm: {
      const WeaponDef* d = content_->weapon(id);
      if (!d) return Availability::Empty;
      cost = d->cost;
      rankReq = d->rankRequired;
      classReq = d->classRequired;
      owned = profile_->ownsWeapon(id);
      equipped = (c == Column::Sidearm) ? profile_->equippedSidearm == id
                                        : profile_->equippedWeapon == id;
      break;
    }
    case Column::Armour: {
      const ArmorDef* d = content_->armor(id);
      if (!d) return Availability::Empty;
      cost = d->cost;
      rankReq = d->rankRequired;
      owned = profile_->ownsArmor(id);
      equipped = profile_->equippedArmor == id;
      break;
    }
    case Column::Shader: {
      const CosmeticDef* d = content_->cosmetic(id);
      if (!d) return Availability::Empty;
      cost = d->cost;
      rankReq = d->rankRequired;
      owned = profile_->ownsCosmetic(id);
      equipped = profile_->equippedCosmetic == id;
      break;
    }
    default: return Availability::Empty;
  }

  if (equipped) return Availability::Equipped;
  if (owned) return Availability::Owned;
  // Order matters: doctrine first, then rank, then money. It is the order the
  // reasons actually bind in — being poor is not why a Wraith cannot carry a
  // MAUL-12 — and it decides which one the row shows.
  if (!classReq.empty() && classReq != profile_->classId) return Availability::WrongDoctrine;
  if (!content_->rankReached(rankReq, profile_->xp)) return Availability::RankLocked;
  if (profile_->chits < cost) return Availability::TooPoor;
  return Availability::Affordable;
}

std::string Loadout::gateLabel(Column c, const std::string& id) const {
  if (!content_ || id.empty()) return "";
  switch (availability(c, id)) {
    case Availability::Equipped: return "EQUIPPED";
    case Availability::Owned:    return "OWNED";
    case Availability::WrongDoctrine: {
      const WeaponDef* w = content_->weapon(id);
      const ClassDef* cd = w ? content_->playerClass(w->classRequired) : nullptr;
      return cd ? cd->name + " ISSUE" : "NOT YOUR ISSUE";
    }
    case Availability::RankLocked: {
      std::string req;
      if (c == Column::Primary || c == Column::Sidearm) {
        if (const WeaponDef* d = content_->weapon(id)) req = d->rankRequired;
      } else if (c == Column::Armour) {
        if (const ArmorDef* d = content_->armor(id)) req = d->rankRequired;
      } else if (const CosmeticDef* d = content_->cosmetic(id)) {
        req = d->rankRequired;
      }
      const RankDef* r = content_->rank(req);
      return r ? r->name : "LOCKED";
    }
    default: {
      int cost = 0;
      if (c == Column::Primary || c == Column::Sidearm) {
        if (const WeaponDef* d = content_->weapon(id)) cost = d->cost;
      } else if (c == Column::Armour) {
        if (const ArmorDef* d = content_->armor(id)) cost = d->cost;
      } else if (const CosmeticDef* d = content_->cosmetic(id)) {
        cost = d->cost;
      }
      return std::to_string(cost) + " CHITS";
    }
  }
}

bool Loadout::equipSelected() {
  if (!content_ || !profile_) return false;
  const Column c = column_;
  const std::vector<std::string>& list = ids_[(int)c];
  if (list.empty()) return false;
  const std::string& id = list[(size_t)std::clamp(rows_[(int)c], 0, (int)list.size() - 1)];

  const Availability a = availability(c, id);
  switch (a) {
    case Availability::Equipped:
      setNotice("ALREADY CARRYING IT");
      return false;
    case Availability::WrongDoctrine:
      setNotice("ISSUED TO ANOTHER DOCTRINE");
      return false;
    case Availability::RankLocked:
      setNotice("NOT RELEASED AT YOUR RANK - " + gateLabel(c, id));
      return false;
    case Availability::TooPoor:
      setNotice("NOT ENOUGH CHITS");
      return false;
    case Availability::Empty:
      return false;
    case Availability::Owned:
    case Availability::Affordable:
      break;
  }

  // Buy it if this is the first time. Deducted once — Availability::Owned
  // skips this, so re-equipping something is free.
  int cost = 0;
  if (a == Availability::Affordable) {
    switch (c) {
      case Column::Primary:
      case Column::Sidearm:
        if (const WeaponDef* d = content_->weapon(id)) cost = d->cost;
        profile_->ownedWeapons.push_back(id);
        break;
      case Column::Armour:
        if (const ArmorDef* d = content_->armor(id)) cost = d->cost;
        profile_->ownedArmor.push_back(id);
        break;
      case Column::Shader:
        if (const CosmeticDef* d = content_->cosmetic(id)) cost = d->cost;
        profile_->ownedCosmetics.push_back(id);
        break;
      default: break;
    }
    profile_->chits -= cost;
  }

  std::string name = id;
  switch (c) {
    case Column::Primary:
      profile_->equippedWeapon = id;
      if (const WeaponDef* d = content_->weapon(id)) name = d->name;
      break;
    case Column::Sidearm:
      profile_->equippedSidearm = id;
      if (const WeaponDef* d = content_->weapon(id)) name = d->name;
      break;
    case Column::Armour:
      profile_->equippedArmor = id;
      if (const ArmorDef* d = content_->armor(id)) name = d->name;
      break;
    case Column::Shader:
      profile_->equippedCosmetic = id;
      if (const CosmeticDef* d = content_->cosmetic(id)) name = d->name;
      break;
    default: break;
  }

  setNotice(cost > 0 ? name + " - BOUGHT AND EQUIPPED" : name + " - EQUIPPED");
  return true;
}

bool Loadout::update(GLFWwindow* window, const char* scripted) {
  if (!open_) return false;
  noticeT_ += 1.0f / 60.0f;   // only drives a fade; exactness buys nothing

  auto edge = [&](int a, int b, bool& prev, const char* token) {
    bool down = (window && (glfwGetKey(window, a) == GLFW_PRESS ||
                            (b && glfwGetKey(window, b) == GLFW_PRESS))) ||
                (scripted && std::strcmp(scripted, token) == 0);
    bool fired = down && !prev;
    prev = down;
    return fired;
  };

  const int cols = (int)Column::Count;
  if (edge(GLFW_KEY_LEFT, GLFW_KEY_A, prevLeft_, "left")) {
    column_ = (Column)(((int)column_ + cols - 1) % cols);
  }
  if (edge(GLFW_KEY_RIGHT, GLFW_KEY_D, prevRight_, "right")) {
    column_ = (Column)(((int)column_ + 1) % cols);
  }

  const int n = (int)ids_[(int)column_].size();
  if (n > 0) {
    if (edge(GLFW_KEY_UP, GLFW_KEY_W, prevUp_, "up")) {
      rows_[(int)column_] = (rows_[(int)column_] + n - 1) % n;
    }
    if (edge(GLFW_KEY_DOWN, GLFW_KEY_S, prevDown_, "down")) {
      rows_[(int)column_] = (rows_[(int)column_] + 1) % n;
    }
  }

  if (edge(GLFW_KEY_ENTER, GLFW_KEY_F, prevEquip_, "equip")) equipSelected();

  if (edge(GLFW_KEY_G, GLFW_KEY_ESCAPE, prevClose_, "close")) {
    open_ = false;
    return true;   // the one frame on which the caller re-reads the profile
  }
  return false;
}
