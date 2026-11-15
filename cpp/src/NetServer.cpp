#include "NetServer.h"

#include "NetProtocol.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
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
  uint64_t follow = 0;         // arriving to join this player
  int team = 0;                // the Tuning: which side; 0 everywhere else
};

struct Instance {
  int id = 0;
  std::string place;
  int number = 1;              // the 1 in "DUST SHELF 1"
  std::vector<uint64_t> members;
  // PvP: downs per player, first to `limit` wins; the leader is the one the
  // song marks for everybody else.
  std::map<uint64_t, int> score;
  int limit = 10;
  uint64_t leader = 0;
  double resetAt = 0.0;        // a match just ended: scores clear at this time
  uint64_t hosts[2] = {0, 0};  // co-op: whose game runs the Cybers, per team
  uint64_t owner = 0;          // a story mission's instance is its squad's alone
  bool teams = false;          // the Tuning: two sides, each in its own copy
  int notes[2] = {0, 0};       // the Tuning: banked per team
};

std::string upper(std::string s) {
  for (char& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}

class Server {
public:
  explicit Server(uint16_t port) : port_(port) {
    if (const char* t = std::getenv("EREBUS_SERVER_TAGS")) tagsPath_ = t;
    loadTags();
  }

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
        resetMatches(t);
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

  // ---- gamer tags: who owns which NAME#1234, kept on disk so a tag
  // survives the server restarting.
  std::string tagsPath_ = "server_tags.txt";
  std::map<std::string, uint64_t> tagOwner_;
  std::map<uint64_t, std::string> tagOf_;

  void loadTags() {
    std::ifstream f(tagsPath_);
    std::string line;
    while (std::getline(f, line)) {
      std::istringstream ls(line);
      std::string hex, tag;
      ls >> hex;
      std::getline(ls, tag);
      const size_t a = tag.find_first_not_of(' ');
      if (a == std::string::npos) continue;
      tag = tag.substr(a);
      try {
        const uint64_t id = std::stoull(hex, nullptr, 16);
        tagOwner_[tag] = id;
        tagOf_[id] = tag;
      } catch (...) {
      }
    }
  }

  void saveTags() {
    std::ofstream f(tagsPath_, std::ios::trunc);
    for (const auto& kv : tagOf_) {
      char hex[24];
      std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)kv.first);
      f << hex << " " << kv.second << "\n";
    }
  }

  // Settles a player's tag from the name they asked for and the number they
  // had last time: kept if it is still theirs, a free one otherwise.
  void settleTag(Player& p, const std::string& asked, uint16_t wantNumber) {
    const std::string base = cleanTagName(asked);
    auto mine = tagOf_.find(p.id);
    if (mine != tagOf_.end() && tagName(mine->second) == base) {
      p.name = mine->second;
      return;
    }
    std::string tag;
    auto freeFor = [&](const std::string& t) {
      auto o = tagOwner_.find(t);
      return o == tagOwner_.end() || o->second == p.id;
    };
    if (wantNumber >= 1000 && wantNumber <= 9999 && freeFor(base + "#" + std::to_string(wantNumber)))
      tag = base + "#" + std::to_string(wantNumber);
    for (int k = 0; tag.empty() && k < 200; k++) {
      const std::string t = base + "#" + std::to_string(1000 + roll() % 9000);
      if (freeFor(t)) tag = t;
    }
    for (int n = 1000; tag.empty() && n <= 9999; n++) {
      const std::string t = base + "#" + std::to_string(n);
      if (freeFor(t)) tag = t;
    }
    if (tag.empty()) tag = base + "#0000";
    if (mine != tagOf_.end()) tagOwner_.erase(mine->second);
    tagOf_[p.id] = tag;
    tagOwner_[tag] = p.id;
    saveTags();
    p.name = tag;
  }

  void sendTag(const Player& p) {
    Writer w;
    header(w, Msg::Tag);
    w.str(p.name);
    const size_t h = p.name.find('#');
    w.u16((uint16_t)(h == std::string::npos ? 0 : std::atoi(p.name.c_str() + h + 1)));
    send(p, w);
  }

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
      if (in->score.erase(p.id)) {
        if (in->leader == p.id) in->leader = 0;
        sendScore(*in);
      }
      if (in->hosts[p.team] == p.id) {
        in->hosts[p.team] = 0;
        for (uint64_t mid : in->members) {
          auto m = players_.find(mid);
          if (m != players_.end() && m->second.team == p.team) { in->hosts[p.team] = mid; break; }
        }
        sendHost(*in);
      }
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
    // A story mission is not a public place: its instance belongs to the
    // squad that launched it (or to you alone), and strangers never land in it.
    const uint64_t owner = p.activity == "mission" ? (p.squad ? p.squad : p.id) : 0;
    // Joining someone: their instance, if it is this place and has room.
    if (p.follow && owner == 0) {
      auto f = players_.find(p.follow);
      if (f != players_.end() && f->second.place == p.place) {
        Instance* in = instanceById(f->second.instance);
        if (in && in->owner == 0 && hasRoom(*in)) target = in;
      }
    }
    if (!target && p.squad) {
      for (auto& kv : players_) {
        const Player& o = kv.second;
        if (o.id == p.id || o.squad != p.squad || o.place != p.place || o.instance < 0) continue;
        Instance* in = instanceById(o.instance);
        if (in && in->owner == owner && hasRoom(*in)) { target = in; break; }
      }
    }
    if (!target) {
      std::vector<Instance*> open;
      for (Instance& in : instances_) {
        if (in.place == p.place && in.owner == owner && hasRoom(in)) open.push_back(&in);
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
      in.owner = owner;
      in.teams = p.activity == "tuning";
      instances_.push_back(in);
      target = &instances_.back();
    }
    // A side for the Tuning: a squad mate's if they are here, else the
    // smaller one.
    p.team = 0;
    if (target->teams) {
      int count[2] = {0, 0}, mate = -1;
      for (uint64_t mid : target->members) {
        auto m = players_.find(mid);
        if (m == players_.end()) continue;
        count[m->second.team]++;
        if (p.squad && m->second.squad == p.squad) mate = m->second.team;
      }
      p.team = (mate >= 0 && count[mate] < kTeamCap) ? mate : (count[1] < count[0] ? 1 : 0);
    }
    target->members.push_back(p.id);
    p.instance = target->id;
    if (!target->hosts[p.team]) target->hosts[p.team] = p.id;
    Writer w;
    header(w, Msg::Welcome);
    w.u32((uint32_t)target->id);
    w.str(instanceName(*target));
    w.u16((uint16_t)target->members.size());
    send(p, w);
    if (p.activity == "pvp") sendScore(*target);
    sendHost(*target);
    if (target->teams) sendTeamScore(*target);
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

  // A whole tag finds exactly that player; a bare name finds whoever online
  // has it (the first, if several do).
  Player* byName(const std::string& name) {
    const std::string want = upper(name);
    for (auto& kv : players_) if (upper(kv.second.name) == want) return &kv.second;
    if (want.find('#') == std::string::npos) {
      for (auto& kv : players_) if (tagName(kv.second.name) == cleanTagName(want)) return &kv.second;
    }
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
      const std::string asked = r.str();
      std::string place = r.str(), activity = r.str();
      if (!r.ok) return;
      p.follow = r.u64();
      uint16_t wantNumber = r.u16();
      if (!r.ok) {
        p.follow = 0;
        wantNumber = 0;
      }
      const std::string before = p.name;
      if (fresh || tagName(p.name) != cleanTagName(asked)) settleTag(p, asked, wantNumber);
      if (fresh || p.name != before) sendTag(p);
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
      case Msg::Hit: {
        const uint64_t tid = r.u64();
        const float dmg = r.f32();
        const uint8_t head = r.u8();
        auto t = players_.find(tid);
        if (!r.ok || t == players_.end()) break;
        // Only between two players in the same PvP instance, and nothing
        // absurd: a client can lie, but not by a whole health bar per round.
        if (t->second.instance != p.instance || p.activity != "pvp" || t->second.activity != "pvp") break;
        Writer w;
        header(w, Msg::HitFrom);
        w.u64(id);
        w.f32(std::clamp(dmg, 0.0f, 250.0f));
        w.u8(head);
        send(t->second, w);
        break;
      }
      case Msg::Hostiles: {
        Instance* in = instanceById(p.instance);
        if (!in || in->hosts[p.team] != id) break;     // only the host speaks for the Cybers
        for (uint64_t mid : in->members) {
          if (mid == id) continue;
          auto m = players_.find(mid);
          if (m != players_.end() && m->second.team == p.team) sock_.send(m->second.addr, data, len);
        }
        break;
      }
      case Msg::HostileHit: {
        Instance* in = instanceById(p.instance);
        if (!in || !in->hosts[p.team] || in->hosts[p.team] == id) break;
        auto h = players_.find(in->hosts[p.team]);
        if (h != players_.end()) sock_.send(h->second.addr, data, len);
        break;
      }
      case Msg::AllyDamage: {
        Instance* in = instanceById(p.instance);
        const uint64_t tid = r.u64();
        auto t = players_.find(tid);
        if (!r.ok || !in || in->hosts[p.team] != id || t == players_.end() || t->second.instance != p.instance ||
            t->second.team != p.team)
          break;
        sock_.send(t->second.addr, data, len);
        break;
      }
      case Msg::Bank: {
        const int count = r.u8();
        const int limit = r.u8();
        Instance* in = instanceById(p.instance);
        if (!r.ok || !in || !in->teams || count <= 0 || in->resetAt > 0.0) break;
        if (limit > 0) in->limit = limit;
        const int t = p.team;
        in->notes[t] += std::min(count, 15);
        // A big deposit sends trouble across: 5 notes one screamer, 10 two,
        // 15 four.
        const int screamers = count >= 15 ? 4 : count >= 10 ? 2 : count >= 5 ? 1 : 0;
        if (screamers && in->hosts[1 - t]) {
          auto h = players_.find(in->hosts[1 - t]);
          if (h != players_.end()) {
            Writer w;
            header(w, Msg::Screamers);
            w.u8((uint8_t)screamers);
            send(h->second, w);
          }
          for (uint64_t mid : in->members) {
            auto m = players_.find(mid);
            if (m == players_.end()) continue;
            notice(m->second, m->second.team == t
                                  ? tagName(p.name) + " SENT " + std::to_string(screamers) + " SCREAMER" + (screamers > 1 ? "S" : "") + " ACROSS"
                                  : std::string(teamName(t)) + " SENT " + std::to_string(screamers) + " SCREAMER" + (screamers > 1 ? "S" : "") + " AT YOU");
          }
        }
        sendTeamScore(*in);
        if (in->notes[t] >= in->limit) {
          for (uint64_t mid : in->members) {
            auto m = players_.find(mid);
            if (m == players_.end()) continue;
            Writer w;
            header(w, Msg::MatchOver);
            w.str(teamName(t));
            send(m->second, w);
          }
          in->resetAt = now() + 10.0;
          std::printf("[Server] %s won the Tuning in %s\n", teamName(t), instanceName(*in).c_str());
          std::fflush(stdout);
        }
        break;
      }
      case Msg::Kill: {
        const uint64_t killer = r.u64();
        const int limit = r.u8();
        Instance* in = instanceById(p.instance);
        if (!r.ok || !in || p.activity != "pvp") break;
        if (limit > 0) in->limit = limit;
        auto k = players_.find(killer);
        if (in->resetAt > 0.0) break;            // between matches
        if (k == players_.end() || killer == id || k->second.instance != p.instance) {
          broadcastNotice(*in, tagName(p.name) + " WENT DOWN");
          break;
        }
        const int now_ = ++in->score[killer];
        in->score.emplace(id, 0);
        broadcastNotice(*in, tagName(k->second.name) + " DOWNED " + tagName(p.name));
        int best = 0;
        for (auto& sc : in->score) best = std::max(best, sc.second);
        if (now_ >= best) in->leader = killer;
        sendScore(*in);
        if (now_ >= in->limit) {
          for (uint64_t mid : in->members) {
            Writer w;
            header(w, Msg::MatchOver);
            w.str(k->second.name);
            send(players_[mid], w);
          }
          in->resetAt = now() + 8.0;
          std::printf("[Server] %s won in %s\n", k->second.name.c_str(), instanceName(*in).c_str());
          std::fflush(stdout);
        }
        break;
      }
      default:
        break;
    }
  }

  void place_(Player& p) { place(p); }

  void broadcastNotice(const Instance& in, const std::string& text) {
    for (uint64_t mid : in.members) {
      auto m = players_.find(mid);
      if (m != players_.end()) notice(m->second, text);
    }
  }

  bool hasRoom(const Instance& in) const {
    return (int)in.members.size() < (in.teams ? 2 * kTeamCap : kInstanceCap);
  }

  void sendHost(const Instance& in) {
    for (uint64_t mid : in.members) {
      auto m = players_.find(mid);
      if (m == players_.end()) continue;
      Writer w;
      header(w, Msg::Host);
      w.u64(in.hosts[m->second.team]);
      w.u8((uint8_t)m->second.team);
      send(m->second, w);
    }
  }

  void sendTeamScore(const Instance& in) {
    Writer w;
    header(w, Msg::TeamScore);
    w.u8((uint8_t)std::min(in.limit, 255));
    w.u16((uint16_t)in.notes[0]);
    w.u16((uint16_t)in.notes[1]);
    for (uint64_t mid : in.members) {
      auto m = players_.find(mid);
      if (m != players_.end()) send(m->second, w);
    }
  }

  void sendScore(const Instance& in) {
    Writer w;
    header(w, Msg::Score);
    w.u8((uint8_t)in.limit);
    w.u64(in.leader);
    std::vector<std::pair<uint64_t, int>> rows;
    for (uint64_t mid : in.members) {
      auto sc = in.score.find(mid);
      rows.push_back({mid, sc == in.score.end() ? 0 : sc->second});
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (rows.size() > 16) rows.resize(16);
    w.u8((uint8_t)rows.size());
    for (const auto& row : rows) {
      auto m = players_.find(row.first);
      w.u64(row.first);
      w.str(m != players_.end() ? m->second.name : std::string("?"));
      w.u16((uint16_t)row.second);
    }
    for (uint64_t mid : in.members) {
      auto m = players_.find(mid);
      if (m != players_.end()) send(m->second, w);
    }
  }

  void resetMatches(double t) {
    for (Instance& in : instances_) {
      if (in.resetAt <= 0.0 || t < in.resetAt) continue;
      in.resetAt = 0.0;
      in.score.clear();
      in.leader = 0;
      in.notes[0] = in.notes[1] = 0;
      if (in.teams) sendTeamScore(in);
      else sendScore(in);
      broadcastNotice(in, "NEW MATCH - FIRST TO " + std::to_string(in.limit));
    }
  }

  void snapshots() {
    for (const Instance& in : instances_) {
      for (uint64_t viewerId : in.members) {
        auto v = players_.find(viewerId);
        if (v == players_.end()) continue;
        Writer w;
        header(w, Msg::Snapshot);
        // In the Tuning each side fights in its own copy of the arena: you
        // see your team, not theirs.
        auto visible = [&](uint64_t oid) {
          return oid != viewerId && (!in.teams || players_[oid].team == v->second.team);
        };
        uint8_t count = 0;
        for (uint64_t oid : in.members) if (visible(oid)) count++;
        w.u8(count);
        for (uint64_t oid : in.members) {
          if (!visible(oid)) continue;
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
