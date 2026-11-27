// =============================================================================
//  Host.cpp — net::Host over ENet. The only file that includes <enet/enet.h>
//  (and, through it on Windows, <winsock2.h>).
//
//  PEERS are numbered by this Host (Peer::id, from 1, never reused) and
//  looked up in two maps; the game never holds an ENetPeer*. When ENet
//  reports a disconnect, the number is forgotten, so a stale Peer simply
//  matches nothing.
//
//  RECEIVED PACKETS are copied into a net::Packet and the ENetPacket freed at
//  once: game packets are small, and no ENet lifetime rule leaks out.
//
//  ENet's global start-up (enet_initialize, which on Windows starts
//  Winsock) is counted: the first Host starts it, the last one stops it.
//
//  SIMULATED CONDITIONS (simulate()): send() puts each packet in a queue
//  ordered by when it may leave, instead of handing it to ENet; service()
//  and flush() hand over whatever is due. Reliable packets to one peer
//  never overtake each other (each leaves no earlier than the one before),
//  unreliable ones may. ENet's own acknowledgements and pings are not
//  delayed, so PeerInfo::roundTripMs does not include the simulated delay.
// =============================================================================

#include "playground/net/Host.h"

#include "playground/core/Log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <unordered_map>
#include <utility>

#include <enet/enet.h>

namespace playground::net {
namespace {

using log::Category;
using Clock = std::chrono::steady_clock;

std::mutex gLibraryMutex;
int gLibraryUsers = 0;

Status acquireLibrary() {
  const std::scoped_lock lock(gLibraryMutex);
  if (gLibraryUsers == 0 && enet_initialize() != 0) {
    return fail(Errc::PlatformFailure, "ENet could not start (enet_initialize failed)");
  }
  ++gLibraryUsers;
  return {};
}

void releaseLibrary() noexcept {
  const std::scoped_lock lock(gLibraryMutex);
  if (--gLibraryUsers == 0) enet_deinitialize();
}

std::string addressString(const ENetAddress& address) {
  std::array<char, 64> ip{};
  if (enet_address_get_host_ip(&address, ip.data(), ip.size()) != 0) return "?";
  return std::string(ip.data()) + ":" + std::to_string(address.port);
}

}  // namespace

std::string_view describe(Channel channel) noexcept {
  return channel == Channel::Reliable ? "reliable" : "unreliable";
}

namespace {

// A packet (or, with no packet, a "disconnect once sent") waiting in the
// simulated network until `due`.
struct Delayed {
  Clock::time_point due;
  u64 order = 0;  // ties leave in the order sent
  u32 peer = 0;
  std::optional<Packet> packet;
};

struct LeavesLater {
  bool operator()(const Delayed& a, const Delayed& b) const noexcept {
    return a.due != b.due ? a.due > b.due : a.order > b.order;  // the top is the earliest
  }
};

Status sendNow(ENetPeer* target, u32 id, const Packet& packet) {
  if (target == nullptr || target->state != ENET_PEER_STATE_CONNECTED) {
    return fail(Errc::InvalidArgument, "network: peer {} is not connected", id);
  }
  const enet_uint32 flags =
      packet.channel() == Channel::Reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED;
  ENetPacket* raw = enet_packet_create(packet.bytes().data(), packet.size(), flags);  // copies the bytes
  if (raw == nullptr) return fail(Errc::IoFailure, "network: could not allocate a {}-byte packet", packet.size());
  if (enet_peer_send(target, static_cast<enet_uint8>(packet.channel()), raw) != 0) {
    enet_packet_destroy(raw);  // not queued: still ours
    return fail(Errc::IoFailure, "network: ENet refused a {}-byte {} packet for peer {}", packet.size(),
                describe(packet.channel()), id);
  }
  return {};  // queued: ENet owns it now
}

}  // namespace

struct Host::Impl {
  ENetHost* host = nullptr;
  std::unordered_map<ENetPeer*, u32> ids;
  std::unordered_map<u32, ENetPeer*> peers;
  u32 nextId = 1;

  // The simulated network (empty and unused while conditions are off).
  NetworkConditions conditions;
  SimulationStats simulationStats;
  std::mt19937 random{1u};
  std::priority_queue<Delayed, std::vector<Delayed>, LeavesLater> delayed;
  std::unordered_map<u32, Clock::time_point> lastReliableDue;  // per peer: keeps reliable packets in order
  std::unordered_map<u32, Clock::time_point> lastDue;          // per peer: anything
  u64 nextOrder = 0;

  Impl() = default;
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  ~Impl() {
    if (host != nullptr) enet_host_destroy(host);  // drops every peer without a goodbye
    releaseLibrary();
  }

  u32 remember(ENetPeer* peer) {
    if (const auto it = ids.find(peer); it != ids.end()) return it->second;
    const u32 id = nextId++;
    ids.emplace(peer, id);
    peers.emplace(id, peer);
    return id;
  }

  [[nodiscard]] ENetPeer* find(Peer peer) const {
    const auto it = peers.find(peer.id);
    return it == peers.end() ? nullptr : it->second;
  }

  void forget(u32 id) {
    peers.erase(id);
    lastReliableDue.erase(id);
    lastDue.erase(id);
  }

  // Into the simulated network: lost, or due after the latency and jitter.
  void delay(u32 peer, std::optional<Packet> packet) {
    const Clock::time_point now = Clock::now();
    auto wait = std::chrono::milliseconds(conditions.latencyMs);
    if (conditions.jitterMs > 0) {
      wait += std::chrono::milliseconds(std::uniform_int_distribution<u32>(0, conditions.jitterMs)(random));
    }
    const bool lost = conditions.lossPercent > 0.0f &&
                      std::uniform_real_distribution<f32>(0.0f, 100.0f)(random) < conditions.lossPercent;
    Clock::time_point due = now + wait;
    if (packet && packet->channel() == Channel::Unreliable) {
      if (lost) {
        ++simulationStats.dropped;
        return;
      }
    } else {
      if (lost && packet) {  // ENet would notice and resend it: a round trip later
        ++simulationStats.reliableHeldBack;
        due += 2 * std::chrono::milliseconds(std::max<u32>(conditions.latencyMs, 1));
      }
      Clock::time_point& previous = packet ? lastReliableDue[peer] : lastDue[peer];
      due = std::max(due, previous);  // never ahead of the reliable packet (or anything, for a goodbye) before it
      lastReliableDue[peer] = std::max(lastReliableDue[peer], due);
    }
    lastDue[peer] = std::max(lastDue[peer], due);
    ++simulationStats.delayed;
    delayed.push(Delayed{.due = due, .order = nextOrder++, .peer = peer, .packet = std::move(packet)});
  }

  // Hands ENet everything whose time has come.
  void releaseDue() {
    const Clock::time_point now = Clock::now();
    while (!delayed.empty() && delayed.top().due <= now) {
      const Delayed& next = delayed.top();
      ENetPeer* target = find(Peer{next.peer});
      if (next.packet) {
        if (target != nullptr) (void)sendNow(target, next.peer, *next.packet);  // gone meanwhile: lost
      } else if (target != nullptr) {
        enet_peer_disconnect_later(target, 0);
      }
      delayed.pop();
    }
  }

  // How long service() may wait before something in the queue falls due.
  [[nodiscard]] u32 waitLimitMs(u32 timeoutMs) const {
    if (delayed.empty()) return timeoutMs;
    const auto until = std::chrono::ceil<std::chrono::milliseconds>(delayed.top().due - Clock::now()).count();
    return static_cast<u32>(std::clamp<i64>(until, 0, timeoutMs));
  }

  static Result<std::unique_ptr<Impl>> create(const ENetAddress* address, u32 maxPeers) {
    if (Status s = acquireLibrary(); !s) return tl::unexpected(std::move(s.error()));
    auto impl = std::make_unique<Impl>();  // from here on, its destructor releases the library
    impl->host = enet_host_create(address, maxPeers, kChannelCount, 0, 0);
    if (impl->host == nullptr) {
      if (address != nullptr) {
        return fail(Errc::IoFailure, "could not listen on UDP port {} (already in use, or not allowed)",
                    address->port);
      }
      return fail(Errc::IoFailure, "could not create a network client (no UDP socket)");
    }
    return impl;
  }
};

Host::Host(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Host::Host(Host&& other) noexcept = default;
Host& Host::operator=(Host&& other) noexcept = default;
Host::~Host() = default;

Result<Host> Host::listen(u16 port, u32 maxPeers) {
  ENetAddress address{};
  address.host = ENET_HOST_ANY;
  address.port = port;
  auto impl = Impl::create(&address, maxPeers);
  if (!impl) return tl::unexpected(std::move(impl.error()));
  Host host(std::move(*impl));
  log::info(Category::Net, "network: listening on UDP port {} (up to {} peers)", host.port(), maxPeers);
  return host;
}

Result<Host> Host::client(u32 maxPeers) {
  auto impl = Impl::create(nullptr, maxPeers);
  if (!impl) return tl::unexpected(std::move(impl.error()));
  return Host(std::move(*impl));
}

Result<Peer> Host::connect(std::string_view hostName, u16 port) {
  ENetAddress address{};
  const std::string name(hostName);
  if (enet_address_set_host(&address, name.c_str()) != 0) {
    return fail(Errc::InvalidArgument, "network: could not resolve host \"{}\"", name);
  }
  address.port = port;
  ENetPeer* peer = enet_host_connect(impl_->host, &address, kChannelCount, 0);
  if (peer == nullptr) {
    return fail(Errc::IoFailure, "network: no free connection slot to connect to {}:{}", name, port);
  }
  const u32 id = impl_->remember(peer);
  log::info(Category::Net, "network: connecting to {}:{} (peer {})", name, port, id);
  return Peer{id};
}

std::vector<Event> Host::service(u32 timeoutMs) {
  std::vector<Event> events;
  impl_->releaseDue();
  ENetEvent event{};
  int result = enet_host_service(impl_->host, &event, impl_->waitLimitMs(timeoutMs));
  while (result > 0) {
    switch (event.type) {
      case ENET_EVENT_TYPE_CONNECT:
        events.push_back({.type = EventType::Connected, .peer = Peer{impl_->remember(event.peer)}});
        break;
      case ENET_EVENT_TYPE_RECEIVE: {
        const auto known = impl_->ids.find(event.peer);
        if (known != impl_->ids.end()) {
          const auto* data = reinterpret_cast<const std::byte*>(event.packet->data);
          std::vector<std::byte> bytes(data, data + event.packet->dataLength);
          const Channel channel = event.channelID == static_cast<u8>(Channel::Unreliable) ? Channel::Unreliable
                                                                                         : Channel::Reliable;
          events.push_back({.type = EventType::Received, .peer = Peer{known->second},
                            .packet = Packet(channel, std::move(bytes))});
        }
        enet_packet_destroy(event.packet);
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT: {
        const auto known = impl_->ids.find(event.peer);
        if (known != impl_->ids.end()) {
          const u32 id = known->second;
          impl_->forget(id);
          impl_->ids.erase(known);
          events.push_back({.type = EventType::Disconnected, .peer = Peer{id}});
        }
        break;
      }
      case ENET_EVENT_TYPE_NONE:
        break;
    }
    result = enet_host_service(impl_->host, &event, 0);  // collect what is already waiting, without waiting
  }
  if (result < 0) log::warn(Category::Net, "network: ENet reported a socket error while servicing");
  if (!impl_->delayed.empty()) {  // what fell due while waiting goes now, not at the next call
    impl_->releaseDue();
    enet_host_flush(impl_->host);
  }
  return events;
}

Status Host::send(Peer peer, const Packet& packet) {
  ENetPeer* target = impl_->find(peer);
  if (!impl_->conditions.active()) return sendNow(target, peer.id, packet);
  if (target == nullptr || target->state != ENET_PEER_STATE_CONNECTED) {
    return fail(Errc::InvalidArgument, "network: peer {} is not connected", peer.id);
  }
  impl_->delay(peer.id, packet);
  return {};
}

void Host::broadcast(const Packet& packet) {
  for (const auto& [id, peer] : impl_->peers) {
    if (peer->state == ENET_PEER_STATE_CONNECTED) (void)send(Peer{id}, packet);
  }
}

void Host::flush() {
  impl_->releaseDue();
  enet_host_flush(impl_->host);
}

void Host::disconnect(Peer peer) {
  if (ENetPeer* target = impl_->find(peer)) enet_peer_disconnect(target, 0);
}

void Host::disconnectAfterSending(Peer peer) {
  ENetPeer* target = impl_->find(peer);
  if (target == nullptr) return;
  if (impl_->delayed.empty()) {
    enet_peer_disconnect_later(target, 0);
  } else {
    impl_->delay(peer.id, std::nullopt);  // after what is still in the simulated network
  }
}

void Host::disconnectAll() {
  for (const auto& [id, peer] : impl_->peers) enet_peer_disconnect(peer, 0);
}

u16 Host::port() const noexcept { return impl_->host->address.port; }

usize Host::connectedPeers() const noexcept {
  usize count = 0;
  for (const auto& [id, peer] : impl_->peers) count += peer->state == ENET_PEER_STATE_CONNECTED ? 1 : 0;
  return count;
}

usize Host::knownPeers() const noexcept { return impl_->peers.size(); }

std::optional<PeerInfo> Host::info(Peer peer) const {
  const ENetPeer* target = impl_->find(peer);
  if (target == nullptr) return std::nullopt;
  return PeerInfo{.address = addressString(target->address), .roundTripMs = target->roundTripTime,
                  .connected = target->state == ENET_PEER_STATE_CONNECTED};
}

void Host::simulate(const NetworkConditions& conditions) {
  impl_->conditions = conditions;
  impl_->conditions.lossPercent = std::clamp(conditions.lossPercent, 0.0f, 100.0f);
  impl_->random.seed(conditions.seed);
  if (conditions.active()) {
    log::info(Category::Net, "network: simulating {} ms latency (+0..{} ms jitter) and {:.1f}% loss on port {}",
              conditions.latencyMs, conditions.jitterMs, impl_->conditions.lossPercent, port());
  }
}

const NetworkConditions& Host::simulated() const noexcept { return impl_->conditions; }

SimulationStats Host::simulationStats() const noexcept { return impl_->simulationStats; }

}  // namespace playground::net
