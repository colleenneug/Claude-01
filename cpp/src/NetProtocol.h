#pragma once

#include "Net.h"

#include <cstdint>
#include <string>

// What the game and the server say to each other. Every packet starts with
// the magic and the protocol version, then a message type, then its body.
//
// The shape of it, like Destiny's patrol zones:
//   - Every place you can be — a planet, the hideout, the Cradle, a mission's
//     map — is split into instances. Arriving puts you in a random instance
//     of that place with room in it, so the players you see there are
//     whoever else happened to land; a squad mate already there pulls you
//     into theirs instead.
//   - A squad link joins players into a squad. How many fit depends on what
//     the squad is doing (squadLimit below).
//   - Friends: ask someone you can see, they accept, and from then on you
//     see where they are whenever you are both online.
namespace net {

constexpr uint32_t kMagic = 0x31425245u;   // "ERB1"
constexpr uint8_t kVersion = 1;
constexpr uint16_t kDefaultPort = 27960;
constexpr int kInstanceCap = 12;           // players in one instance of a place
constexpr double kTimeout = 10.0;          // seconds of silence before a player is dropped

enum class Msg : uint8_t {
  // client -> server
  Hello = 1,        // id, name, place, activity, squad hint
  State,            // where I am and what I am doing, ~15 times a second
  Bye,
  FindPlayer,       // name -> PlayerFound or a Notice
  FriendRequest,    // target id
  FriendAccept,     // requester id
  Watch,            // the friend ids whose presence I want
  SquadInvite,      // target id
  SquadAccept,      // inviter id
  SquadLeave,
  Hit,              // target id, damage, head: my round reached them (PvP)
  Kill,             // killer id: they put me down (PvP)
  // Co-op. The instance's host runs the Cybers; these three are sent to the
  // server and passed on unchanged (sender id included) to whom they concern.
  Hostiles,         // host -> everyone else: count, (index, pos, yaw, hp, state)...
  HostileHit,       // guest -> host: index, damage, head
  AllyDamage,       // host -> one player: target id, damage (a Cyber hit them)
  // server -> client
  Welcome = 64,     // the instance you are in
  Snapshot,         // everyone else in your instance
  PlayerFound,      // id, name
  FriendRequestFrom,// id, name
  FriendAdded,      // id, name
  Presence,         // friend id, online, place, instance, activity
  SquadInviteFrom,  // id, name
  Squad,            // squad id, limit, members
  Notice,           // a line of text for the corner of the screen
  HitFrom,          // attacker id, damage, head
  Score,            // limit, leader id, count, (id, name, downs)...
  MatchOver,        // winner name; scores reset a few seconds later
  Host,             // who runs the Cybers in your instance
};

// The most players a squad can hold for what it is doing. A campaign mission
// is a fireteam of three; open places and the player-versus-player modes
// take six; a Gambit-style match is four a side.
inline int squadLimit(const std::string& activity) {
  if (activity == "mission") return 3;
  if (activity == "gambit") return 4;
  return 6;   // hub, station, freeroam, planet, space, pvp
}

// A Cyber as the host sends it.
struct NetHostile {
  uint16_t index = 0;
  float x = 0, y = 0, z = 0, yaw = 0, hp = 1.0f;   // hp 0..1 of its maximum
  uint8_t state = 0;                               // HostileState
};

// One player as everyone else sees them.
struct PlayerState {
  uint64_t id = 0;
  std::string name;
  float x = 0, y = 0, z = 0, yaw = 0, pitch = 0;
  float hp = 1.0f;          // 0..1
  uint8_t flags = 0;        // bit 0: fired since the last state; bit 1: downed
  std::string weapon;       // its silhouette (rifle, sniper, ...)
  bool squadmate = false;   // filled in by the server per receiver
};

inline void header(Writer& w, Msg m) {
  w.u32(kMagic);
  w.u8(kVersion);
  w.u8((uint8_t)m);
}

// Reads the header; false if the packet is not ours or not this version.
inline bool header(Reader& r, Msg& m) {
  if (r.u32() != kMagic) return false;
  if (r.u8() != kVersion) return false;
  m = (Msg)r.u8();
  return r.ok;
}

}  // namespace net
