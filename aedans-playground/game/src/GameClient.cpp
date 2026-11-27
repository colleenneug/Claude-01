// =============================================================================
//  GameClient.cpp — see GameClient.h.
// =============================================================================

#include "playground/game/GameClient.h"

#include "playground/core/Log.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <unordered_map>

#include <glm/geometric.hpp>

namespace playground::game {
namespace {

using log::Category;
using Clock = std::chrono::steady_clock;

// A correction's visual offset keeps this much of itself each tick: gone
// (below 1%) in about a quarter of a second.
constexpr f32 kCorrectionDecay = 0.85f;
// A bigger jump than this is a teleport, not a misprediction: shown at once.
constexpr f32 kMaxSmoothedCorrection = 120.0f;

}  // namespace

std::string_view describe(ClientState state) noexcept {
  switch (state) {
    case ClientState::Connecting: return "connecting";
    case ClientState::Handshaking: return "handshaking";
    case ClientState::Playing: return "playing";
    case ClientState::Rejected: return "rejected";
    case ClientState::Disconnected: return "disconnected";
  }
  return "?";
}

struct GameClient::Impl {
  ClientConfig config;
  net::Host host;
  net::Peer server;
  ClientState state = ClientState::Connecting;
  std::string failure;
  Clock::time_point deadline;

  u32 playerId = 0;
  Prediction prediction;
  glm::vec2 previousRender{0.0f};
  glm::vec2 currentRender{0.0f};
  glm::vec2 correctionOffset{0.0f};
  glm::vec2 serverPosition{0.0f};

  SnapshotBuffer snapshots;
  InterpolationClock clock;
  std::unordered_map<u32, EntityKind> known;  // what the server has spawned for us
  bool anySnapshot = false;
  u32 lastSnapshotTick = 0;
  f64 ackDelayMs = 0.0;
  ClientStats stats;

  Impl(const ClientConfig& clientConfig, net::Host clientHost, net::Peer serverPeer)
      : config(clientConfig), host(std::move(clientHost)), server(serverPeer),
        deadline(Clock::now() + std::chrono::milliseconds(clientConfig.connectTimeoutMs)) {}

  void sendReliable(std::vector<std::byte> bytes) {
    if (Status s = host.send(server, net::Packet(net::Channel::Reliable, std::move(bytes))); !s) {
      log::warn(Category::Net, "client: could not send: {}", s.error().message);
    }
  }

  void giveUp(std::string why) {
    log::error(Category::Net, "client: {}", why);
    failure = std::move(why);
    state = ClientState::Disconnected;
    host.disconnect(server);
  }

  void onWelcome(const protocol::Welcome& welcome) {
    if (state != ClientState::Handshaking) return;
    if (welcome.version != protocol::kVersion) {
      giveUp(std::format("the server speaks protocol {}, this client {}", welcome.version, protocol::kVersion));
      return;
    }
    playerId = welcome.playerId;
    prediction.reset(PlayerState{.position = welcome.spawnPosition});
    previousRender = currentRender = serverPosition = welcome.spawnPosition;
    known[playerId] = EntityKind::Player;
    state = ClientState::Playing;
    log::info(Category::Net, "client: welcomed: protocol {}, {} Hz, server tick {}, our player is entity {} at "
              "({:.0f}, {:.0f})", welcome.version, welcome.tickRate, welcome.serverTick, welcome.playerId,
              welcome.spawnPosition.x, welcome.spawnPosition.y);
  }

  void onSnapshot(const protocol::Snapshot& snapshot) {
    if (anySnapshot && snapshot.tick <= lastSnapshotTick) {  // overtaken by a newer one: old news
      ++stats.outOfOrder;
      return;
    }
    anySnapshot = true;
    lastSnapshotTick = snapshot.tick;
    ++stats.snapshots;
    snapshots.push(snapshot.tick, snapshot.entities);
    clock.heard(snapshot.tick);

    if (state != ClientState::Playing || !snapshot.yourPosition || snapshot.lastAckedInputSeq == 0) return;
    serverPosition = *snapshot.yourPosition;
    const glm::vec2 before = prediction.current().position;
    if (prediction.reconcile(snapshot.lastAckedInputSeq, PlayerState{.position = *snapshot.yourPosition})) {
      // Draw the player where it was, gliding to where it now is.
      correctionOffset += before - prediction.current().position;
      if (glm::length(correctionOffset) > kMaxSmoothedCorrection) correctionOffset = glm::vec2(0.0f);
      log::debug(Category::Game, "client: corrected by {:.2f} units at input {}",
                 glm::length(before - prediction.current().position), snapshot.lastAckedInputSeq);
    }
    const f64 delay = static_cast<f64>(prediction.newestSeq() - std::min(prediction.newestSeq(),
                                                                          snapshot.lastAckedInputSeq)) *
                      1000.0 / protocol::kTickRate;
    ackDelayMs = ackDelayMs == 0.0 ? delay : ackDelayMs + 0.1 * (delay - ackDelayMs);
  }

  void onReceived(const net::Packet& packet) {
    const auto bytes = packet.bytes();
    switch (protocol::peekKind(bytes).value_or(protocol::MessageKind::Hello)) {
      case protocol::MessageKind::Welcome:
        if (const auto m = protocol::decodeWelcome(bytes)) return onWelcome(*m);
        break;
      case protocol::MessageKind::Reject:
        if (const auto m = protocol::decodeReject(bytes)) {
          failure = std::format("rejected by the server: {} (it speaks protocol {}, this client {})",
                                protocol::describe(m->reason), m->serverVersion, config.protocolVersion);
          log::error(Category::Net, "client: {}", failure);
          state = ClientState::Rejected;  // the server says goodbye next
          return;
        }
        break;
      case protocol::MessageKind::Spawn:
        if (const auto m = protocol::decodeSpawn(bytes)) {
          for (const protocol::SpawnEntry& e : m->entities) known[e.id] = e.kind;
          stats.spawned += m->entities.size();
          return;
        }
        break;
      case protocol::MessageKind::Despawn:
        if (const auto m = protocol::decodeDespawn(bytes)) {
          for (const u32 id : m->ids) known.erase(id);
          stats.despawned += m->ids.size();
          return;
        }
        break;
      case protocol::MessageKind::Snapshot:
        if (const auto m = protocol::decodeSnapshot(bytes)) return onSnapshot(*m);
        break;
      case protocol::MessageKind::Hello:
      case protocol::MessageKind::Input:
        break;  // not for a client
    }
    ++stats.malformed;
    log::debug(Category::Net, "client: dropped a malformed or unexpected {}-byte packet", packet.size());
  }
};

Result<std::unique_ptr<GameClient>> GameClient::connect(const ClientConfig& config) {
  auto host = net::Host::client();
  if (!host) return tl::unexpected(std::move(host.error()));
  host->simulate(config.network);
  auto server = host->connect(config.host, config.port);
  if (!server) return tl::unexpected(std::move(server.error()));
  return std::unique_ptr<GameClient>(new GameClient(std::make_unique<Impl>(config, std::move(*host), *server)));
}

GameClient::GameClient(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
GameClient::~GameClient() = default;

void GameClient::pumpNetwork(u32 waitMs) {
  Impl& m = *impl_;
  for (const net::Event& e : m.host.service(waitMs)) {
    if (e.type == net::EventType::Connected) {
      m.state = ClientState::Handshaking;
      log::info(Category::Net, "client: connected to {}:{}; saying hello (protocol {})", m.config.host, m.config.port,
                m.config.protocolVersion);
      m.sendReliable(protocol::encode(protocol::Hello{.version = m.config.protocolVersion, .name = m.config.name}));
      m.host.flush();
    } else if (e.type == net::EventType::Disconnected) {
      if (m.state == ClientState::Connecting) {
        m.failure = std::format("could not connect to {}:{}", m.config.host, m.config.port);
      } else if (m.state != ClientState::Rejected && m.state != ClientState::Disconnected) {
        m.failure = "the server closed the connection";
      }
      if (m.state != ClientState::Rejected) m.state = ClientState::Disconnected;
      if (!m.failure.empty()) log::warn(Category::Net, "client: {}", m.failure);
    } else {
      m.onReceived(e.packet);
    }
  }
  if ((m.state == ClientState::Connecting || m.state == ClientState::Handshaking) && Clock::now() > m.deadline) {
    m.giveUp(std::format("no {} from {}:{} within {} ms", m.state == ClientState::Connecting ? "answer" : "welcome",
                         m.config.host, m.config.port, m.config.connectTimeoutMs));
  }
}

void GameClient::step(const PlayerInput& input) {
  Impl& m = *impl_;
  m.clock.advance();
  if (m.state != ClientState::Playing) return;

  // Predict: this tick's input, as the server will see it, applied now.
  const u32 seq = m.prediction.newestSeq() + 1;
  m.prediction.predict(seq, protocol::quantize(input));
  m.correctionOffset *= kCorrectionDecay;
  m.previousRender = m.currentRender;
  m.currentRender = m.prediction.current().position + m.correctionOffset;

  // Send it, with the few before it the server has not acknowledged yet.
  const u32 oldest = std::max(m.prediction.lastAcked() + 1, seq + 1 - std::min(seq, protocol::kMaxInputsPerPacket));
  const protocol::Input message{.newestSeq = seq, .inputs = m.prediction.inputsSince(oldest)};
  if (Status s = m.host.send(m.server, net::Packet(net::Channel::Unreliable, protocol::encode(message))); s) {
    ++m.stats.inputPackets;
  }
  m.host.flush();
}

void GameClient::disconnect(u32 waitMs) {
  Impl& m = *impl_;
  if (m.host.knownPeers() == 0) return;
  m.host.disconnect(m.server);
  const auto deadline = Clock::now() + std::chrono::milliseconds(waitMs);
  while (m.host.knownPeers() > 0 && Clock::now() < deadline) (void)m.host.service(10);
  if (m.state != ClientState::Rejected) m.state = ClientState::Disconnected;
}

ClientState GameClient::state() const noexcept { return impl_->state; }
const std::string& GameClient::failure() const noexcept { return impl_->failure; }
u32 GameClient::playerId() const noexcept { return impl_->playerId; }
glm::vec2 GameClient::serverPosition() const noexcept { return impl_->serverPosition; }
glm::vec2 GameClient::predictedPosition() const noexcept { return impl_->prediction.current().position; }
usize GameClient::knownEntityCount() const noexcept { return impl_->known.size(); }
u32 GameClient::lastServerTick() const noexcept { return impl_->lastSnapshotTick; }
f64 GameClient::interpolationTick() const noexcept { return impl_->clock.renderTick(); }
u32 GameClient::inputSeq() const noexcept { return impl_->prediction.newestSeq(); }
u32 GameClient::lastAckedInputSeq() const noexcept { return impl_->prediction.lastAcked(); }
f64 GameClient::ackDelayMs() const noexcept { return impl_->ackDelayMs; }

glm::vec2 GameClient::renderPosition(f32 alpha) const noexcept {
  return impl_->previousRender + (impl_->currentRender - impl_->previousRender) * std::clamp(alpha, 0.0f, 1.0f);
}

std::vector<RemoteEntity> GameClient::remoteEntities(f32 alpha) const {
  const Impl& m = *impl_;
  std::vector<RemoteEntity> out;
  if (!m.clock.started()) return out;
  // Between the previous tick's render time and this one's.
  for (const EntitySample& s : m.snapshots.sample(m.clock.renderTick() - 1.0 + static_cast<f64>(alpha))) {
    if (s.id == m.playerId && m.state == ClientState::Playing) continue;  // ours is predicted, not interpolated
    const auto kind = m.known.find(s.id);
    if (kind == m.known.end()) continue;  // not spawned (yet), or despawned
    out.push_back({.id = s.id, .kind = kind->second, .position = s.position, .rotation = s.rotation});
  }
  return out;
}

ClientStats GameClient::stats() const noexcept {
  ClientStats stats = impl_->stats;
  stats.prediction = impl_->prediction.stats();
  return stats;
}

}  // namespace playground::game
