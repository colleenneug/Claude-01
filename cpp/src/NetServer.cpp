#include "NetServer.h"

#include "NetProtocol.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <chrono>
#include <thread>
#include <vector>

// The server: one process that holds every place's instances, every squad,
// and who is online. It does not run the fights — each player's game does
// that — it decides who is with whom and passes everyone's position on to
// the others in their instance.
namespace net {
namespace {

struct Player {
  uint64_t id = 0;
  std::string name;
  Address addr;
  std::string place, activity;
  int instance = -1;
  uint64_t squad = 0;          // 0: on their own
  double lastHeard = 0.0;
  PlayerState state;
  std::vector<uint64_t> watch; // friends whose presence they want
};

struct Instance {
  int id = 0;
  std::string place;
  int number = 1;              // the 1 in "DUST SHELF 1"
  std::vector<uint64_t> members;
};

std::string upper(std::string s) {
  for (char& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}

class Server {
public:
  explicit Server(uint16_t port) : port_(port) {}

  bool start() {
    if (!sock_.open(port_)) {
      std::fprintf(stderr, "[Server] could not open UDP port %u\n", (unsigned)port_);
      return false;
    }
    std::printf("[Server] listening on UDP port %u\n", (unsigned)sock_.localPort());
    std::fflush(stdout);
    return true;
  }

  void run(double seconds) {
    const double end = seconds > 0.0 ? now() + seconds : 1e300;
    double nextSnap = 0.0, nextPresence = 0.0;
    uint8_t buf[2048];
    while (now() < end) {
      Address from;
      int n;
      while ((n = sock_.recv(from, buf, sizeof(buf))) > 0) handle(from, buf, (size_t)n);
      const double t = now();
      if (t >= nextSnap) {
        snapshots();
        nextSnap = t + 0.05;      // 20 a second
      }
      if (t >= nextPresence) {
        presence();
        dropSilent(t);
        nextPresence = t + 1.0;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

private:
  uint16_t port_;
  Socket sock_;
  std::map<uint64_t, Player> players_;
  std::vector<Instance> instances_;
  int nextInstance_ = 1;
  uint32_t rng_ = 0x2545f491u;

  uint32_t roll() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return rng_ >> 8;
  }

  void send(const Player& p, const Writer& w) { sock_.send(p.addr, w.b.data(), w.b.size()); }

  void notice(const Player& p, const std::string& text) {
    Writer w;
    header(w, Msg::Notice);
    w.str(text);
    send(p, w);
  }

  Instance* instanceById(int id) {
    for (Instance& in : instances_) if (in.id == id) return &in;
    return nullptr;
  }

  std::string instanceName(const Instance& in) {
    return upper(in.place) + " " + std::to_string(in.number);
  }

  void leaveInstance(Player& p) {
    if (Instance* in = instanceById(p.instance)) {
      in->members.erase(std::remove(in->members.begin(), in->members.end(), p.id), in->members.end());
    }
    p.instance = -1;
    // An empty instance goes; the next arrival gets a fresh one.
    instances_.erase(std::remove_if(instances_.begin(), instances_.end(),
                                    [](const Instance& in) { return in.members.empty(); }),
                     instances_.end());
  }

  // Where an arrival goes: into a squad mate's instance of the same place if
  // there is one with room; otherwise a random instance of that place with
  // room; otherwise a new one.
  void place(Player& p) {
    leaveInstance(p);
    Instance* target = nullptr;
    if (p.squad) {
      for (auto& kv : players_) {
        const Player& o = kv.second;
        if (o.id == p.id || o.squad != p.squad || o.place != p.place || o.instance < 0) continue;
        Instance* in = instanceById(o.instance);
        if (in && (int)in->members.size() < kInstanceCap) { target = in; break; }
      }
    }
    if (!target) {
      std::vector<Instance*> open;
      for (Instance& in : instances_) {
        if (in.place == p.place && (int)in.members.size() < kInstanceCap) open.push_back(&in);
      }
      if (!open.empty()) target = open[roll() % open.size()];
    }
    if (!target) {
      Instance in;
      in.id = nextInstance_++;
      in.place = p.place;
      int number = 1;
      for (const Instance& o : instances_) if (o.place == p.place) number = std::max(number, o.number + 1);
      in.number = number;
      instances_.push_back(in);
      target = &instances_.back();
    }
    target->members.push_back(p.id);
    p.instance = target->id;
    Writer w;
    header(w, Msg::Welcome);
    w.u32((uint32_t)target->id);
    w.str(instanceName(*target));
    w.u16((uint16_t)target->members.size());
    send(p, w);
    std::printf("[Server] %s -> %s (%zu here)\n", p.name.c_str(), instanceName(*target).c_str(),
                target->members.size());
    std::fflush(stdout);
  }

  std::vector<Player*> squadMembers(uint64_t squad) {
    std::vector<Player*> out;
    if (!squad) return out;
    for (auto& kv : players_) if (kv.second.squad == squad) out.push_back(&kv.second);
    return out;
  }

  // Everyone in a squad hears who is in it, and its limit for what the
  // leader (the squad's id) is doing.
  void squadUpdate(uint64_t squad) {
    std::vector<Player*> members = squadMembers(squad);
    if (members.size() == 1) {   // a squad of one is no squad
      Player& last = *members[0];
      last.squad = 0;
      Writer w;
      header(w, Msg::Squad);
      w.u64(0);
      w.u8((uint8_t)squadLimit(last.activity));
      w.u8(0);
      send(last, w);
      return;
    }
    auto it = players_.find(squad);
    const int limit = squadLimit(it != players_.end() ? it->second.activity : std::string("hub"));
    for (Player* m : members) {
      Writer w;
      header(w, Msg::Squad);
      w.u64(squad);
      w.u8((uint8_t)limit);
      w.u8((uint8_t)members.size());
      for (Player* o : members) {
        w.u64(o->id);
        w.str(o->name);
        w.str(o->place);
      }
      send(*m, w);
    }
  }

  void leaveSquad(Player& p) {
    const uint64_t old = p.squad;
    if (!old) return;
    p.squad = 0;
    Writer w;
    header(w, Msg::Squad);
    w.u64(0);
    w.u8((uint8_t)squadLimit(p.activity));
    w.u8(0);
    send(p, w);
    // If the leader leaves, the squad carries on under the next member.
    std::vector<Player*> rest = squadMembers(old);
    if (!rest.empty() && old == p.id) {
      for (Player* m : rest) m->squad = rest[0]->id;
      squadUpdate(rest[0]->id);
    } else if (!rest.empty()) {
      squadUpdate(old);
    }
  }

  Player* byName(const std::string& name) {
    const std::string want = upper(name);
    for (auto& kv : players_) if (upper(kv.second.name) == want) return &kv.second;
    return nullptr;
  }

  void handle(const Address& from, const uint8_t* data, size_t len) {
    Reader r(data, len);
    Msg m;
    if (!header(r, m)) return;
    const uint64_t id = r.u64();
    if (!r.ok || id == 0) return;

    if (m == Msg::Hello) {
      Player& p = players_[id];
      const bool fresh = p.id == 0;
      p.id = id;
      p.addr = from;
      p.name = r.str();
      std::string place = r.str(), activity = r.str();
      if (!r.ok) return;
      p.lastHeard = now();
      p.state.id = id;
      p.state.name = p.name;
      const bool moved = fresh || place != p.place || p.instance < 0;
      p.place = place;
      p.activity = activity;
      if (moved) place_(p);
      if (fresh) std::printf("[Server] %s is online\n", p.name.c_str());
      std::fflush(stdout);
      return;
    }

    auto it = players_.find(id);
    if (it == players_.end()) return;   // say hello first
    Player& p = it->second;
    p.addr = from;
    p.lastHeard = now();

    switch (m) {
      case Msg::State: {
        PlayerState s;
        s.id = id;
        s.name = p.name;
        s.x = r.f32(); s.y = r.f32(); s.z = r.f32();
        s.yaw = r.f32(); s.pitch = r.f32();
        s.hp = r.f32();
        s.flags = r.u8();
        s.weapon = r.str();
        const std::string activity = r.str();
        if (!r.ok) return;
        // A fired flag is sticky until the next snapshot carries it.
        const uint8_t fired = p.state.flags & 1u;
        p.state = s;
        p.state.flags |= fired;
        if (activity != p.activity) {
          p.activity = activity;
          if (p.squad) squadUpdate(p.squad);
        }
        break;
      }
      case Msg::Bye: {
        std::printf("[Server] %s left\n", p.name.c_str());
        std::fflush(stdout);
        leaveSquad(p);
        leaveInstance(p);
        players_.erase(it);
        break;
      }
      case Msg::FindPlayer: {
        const std::string name = r.str();
        Player* o = byName(name);
        if (!o || o->id == id) {
          notice(p, "NOBODY CALLED " + upper(name) + " IS ONLINE");
          break;
        }
        Writer w;
        header(w, Msg::PlayerFound);
        w.u64(o->id);
        w.str(o->name);
        send(p, w);
        break;
      }
      case Msg::FriendRequest: {
        auto t = players_.find(r.u64());
        if (t == players_.end()) { notice(p, "THEY ARE NOT ONLINE"); break; }
        Writer w;
        header(w, Msg::FriendRequestFrom);
        w.u64(id);
        w.str(p.name);
        send(t->second, w);
        notice(p, "FRIEND REQUEST SENT TO " + upper(t->second.name));
        break;
      }
      case Msg::FriendAccept: {
        auto t = players_.find(r.u64());
        if (t == players_.end()) break;
        for (int k = 0; k < 2; k++) {
          Player& to = k ? t->second : p;
          const Player& other = k ? p : t->second;
          Writer w;
          header(w, Msg::FriendAdded);
          w.u64(other.id);
          w.str(other.name);
          send(to, w);
        }
        break;
      }
      case Msg::Watch: {
        p.watch.clear();
        const uint16_t n = r.u16();
        for (uint16_t k = 0; k < n && r.ok && k < 200; k++) p.watch.push_back(r.u64());
        break;
      }
      case Msg::SquadInvite: {
        auto t = players_.find(r.u64());
        if (t == players_.end()) { notice(p, "THEY ARE NOT ONLINE"); break; }
        const uint64_t squad = p.squad ? p.squad : p.id;
        const int limit = squadLimit(players_.count(squad) ? players_[squad].activity : p.activity);
        const size_t have = std::max<size_t>(1, squadMembers(p.squad).size());
        if ((int)have >= limit) {
          notice(p, "SQUAD IS FULL FOR THIS ACTIVITY (" + std::to_string(limit) + ")");
          break;
        }
        Writer w;
        header(w, Msg::SquadInviteFrom);
        w.u64(id);
        w.str(p.name);
        send(t->second, w);
        notice(p, "SQUAD INVITE SENT TO " + upper(t->second.name));
        break;
      }
      case Msg::SquadAccept: {
        auto t = players_.find(r.u64());
        if (t == players_.end()) break;
        Player& leader = t->second;
        const uint64_t squad = leader.squad ? leader.squad : leader.id;
        const int limit = squadLimit(players_.count(squad) ? players_[squad].activity : leader.activity);
        if ((int)squadMembers(squad).size() >= limit && limit > 0 && squadMembers(squad).size() > 0) {
          notice(p, "THAT SQUAD IS FULL");
          break;
        }
        if (p.squad && p.squad != squad) leaveSquad(p);
        leader.squad = squad;
        p.squad = squad;
        squadUpdate(squad);
        // Same place: join them in their instance.
        if (p.place == leader.place && p.instance != leader.instance) place_(p);
        break;
      }
      case Msg::SquadLeave:
        leaveSquad(p);
        break;
      default:
        break;
    }
  }

  void place_(Player& p) { place(p); }

  void snapshots() {
    for (const Instance& in : instances_) {
      for (uint64_t viewerId : in.members) {
        auto v = players_.find(viewerId);
        if (v == players_.end()) continue;
        Writer w;
        header(w, Msg::Snapshot);
        uint8_t count = 0;
        for (uint64_t oid : in.members) if (oid != viewerId) count++;
        w.u8(count);
        for (uint64_t oid : in.members) {
          if (oid == viewerId) continue;
          const Player& o = players_[oid];
          const PlayerState& s = o.state;
          w.u64(o.id);
          w.str(o.name);
          w.f32(s.x); w.f32(s.y); w.f32(s.z);
          w.f32(s.yaw); w.f32(s.pitch); w.f32(s.hp);
          w.u8(s.flags);
          w.str(s.weapon);
          w.u8(o.squad && o.squad == v->second.squad ? 1 : 0);
        }
        send(v->second, w);
      }
      for (uint64_t oid : in.members) players_[oid].state.flags &= ~1u;   // fired: told
    }
  }

  void presence() {
    for (auto& kv : players_) {
      Player& p = kv.second;
      for (uint64_t fid : p.watch) {
        auto f = players_.find(fid);
        Writer w;
        header(w, Msg::Presence);
        w.u64(fid);
        const bool on = f != players_.end();
        w.u8(on ? 1 : 0);
        w.str(on ? f->second.place : std::string());
        const Instance* in = on ? instanceById(f->second.instance) : nullptr;
        w.str(in ? instanceName(*in) : std::string());
        w.str(on ? f->second.activity : std::string());
        send(p, w);
      }
    }
  }

  void dropSilent(double t) {
    std::vector<uint64_t> gone;
    for (auto& kv : players_) if (t - kv.second.lastHeard > kTimeout) gone.push_back(kv.first);
    for (uint64_t id : gone) {
      Player& p = players_[id];
      std::printf("[Server] %s timed out\n", p.name.c_str());
      leaveSquad(p);
      leaveInstance(p);
      players_.erase(id);
    }
  }
};

}  // namespace

int runServer(uint16_t port, double seconds) {
  Server s(port);
  if (!s.start()) return 1;
  s.run(seconds);
  return 0;
}

}  // namespace net
