#include "NetScene.h"

#include "playground/core/Log.h"
#include "playground/game/Simulation.h"

#include <glm/geometric.hpp>

namespace playground::app {
namespace {

using log::Category;

constexpr f32 kPlayfieldLayer = -1.0f;
constexpr f32 kBallLayer = 0.0f;
constexpr f32 kPlayerLayer = 1.0f;
constexpr f32 kLocalPlayerLayer = 2.0f;

// A steady colour per entity id, so each ball keeps its colour.
glm::vec4 ballColour(u32 id) {
  const u32 h = id * 2654435761u;
  auto channel = [h](u32 shift) { return 0.25f + 0.75f * static_cast<f32>((h >> shift) & 0xFFu) / 255.0f; };
  return {channel(0), channel(8), channel(16), 1.0f};
}

}  // namespace

Result<std::unique_ptr<NetScene>> NetScene::create(engine::Application& application, const ClientOptions& options) {
  auto client = game::GameClient::connect({.host = options.host, .port = options.port, .name = "windowed player",
                                           .network = options.network});
  if (!client) return tl::unexpected(std::move(client.error()));
  return std::unique_ptr<NetScene>(new NetScene(application, std::move(*client)));
}

NetScene::NetScene(engine::Application& application, std::unique_ptr<game::GameClient> client)
    : application_(application), client_(std::move(client)) {
  camera_.orthoHalfExtent = game::kWorldBounds.halfSize;
  camera_.projection = engine::Projection::Orthographic;
  camera_.position = {0.0f, 0.0f, 1000.0f};
  camera_.target = {0.0f, 0.0f, 0.0f};
  log::info(Category::Game, "network client: move with WASD or the arrow keys, Escape quits");
}

// Every 1/120 s: what arrived, then this tick's input (predicted and sent).
void NetScene::fixedUpdate(const FixedStep& /*step*/, const platform::InputState& input) {
  client_->pumpNetwork(0);
  game::PlayerInput playerInput{.move = actions_.axis2D(input, engine::actions::kMove)};
  if (actions_.down(input, engine::actions::kFire)) playerInput.buttons |= 1u;
  client_->step(playerInput);
}

void NetScene::update(const FrameTime& time, const platform::InputState& /*input*/) {
  client_->pumpNetwork(0);
  const game::ClientState state = client_->state();
  if (!failed_ && (state == game::ClientState::Rejected || state == game::ClientState::Disconnected)) {
    log::critical(Category::Net, "network client stopped: {}", client_->failure());
    failed_ = true;
    application_.requestQuit();
  }
  secondsSinceStatus_ += time.rawDeltaSeconds;
  if (secondsSinceStatus_ >= 5.0 && state == game::ClientState::Playing) {
    secondsSinceStatus_ = 0.0;
    const game::ClientStats stats = client_->stats();
    log::info(Category::Game, "net: server tick {}, input {} (acknowledged {}, ~{:.0f} ms), {} snapshots ({} out "
              "of order), {} corrections, {} entities", client_->lastServerTick(), client_->inputSeq(),
              client_->lastAckedInputSeq(), client_->ackDelayMs(), stats.snapshots, stats.outOfOrder,
              stats.prediction.corrections, client_->knownEntityCount());
  }
}

void NetScene::extract(render::FrameData& frame, f64 alpha) {
  const auto blend = static_cast<f32>(alpha);
  frame.viewProjection = camera_.viewProjection(frame.viewportSize);
  frame.sprites.push_back({.position = {0.0f, 0.0f}, .size = game::kWorldBounds.size(),
                           .colour = {0.03f, 0.03f, 0.07f, 1.0f}, .depth = kPlayfieldLayer});
  for (const game::RemoteEntity& e : client_->remoteEntities(blend)) {
    if (e.kind == game::EntityKind::Player) {
      frame.sprites.push_back({.position = e.position, .size = glm::vec2(game::kPlayerSize),
                               .colour = {1.0f, 0.55f, 0.15f, 1.0f}, .depth = kPlayerLayer});
    } else {
      frame.sprites.push_back({.position = e.position, .size = glm::vec2(game::kBallSize), .rotation = e.rotation,
                               .colour = ballColour(e.id), .depth = kBallLayer});
    }
  }
  if (client_->state() == game::ClientState::Playing) {
    frame.sprites.push_back({.position = client_->renderPosition(blend), .size = glm::vec2(game::kPlayerSize),
                             .colour = {1.0f, 1.0f, 1.0f, 1.0f}, .depth = kLocalPlayerLayer});
  }
}

void NetScene::logSummary() {
  const game::ClientStats stats = client_->stats();
  const glm::vec2 at = client_->predictedPosition();
  log::info(Category::Game, "network client summary: {}, player entity {} ended at x={:.0f}, y={:.0f}; {} snapshots, "
            "{} corrections, {} entities known", game::describe(client_->state()), client_->playerId(), at.x, at.y,
            stats.snapshots, stats.prediction.corrections, client_->knownEntityCount());
  if (!failed_) client_->disconnect(500);
}

}  // namespace playground::app
