#pragma once
#include "Gl.h"
#include "Bounty.h"
#include "Content.h"
#include "Profile.h"
#include <string>
#include <vector>

// A CREW MEMBER'S POST — what is behind their desk once the talking is done.
//
// Two halves, on two tabs:
//
//   SERVICE   what only this person does. For the quartermasters, the flight
//             officers and the contract boards that is their own counter in
//             the hub, narrowed to their own stock; for Bright, Marcheva and
//             Odil it is a line of permanent upgrades; for the Rook it is
//             appraisal, gear you are not using sold back for chits.
//   BOUNTIES  their jobs. Taken here, worked on across any mission you fly,
//             and paid here — by them, not by a terminal. The point of a
//             bounty board per person is that it gives you a reason to walk
//             over to somebody.
//
// A modal overlay, like the kit screen: the station keeps running underneath
// with its input frozen, and whatever this changes is written straight to the
// Profile.
class Post {
public:
  enum class Tab : int { Service = 0, Bounties, Count };

  // What the caller has to do after a frame.
  enum class Result { None, Closed, OpenCounter };

  // A counter can be shut: Hale will not open the route to an unrated
  // record, and the contract boards stay shut until the campaign is done.
  // `refusal` is the heading and `refusalWhy` the line under it; an empty
  // heading means the counter is open.
  struct Refusal {
    std::string heading, why;
    [[nodiscard]] bool refused() const { return !heading.empty(); }
  };
  void open(const Content& content, Profile& profile, const CrewDef& host,
            const Refusal& refusal = Refusal{});
  void close() { open_ = false; }
  [[nodiscard]] bool isOpen() const { return open_; }

  // `scripted`, when non-null, substitutes for the keyboard — one of "left",
  // "right", "up", "down", "use", "close" — and is used only by headless
  // verification. `engagePulse` is the station's scripted E, so a run that
  // talks its way up to a desk can also act at it.
  Result update(GLFWwindow* window, const char* scripted = nullptr, bool engagePulse = false);

  // ---- what the renderer needs
  [[nodiscard]] const CrewDef* host() const { return host_; }
  [[nodiscard]] Tab tab() const { return tab_; }
  [[nodiscard]] int row() const { return rows_[(int)tab_]; }
  [[nodiscard]] int row(Tab t) const { return rows_[(int)t]; }
  [[nodiscard]] bool refused() const { return refusal_.refused(); }
  [[nodiscard]] const Refusal& refusal() const { return refusal_; }
  [[nodiscard]] const std::vector<std::string>& bounties() const { return bountyIds_; }
  [[nodiscard]] const std::vector<std::string>& upgrades() const { return upgradeIds_; }
  [[nodiscard]] const std::vector<Appraisal>& appraisals() const { return appraisals_; }
  [[nodiscard]] const std::vector<std::string>& ships() const { return shipIds_; }
  // How many rows the service tab has. A counter service has one: the door.
  [[nodiscard]] int serviceRows() const;
  // The heading of the service tab: DRILLS, APPRAISAL, ARMOURY...
  [[nodiscard]] std::string serviceName() const;
  // Whether this person's service is opening the hub rather than a list here.
  [[nodiscard]] bool serviceIsCounter() const;

  [[nodiscard]] const std::string& notice() const { return notice_; }
  [[nodiscard]] float noticeAge() const { return noticeT_; }

private:
  void rebuild();
  Result act();
  void setNotice(const std::string& s) { notice_ = s; noticeT_ = 0.0f; }

  const Content* content_ = nullptr;
  Profile* profile_ = nullptr;
  const CrewDef* host_ = nullptr;
  bool open_ = false;
  Refusal refusal_;

  Tab tab_ = Tab::Service;
  int rows_[(int)Tab::Count] = {0, 0};
  std::vector<std::string> bountyIds_, upgradeIds_, shipIds_;
  std::vector<Appraisal> appraisals_;

  std::string notice_;
  float noticeT_ = 99.0f;

  bool prevUp_ = false, prevDown_ = false, prevLeft_ = false, prevRight_ = false;
  bool prevUse_ = false, prevClose_ = false, prevTab_ = false;
};
