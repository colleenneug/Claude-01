#include "Post.h"

#include <algorithm>
#include <cstring>

void Post::open(const Content& content, Profile& profile, const CrewDef& host,
                const Refusal& refusal) {
  content_ = &content;
  profile_ = &profile;
  host_ = &host;
  refusal_ = refusal;
  open_ = true;
  notice_.clear();
  noticeT_ = 99.0f;
  rows_[0] = rows_[1] = 0;
  rebuild();

  // Open on whatever is worth your attention: money waiting to be collected
  // first, then their service, and the board when they have nothing else.
  tab_ = serviceRows() > 0 ? Tab::Service : Tab::Bounties;
  for (size_t i = 0; i < bountyIds_.size(); i++) {
    if (bountyStatus(profile, content, bountyIds_[i]) == BountyStatus::Ready) {
      tab_ = Tab::Bounties;
      rows_[(int)Tab::Bounties] = (int)i;
      break;
    }
  }

  // The key that ended the conversation is still down. Seeded true, the same
  // as the kit screen, or that press reads as a fresh one and acts on frame one.
  prevUp_ = prevDown_ = prevLeft_ = prevRight_ = false;
  prevUse_ = true;
  prevClose_ = true;
  prevTab_ = true;
}

void Post::rebuild() {
  bountyIds_.clear();
  upgradeIds_.clear();
  appraisals_.clear();
  shipIds_.clear();
  marketIds_.clear();
  if (!content_ || !host_) return;

  // The board: what is ready, then what you are working on, then what you
  // could take, then what is closed to you. A one-off already paid is gone.
  std::vector<std::string> all = content_->bountyIds(host_->id);
  for (BountyStatus want : {BountyStatus::Ready, BountyStatus::Active, BountyStatus::Available,
                            BountyStatus::RankLocked}) {
    for (const std::string& id : all) {
      if (bountyStatus(*profile_, *content_, id) == want) bountyIds_.push_back(id);
    }
  }

  if (host_->shop == CrewShop::Upgrades) upgradeIds_ = content_->upgradeIds(host_->id);
  if (host_->shop == CrewShop::Appraisal) {
    appraisals_ = appraisable(*profile_, *content_);
    marketIds_ = ::blackMarket(*content_);
  }
  if (host_->shop == CrewShop::Shipyard) shipIds_ = content_->shipIds();

  for (int t = 0; t < (int)Tab::Count; t++) {
    const int n = t == (int)Tab::Service ? serviceRows() : (int)bountyIds_.size();
    rows_[t] = n <= 0 ? 0 : std::clamp(rows_[t], 0, n - 1);
  }
}

bool Post::serviceIsCounter() const {
  if (!host_) return false;
  return host_->shop == CrewShop::Gear || host_->shop == CrewShop::Route ||
         host_->shop == CrewShop::Contracts;
}

int Post::serviceRows() const {
  if (!host_) return 0;
  if (serviceIsCounter()) return 1;
  if (host_->shop == CrewShop::Upgrades) return (int)upgradeIds_.size();
  if (host_->shop == CrewShop::Shipyard) return (int)shipIds_.size();
  // The Rook's tab is there even with nothing to sell, so that "nothing on
  // you I would take" is something the screen says rather than a missing tab.
  if (host_->shop == CrewShop::Appraisal) {
    return (int)marketIds_.size() + std::max(1, (int)appraisals_.size());
  }
  return 0;
}

std::string Post::serviceName() const {
  if (!host_) return "";
  if (!host_->serviceName.empty()) return host_->serviceName;
  switch (host_->shop) {
    case CrewShop::Gear: return "ARMOURY";
    case CrewShop::Route: return "FLIGHT LINE";
    case CrewShop::Contracts: return "CONTRACTS";
    case CrewShop::Upgrades: return "TRAINING";
    case CrewShop::Appraisal: return "APPRAISAL";
    case CrewShop::Shipyard: return "SHIPYARD";
    case CrewShop::None: break;
  }
  return "";
}

Post::Result Post::act() {
  if (!content_ || !profile_ || !host_) return Result::None;

  if (tab_ == Tab::Bounties) {
    if (bountyIds_.empty()) {
      setNotice("NOTHING ON THE BOARD");
      return Result::None;
    }
    const std::string id = bountyIds_[(size_t)rows_[(int)Tab::Bounties]];
    std::string note;
    switch (bountyStatus(*profile_, *content_, id)) {
      case BountyStatus::Ready: turnInBounty(*profile_, *content_, id, &note); break;
      case BountyStatus::Active: {
        const BountyDef* b = content_->bounty(id);
        const Profile::ActiveBounty* a = profile_->activeBounty(id);
        note = "STILL OPEN - " + std::to_string(a ? a->progress : 0) + " OF " +
               std::to_string(b ? b->count : 0);
        break;
      }
      default: takeBounty(*profile_, *content_, id, &note); break;
    }
    setNotice(note);
    // The board re-sorts on what just changed, and the caret follows the
    // bounty rather than staying on a row that is now something else.
    rebuild();
    auto it = std::find(bountyIds_.begin(), bountyIds_.end(), id);
    if (it != bountyIds_.end()) rows_[(int)Tab::Bounties] = (int)(it - bountyIds_.begin());
    return Result::None;
  }

  // ---- service
  if (serviceIsCounter()) {
    if (refusal_.refused()) {
      setNotice(refusal_.heading);
      return Result::None;
    }
    open_ = false;
    return Result::OpenCounter;
  }

  std::string note;
  if (host_->shop == CrewShop::Upgrades && !upgradeIds_.empty()) {
    buyUpgrade(*profile_, *content_, upgradeIds_[(size_t)rows_[(int)Tab::Service]], &note);
  } else if (host_->shop == CrewShop::Shipyard && !shipIds_.empty()) {
    buyOrFlyShip(*profile_, *content_, shipIds_[(size_t)rows_[(int)Tab::Service]], &note);
  } else if (host_->shop == CrewShop::Appraisal) {
    const int r = rows_[(int)Tab::Service];
    if (r < (int)marketIds_.size()) {
      buyWeapon(*profile_, *content_, marketIds_[(size_t)r], &note);
    } else if (appraisals_.empty()) {
      note = "NOTHING ON YOU I WOULD TAKE";
    } else {
      sellToAppraiser(*profile_, *content_,
                      appraisals_[(size_t)(r - (int)marketIds_.size())], &note);
    }
  }
  setNotice(note);
  rebuild();
  return Result::None;
}

Post::Result Post::update(GLFWwindow* window, const char* scripted, bool engagePulse) {
  if (!open_) return Result::None;
  noticeT_ += 1.0f / 60.0f;   // only drives a fade

  auto down = [&](int a, int b, const char* token) {
    return (window && (glfwGetKey(window, a) == GLFW_PRESS ||
                       (b && glfwGetKey(window, b) == GLFW_PRESS))) ||
           (scripted && std::strcmp(scripted, token) == 0);
  };
  auto edge = [&](bool now, bool& prev) {
    const bool fired = now && !prev;
    prev = now;
    return fired;
  };

  // Tab and the two arrow pairs all switch halves: there are only two.
  const bool tabKey = window && glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
  if (edge(down(GLFW_KEY_LEFT, GLFW_KEY_A, "left"), prevLeft_) |
      edge(down(GLFW_KEY_RIGHT, GLFW_KEY_D, "right"), prevRight_) |
      edge(tabKey, prevTab_)) {
    tab_ = tab_ == Tab::Service ? Tab::Bounties : Tab::Service;
  }

  const int n = tab_ == Tab::Service ? serviceRows() : (int)bountyIds_.size();
  if (n > 0) {
    int& r = rows_[(int)tab_];
    if (edge(down(GLFW_KEY_UP, GLFW_KEY_W, "up"), prevUp_)) r = (r + n - 1) % n;
    if (edge(down(GLFW_KEY_DOWN, GLFW_KEY_S, "down"), prevDown_)) r = (r + 1) % n;
  }

  // Enter, F or E acts. E as well because it is the key you walked up and
  // talked with, and reaching for another one to take a job is friction.
  const bool use = down(GLFW_KEY_ENTER, GLFW_KEY_F, "use") ||
                   (window && glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) || engagePulse;
  if (edge(use, prevUse_)) {
    const Result r = act();
    if (r != Result::None) return r;
  }

  // Escape or Backspace leaves. Not Q: Q undocks in the station underneath,
  // and a close key shared with "leave the station" is one press away from
  // walking you out of an airlock.
  if (edge(down(GLFW_KEY_ESCAPE, GLFW_KEY_BACKSPACE, "close"), prevClose_)) {
    open_ = false;
    return Result::Closed;
  }
  return Result::None;
}
