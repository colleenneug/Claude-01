#pragma once
// =============================================================================
//  playground/net/Host.h — sending messages between game processes.
//
//  Built on ENet (reliable UDP), entirely hidden: no ENet or socket header
//  appears here, so nothing that includes this file depends on them (the
//  implementation is behind a pointer, "pimpl").
//
//  A HOST is one end of the network: a server listening on a port, or a
//  client that connects out. Each connection to another host is a PEER.
//
//  TWO CHANNELS, because game messages come in two kinds:
//    Channel::Reliable    arrives, exactly once, in the order sent; resent
//                         until acknowledged. For what must not be lost:
//                         spawns, hits, chat, state changes.
//    Channel::Unreliable  sent once, may be lost, may arrive out of order,
//                         never waited for. For what is out of date by the
//                         time it would be resent: input every tick,
//                         position snapshots 20+ times a second. A lost one
//                         is simply replaced by the next.
//  (Reliable traffic on its own channel means a lost reliable message never
//  holds up the unreliable stream behind it.)
//
//  ONE THREAD: a Host and everything about it is used from one thread (ENet
//  hosts are not thread-safe). Call service() regularly, every frame or
//  tick: it sends what was queued, receives, resends, keeps connections
//  alive, and returns what happened.
//
//      auto server = net::Host::listen(27015);
//      for (const net::Event& e : server->service(5)) {
//        if (e.type == net::EventType::Received) handle(e.peer, e.packet);
//      }
//      server->send(peer, net::Packet(net::Channel::Unreliable, writer.take()));
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace playground::net {

enum class Channel : u8 { Reliable = 0, Unreliable = 1 };
inline constexpr u8 kChannelCount = 2;
[[nodiscard]] std::string_view describe(Channel channel) noexcept;

// A connection to another host: a number this Host gave it. Not an owner:
// the Host keeps the connection. A Peer of a connection that has ended
// matches nothing (numbers are never reused within a Host).
struct Peer {
  u32 id = 0;  // 0: none
  [[nodiscard]] constexpr bool valid() const noexcept { return id != 0; }
  friend constexpr bool operator==(Peer, Peer) = default;
};

// A message: bytes and the channel they travel on. Owns its bytes.
class Packet {
 public:
  Packet() = default;
  Packet(Channel channel, std::vector<std::byte> bytes) noexcept : channel_(channel), bytes_(std::move(bytes)) {}

  [[nodiscard]] Channel channel() const noexcept { return channel_; }
  [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }
  [[nodiscard]] usize size() const noexcept { return bytes_.size(); }

 private:
  Channel channel_ = Channel::Reliable;
  std::vector<std::byte> bytes_;
};

enum class EventType : u8 {
  Connected,     // a peer finished connecting (either side)
  Disconnected,  // a peer left, timed out, or a connect attempt failed
  Received,      // a packet arrived from a peer
};

struct Event {
  EventType type = EventType::Received;
  Peer peer;
  Packet packet;  // Received only
};

struct PeerInfo {
  std::string address;  // "127.0.0.1:52144"
  u32 roundTripMs = 0;  // ENet's smoothed estimate
  bool connected = false;
};

class Host {
 public:
  // A server on `port` (0: any free port; port() says which), accepting up
  // to `maxPeers` connections. Errc::IoFailure if the port cannot be used.
  [[nodiscard]] static Result<Host> listen(u16 port, u32 maxPeers = 32);
  // A client: makes outgoing connections only.
  [[nodiscard]] static Result<Host> client(u32 maxPeers = 1);

  Host(Host&& other) noexcept;
  Host& operator=(Host&& other) noexcept;
  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;
  ~Host();  // drops every connection at once (call disconnectAll + service first to say goodbye)

  // Starts connecting; an EventType::Connected (or Disconnected, if it
  // fails) arrives from service() later. Errc::InvalidArgument for a name
  // that does not resolve; Errc::IoFailure if no peer slot is free.
  [[nodiscard]] Result<Peer> connect(std::string_view hostName, u16 port);

  // Runs the network: waits up to `timeoutMs` for the first event (0: no
  // waiting), then collects every event already waiting.
  [[nodiscard]] std::vector<Event> service(u32 timeoutMs = 0);

  // Queues `packet` for `peer` (sent by the next service() or flush()).
  // Errc::InvalidArgument for an unknown or disconnected peer;
  // Errc::IoFailure if ENet refuses.
  [[nodiscard]] Status send(Peer peer, const Packet& packet);
  // To every connected peer.
  void broadcast(const Packet& packet);
  // Sends queued packets now, without receiving.
  void flush();

  // Politely: the peer is told, and a Disconnected event follows once it
  // has acknowledged (or after a timeout).
  void disconnect(Peer peer);
  void disconnectAll();

  [[nodiscard]] u16 port() const noexcept;  // the local port
  [[nodiscard]] usize connectedPeers() const noexcept;
  // Every peer this Host still knows: connected, connecting, or saying
  // goodbye. Zero once every Disconnected event has been delivered.
  [[nodiscard]] usize knownPeers() const noexcept;
  [[nodiscard]] std::optional<PeerInfo> info(Peer peer) const;

 private:
  struct Impl;
  explicit Host(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

}  // namespace playground::net
