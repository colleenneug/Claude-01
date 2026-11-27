// Integration tests for the networked game: a real GameServer and real
// GameClients in this process, talking UDP over 127.0.0.1 through a BAD
// simulated network (net::NetworkConditions: tens of milliseconds of delay,
// jitter, lost packets, in both directions).
//
// Everything runs on this thread at real time: each 1/120 s every host is
// serviced, then the server and each client take one step. Checked:
//   - two clients join, move, and CONVERGE: once they stop, each one's
//     predicted position is exactly the server's, and each sees the other
//     where the server has it;
//   - a client leaving is despawned for the other;
//   - another protocol version is rejected, with the reason;
//   - a client that connects and says nothing is timed out;
//   - a client sending inputs four times too fast moves no faster;
//   - a ping too high for the prediction ring overflows it safely and
//     still converges.
//
// No window, no GPU. Port 0: the OS picks a free one.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/game/GameClient.h"
#include "playground/game/GameServer.h"
#include "playground/game/InputQueue.h"
#include "playground/game/Protocol.h"
#include "playground/game/Simulation.h"
#include "playground/net/Host.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <optional>
#include <thread>
#include <vector>

#include <glm/geometric.hpp>

using namespace playground;
using namespace playground::game;
using Clock = std::chrono::steady_clock;

namespace {

constexpr auto kTick = std::chrono::nanoseconds(1'000'000'000 / protocol::kTickRate);

// Runs up to `maxTicks` real-time ticks. Between ticks every host is
// serviced (`pump`); each tick, `tick(t)` steps whatever it likes. Stops
// early once `done()` is true. Returns whether it was.
bool runTicks(u32 maxTicks, const std::function<void()>& pump, const std::function<void(u32)>& tick,
              const std::function<bool()>& done = [] { return false; }) {
  auto next = Clock::now();
  for (u32 t = 0; t < maxTicks; ++t) {
    if (done()) return true;
    next += kTick;
    do {
      pump();
      std::this_thread::sleep_for(std::chrono::microseconds(300));
    } while (Clock::now() < next);
    tick(t);
  }
  return done();
}

// The usual case: a server and some GameClients, all pumped and stepped.
struct Game {
  std::unique_ptr<GameServer> server;
  std::vector<std::unique_ptr<GameClient>> clients;

  void pump() {
    server->pumpNetwork(0);
    for (auto& c : clients) c->pumpNetwork(0);
  }

  bool run(u32 ticks, const std::function<PlayerInput(usize client, u32 tick)>& input,
           const std::function<bool()>& done = [] { return false; }) {
    return runTicks(
        ticks, [this] { pump(); },
        [&](u32 t) {
          server->step();
          for (usize i = 0; i < clients.size(); ++i) clients[i]->step(input(i, t));
        },
        done);
  }

  bool idleUntil(u32 ticks, const std::function<bool()>& done) {
    return run(ticks, [](usize, u32) { return PlayerInput{}; }, done);
  }
};

std::unique_ptr<GameServer> startServer(ServerConfig config) {
  config.port = 0;
  auto server = GameServer::start(config);
  REQUIRE_MESSAGE(server.has_value(), (server ? "" : server.error().message));
  return std::move(*server);
}

std::unique_ptr<GameClient> connectClient(const GameServer& server, ClientConfig config) {
  config.host = "127.0.0.1";
  config.port = server.port();
  auto client = GameClient::connect(config);
  REQUIRE_MESSAGE(client.has_value(), (client ? "" : client.error().message));
  return std::move(*client);
}

std::optional<RemoteEntity> findEntity(const std::vector<RemoteEntity>& entities, u32 id) {
  for (const RemoteEntity& e : entities) {
    if (e.id == id) return e;
  }
  return std::nullopt;
}

// 40 ms each way, plus up to 10 ms of jitter, and one packet in ten lost.
net::NetworkConditions badNetwork(u32 seed) {
  return {.latencyMs = 40, .jitterMs = 10, .lossPercent = 10.0f, .seed = seed};
}

}  // namespace

TEST_CASE("a server and two clients converge over a laggy, lossy network; a leaver is despawned") {
  Game game;
  game.server = startServer({.balls = 64, .network = badNetwork(1)});
  game.clients.push_back(connectClient(*game.server, {.name = "circle", .network = badNetwork(2)}));
  game.clients.push_back(connectClient(*game.server, {.name = "zigzag", .network = badNetwork(3)}));
  GameClient& a = *game.clients[0];
  GameClient& b = *game.clients[1];

  // Join: both welcomed, each told of all 64 balls and both players.
  REQUIRE(game.idleUntil(360, [&] { return a.knownEntityCount() == 66 && b.knownEntityCount() == 66; }));
  REQUIRE(a.state() == ClientState::Playing);
  REQUIRE(b.state() == ClientState::Playing);
  CHECK(a.playerId() != b.playerId());
  CHECK(game.server->playerCount() == 2);
  const glm::vec2 aStart = a.predictedPosition();

  // Play: three seconds of A walking a circle and B a zigzag. Prediction
  // means each moves on its own screen at once, a full step per tick, with
  // no waiting for the server.
  f32 aFurthest = 0.0f;
  game.run(360, [&](usize client, u32 t) {
    if (client == 0) {
      aFurthest = std::max(aFurthest, glm::length(a.predictedPosition() - aStart));
      const f32 angle = static_cast<f32>(t) * 2.0f * std::numbers::pi_v<f32> / 180.0f;
      return PlayerInput{.move = {std::cos(angle), std::sin(angle)}};
    }
    return PlayerInput{.move = {(t / 40) % 2 == 0 ? 1.0f : -1.0f, 0.6f}};
  });
  CHECK(aFurthest > 100.0f);  // two circles of radius ~86: out and back

  // Stop, and give the network a second to deliver everything.
  game.idleUntil(120, [] { return false; });

  const auto serverA = game.server->playerPosition(a.playerId());
  const auto serverB = game.server->playerPosition(b.playerId());
  REQUIRE(serverA.has_value());
  REQUIRE(serverB.has_value());
  INFO("A predicted (" << a.predictedPosition().x << ", " << a.predictedPosition().y << "), server (" << serverA->x
                       << ", " << serverA->y << ")");
  INFO("B predicted (" << b.predictedPosition().x << ", " << b.predictedPosition().y << "), server (" << serverB->x
                       << ", " << serverB->y << ")");
  // Converged: the predictions ARE the server's answer (same rule, same inputs).
  CHECK(glm::length(a.predictedPosition() - *serverA) < 1e-3f);
  CHECK(glm::length(b.predictedPosition() - *serverB) < 1e-3f);
  CHECK(glm::length(b.predictedPosition() - b.serverPosition()) < 1e-3f);
  // And each sees the other where the server has it (within a snapshot's quantization).
  const auto aSeesB = findEntity(a.remoteEntities(), b.playerId());
  const auto bSeesA = findEntity(b.remoteEntities(), a.playerId());
  REQUIRE(aSeesB.has_value());
  REQUIRE(bSeesA.has_value());
  CHECK(aSeesB->kind == EntityKind::Player);
  CHECK(glm::length(aSeesB->position - *serverB) < 0.05f);
  CHECK(glm::length(bSeesA->position - *serverA) < 0.05f);
  // Our own player is predicted, not drawn from snapshots.
  CHECK_FALSE(findEntity(a.remoteEntities(), a.playerId()).has_value());
  CHECK(a.remoteEntities().size() == 65);  // 64 balls and B

  // The network really was bad: snapshots were lost, and still nothing went wrong.
  const ServerStats& server = game.server->stats();
  for (GameClient* client : {&a, &b}) {
    const ClientStats stats = client->stats();
    CHECK(stats.snapshots > 40);
    CHECK(stats.snapshots + stats.outOfOrder < server.snapshotsSent / 2);  // some never arrived
    CHECK(stats.malformed == 0);
    CHECK(client->lastAckedInputSeq() + 30 > client->inputSeq());  // acknowledged up to about a round trip ago
  }
  for (const ServerClientStats& c : game.server->clientStats()) {
    CHECK(c.inputsApplied + c.inputsDropped + c.inputsSkipped + 30 > c.lastAckedInputSeq);
    CHECK(c.inputsApplied > 400);
  }

  // B leaves: A is told, and stops drawing it.
  const u32 bId = b.playerId();
  b.disconnect(0);
  REQUIRE(game.idleUntil(240, [&] { return a.knownEntityCount() == 65 && game.server->playerCount() == 1; }));
  CHECK_FALSE(findEntity(a.remoteEntities(), bId).has_value());
  CHECK(a.stats().despawned == 1);
  CHECK(a.state() == ClientState::Playing);
}

TEST_CASE("a client speaking another protocol version is rejected, and told why") {
  Game game;
  game.server = startServer({});
  game.clients.push_back(connectClient(*game.server, {.protocolVersion = 1}));
  GameClient& old = *game.clients[0];
  REQUIRE(game.idleUntil(360, [&] { return old.state() == ClientState::Rejected; }));
  CHECK(old.failure().find("protocol version mismatch") != std::string::npos);
  CHECK(game.server->playerCount() == 0);
  CHECK(game.server->stats().rejected == 1);
  // Then the server says goodbye (after the Reject, not before it).
  REQUIRE(game.idleUntil(360, [&] { return game.server->connectedCount() == 0; }));
  CHECK(old.state() == ClientState::Rejected);
}

TEST_CASE("a client that connects but never says hello is disconnected after the handshake timeout") {
  auto server = startServer({.handshakeTimeoutMs = 300});
  auto silent = net::Host::client();
  REQUIRE(silent.has_value());
  REQUIRE(silent->connect("127.0.0.1", server->port()).has_value());
  const auto started = Clock::now();
  bool connected = false;
  bool dropped = false;
  REQUIRE(runTicks(
      600,
      [&] {
        server->pumpNetwork(0);
        for (const net::Event& e : silent->service(0)) {
          if (e.type == net::EventType::Connected) connected = true;
          if (e.type == net::EventType::Disconnected) dropped = true;
        }
      },
      [&](u32) { server->step(); }, [&] { return dropped; }));
  CHECK(connected);  // it was a real connection, just a silent one
  CHECK(Clock::now() - started >= std::chrono::milliseconds(300));
  CHECK(server->stats().handshakeTimeouts == 1);
  CHECK(server->playerCount() == 0);
}

TEST_CASE("a client sending inputs four times too fast moves no faster than an honest one") {
  auto server = startServer({});
  auto cheat = net::Host::client();
  REQUIRE(cheat.has_value());
  auto toServer = cheat->connect("127.0.0.1", server->port());
  REQUIRE(toServer.has_value());

  std::optional<protocol::Welcome> welcome;
  const auto pump = [&] {
    server->pumpNetwork(0);
    for (const net::Event& e : cheat->service(0)) {
      if (e.type == net::EventType::Connected) {
        REQUIRE(cheat->send(*toServer, net::Packet(net::Channel::Reliable, protocol::encode(protocol::Hello{}))));
      } else if (e.type == net::EventType::Received) {
        if (auto w = protocol::decodeWelcome(e.packet.bytes())) welcome = w;
      }
    }
  };
  REQUIRE(runTicks(360, pump, [&](u32) { server->step(); }, [&] { return welcome.has_value(); }));
  const glm::vec2 start = welcome->spawnPosition;

  // Four new inputs a tick, all "full speed right", each packet the newest 8.
  constexpr u32 kTicks = 240;
  u32 seq = 0;
  runTicks(kTicks, pump, [&](u32) {
    seq += 4;
    const protocol::Input input{.newestSeq = seq, .inputs = std::vector<PlayerInput>(8, PlayerInput{.move = {1.0f, 0.0f}})};
    (void)cheat->send(*toServer, net::Packet(net::Channel::Unreliable, protocol::encode(input)));
    cheat->flush();
    server->step();
  });
  const auto at = server->playerPosition(welcome->playerId);
  REQUIRE(at.has_value());
  const f32 travelled = at->x - start.x;
  // An honest client in the same time: one input a tick (and the bucket's small burst).
  const f32 honestMost = kPlayerSpeed * kStepSeconds * static_cast<f32>(kTicks + InputQueue::kBurst);
  INFO("travelled " << travelled << ", an honest client at most " << honestMost);
  CHECK(travelled <= honestMost + 0.01f);
  CHECK(travelled > 0.8f * honestMost);  // it was not simply frozen either
  const auto stats = server->clientStats();
  REQUIRE(stats.size() == 1);
  CHECK(stats[0].inputsDropped > 0);
}

TEST_CASE("a ping too high for the prediction ring overflows it safely, and still converges") {
  // 1.2 s each way: inputs come back acknowledged ~2.4 s later, longer than
  // the ring's 256 ticks (2.1 s).
  const net::NetworkConditions awful{.latencyMs = 1200};
  Game game;
  game.server = startServer({.network = awful});
  game.clients.push_back(connectClient(*game.server, {.connectTimeoutMs = 8000, .network = awful}));
  GameClient& client = *game.clients[0];
  REQUIRE(game.idleUntil(1200, [&] { return client.state() == ClientState::Playing; }));

  game.run(420, [](usize, u32 t) { return PlayerInput{.move = {t < 210 ? 1.0f : -0.5f, 0.3f}}; });
  CHECK(client.stats().prediction.overflows > 0);
  CHECK(client.stats().prediction.corrections > 0);  // it could not check its history, so it took the server's word

  // Standing still long enough for the last inputs to come back: exact again.
  game.idleUntil(480, [] { return false; });
  const auto server = game.server->playerPosition(client.playerId());
  REQUIRE(server.has_value());
  CHECK(glm::length(client.predictedPosition() - *server) < 1e-3f);
}
