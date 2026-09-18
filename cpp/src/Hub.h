#pragma once
#include "Gl.h"
#include "Content.h"
#include "Profile.h"
#include <string>
#include <vector>

// The between-mission space: pick a loadout and a mission from everything
// content/ has, entirely through the keyboard — this project has no text
// rendering (see Hud.h), so there's no shop UI with labels and prices to
// click through. Keys 1/2/3 cycle the weapon/armour/cosmetic selection,
// equipping it immediately if owned or buying-then-equipping it if not
// (silently skipping the buy if it can't be afforded — the selection still
// moves so it's visible what you don't own yet); Tab cycles the mission;
// Enter/Space launches it. Hud::drawHub renders all of that back as the
// same bar/colour language the in-mission Hud already speaks.
class Hub {
public:
  void init(const Content& content, Profile& profile);

  // Advances one frame. `scriptedKey`, when non-null, substitutes for real
  // keyboard input — one of "1", "2", "3", "mission", "launch" — and is
  // used only by headless verification (EREBUS_HUB_SCRIPT in main.cpp);
  // ordinary play always passes nullptr and this polls `window` instead.
  // Returns true exactly on the frame the player commits to a mission.
  bool update(GLFWwindow* window, const char* scriptedKey = nullptr);

  const std::string& selectedMission() const { return missionIds_[missionIndex_]; }

  // The route down the ark runs in story order and opens one sector at a
  // time: a campaign mission is locked until the one before it is cleared.
  // Side content (a planet's patrol) is never locked, so the first entries
  // of missionIds() are the route and the rest are free to fly at any time.
  bool missionLocked(int index) const;
  int campaignCount() const { return campaignCount_; }

  // Which half of the screen the person who opened it is responsible for.
  // Both halves stay usable — walking back down three decks to buy a rifle
  // you forgot would be a punishment, not a hub — but the one you came for
  // is lit and the other is dimmed, so it is obvious whose counter you are
  // standing at.
  enum class Focus { All, Gear, Route };
  void setFocus(Focus f) { focus_ = f; }
  Focus focus() const { return focus_; }

  // Who is serving. Empty when the screen was opened by a terminal rather
  // than by a person.
  void setHost(const std::string& name, const std::string& title, glm::vec3 colour) {
    hostName_ = name;
    hostTitle_ = title;
    hostColour_ = colour;
  }
  const std::string& hostName() const { return hostName_; }
  const std::string& hostTitle() const { return hostTitle_; }
  glm::vec3 hostColour() const { return hostColour_; }

  // Move the selection to the first mission that is not part of the ark
  // campaign — the side work, which is what the contracts post deals in.
  void preselectFirstSideContract();

  const std::vector<std::string>& weaponIds() const { return weaponIds_; }
  const std::vector<std::string>& armorIds() const { return armorIds_; }
  const std::vector<std::string>& cosmeticIds() const { return cosmeticIds_; }
  const std::vector<std::string>& missionIds() const { return missionIds_; }
  int weaponIndex() const { return weaponIndex_; }
  int armorIndex() const { return armorIndex_; }
  int cosmeticIndex() const { return cosmeticIndex_; }
  int missionIndex() const { return missionIndex_; }

  // Lets main.cpp honour --mission as the hub's initial highlighted
  // selection rather than only as a headless-bypass argument. A no-op if
  // `id` isn't a known mission.
  void preselectMission(const std::string& id);

private:
  void cycleWeapon();
  void cycleArmor();
  void cycleCosmetic();
  void cycleMission();

  const Content* content_ = nullptr;
  Profile* profile_ = nullptr;

  std::vector<std::string> weaponIds_, armorIds_, cosmeticIds_, missionIds_;
  int campaignCount_ = 0;   // how many leading entries of missionIds_ are the route
  Focus focus_ = Focus::All;
  std::string hostName_, hostTitle_;
  glm::vec3 hostColour_{0.8f, 0.9f, 1.0f};
  int weaponIndex_ = 0, armorIndex_ = 0, cosmeticIndex_ = 0, missionIndex_ = 0;

  bool prev1_ = false, prev2_ = false, prev3_ = false, prevTab_ = false,
       prevEnter_ = false, prevSpace_ = false;
};
