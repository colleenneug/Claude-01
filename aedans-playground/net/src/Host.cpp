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
// =============================================================================

#include "playground/net/Host.h"

#include "playground/core/Log.h"

#include <array>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <enet/enet.h>

namespace playground::net {
namespace {

using log::Category;

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

struct Host::Impl {
  ENetHost* host = nullptr;
  std::unordered_map<ENetPeer*, u32> ids;
  std::unordered_map<u32, ENetPeer*> peers;
  u32 nextId = 1;

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
  ENetEvent event{};
  int result = enet_host_service(impl_->host, &event, timeoutMs);
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
          impl_->peers.erase(id);
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
  return events;
}

Status Host::send(Peer peer, const Packet& packet) {
  ENetPeer* target = impl_->find(peer);
  if (target == nullptr || target->state != ENET_PEER_STATE_CONNECTED) {
    return fail(Errc::InvalidArgument, "network: peer {} is not connected", peer.id);
  }
  const enet_uint32 flags =
      packet.channel() == Channel::Reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED;
  ENetPacket* raw = enet_packet_create(packet.bytes().data(), packet.size(), flags);  // copies the bytes
  if (raw == nullptr) return fail(Errc::IoFailure, "network: could not allocate a {}-byte packet", packet.size());
  if (enet_peer_send(target, static_cast<enet_uint8>(packet.channel()), raw) != 0) {
    enet_packet_destroy(raw);  // not queued: still ours
    return fail(Errc::IoFailure, "network: ENet refused a {}-byte {} packet for peer {}", packet.size(),
                describe(packet.channel()), peer.id);
  }
  return {};  // queued: ENet owns it now
}

void Host::broadcast(const Packet& packet) {
  for (const auto& [id, peer] : impl_->peers) {
    if (peer->state == ENET_PEER_STATE_CONNECTED) (void)send(Peer{id}, packet);
  }
}

void Host::flush() { enet_host_flush(impl_->host); }

void Host::disconnect(Peer peer) {
  if (ENetPeer* target = impl_->find(peer)) enet_peer_disconnect(target, 0);
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

}  // namespace playground::net
