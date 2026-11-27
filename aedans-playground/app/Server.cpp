// =============================================================================
//  Server.cpp — the dedicated server and the headless bot client (Server.h).
//
//  THE SERVER LOOP, with no window and no GPU:
//
//    while not stopped (Ctrl+C / SIGTERM, or --frames N ticks):
//      1. the network, waiting at most until the next tick is due: the
//         wait for packets IS the sleep, so the loop neither spins a core
//         nor oversleeps a tick;
//      2. FrameClock + FixedStepAccumulator (core/FixedStep.h, the same
//         rules as the windowed game): 0..n 120 Hz GameServer steps (inputs,
//         balls, a snapshot to each client every 6th tick);
//      3. every 5 s, a status line.
//    then: goodbye to every client, waiting up to half a second for each to
//    answer, so they see a clean disconnect, not a timeout.
//
//  The bot client runs the same loop shape around a GameClient.
// =============================================================================

#include "Server.h"

#include "playground/core/FixedStep.h"
#include "playground/core/FrameClock.h"
#include "playground/core/Interrupt.h"
#include "playground/core/Log.h"
#include "playground/game/GameClient.h"
#include "playground/game/GameServer.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <numbers>

#include <glm/geometric.hpp>

namespace playground::app {
namespace {

using log::Category;
using Clock = std::chrono::steady_clock;

// How long to wait for packets before the next fixed step is due.
u32 msUntilNextStep(const FixedStepAccumulator& steps) {
  const f64 untilNextTick = std::max(0.0, steps.stepSeconds() - steps.accumulatedSeconds());
  return static_cast<u32>(std::clamp(untilNextTick * 1000.0, 0.0, 8.0));
}

}  // namespace

Status parseAddress(std::string_view text, ClientOptions& options) {
  const usize colon = text.rfind(':');
  options.host = std::string(text.substr(0, colon));
  if (colon == std::string_view::npos) return {};
  const std::string_view portText = text.substr(colon + 1);
  const auto [end, ec] = std::from_chars(portText.data(), portText.data() + portText.size(), options.port);
  if (ec != std::errc{} || end != portText.data() + portText.size() || options.host.empty()) {
    return fail(Errc::InvalidArgument, "--connect expects HOST[:PORT], got \"{}\"", text);
  }
  return {};
}

int runServer(const ServerOptions& options) {
  InterruptGuard interrupts;  // Ctrl+C / SIGTERM: stop in order, not mid-tick
  auto server = game::GameServer::start({.port = options.port, .maxClients = options.maxClients,
                                         .balls = options.balls, .network = options.network});
  if (!server) {
    log::critical(Category::Net, "dedicated server could not start: {}", server.error().message);
    return 1;
  }
  game::GameServer& game = **server;

  FrameClock clock;
  FixedStepAccumulator steps(1.0 / game::protocol::kTickRate);
  log::info(Category::Game, "dedicated server: protocol {}, {} Hz, UDP port {}, {} balls, up to {} players; "
            "Ctrl+C stops it", game::protocol::kVersion, game::protocol::kTickRate, game.port(), options.balls,
            options.maxClients);
  log::info(Category::Engine, "entering server loop");

  auto lastStatus = Clock::now();
  u32 ticksAtLastStatus = 0;
  while (!InterruptGuard::interruptRequested() && (options.maxTicks == 0 || game.tick() < options.maxTicks)) {
    game.pumpNetwork(msUntilNextStep(steps));
    const FixedStepPlan plan = steps.advance(clock.tick().rawDeltaSeconds);
    for (u32 i = 0; i < plan.steps; ++i) game.step();

    if (Clock::now() - lastStatus >= std::chrono::seconds(5)) {
      const f64 seconds = std::chrono::duration<f64>(Clock::now() - lastStatus).count();
      const game::ServerStats& stats = game.stats();
      log::info(Category::Game, "server: tick {}, {:.1f} ticks/s, {} player(s), {} snapshots sent ({} as deltas, "
                "{:.0f} bytes each on average)", game.tick(), (game.tick() - ticksAtLastStatus) / seconds,
                game.playerCount(), stats.snapshotsSent, stats.deltaSnapshotsSent,
                stats.snapshotsSent > 0 ? static_cast<f64>(stats.snapshotBytesSent) / static_cast<f64>(stats.snapshotsSent) : 0.0);
      for (const game::ServerClientStats& c : game.clientStats()) {
        log::info(Category::Game, "  client {}: entity {} at ({:.0f}, {:.0f}), input {} applied ({} applied, {} over "
                  "the rate limit, {} lost)", c.peer, c.playerId, c.position.x, c.position.y, c.lastAckedInputSeq,
                  c.inputsApplied, c.inputsDropped, c.inputsSkipped);
      }
      lastStatus = Clock::now();
      ticksAtLastStatus = game.tick();
    }
  }

  log::info(Category::Engine, "server stopping: {}",
            InterruptGuard::interruptRequested() ? "interrupted (Ctrl+C or SIGTERM)" : "tick limit reached");
  game.shutdown(500);
  const game::ServerStats& stats = game.stats();
  log::info(Category::Game, "dedicated server stopped after {} ticks: {} snapshots sent ({} as deltas, {} bytes), "
            "{} joined, {} rejected, {} handshake timeouts, {} malformed packets", game.tick(), stats.snapshotsSent,
            stats.deltaSnapshotsSent, stats.snapshotBytesSent, stats.joined, stats.rejected, stats.handshakeTimeouts,
            stats.malformedPackets);
  return 0;
}

int runClient(const ClientOptions& options) {
  InterruptGuard interrupts;
  auto connected = game::GameClient::connect({.host = options.host, .port = options.port, .name = "headless bot",
                                              .network = options.network});
  if (!connected) {
    log::critical(Category::Net, "client could not start: {}", connected.error().message);
    return 1;
  }
  game::GameClient& client = **connected;

  // Walk a circle for `ticks` ticks, then stand still long enough for every
  // input to come back acknowledged (well over a round trip).
  constexpr u64 kSettleTicks = 60;
  FrameClock clock;
  FixedStepAccumulator steps(1.0 / game::protocol::kTickRate);
  u64 played = 0;
  while (!InterruptGuard::interruptRequested()) {
    client.pumpNetwork(msUntilNextStep(steps));
    const game::ClientState state = client.state();
    if (state == game::ClientState::Rejected || state == game::ClientState::Disconnected) break;
    if (options.ticks > 0 && played >= options.ticks + kSettleTicks) break;
    const FixedStepPlan plan = steps.advance(clock.tick().rawDeltaSeconds);
    for (u32 i = 0; i < plan.steps; ++i) {
      game::PlayerInput input;
      if (state == game::ClientState::Playing && (options.ticks == 0 || played < options.ticks)) {
        const f32 angle = static_cast<f32>(played) * 2.0f * std::numbers::pi_v<f32> / 240.0f;
        input.move = {std::cos(angle), std::sin(angle)};
      }
      client.step(input);
      if (state == game::ClientState::Playing) ++played;
    }
  }

  const game::ClientStats stats = client.stats();
  const bool welcomed = played > 0;
  log::info(Category::Game, "client summary: welcome {}, player entity {}, {} snapshots ({} as deltas, {} out of "
            "order, {} missing baselines), last server tick {}, {} entities known, {} malformed packets, {} inputs "
            "sent, {} acknowledged, {} corrections, prediction error {:.3f} units",
            welcomed ? "received" : "MISSING", client.playerId(), stats.snapshots, stats.deltaSnapshots,
            stats.outOfOrder, stats.missingBaseline,
            client.lastServerTick(), client.knownEntityCount(), stats.malformed, client.inputSeq(),
            client.lastAckedInputSeq(), stats.prediction.corrections,
            glm::length(client.predictedPosition() - client.serverPosition()));
  const bool lost = client.state() == game::ClientState::Disconnected || client.state() == game::ClientState::Rejected;
  if (lost) {
    log::error(Category::Net, "client: {}", client.failure());
  } else {
    client.disconnect(500);
  }
  return welcomed && !lost ? 0 : 1;
}

}  // namespace playground::app
