#include "NetScene.h"

#include "playground/core/Log.h"
#include "playground/engine/Hud.h"
#include "playground/game/Simulation.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace playground::app {
namespace {

constexpr u32 kCoopCutscene = 1;  // the server's id for assets/cutscenes/coop.json

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

Result<std::unique_ptr<NetScene>> NetScene::create(engine::Application& application, const ClientOptions& options,
                                                   std::filesystem::path sceneFile,
                                                   std::filesystem::path cutsceneDirectory) {
  auto client = game::GameClient::connect({.host = options.host, .port = options.port, .name = "windowed player",
                                           .network = options.network});
  if (!client) return tl::unexpected(std::move(client.error()));
  std::unique_ptr<NetScene> scene(new NetScene(application, std::move(*client), std::move(sceneFile)));
  // The co-op cutscenes, by the id the server names them with.
  if (!cutsceneDirectory.empty()) {
    if (auto coop = engine::CutsceneSequence::loadFromFile((cutsceneDirectory / "coop.json").string())) {
      scene->sequences_.emplace(kCoopCutscene, std::move(*coop));
    } else {
      log::warn(Category::Game, "no co-op cutscene: {}", coop.error().message);
    }
  }
  return scene;
}

NetScene::NetScene(engine::Application& application, std::unique_ptr<game::GameClient> client,
                   std::filesystem::path sceneFile)
    : application_(application), client_(std::move(client)), editor_(std::move(sceneFile)) {
  camera_.orthoHalfExtent = game::kWorldBounds.halfSize;
  camera_.projection = engine::Projection::Orthographic;
  camera_.position = {0.0f, 0.0f, 1000.0f};
  camera_.target = {0.0f, 0.0f, 0.0f};
  log::info(Category::Game, "network client: move with WASD or the arrow keys, Escape quits");
}

// Every 1/120 s: what arrived, the cutscene on the server's clock, then this
// tick's input (predicted and sent; with the skip vote while one plays).
void NetScene::fixedUpdate(const FixedStep& /*step*/, const platform::InputState& input) {
  client_->pumpNetwork(0);
  if (const auto cue = client_->cutscene(); cue && sequences_.contains(cue->sequenceId)) {
    if (cueStart_ != cue->startTick) {
      if (cutscene_.start(sequences_.at(cue->sequenceId), cue->startTick)) {
        cueStart_ = cue->startTick;
        ++cutscenesSeen_;
      }
    }
    if (cue->endedEarly) cutscene_.skipAt(cue->endTick);
  }
  // The server's tick, as estimated here; never backwards (the estimate can wobble).
  lastServerTick_ = std::max(lastServerTick_, static_cast<u64>(std::max(0.0, client_->serverTickNow())));
  cutscene_.tick(lastServerTick_, [](const engine::CutsceneEvent& event) {
    log::info(Category::Game, "co-op cutscene event at {:.2f} s: {} {}", event.time, event.action, event.target);
  });
  holdingSkip_ = cutscene_.playing() && (input.down(platform::Key::Space) || input.down(platform::Key::Escape));
  (void)cutscene_.holdSkip(holdingSkip_);  // only for the prompt's bar: the server decides

  game::PlayerInput playerInput{.move = actions_.axis2D(input, engine::actions::kMove)};
  if (actions_.down(input, engine::actions::kFire)) playerInput.buttons |= game::protocol::kButtonFire;
  if (holdingSkip_) playerInput.buttons |= game::protocol::kButtonSkip;
  client_->step(playerInput);  // during a cutscene it sends no movement (GameClient)
}

void NetScene::drawHud() {
  engine::hud::letterbox(cutscene_.letterbox(1.0f));
  if (const engine::Subtitle* line = cutscene_.subtitle()) engine::hud::subtitle(line->speaker, line->text);
  if (cutscene_.playing()) {
    const bool waiting = holdingSkip_ && cutscene_.skipProgress() >= 1.0f;
    engine::hud::skipPrompt("Space or Escape", cutscene_.skipProgress(), waiting ? "(waiting for everyone)" : "");
  }
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
  if (cutscene_.active() || cutscene_.handingOff()) {
    // A perspective camera over the playfield; it hands back to the
    // overhead view (the same picture as the orthographic one: straight
    // down, far enough up to see the whole world), then that takes over.
    constexpr f32 kFov = 60.0f;
    const f32 height = game::kWorldBounds.halfSize.y / std::tan(glm::radians(0.5f * kFov));
    const engine::CameraPose overhead{.position = {0.0f, 0.0f, height}, .fovDegrees = kFov};
    const engine::CameraPose shown = cutscene_.camera(overhead, blend);
    engine::Camera view;
    view.projection = engine::Projection::Perspective;
    view.position = shown.position;
    view.target = shown.position + shown.forward();
    view.up = shown.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
    view.fovY = glm::radians(shown.fovDegrees);
    view.nearPlane = 1.0f;
    view.farPlane = 10000.0f;
    frame.viewProjection = view.viewProjection(frame.viewportSize);
  } else {
    frame.viewProjection = camera_.viewProjection(frame.viewportSize);
  }
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

// The profiler, with the network's numbers (the world here is the server's: nothing to edit).
void NetScene::drawDebugUi(engine::ProfilerData data) {
  const game::ClientStats stats = client_->stats();
  data.entities = client_->knownEntityCount();
  data.net = engine::NetProfile{.state = std::string(game::describe(client_->state())),
                                .roundTripMs = client_->roundTripMs(), .ackDelayMs = client_->ackDelayMs(),
                                .snapshots = stats.snapshots, .deltaSnapshots = stats.deltaSnapshots,
                                .outOfOrder = stats.outOfOrder, .missingBaseline = stats.missingBaseline,
                                .corrections = stats.prediction.corrections, .inputSeq = client_->inputSeq(),
                                .ackedInputSeq = client_->lastAckedInputSeq(),
                                .knownEntities = client_->knownEntityCount()};
  editor_.drawProfilerOnly(data);
}

void NetScene::logSummary() {
  if (const auto cue = client_->cutscene()) {
    log::info(Category::Game, "network client cutscene: sequence {}, server ticks {} to {}, {}; {} played here",
              cue->sequenceId, cue->startTick, cue->endTick, cue->endedEarly ? "skipped by everyone" : "played through",
              cutscenesSeen_);
  }
  const game::ClientStats stats = client_->stats();
  const glm::vec2 at = client_->predictedPosition();
  log::info(Category::Game, "network client summary: {}, player entity {} ended at x={:.0f}, y={:.0f}; {} snapshots, "
            "{} corrections, {} entities known", game::describe(client_->state()), client_->playerId(), at.x, at.y,
            stats.snapshots, stats.prediction.corrections, client_->knownEntityCount());
  if (!failed_) client_->disconnect(500);
}

}  // namespace playground::app
