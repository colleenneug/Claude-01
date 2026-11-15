#pragma once

#include "Net.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

// The game's side of online play. It knows nothing about missions or GL:
// main.cpp tells it where you are and what you are doing every frame, and
// reads back who else is here, your squad, your friends and anything the
// server wants you to know. Everything is non-blocking; with no server set
// up it does nothing at all and the game is the same single-player game.
namespace net {

struct Remote {
  uint64_t id = 0;
  std::string name;
  glm::vec3 pos{0.0f};       // where they are drawn: eased toward `target`
  glm::vec3 target{0.0f};    // the last position the server sent
  float yaw = 0.0f, pitch = 0.0f, hp = 1.0f;
  uint8_t flags = 0;
  std::string weapon;
  bool squadmate = false;
  double heard = 0.0;
  float firedT = 0.0f;       // muzzle flash left to show, seconds
  float walk = 0.0f;         // stride phase, from how far they moved
};

struct Friend {
  uint64_t id = 0;
  std::string name;
  bool online = false;
  std::string place, instance, activity;
};

struct Invite {
  uint64_t from = 0;
  std::string name;
  bool squad = false;        // a squad invite; otherwise a friend request
};

struct SquadMember {
  uint64_t id = 0;
  std::string name, place;
};

class Client {
public:
  // hostPort: "host" or "host:port". False if it cannot be looked up; the
  // client then stays off.
  bool configure(const std::string& hostPort, uint64_t id, const std::string& name);
  bool enabled() const { return enabled_; }
  // Heard from the server recently.
  bool connected() const;

  // Friends from the save file, so their presence can be watched.
  void setFriends(const std::vector<std::pair<uint64_t, std::string>>& friends);

  // Where you are. A change of place puts you in an instance of the new one.
  void setWhere(const std::string& place, const std::string& activity);
  void setSelf(const glm::vec3& feet, float yaw, float pitch, float hp01, bool fired, bool downed,
               const std::string& weapon);

  // Send and receive. Call once a frame.
  void update(float dt);
  void bye();

  // Social.
  void findPlayer(const std::string& name);
  void requestFriend(uint64_t id);
  void inviteToSquad(uint64_t id);
  void accept(const Invite& in);
  void leaveSquad();

  const std::vector<Remote>& remotes() const { return remotes_; }
  const std::vector<Friend>& friends() const { return friends_; }
  const std::vector<SquadMember>& squad() const { return squad_; }
  int squadLimit() const { return squadLimit_; }
  const std::string& instance() const { return instance_; }
  const std::string& place() const { return place_; }
  const std::string& activity() const { return activity_; }
  uint64_t id() const { return id_; }
  const std::string& name() const { return name_; }
  std::deque<Invite>& invites() { return invites_; }
  // Lines for the corner of the screen, newest last.
  const std::deque<std::pair<std::string, float>>& notices() const { return notices_; }
  void notice(const std::string& text);

  // Friends the server confirmed since the last call: the save file keeps
  // them.
  std::vector<std::pair<uint64_t, std::string>> takeNewFriends();
  // Set when FindPlayer came back.
  bool takeFound(uint64_t& id, std::string& name);

private:
  bool enabled_ = false;
  Socket sock_;
  Address server_;
  uint64_t id_ = 0;
  std::string name_;
  std::string place_, activity_;
  bool sentHello_ = false;
  double lastHeard_ = -1e9;
  double nextHello_ = 0.0, nextState_ = 0.0;

  glm::vec3 feet_{0.0f};
  float yaw_ = 0.0f, pitch_ = 0.0f, hp_ = 1.0f;
  uint8_t flags_ = 0;
  std::string weapon_;

  std::string instance_;
  std::vector<Remote> remotes_;
  std::vector<Friend> friends_;
  std::vector<SquadMember> squad_;
  int squadLimit_ = 6;
  std::deque<Invite> invites_;
  std::deque<std::pair<std::string, float>> notices_;
  std::vector<std::pair<uint64_t, std::string>> newFriends_;
  bool found_ = false;
  uint64_t foundId_ = 0;
  std::string foundName_;

  void sendHello();
  void sendWatch();
  void send(const Writer& w) { sock_.send(server_, w.b.data(), w.b.size()); }
  void handle(const uint8_t* data, size_t len);
};

// A fresh random id for a new record: 64 bits from the clock and the
// address of a stack variable, which is plenty to keep two players apart.
uint64_t freshId();

}  // namespace net
