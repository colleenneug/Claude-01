#include "NetClient.h"

#include "NetProtocol.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace net {

uint64_t freshId() {
  int local = 0;
  uint64_t x = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
  x ^= (uint64_t)(uintptr_t)&local * 0x9E3779B97F4A7C15ull;
  // splitmix64, so neighbouring clock values land far apart.
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  x ^= x >> 31;
  return x ? x : 1;
}

bool Client::configure(const std::string& hostPort, uint64_t id, const std::string& name) {
  enabled_ = false;
  if (hostPort.empty() || id == 0) return false;
  if (!resolve(hostPort, kDefaultPort, server_)) {
    std::fprintf(stderr, "[Net] could not find server '%s'\n", hostPort.c_str());
    return false;
  }
  if (!sock_.open(0)) {
    std::fprintf(stderr, "[Net] could not open a UDP socket\n");
    return false;
  }
  id_ = id;
  name_ = name.empty() ? std::string("STRIDER") : name;
  enabled_ = true;
  std::printf("[Net] online via %s as %s\n", server_.str().c_str(), name_.c_str());
  return true;
}

bool Client::connected() const { return enabled_ && now() - lastHeard_ < 4.0; }

void Client::notice(const std::string& text) {
  notices_.push_back({text, 6.0f});
  while (notices_.size() > 5) notices_.pop_front();
}

void Client::setFriends(const std::vector<std::pair<uint64_t, std::string>>& list) {
  std::vector<Friend> next;
  for (const auto& f : list) {
    Friend fr;
    fr.id = f.first;
    fr.name = f.second;
    for (const Friend& old : friends_) if (old.id == f.first) fr = old;
    fr.name = f.second;
    next.push_back(fr);
  }
  friends_ = next;
  if (enabled_) sendWatch();
}

void Client::setWhere(const std::string& place, const std::string& activity) {
  if (place == place_ && activity == activity_) return;
  const bool moved = place != place_;
  place_ = place;
  activity_ = activity;
  if (moved) {
    score_.clear();
    leader_ = 0;
    host_ = 0;
    hostiles_.clear();
    newHostiles_ = false;
    hostileHits_.clear();
    allyDamage_ = 0.0f;
    hits_.clear();
    remotes_.clear();     // whoever was here is not where you are going
    instance_.clear();
    nextHello_ = 0.0;     // say where you are now, now
  }
}

void Client::setSelf(const glm::vec3& feet, float yaw, float pitch, float hp01, bool fired, bool downed,
                     const std::string& weapon) {
  feet_ = feet;
  yaw_ = yaw;
  pitch_ = pitch;
  hp_ = hp01;
  if (fired) flags_ |= 1u;
  flags_ = (uint8_t)((flags_ & 1u) | (downed ? 2u : 0u));
  weapon_ = weapon;
}

void Client::sendHello() {
  Writer w;
  header(w, Msg::Hello);
  w.u64(id_);
  w.str(name_);
  w.str(place_);
  w.str(activity_);
  send(w);
  sentHello_ = true;
}

void Client::sendWatch() {
  Writer w;
  header(w, Msg::Watch);
  w.u64(id_);
  w.u16((uint16_t)std::min<size_t>(friends_.size(), 200));
  for (size_t i = 0; i < friends_.size() && i < 200; i++) w.u64(friends_[i].id);
  send(w);
}

void Client::update(float dt) {
  winnerT_ = std::max(0.0f, winnerT_ - dt);
  for (auto& n : notices_) n.second -= dt;
  while (!notices_.empty() && notices_.front().second <= 0.0f) notices_.pop_front();
  if (!enabled_ || place_.empty()) return;

  const double t = now();
  // Hello doubles as the keep-alive and the reconnect: if the server
  // restarted, it has never heard of us and drops our states until it does.
  if (t >= nextHello_) {
    sendHello();
    sendWatch();
    nextHello_ = t + (connected() ? 2.0 : 0.5);
  }
  if (t >= nextState_) {
    Writer w;
    header(w, Msg::State);
    w.u64(id_);
    w.f32(feet_.x); w.f32(feet_.y); w.f32(feet_.z);
    w.f32(yaw_); w.f32(pitch_);
    w.f32(hp_);
    w.u8(flags_);
    w.str(weapon_);
    w.str(activity_);
    send(w);
    flags_ &= ~1u;
    nextState_ = t + 1.0 / 15.0;
  }

  uint8_t buf[4096];
  Address from;
  int n;
  while ((n = sock_.recv(from, buf, sizeof(buf))) > 0) {
    if (from != server_) continue;
    handle(buf, (size_t)n);
  }

  // Ease everyone toward where the server last put them, and drop anyone
  // the snapshots stopped carrying.
  const float k = 1.0f - std::exp(-12.0f * dt);
  for (Remote& r : remotes_) {
    const glm::vec3 before = r.pos;
    const glm::vec3 d = r.target - r.pos;
    if (glm::dot(d, d) > 25.0f) r.pos = r.target;   // teleported: snap
    else r.pos += d * k;
    const glm::vec3 moved = r.pos - before;
    r.walk += std::sqrt(moved.x * moved.x + moved.z * moved.z) * 2.2f;
    r.firedT = std::max(0.0f, r.firedT - dt);
  }
  remotes_.erase(std::remove_if(remotes_.begin(), remotes_.end(),
                                [t](const Remote& r) { return t - r.heard > 1.5; }),
                 remotes_.end());
}

void Client::handle(const uint8_t* data, size_t len) {
  Reader r(data, len);
  Msg m;
  if (!header(r, m)) return;
  lastHeard_ = now();
  switch (m) {
    case Msg::Welcome: {
      r.u32();
      const std::string name = r.str();
      const uint16_t here = r.u16();
      if (!r.ok) return;
      if (name != instance_) {
        instance_ = name;
        notice(here > 1 ? name + " - " + std::to_string(here - 1) + " OTHER STRIDER" + (here > 2 ? "S" : "") + " HERE"
                        : name);
      }
      break;
    }
    case Msg::Snapshot: {
      const uint8_t count = r.u8();
      const double t = now();
      for (uint8_t i = 0; i < count && r.ok; i++) {
        const uint64_t id = r.u64();
        std::string name = r.str();
        glm::vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        const float yaw = r.f32(), pitch = r.f32(), hp = r.f32();
        const uint8_t flags = r.u8();
        std::string weapon = r.str();
        const bool mate = r.u8() != 0;
        if (!r.ok) break;
        auto it = std::find_if(remotes_.begin(), remotes_.end(), [id](const Remote& x) { return x.id == id; });
        if (it == remotes_.end()) {
          Remote nr;
          nr.id = id;
          nr.pos = p;
          remotes_.push_back(nr);
          it = remotes_.end() - 1;
        }
        it->name = name;
        it->target = p;
        it->yaw = yaw;
        it->pitch = pitch;
        it->hp = hp;
        it->flags = flags;
        it->weapon = weapon;
        it->squadmate = mate;
        it->heard = t;
        if (flags & 1u) it->firedT = 0.08f;
      }
      break;
    }
    case Msg::PlayerFound: {
      foundId_ = r.u64();
      foundName_ = r.str();
      found_ = r.ok;
      break;
    }
    case Msg::FriendRequestFrom:
    case Msg::SquadInviteFrom: {
      Invite in;
      in.from = r.u64();
      in.name = r.str();
      in.squad = m == Msg::SquadInviteFrom;
      if (!r.ok) return;
      for (const Invite& o : invites_) if (o.from == in.from && o.squad == in.squad) return;
      invites_.push_back(in);
      while (invites_.size() > 6) invites_.pop_front();
      notice(in.squad ? in.name + " INVITED YOU TO THEIR SQUAD - O TO ANSWER"
                      : in.name + " WANTS TO BE FRIENDS - O TO ANSWER");
      break;
    }
    case Msg::FriendAdded: {
      const uint64_t id = r.u64();
      const std::string name = r.str();
      if (!r.ok) return;
      bool have = false;
      for (const Friend& f : friends_) have |= f.id == id;
      if (!have) {
        Friend f;
        f.id = id;
        f.name = name;
        f.online = true;
        friends_.push_back(f);
        newFriends_.push_back({id, name});
        notice(name + " IS NOW YOUR FRIEND");
        sendWatch();
      }
      break;
    }
    case Msg::Presence: {
      const uint64_t id = r.u64();
      const bool on = r.u8() != 0;
      std::string place = r.str(), instance = r.str(), activity = r.str();
      if (!r.ok) return;
      for (Friend& f : friends_) {
        if (f.id != id) continue;
        if (on && !f.online) notice(f.name + " CAME ONLINE");
        f.online = on;
        f.place = place;
        f.instance = instance;
        f.activity = activity;
      }
      break;
    }
    case Msg::Squad: {
      const uint64_t squadId = r.u64();
      squadLimit_ = r.u8();
      const uint8_t n = r.u8();
      std::vector<SquadMember> next;
      for (uint8_t i = 0; i < n && r.ok; i++) {
        SquadMember sm;
        sm.id = r.u64();
        sm.name = r.str();
        sm.place = r.str();
        next.push_back(sm);
      }
      if (!r.ok) return;
      if (squadId == 0 || next.empty()) {
        if (!squad_.empty()) notice("SQUAD LINK CLOSED");
        squad_.clear();
      } else {
        if (squad_.size() < next.size()) {
          for (const SquadMember& sm : next) {
            bool had = false;
            for (const SquadMember& o : squad_) had |= o.id == sm.id;
            if (!had && sm.id != id_) notice(sm.name + " JOINED THE SQUAD");
          }
        }
        squad_ = next;
      }
      break;
    }
    case Msg::HitFrom: {
      IncomingHit h;
      h.from = r.u64();
      h.damage = r.f32();
      h.head = r.u8() != 0;
      if (r.ok) hits_.push_back(h);
      break;
    }
    case Msg::Score: {
      const int limit = r.u8();
      const uint64_t leader = r.u64();
      const uint8_t n = r.u8();
      std::vector<ScoreRow> rows;
      for (uint8_t i = 0; i < n && r.ok; i++) {
        ScoreRow row;
        row.id = r.u64();
        row.name = r.str();
        row.downs = r.u16();
        rows.push_back(row);
      }
      if (!r.ok) return;
      scoreLimit_ = limit;
      leader_ = leader;
      score_ = rows;
      break;
    }
    case Msg::Host: {
      const uint64_t h = r.u64();
      if (r.ok) host_ = h;
      break;
    }
    case Msg::Hostiles: {
      const uint64_t from = r.u64();
      if (from != host_) break;     // an old host's last word
      const uint16_t n = r.u16();
      std::vector<NetHostile> list;
      for (uint16_t i = 0; i < n && r.ok && i < 512; i++) {
        NetHostile h;
        h.index = r.u16();
        h.x = r.f32(); h.y = r.f32(); h.z = r.f32();
        h.yaw = r.f32();
        h.hp = r.u8() / 255.0f;
        h.state = r.u8();
        list.push_back(h);
      }
      if (!r.ok) break;
      hostiles_ = list;
      newHostiles_ = true;
      break;
    }
    case Msg::HostileHit: {
      HostileHitIn h;
      h.from = r.u64();
      h.index = r.u16();
      h.damage = r.f32();
      h.head = r.u8() != 0;
      if (r.ok && isHost()) hostileHits_.push_back(h);
      break;
    }
    case Msg::AllyDamage: {
      r.u64();                      // from (the host)
      const uint64_t target = r.u64();
      const float dmg = r.f32();
      if (r.ok && target == id_) allyDamage_ += std::clamp(dmg, 0.0f, 200.0f);
      break;
    }
    case Msg::MatchOver: {
      winner_ = r.str();
      winnerT_ = 8.0f;
      break;
    }
    case Msg::Notice: {
      const std::string text = r.str();
      if (r.ok) notice(text);
      break;
    }
    default:
      break;
  }
}

void Client::bye() {
  if (!enabled_) return;
  Writer w;
  header(w, Msg::Bye);
  w.u64(id_);
  send(w);
}

void Client::findPlayer(const std::string& name) {
  if (!enabled_) return;
  Writer w;
  header(w, Msg::FindPlayer);
  w.u64(id_);
  w.str(name);
  send(w);
}

void Client::requestFriend(uint64_t id) {
  if (!enabled_) return;
  Writer w;
  header(w, Msg::FriendRequest);
  w.u64(id_);
  w.u64(id);
  send(w);
}

void Client::inviteToSquad(uint64_t id) {
  if (!enabled_) return;
  Writer w;
  header(w, Msg::SquadInvite);
  w.u64(id_);
  w.u64(id);
  send(w);
}

void Client::accept(const Invite& in) {
  if (!enabled_) return;
  Writer w;
  header(w, in.squad ? Msg::SquadAccept : Msg::FriendAccept);
  w.u64(id_);
  w.u64(in.from);
  send(w);
}

void Client::leaveSquad() {
  if (!enabled_) return;
  Writer w;
  header(w, Msg::SquadLeave);
  w.u64(id_);
  send(w);
}

void Client::sendHit(uint64_t target, float damage, bool head) {
  if (!enabled_) return;
  Writer w;
  header(w, Msg::Hit);
  w.u64(id_);
  w.u64(target);
  w.f32(damage);
  w.u8(head ? 1 : 0);
  send(w);
}

void Client::sendKill(uint64_t killer, int limit) {
  if (!enabled_) return;
  Writer w;
  header(w, Msg::Kill);
  w.u64(id_);
  w.u64(killer);
  w.u8((uint8_t)std::clamp(limit, 1, 255));
  send(w);
}

void Client::sendHostiles(const std::vector<NetHostile>& list) {
  if (!enabled_) return;
  Writer w;
  header(w, Msg::Hostiles);
  w.u64(id_);
  const size_t n = std::min<size_t>(list.size(), 60);   // one packet's worth
  w.u16((uint16_t)n);
  for (size_t i = 0; i < n; i++) {
    const NetHostile& h = list[i];
    w.u16(h.index);
    w.f32(h.x); w.f32(h.y); w.f32(h.z);
    w.f32(h.yaw);
    w.u8((uint8_t)std::lround(std::clamp(h.hp, 0.0f, 1.0f) * 255.0f));
    w.u8(h.state);
  }
  send(w);
}

void Client::sendHostileHit(uint16_t index, float damage, bool head) {
  if (!enabled_) return;
  Writer w;
  header(w, Msg::HostileHit);
  w.u64(id_);
  w.u16(index);
  w.f32(damage);
  w.u8(head ? 1 : 0);
  send(w);
}

void Client::sendAllyDamage(uint64_t target, float damage) {
  if (!enabled_) return;
  Writer w;
  header(w, Msg::AllyDamage);
  w.u64(id_);
  w.u64(target);
  w.f32(damage);
  send(w);
}

bool Client::takeHostiles(std::vector<NetHostile>& out) {
  if (!newHostiles_) return false;
  newHostiles_ = false;
  out = hostiles_;
  return true;
}

std::vector<Client::HostileHitIn> Client::takeHostileHits() {
  std::vector<HostileHitIn> out;
  out.swap(hostileHits_);
  return out;
}

float Client::takeAllyDamage() {
  const float d = allyDamage_;
  allyDamage_ = 0.0f;
  return d;
}

std::vector<IncomingHit> Client::takeHits() {
  std::vector<IncomingHit> out;
  out.swap(hits_);
  return out;
}

std::vector<std::pair<uint64_t, std::string>> Client::takeNewFriends() {
  std::vector<std::pair<uint64_t, std::string>> out;
  out.swap(newFriends_);
  return out;
}

bool Client::takeFound(uint64_t& id, std::string& name) {
  if (!found_) return false;
  found_ = false;
  id = foundId_;
  name = foundName_;
  return true;
}

}  // namespace net
