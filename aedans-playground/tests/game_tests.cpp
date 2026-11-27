// Tests for the game library's pieces, each on its own, with no network:
// the protocol (round trips, and refusing malformed packets), the shared
// movement rule, the server's input queue (order, duplicates, the rate
// limit), client-side prediction (confirm, correct and replay, ring
// overflow) and interpolation (blending, holding, the clock).
//
// No window, no GPU, no sockets.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/game/InputQueue.h"
#include "playground/game/Interpolation.h"
#include "playground/game/Prediction.h"
#include "playground/game/Protocol.h"
#include "playground/game/Simulation.h"

#include "playground/net/BitStream.h"

#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

#include <glm/geometric.hpp>

using namespace playground;
using namespace playground::game;

namespace {

PlayerInput right() { return {.move = {1.0f, 0.0f}}; }
PlayerInput up() { return {.move = {0.0f, 1.0f}}; }

// Every proper prefix of `bytes` must be refused by `decode`.
template <typename Decode>
void checkTruncationsRefused(const std::vector<std::byte>& bytes, Decode decode) {
  for (usize n = 0; n < bytes.size(); ++n) {
    INFO("truncated to " << n << " of " << bytes.size() << " bytes");
    CHECK_FALSE(decode(std::span<const std::byte>(bytes.data(), n)).has_value());
  }
}

}  // namespace

// --- Protocol ----------------------------------------------------------------------------

TEST_CASE("protocol: every message round-trips, and the kinds are told apart") {
  const auto hello = protocol::decodeHello(protocol::encode(protocol::Hello{.version = 7, .name = "Aedan"}));
  REQUIRE(hello);
  CHECK(hello->version == 7);
  CHECK(hello->name == "Aedan");

  const auto welcome = protocol::decodeWelcome(protocol::encode(
      protocol::Welcome{.serverTick = 1234, .playerId = 65, .spawnPosition = {-120.125f, 3.5f}}));
  REQUIRE(welcome);
  CHECK(welcome->version == protocol::kVersion);
  CHECK(welcome->tickRate == protocol::kTickRate);
  CHECK(welcome->serverTick == 1234);
  CHECK(welcome->playerId == 65);
  CHECK(welcome->spawnPosition == glm::vec2(-120.125f, 3.5f));  // exact

  const auto reject = protocol::decodeReject(protocol::encode(protocol::Reject{.reason = protocol::RejectReason::ServerFull}));
  REQUIRE(reject);
  CHECK(reject->reason == protocol::RejectReason::ServerFull);
  CHECK(reject->serverVersion == protocol::kVersion);

  const auto spawn = protocol::decodeSpawn(protocol::encode(
      protocol::Spawn{.entities = {{.id = 3, .kind = EntityKind::Ball}, {.id = 900, .kind = EntityKind::Player}}}));
  REQUIRE(spawn);
  CHECK(spawn->entities == std::vector<protocol::SpawnEntry>{{3, EntityKind::Ball}, {900, EntityKind::Player}});

  const auto despawn = protocol::decodeDespawn(protocol::encode(protocol::Despawn{.ids = {1, 2, 70000}}));
  REQUIRE(despawn);
  CHECK(despawn->ids == std::vector<u32>{1, 2, 70000});

  const auto input = protocol::decodeInput(protocol::encode(
      protocol::Input{.newestSeq = 50, .inputs = {right(), up(), {.move = {-0.5f, 0.25f}, .buttons = 1}}}));
  REQUIRE(input);
  CHECK(input->newestSeq == 50);
  REQUIRE(input->inputs.size() == 3);
  CHECK(input->inputs[0] == right());
  CHECK(input->inputs[1] == up());
  CHECK(input->inputs[2] == protocol::quantize({.move = {-0.5f, 0.25f}, .buttons = 1}));

  protocol::Snapshot snapshot{.tick = 600, .lastAckedInputSeq = 598, .yourPosition = glm::vec2(10.0625f, -3.0f)};
  snapshot.entities = {{.id = 1, .position = {100.0f, -200.0f}, .rotation = 1.0f},
                       {.id = 2, .position = {-1023.0f, 1023.0f}, .rotation = -0.5f}};
  const auto decoded = protocol::decodeSnapshot(protocol::encode(snapshot));
  REQUIRE(decoded);
  CHECK(decoded->tick == 600);
  CHECK(decoded->lastAckedInputSeq == 598);
  REQUIRE(decoded->yourPosition);
  CHECK(*decoded->yourPosition == glm::vec2(10.0625f, -3.0f));  // exact: the client compares against it
  REQUIRE(decoded->entities.size() == 2);
  CHECK(glm::length(decoded->entities[0].position - glm::vec2(100.0f, -200.0f)) < 0.03f);
  CHECK(decoded->entities[1].rotation == doctest::Approx(2.0f * std::numbers::pi_v<f32> - 0.5f).epsilon(0.002));

  // Kinds: each decoder refuses the others' messages.
  const auto helloBytes = protocol::encode(protocol::Hello{});
  CHECK(protocol::peekKind(helloBytes) == protocol::MessageKind::Hello);
  CHECK_FALSE(protocol::decodeWelcome(helloBytes));
  CHECK_FALSE(protocol::decodeSnapshot(helloBytes));
  CHECK_FALSE(protocol::peekKind(std::vector<std::byte>{std::byte{0x0F}}));  // kind 15: not a message
}

TEST_CASE("protocol: truncated, oversized and nonsense packets are refused") {
  checkTruncationsRefused(protocol::encode(protocol::Hello{.name = "someone"}), protocol::decodeHello);
  checkTruncationsRefused(protocol::encode(protocol::Welcome{.playerId = 300}), protocol::decodeWelcome);
  checkTruncationsRefused(protocol::encode(protocol::Reject{}), protocol::decodeReject);
  checkTruncationsRefused(protocol::encode(protocol::Spawn{.entities = {{.id = 1}, {.id = 2}}}), protocol::decodeSpawn);
  checkTruncationsRefused(protocol::encode(protocol::Despawn{.ids = {1, 2}}), protocol::decodeDespawn);
  checkTruncationsRefused(protocol::encode(protocol::Input{.newestSeq = 9, .inputs = {right(), up()}}),
                          protocol::decodeInput);
  protocol::Snapshot snapshot{.tick = 6, .lastAckedInputSeq = 2, .yourPosition = glm::vec2(1.0f)};
  snapshot.entities.resize(3);
  checkTruncationsRefused(protocol::encode(snapshot), protocol::decodeSnapshot);

  // An input claiming more inputs than sequence numbers before it (seq 1 with 3 inputs).
  CHECK_FALSE(protocol::decodeInput(protocol::encode(protocol::Input{.newestSeq = 2, .inputs = {right(), up(), up()}})));
  // A spawn count over the limit: refused before anything is sized from it.
  net::BitWriter out;
  out.writeBits(static_cast<u32>(protocol::MessageKind::Spawn), 4);
  out.writeVarU32(1'000'000);
  CHECK_FALSE(protocol::decodeSpawn(out.take()));
  // A snapshot with a NaN for "your position".
  net::BitWriter nan;
  nan.writeBits(static_cast<u32>(protocol::MessageKind::Snapshot), 4);
  nan.writeU32(1);
  nan.writeU32(1);
  nan.writeBool(true);
  nan.writeF32(std::numeric_limits<f32>::quiet_NaN());
  nan.writeF32(0.0f);
  nan.writeVarU32(0);
  CHECK_FALSE(protocol::decodeSnapshot(nan.take()));
}

TEST_CASE("input quantization: zero stays exactly zero, full stays full, and quantizing twice changes nothing") {
  CHECK(protocol::quantize({}).move == glm::vec2(0.0f));
  CHECK(protocol::quantize(right()).move == glm::vec2(1.0f, 0.0f));
  CHECK(protocol::quantize({.move = {-1.0f, -1.0f}}).move == glm::vec2(-1.0f, -1.0f));
  CHECK(protocol::quantize({.move = {5.0f, std::numeric_limits<f32>::quiet_NaN()}}).move == glm::vec2(1.0f, 0.0f));
  const PlayerInput once = protocol::quantize({.move = {0.3333f, -0.71f}, .buttons = 0xFF});
  CHECK(protocol::quantize(once) == once);
  CHECK(once.buttons == 0x0F);
  CHECK(std::abs(once.move.x - 0.3333f) <= 0.5f / 127.0f);
}

// --- The movement rule --------------------------------------------------------------------

TEST_CASE("stepPlayer: speed per tick, diagonals no faster, walls hold, the same input gives the same answer") {
  const PlayerState start{.position = {0.0f, 0.0f}};
  CHECK(stepPlayer(start, right()).position.x == doctest::Approx(kPlayerSpeed * kStepSeconds));
  const PlayerState diagonal = stepPlayer(start, {.move = {1.0f, 1.0f}});
  CHECK(glm::length(diagonal.position) == doctest::Approx(kPlayerSpeed * kStepSeconds));
  CHECK(stepPlayer(start, {}) == start);

  PlayerState far = start;
  for (int i = 0; i < 1000; ++i) far = stepPlayer(far, right());
  CHECK(far.position.x == doctest::Approx(kWorldBounds.halfSize.x - 0.5f * kPlayerSize));

  // Determinism: two runs of the same inputs agree bit for bit.
  PlayerState a = start;
  PlayerState b = start;
  for (u32 i = 0; i < 500; ++i) {
    const PlayerInput input = protocol::quantize({.move = {std::sin(0.1f * static_cast<f32>(i)), std::cos(0.07f * static_cast<f32>(i))}});
    a = stepPlayer(a, input);
    b = stepPlayer(b, input);
  }
  CHECK(a == b);
}

// --- The server's input queue -------------------------------------------------------------

TEST_CASE("input queue: in order, once each, duplicates and old inputs ignored") {
  InputQueue queue;
  const std::vector<PlayerInput> three{right(), up(), right()};
  queue.receive(3, three);  // 1, 2, 3
  queue.receive(4, std::vector<PlayerInput>{up(), right(), up()});  // 2, 3 again, and 4
  CHECK(queue.queued() == 4);
  CHECK(queue.stats().duplicate == 2);

  std::vector<u32> applied;
  const auto record = [&](u32 seq, const PlayerInput&) { applied.push_back(seq); };
  queue.tick(record);  // the bucket allows a burst of 3
  CHECK(applied == std::vector<u32>{1, 2, 3});
  queue.tick(record);
  CHECK(applied == std::vector<u32>{1, 2, 3, 4});
  CHECK(queue.lastApplied() == 4);

  queue.receive(2, std::vector<PlayerInput>{right(), right()});  // already applied
  CHECK(queue.queued() == 0);
  queue.tick(record);  // two quiet ticks: the bucket refills
  queue.tick(record);

  // Out of order: 7 arrives before 5 and 6; they are put in order.
  queue.receive(7, std::vector<PlayerInput>{up()});
  queue.receive(6, std::vector<PlayerInput>{up(), up()});
  applied.clear();
  queue.tick(record);
  CHECK(applied == std::vector<u32>{5, 6, 7});
}

TEST_CASE("input queue: a gap that never arrives is skipped, not waited for") {
  InputQueue queue;
  queue.receive(1, std::vector<PlayerInput>{right()});
  queue.receive(5, std::vector<PlayerInput>{up()});  // 2..4 lost, every copy
  std::vector<u32> applied;
  queue.tick([&](u32 seq, const PlayerInput&) { applied.push_back(seq); });
  CHECK(applied == std::vector<u32>{1, 5});
  CHECK(queue.stats().skipped == 3);
}

TEST_CASE("input queue: a client sending twice the tick rate moves no faster (the rate limit)") {
  InputQueue queue;
  u32 seq = 0;
  u64 applied = 0;
  for (int tick = 0; tick < 1200; ++tick) {  // 10 s
    // Two new inputs a tick, each packet repeating the last 8 (as a real client would).
    seq += 2;
    std::vector<PlayerInput> burst(std::min<u32>(seq, protocol::kMaxInputsPerPacket), right());
    queue.receive(seq, burst);
    applied += queue.tick([](u32, const PlayerInput&) {});
    CHECK(queue.queued() <= InputQueue::kMaxQueued);
  }
  // At most one a tick, plus the starting burst.
  CHECK(applied <= 1200 + InputQueue::kBurst);
  CHECK(applied >= 1190);
  CHECK(queue.stats().dropped > 1000);
}

TEST_CASE("input queue: a client that fell behind may catch up a few inputs, not unlimited") {
  InputQueue queue;
  for (int i = 0; i < 10; ++i) queue.tick([](u32, const PlayerInput&) {});  // idle: the bucket fills, but only to kBurst
  std::vector<PlayerInput> eight(8, right());
  queue.receive(8, eight);
  CHECK(queue.tick([](u32, const PlayerInput&) {}) == InputQueue::kBurst);
  CHECK(queue.tick([](u32, const PlayerInput&) {}) == 1);
}

// --- Client-side prediction ---------------------------------------------------------------

TEST_CASE("prediction: a server that agrees confirms; nothing is replayed") {
  Prediction prediction;
  prediction.reset({.position = {0.0f, 0.0f}});
  PlayerState server{.position = {0.0f, 0.0f}};
  for (u32 seq = 1; seq <= 10; ++seq) {
    prediction.predict(seq, right());
  }
  for (u32 seq = 1; seq <= 4; ++seq) server = stepPlayer(server, right());  // the server is 4 inputs in
  CHECK_FALSE(prediction.reconcile(4, server));
  CHECK(prediction.stats().confirmed == 1);
  CHECK(prediction.stats().corrections == 0);
  CHECK(prediction.lastAcked() == 4);
  CHECK(prediction.unacknowledged() == 6);
  CHECK(prediction.current().position.x == doctest::Approx(10.0f * kPlayerSpeed * kStepSeconds));
}

TEST_CASE("prediction: a server that disagrees wins; later inputs are replayed on top of its answer") {
  Prediction prediction;
  prediction.reset({});
  for (u32 seq = 1; seq <= 10; ++seq) prediction.predict(seq, seq <= 5 ? right() : up());
  // The server dropped input 3 (say): after input 5 it is one step short in x.
  PlayerState server;
  for (int i = 0; i < 4; ++i) server = stepPlayer(server, right());
  CHECK(prediction.reconcile(5, server));
  CHECK(prediction.stats().corrections == 1);
  CHECK(prediction.stats().replayed == 5);  // 6..10
  const f32 step = kPlayerSpeed * kStepSeconds;
  CHECK(prediction.current().position.x == doctest::Approx(4.0f * step));
  CHECK(prediction.current().position.y == doctest::Approx(5.0f * step));
  // Within epsilon (the quantization of a snapshot) is not a correction.
  CHECK_FALSE(prediction.reconcile(6, {.position = stepPlayer(server, up()).position + glm::vec2(0.004f, 0.0f)}));
  // An old or repeated acknowledgement changes nothing.
  CHECK_FALSE(prediction.reconcile(5, {}));
  CHECK(prediction.stats().corrections == 1);
}

TEST_CASE("prediction: far ahead of the server, the ring overwrites its oldest; nothing breaks") {
  Prediction prediction;
  prediction.reset({});
  const u32 total = Prediction::kCapacity + 100;
  for (u32 seq = 1; seq <= total; ++seq) prediction.predict(seq, right());
  CHECK(prediction.stats().overflows == 100);
  // The server acknowledges input 50, long overwritten: the client cannot
  // compare, so it takes the server's word and replays what it still holds.
  const PlayerState server{.position = {-300.0f, 0.0f}};
  CHECK(prediction.reconcile(50, server));
  const f32 step = kPlayerSpeed * kStepSeconds;
  CHECK(prediction.current().position.x == doctest::Approx(-300.0f + static_cast<f32>(Prediction::kCapacity) * step));
  // The inputs to resend are the newest few, all still held.
  CHECK(prediction.inputsSince(total - 7).size() == 8);
  CHECK(prediction.inputsSince(1).size() == Prediction::kCapacity);  // never more than the ring holds

  // An acknowledgement beyond anything predicted: reset to the server.
  CHECK(prediction.reconcile(total + 50, {.position = {1.0f, 2.0f}}));
  CHECK(prediction.current().position == glm::vec2(1.0f, 2.0f));
  CHECK(prediction.newestSeq() == total + 50);
  CHECK(prediction.inputsSince(1).empty());
}

// --- Interpolation ------------------------------------------------------------------------

TEST_CASE("snapshot buffer: blends between the two snapshots around a time, holds past the newest") {
  SnapshotBuffer buffer;
  const std::vector<protocol::SnapshotEntity> a{{.id = 1, .position = {0.0f, 0.0f}, .rotation = 6.0f},
                                                {.id = 2, .position = {50.0f, 50.0f}}};
  const std::vector<protocol::SnapshotEntity> b{{.id = 1, .position = {60.0f, -30.0f}, .rotation = 0.2f},
                                                {.id = 3, .position = {-5.0f, 5.0f}}};
  buffer.push(100, a);
  buffer.push(106, b);
  buffer.push(103, a);  // older than the newest: ignored
  CHECK(buffer.size() == 2);

  auto at = [](const std::vector<EntitySample>& samples, u32 id) -> const EntitySample* {
    for (const EntitySample& s : samples) if (s.id == id) return &s;
    return nullptr;
  };
  const std::vector<EntitySample> middle = buffer.sample(103.0);
  REQUIRE(at(middle, 1) != nullptr);
  CHECK(at(middle, 1)->position.x == doctest::Approx(30.0f));
  CHECK(at(middle, 1)->position.y == doctest::Approx(-15.0f));
  // 6.0 -> 0.2 rad the short way (through 2 pi), not back round the circle.
  const f32 shortWay = 6.0f + 0.5f * (0.2f + 2.0f * std::numbers::pi_v<f32> - 6.0f);
  CHECK(at(middle, 1)->rotation == doctest::Approx(shortWay));
  CHECK(at(middle, 2) == nullptr);  // gone in the newer snapshot
  REQUIRE(at(middle, 3) != nullptr);  // new in it: where it is
  CHECK(at(middle, 3)->position == glm::vec2(-5.0f, 5.0f));

  const std::vector<EntitySample> later = buffer.sample(140.0);  // past the newest: hold, no guessing
  CHECK(at(later, 1)->position == glm::vec2(60.0f, -30.0f));
  const std::vector<EntitySample> earlier = buffer.sample(50.0);  // before the oldest: the oldest
  CHECK(at(earlier, 1)->position == glm::vec2(0.0f, 0.0f));

  for (u32 i = 0; i < 100; ++i) buffer.push(200 + i * 6, b);
  CHECK(buffer.size() == SnapshotBuffer::kCapacity);
}

TEST_CASE("interpolation clock: runs the delay behind the server, steady through jitter, jumps after a stall") {
  InterpolationClock clock;
  CHECK_FALSE(clock.started());
  clock.heard(600);
  CHECK(clock.renderTick() == doctest::Approx(600.0 - kInterpolationDelayTicks));
  // Snapshots every 6 ticks, arriving a tick early or late.
  u32 serverTick = 600;
  for (u32 t = 1; t <= 600; ++t) {
    clock.advance();
    if (t % 6 == 0) {
      serverTick += 6;
      clock.heard(serverTick + (t % 12 == 0 ? 1u : 0u) - (t % 18 == 0 ? 1u : 0u));
    }
  }
  CHECK(clock.renderTick() == doctest::Approx(static_cast<f64>(serverTick) - kInterpolationDelayTicks).epsilon(0.003));
  // The server is suddenly far ahead (we stalled): jump, do not crawl.
  clock.heard(serverTick + 1000);
  clock.advance();
  CHECK(clock.renderTick() > static_cast<f64>(serverTick) + 900.0);
}
