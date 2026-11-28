// =============================================================================
//  GameServer.cpp — see GameServer.h for a client's life on the server.
// =============================================================================

#include "playground/game/GameServer.h"

#include "playground/core/Log.h"
#include "playground/ecs/World.h"
#include "playground/engine/Components.h"
#include "playground/engine/Systems.h"
#include "playground/game/InputQueue.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <format>
#include <numbers>
#include <random>
#include <string>
#include <unordered_map>

namespace playground::game {
namespace {

using log::Category;
using Clock = std::chrono::steady_clock;

// Squares bouncing round the world, from a fixed seed: every run, and so
// every client, sees the same.
void spawnBalls(ecs::World& world, u32 count) {
  std::mt19937 rng(20261127u);
  auto uniform = [&rng](f32 lo, f32 hi) { return std::uniform_real_distribution<f32>(lo, hi)(rng); };
  const glm::vec2 half = kWorldBounds.halfSize;
  for (u32 i = 0; i < count; ++i) {
    const ecs::Entity entity = world.create();
    const f32 heading = uniform(0.0f, 2.0f * std::numbers::pi_v<f32>);
    world.add<engine::Transform>(entity, engine::Transform{
        .position = {uniform(-half.x, half.x), uniform(-half.y, half.y)},
        .rotation = uniform(0.0f, std::numbers::pi_v<f32>)});
    world.add<engine::Velocity>(entity, engine::Velocity{
        .linear = uniform(60.0f, 260.0f) * glm::vec2{std::cos(heading), std::sin(heading)},
        .angular = uniform(-3.0f, 3.0f)});
    world.add<engine::Sprite>(entity, engine::Sprite{.size = {kBallSize, kBallSize}});
  }
}

struct Client {
  net::Peer peer;
  std::string address;
  std::string name;
  Clock::time_point handshakeDeadline;
  bool playing = false;
  bool leaving = false;  // told to go (rejected, timed out): waiting for the disconnect
  ecs::Entity player;
  PlayerState state;
  InputQueue inputs;
  // Delta snapshots: what was sent lately, and the newest the client says it has.
  std::deque<protocol::Snapshot> sent;  // oldest first, at most kSnapshotHistory
  u32 ackedSnapshotTick = 0;
  bool holdingSkip = false;  // the newest applied input's kButtonSkip
};

}  // namespace

struct GameServer::Impl {
  ServerConfig config;
  net::Host host;
  ecs::World world;
  std::unordered_map<u32, Client> clients;  // by peer id
  u32 tick = 0;
  u32 nextSlot = 0;  // spawn places, in joining order
  ServerStats stats;
  // The cutscene scheduled or playing, and the end tick once agreed.
  std::optional<protocol::CutsceneStart> cutscene;
  std::optional<u32> cutsceneEnd;
  u32 everyoneSkippingTicks = 0;

  [[nodiscard]] bool locked() const {
    return cutscene && tick >= cutscene->startTick &&
           tick < cutsceneEnd.value_or(cutscene->startTick + cutscene->durationTicks);
  }

  void broadcastReliable(const std::vector<std::byte>& bytes) {
    for (auto& [id, client] : clients) {
      if (client.playing && !client.leaving) sendReliable(client.peer, bytes);
    }
  }

  // Once a tick: the skip vote, and the end of the cutscene.
  void stepCutscene() {
    if (!cutscene) return;
    const u32 end = cutsceneEnd.value_or(cutscene->startTick + cutscene->durationTicks);
    if (tick >= end) {
      log::info(Category::Game, "cutscene {} over on tick {}{}", cutscene->sequenceId, tick,
                cutsceneEnd ? " (skipped by everyone)" : "");
      cutscene.reset();
      cutsceneEnd.reset();
      everyoneSkippingTicks = 0;
      return;
    }
    if (!locked() || cutsceneEnd) return;
    usize players = 0;
    usize skipping = 0;
    for (const auto& [id, client] : clients) {
      if (!client.playing || client.leaving) continue;
      ++players;
      skipping += client.holdingSkip ? 1 : 0;
    }
    everyoneSkippingTicks = players > 0 && skipping == players ? everyoneSkippingTicks + 1 : 0;
    if (everyoneSkippingTicks >= GameServer::kCutsceneSkipTicks) {
      cutsceneEnd = tick + GameServer::kCutsceneLeadTicks;
      ++stats.cutscenesSkipped;
      log::info(Category::Game, "cutscene {}: all {} player(s) held skip; it ends on tick {}", cutscene->sequenceId,
                players, *cutsceneEnd);
      broadcastReliable(protocol::encode(protocol::CutsceneEnd{.sequenceId = cutscene->sequenceId,
                                                               .endTick = *cutsceneEnd}));
    }
  }

  Impl(const ServerConfig& serverConfig, net::Host listening) : config(serverConfig), host(std::move(listening)) {}

  void sendReliable(net::Peer peer, std::vector<std::byte> bytes) {
    if (Status s = host.send(peer, net::Packet(net::Channel::Reliable, std::move(bytes))); !s) {
      log::warn(Category::Net, "server: could not send to client {}: {}", peer.id, s.error().message);
    }
  }

  // Every entity that exists, with its kind: what a new client is told.
  [[nodiscard]] protocol::Spawn everything() {
    protocol::Spawn spawn;
    world.each<engine::Transform>([&](ecs::Entity entity, engine::Transform&) {
      spawn.entities.push_back({.id = entity.index,
                                .kind = world.has<engine::PlayerTag>(entity) ? EntityKind::Player : EntityKind::Ball});
    });
    return spawn;
  }

  void onConnected(net::Peer peer) {
    const auto info = host.info(peer);
    Client client{.peer = peer, .address = info ? info->address : "?",
                  .handshakeDeadline = Clock::now() + std::chrono::milliseconds(config.handshakeTimeoutMs)};
    log::info(Category::Net, "client {} connected from {} ({} connected); waiting for its hello", peer.id,
              client.address, host.connectedPeers());
    clients.insert_or_assign(peer.id, std::move(client));
  }

  void onDisconnected(net::Peer peer) {
    const auto it = clients.find(peer.id);
    if (it == clients.end()) return;
    Client& client = it->second;
    log::info(Category::Net, "client {} ({}) disconnected ({} connected)", peer.id, client.address,
              host.connectedPeers());
    if (client.playing) {
      const u32 id = client.player.index;
      world.destroy(client.player);
      world.flush();
      client.playing = false;
      ++stats.left;
      const std::vector<std::byte> despawn = protocol::encode(protocol::Despawn{.ids = {id}});
      for (auto& [otherId, other] : clients) {
        if (otherId != peer.id && other.playing && !other.leaving) sendReliable(other.peer, despawn);
      }
      log::info(Category::Game, "player entity {} ({}) despawned; {} player(s) left", id, client.name,
                playerCount());
    }
    clients.erase(it);
  }

  void reject(Client& client, protocol::RejectReason reason, std::string_view detail) {
    log::warn(Category::Net, "client {} ({}) rejected: {} ({})", client.peer.id, client.address,
              protocol::describe(reason), detail);
    sendReliable(client.peer, protocol::encode(protocol::Reject{.reason = reason}));
    host.disconnectAfterSending(client.peer);  // the Reject first, then goodbye
    client.leaving = true;
    ++stats.rejected;
  }

  void onHello(Client& client, const protocol::Hello& hello) {
    if (client.playing || client.leaving) return;  // a second hello: ignore
    if (hello.version != protocol::kVersion) {
      reject(client, protocol::RejectReason::VersionMismatch,
             std::format("it speaks protocol {}, this server {}", hello.version, protocol::kVersion));
      return;
    }
    if (playerCount() >= config.maxClients) {
      reject(client, protocol::RejectReason::ServerFull, std::format("{} players", playerCount()));
      return;
    }

    // Its player: an entity with a Transform (so it is in every snapshot)
    // and a PlayerTag, but no Velocity: only inputs move it.
    client.name = hello.name.empty() ? std::format("client {}", client.peer.id) : hello.name;
    client.state = PlayerState{.position = spawnPosition(nextSlot++)};
    client.player = world.create();
    world.add<engine::Transform>(client.player, engine::Transform{.position = client.state.position});
    world.add<engine::PlayerTag>(client.player);
    client.playing = true;
    ++stats.joined;

    sendReliable(client.peer, protocol::encode(protocol::Welcome{
        .serverTick = tick, .playerId = client.player.index, .spawnPosition = client.state.position}));
    sendReliable(client.peer, protocol::encode(everything()));
    const std::vector<std::byte> spawn = protocol::encode(protocol::Spawn{
        .entities = {{.id = client.player.index, .kind = EntityKind::Player}}});
    for (auto& [otherId, other] : clients) {
      if (otherId != client.peer.id && other.playing && !other.leaving) sendReliable(other.peer, spawn);
    }
    // Joining mid-cutscene: into it, at the same tick as everyone else.
    if (cutscene) {
      sendReliable(client.peer, protocol::encode(*cutscene));
      if (cutsceneEnd) {
        sendReliable(client.peer, protocol::encode(protocol::CutsceneEnd{.sequenceId = cutscene->sequenceId,
                                                                         .endTick = *cutsceneEnd}));
      }
    }
    log::info(Category::Game, "client {} (\"{}\") joined as player entity {} at ({:.0f}, {:.0f}); {} player(s)",
              client.peer.id, client.name, client.player.index, client.state.position.x, client.state.position.y,
              playerCount());
  }

  void onReceived(net::Peer peer, const net::Packet& packet) {
    const auto it = clients.find(peer.id);
    if (it == clients.end()) return;
    Client& client = it->second;
    const auto kind = protocol::peekKind(packet.bytes());
    if (kind == protocol::MessageKind::Hello) {
      if (const auto hello = protocol::decodeHello(packet.bytes())) {
        onHello(client, *hello);
        return;
      }
    } else if (kind == protocol::MessageKind::Input) {
      if (const auto input = protocol::decodeInput(packet.bytes())) {
        if (client.playing && !client.leaving) {
          client.inputs.receive(input->newestSeq, input->inputs);
          // Inputs can arrive out of order: keep the newest claim.
          client.ackedSnapshotTick = std::max(client.ackedSnapshotTick, input->lastSnapshotTick);
        }
        return;
      }
    }
    ++stats.malformedPackets;
    log::debug(Category::Net, "server: dropped a malformed or unexpected {}-byte packet from client {}",
               packet.size(), peer.id);
  }

  void expireHandshakes() {
    const Clock::time_point now = Clock::now();
    for (auto& [id, client] : clients) {
      if (client.playing || client.leaving || now < client.handshakeDeadline) continue;
      log::warn(Category::Net, "client {} ({}) timed out in the handshake: no hello within {} ms", id,
                client.address, config.handshakeTimeoutMs);
      host.disconnect(client.peer);
      client.leaving = true;
      ++stats.handshakeTimeouts;
    }
  }

  // The world, quantized, once; then for each client a delta against the
  // newest snapshot it has told us it holds, if we still have that one too
  // (a client that claims a tick we never sent, or one too old, gets a full one).
  void sendSnapshots() {
    protocol::Snapshot snapshot{.tick = tick};
    world.each<engine::Transform>([&](ecs::Entity entity, engine::Transform& t) {
      snapshot.entities.push_back(protocol::quantizeEntity(entity.index, t.position, t.rotation));
    });
    std::sort(snapshot.entities.begin(), snapshot.entities.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    for (auto& [id, client] : clients) {
      if (!client.playing || client.leaving) continue;
      snapshot.lastAckedInputSeq = client.inputs.lastApplied();
      snapshot.yourPosition = client.state.position;
      const protocol::Snapshot* baseline = nullptr;
      if (config.deltaSnapshots && client.ackedSnapshotTick != 0) {
        const auto it = std::find_if(client.sent.begin(), client.sent.end(),
                                     [&](const protocol::Snapshot& s) { return s.tick == client.ackedSnapshotTick; });
        if (it != client.sent.end()) baseline = &*it;
      }
      std::vector<std::byte> bytes = protocol::encode(snapshot, baseline);
      const usize size = bytes.size();
      if (Status s = host.send(client.peer, net::Packet(net::Channel::Unreliable, std::move(bytes))); s) {
        ++stats.snapshotsSent;
        stats.deltaSnapshotsSent += baseline != nullptr ? 1 : 0;
        stats.snapshotBytesSent += size;
      }
      client.sent.push_back(snapshot);
      while (client.sent.size() > protocol::kSnapshotHistory) client.sent.pop_front();
    }
  }

  [[nodiscard]] usize playerCount() const noexcept {
    return static_cast<usize>(std::count_if(clients.begin(), clients.end(),
                                            [](const auto& entry) { return entry.second.playing; }));
  }
};

Result<std::unique_ptr<GameServer>> GameServer::start(const ServerConfig& config) {
  if (config.balls + config.maxClients > protocol::kMaxSnapshotEntities) {
    return fail(Errc::InvalidArgument, "server: {} balls and {} players would not fit in a snapshot (at most {})",
                config.balls, config.maxClients, protocol::kMaxSnapshotEntities);
  }
  // A few connection slots more than players, so a full server can still
  // say "full" instead of not answering.
  auto host = net::Host::listen(config.port, config.maxClients + 4);
  if (!host) return tl::unexpected(std::move(host.error()));
  host->simulate(config.network);
  auto impl = std::make_unique<Impl>(config, std::move(*host));
  spawnBalls(impl->world, config.balls);
  return std::unique_ptr<GameServer>(new GameServer(std::move(impl)));
}

GameServer::GameServer(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
GameServer::~GameServer() = default;

void GameServer::pumpNetwork(u32 waitMs) {
  for (const net::Event& e : impl_->host.service(waitMs)) {
    switch (e.type) {
      case net::EventType::Connected: impl_->onConnected(e.peer); break;
      case net::EventType::Disconnected: impl_->onDisconnected(e.peer); break;
      case net::EventType::Received: impl_->onReceived(e.peer, e.packet); break;
    }
  }
  impl_->expireHandshakes();
}

void GameServer::step() {
  // Players: each client's queued inputs, at most a few (InputQueue.h).
  // During a cutscene the inputs are taken (acknowledged, their skip votes
  // counted) but move no one.
  const bool locked = impl_->locked();
  for (auto& [id, client] : impl_->clients) {
    if (!client.playing || client.leaving) continue;
    client.inputs.tick([&client, locked](u32 /*seq*/, const PlayerInput& input) {
      client.holdingSkip = (input.buttons & protocol::kButtonSkip) != 0;
      if (!locked) client.state = stepPlayer(client.state, input);
    });
    impl_->world.get<engine::Transform>(client.player).position = client.state.position;
  }
  impl_->stepCutscene();
  // Balls: the same systems as the 2D demo.
  engine::movementSystem(impl_->world, kStepSeconds);
  engine::bounceSystem(impl_->world, kWorldBounds);
  impl_->world.flush();
  ++impl_->tick;
  if (impl_->tick % protocol::kSnapshotEveryTicks == 0) impl_->sendSnapshots();
  impl_->host.flush();  // out now, not at the next service()
}

void GameServer::shutdown(u32 waitMs) {
  impl_->host.disconnectAll();
  const auto deadline = Clock::now() + std::chrono::milliseconds(waitMs);
  // A peer saying goodbye is no longer "connected" at once, before the
  // goodbye has even gone: wait for its Disconnected event.
  while (impl_->host.knownPeers() > 0 && Clock::now() < deadline) {
    for (const net::Event& e : impl_->host.service(10)) {
      if (e.type == net::EventType::Disconnected) impl_->onDisconnected(e.peer);
    }
  }
}

Status GameServer::startCutscene(u32 sequenceId, u32 durationTicks, u32 leadTicks) {
  if (impl_->cutscene) {
    return fail(Errc::InvalidState, "server: cutscene {} is still playing", impl_->cutscene->sequenceId);
  }
  if (durationTicks == 0) return fail(Errc::InvalidArgument, "server: a cutscene needs a duration");
  impl_->cutscene = protocol::CutsceneStart{.sequenceId = sequenceId, .startTick = impl_->tick + leadTicks,
                                            .durationTicks = durationTicks};
  impl_->cutsceneEnd.reset();
  impl_->everyoneSkippingTicks = 0;
  ++impl_->stats.cutscenesStarted;
  impl_->broadcastReliable(protocol::encode(*impl_->cutscene));
  impl_->host.flush();
  log::info(Category::Game, "cutscene {} starts for {} player(s) on tick {} ({} ticks)", sequenceId,
            impl_->playerCount(), impl_->cutscene->startTick, durationTicks);
  return {};
}

std::optional<protocol::CutsceneStart> GameServer::cutscene() const { return impl_->cutscene; }
bool GameServer::cutsceneLocked() const noexcept { return impl_->locked(); }

u32 GameServer::tick() const noexcept { return impl_->tick; }
u16 GameServer::port() const noexcept { return impl_->host.port(); }
usize GameServer::playerCount() const noexcept { return impl_->playerCount(); }
usize GameServer::connectedCount() const noexcept { return impl_->host.connectedPeers(); }
usize GameServer::entityCount() const noexcept { return impl_->world.count<engine::Transform>(); }
const ServerStats& GameServer::stats() const noexcept { return impl_->stats; }

std::optional<glm::vec2> GameServer::playerPosition(u32 playerId) const {
  for (const auto& [id, client] : impl_->clients) {
    if (client.playing && client.player.index == playerId) return client.state.position;
  }
  return std::nullopt;
}

std::vector<ServerClientStats> GameServer::clientStats() const {
  std::vector<ServerClientStats> out;
  for (const auto& [id, client] : impl_->clients) {
    if (!client.playing) continue;
    const InputQueueStats& inputs = client.inputs.stats();
    out.push_back({.peer = id, .playerId = client.player.index, .lastAckedInputSeq = client.inputs.lastApplied(),
                   .inputsApplied = inputs.applied, .inputsDropped = inputs.dropped, .inputsSkipped = inputs.skipped,
                   .position = client.state.position});
  }
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.peer < b.peer; });
  return out;
}

}  // namespace playground::game
