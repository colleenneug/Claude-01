// =============================================================================
//  Server.cpp — the dedicated server and a headless test client (see Server.h).
//
//  THE SERVER LOOP, with no window and no GPU:
//
//    while not stopped (Ctrl+C / SIGTERM, or --frames N ticks):
//      1. service the network, waiting at most until the next tick is due:
//         the wait for packets IS the sleep, so the loop neither spins a
//         core nor oversleeps a tick;
//      2. FrameClock + FixedStepAccumulator (core/FixedStep.h, the same
//         rules as the windowed game): run 0..n 120 Hz steps of the
//         simulation, broadcasting a snapshot every 6th tick (20 Hz);
//      3. every 5 s, a status line.
//    then: tell every client goodbye and give them up to half a second to
//    acknowledge, so they see a clean disconnect, not a timeout.
// =============================================================================

#include "Server.h"

#include "Protocol.h"

#include "playground/core/FixedStep.h"
#include "playground/core/FrameClock.h"
#include "playground/core/Interrupt.h"
#include "playground/core/Log.h"
#include "playground/ecs/World.h"
#include "playground/engine/Components.h"
#include "playground/engine/Systems.h"
#include "playground/engine/WorldBounds.h"
#include "playground/net/Host.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <random>
#include <unordered_map>

namespace playground::app {
namespace {

using log::Category;
using Clock = std::chrono::steady_clock;

// What the server simulates: squares bouncing round the same world as the 2D
// demo, from a fixed seed (every run, and every client, sees the same).
void spawn(ecs::World& world, u32 count, glm::vec2 halfArea) {
  std::mt19937 rng(20261127u);
  auto uniform = [&rng](f32 lo, f32 hi) { return std::uniform_real_distribution<f32>(lo, hi)(rng); };
  for (u32 i = 0; i < count; ++i) {
    const ecs::Entity entity = world.create();
    const f32 heading = uniform(0.0f, 2.0f * std::numbers::pi_v<f32>);
    world.add<engine::Transform>(entity, engine::Transform{
        .position = {uniform(-halfArea.x, halfArea.x), uniform(-halfArea.y, halfArea.y)},
        .rotation = uniform(0.0f, std::numbers::pi_v<f32>)});
    world.add<engine::Velocity>(entity, engine::Velocity{
        .linear = uniform(60.0f, 260.0f) * glm::vec2{std::cos(heading), std::sin(heading)},
        .angular = uniform(-3.0f, 3.0f)});
    world.add<engine::Sprite>(entity, engine::Sprite{.size = {16.0f, 16.0f}});
  }
}

protocol::Snapshot snapshotOf(ecs::World& world, u32 tick) {
  protocol::Snapshot snapshot{.tick = tick};
  world.each<engine::Transform>([&](ecs::Entity entity, engine::Transform& t) {
    if (snapshot.entities.size() < protocol::kMaxSnapshotEntities) {
      snapshot.entities.push_back({.id = entity.index, .position = t.position, .rotation = t.rotation});
    }
  });
  return snapshot;
}

// Says goodbye to everyone and waits (up to `limitMs`) until each has
// answered: a peer saying goodbye is no longer "connected" at once, before
// the goodbye has even been sent, so wait for its Disconnected event.
void disconnectEveryone(net::Host& host, u32 limitMs) {
  host.disconnectAll();
  const auto deadline = Clock::now() + std::chrono::milliseconds(limitMs);
  while (host.knownPeers() > 0 && Clock::now() < deadline) {
    for (const net::Event& e : host.service(10)) {
      if (e.type == net::EventType::Disconnected) log::info(Category::Net, "peer {} said goodbye", e.peer.id);
    }
  }
}

}  // namespace

int runServer(const ServerOptions& options) {
  InterruptGuard interrupts;  // Ctrl+C / SIGTERM: stop in order, not mid-tick
  auto host = net::Host::listen(options.port, options.maxClients);
  if (!host) {
    log::critical(Category::Net, "dedicated server could not start: {}", host.error().message);
    return 1;
  }

  const engine::WorldBounds bounds{.halfSize = {800.0f, 450.0f}};
  ecs::World world;
  spawn(world, options.entities, bounds.halfSize);
  FrameClock clock;
  FixedStepAccumulator steps;
  const u16 tickRate = static_cast<u16>(std::lround(1.0 / steps.stepSeconds()));
  log::info(Category::Game, "dedicated server: {} Hz, UDP port {}, {} entities, up to {} clients; Ctrl+C stops it",
            tickRate, host->port(), options.entities, options.maxClients);
  log::info(Category::Engine, "entering server loop");

  u32 tick = 0;
  u64 snapshotsSent = 0;
  auto lastStatus = Clock::now();
  u32 ticksAtLastStatus = 0;
  std::unordered_map<u32, std::string> clients;  // peer id -> address, for the log

  while (!InterruptGuard::interruptRequested() && (options.maxTicks == 0 || tick < options.maxTicks)) {
    // 1. The network, waiting until (about) the next tick is due.
    const f64 untilNextTick = std::max(0.0, steps.stepSeconds() - steps.accumulatedSeconds());
    const auto waitMs = static_cast<u32>(std::clamp(untilNextTick * 1000.0, 0.0, 8.0));
    for (const net::Event& e : host->service(waitMs)) {
      if (e.type == net::EventType::Connected) {
        const auto info = host->info(e.peer);
        clients[e.peer.id] = info ? info->address : "?";
        log::info(Category::Net, "client {} connected from {} ({} connected)", e.peer.id, clients[e.peer.id],
                  host->connectedPeers());
        const protocol::Welcome welcome{.tickRate = tickRate, .tick = tick,
                                        .entityCount = static_cast<u32>(world.count<engine::Transform>())};
        if (Status s = host->send(e.peer, net::Packet(net::Channel::Reliable, protocol::encode(welcome))); !s) {
          log::warn(Category::Net, "could not welcome client {}: {}", e.peer.id, s.error().message);
        }
      } else if (e.type == net::EventType::Disconnected) {
        log::info(Category::Net, "client {} ({}) disconnected ({} connected)", e.peer.id, clients[e.peer.id],
                  host->connectedPeers());
        clients.erase(e.peer.id);
      } else {
        log::debug(Category::Net, "{} bytes from client {} on the {} channel (ignored for now)", e.packet.size(),
                   e.peer.id, net::describe(e.packet.channel()));
      }
    }

    // 2. The simulation: fixed 1/120 s steps.
    const FrameTime time = clock.tick();
    const FixedStepPlan plan = steps.advance(time.rawDeltaSeconds);
    for (u32 i = 0; i < plan.steps; ++i) {
      const auto dt = static_cast<f32>(steps.stepSeconds());
      engine::movementSystem(world, dt);
      engine::bounceSystem(world, bounds);
      world.flush();
      ++tick;
      if (tick % protocol::kSnapshotEveryTicks == 0 && host->connectedPeers() > 0) {
        host->broadcast(net::Packet(net::Channel::Unreliable, protocol::encode(snapshotOf(world, tick))));
        ++snapshotsSent;
      }
    }
    host->flush();  // snapshots out now, not at the next service()

    // 3. Now and then, how it is going.
    if (Clock::now() - lastStatus >= std::chrono::seconds(5)) {
      const f64 seconds = std::chrono::duration<f64>(Clock::now() - lastStatus).count();
      log::info(Category::Game, "server: tick {}, {:.1f} ticks/s, {} client(s), {} snapshots sent", tick,
                (tick - ticksAtLastStatus) / seconds, host->connectedPeers(), snapshotsSent);
      lastStatus = Clock::now();
      ticksAtLastStatus = tick;
    }
  }

  log::info(Category::Engine, "server stopping: {}",
            InterruptGuard::interruptRequested() ? "interrupted (Ctrl+C or SIGTERM)" : "tick limit reached");
  disconnectEveryone(*host, 500);
  log::info(Category::Game, "dedicated server stopped after {} ticks, {} snapshots sent", tick, snapshotsSent);
  return 0;
}

int runClient(const ClientOptions& options) {
  InterruptGuard interrupts;
  auto host = net::Host::client();
  if (!host) {
    log::critical(Category::Net, "client could not start: {}", host.error().message);
    return 1;
  }
  auto server = host->connect(options.host, options.port);
  if (!server) {
    log::critical(Category::Net, "client could not connect: {}", server.error().message);
    return 1;
  }

  const auto started = Clock::now();
  const auto runFor = std::chrono::duration<f64>(static_cast<f64>(options.ticks) / 120.0);
  bool connected = false;
  bool lost = false;
  std::optional<protocol::Welcome> welcome;
  u64 snapshots = 0;
  u64 rejected = 0;
  u32 lastTick = 0;
  usize lastEntityCount = 0;

  while (!InterruptGuard::interruptRequested() && !lost) {
    const auto elapsed = Clock::now() - started;
    if (!connected && elapsed > std::chrono::seconds(5)) break;  // nobody answered
    if (connected && options.ticks > 0 && elapsed >= runFor) break;
    for (const net::Event& e : host->service(8)) {
      if (e.type == net::EventType::Connected) {
        connected = true;
        log::info(Category::Net, "client: connected to {}:{}", options.host, options.port);
      } else if (e.type == net::EventType::Disconnected) {
        log::warn(Category::Net, "client: the server closed the connection");
        lost = true;
      } else if (const auto kind = protocol::peekKind(e.packet.bytes())) {
        if (*kind == protocol::MessageKind::Welcome) {
          welcome = protocol::decodeWelcome(e.packet.bytes());
          if (welcome) {
            log::info(Category::Net, "client: welcome from the server: protocol {}, {} Hz, tick {}, {} entities",
                      welcome->version, welcome->tickRate, welcome->tick, welcome->entityCount);
          }
        } else if (const auto snapshot = protocol::decodeSnapshot(e.packet.bytes())) {
          ++snapshots;
          lastTick = std::max(lastTick, snapshot->tick);
          lastEntityCount = snapshot->entities.size();
        } else {
          ++rejected;
        }
      } else {
        ++rejected;
      }
    }
  }

  if (!connected) {
    log::critical(Category::Net, "client: no answer from {}:{} within 5 s", options.host, options.port);
    return 1;
  }
  log::info(Category::Game, "client summary: welcome {}, {} snapshots, last server tick {}, {} entities per "
            "snapshot, {} malformed packets", welcome ? "received" : "MISSING", snapshots, lastTick, lastEntityCount,
            rejected);
  if (!lost) disconnectEveryone(*host, 500);
  return welcome && !lost ? 0 : 1;
}

}  // namespace playground::app
