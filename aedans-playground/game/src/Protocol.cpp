#include "playground/game/Protocol.h"

#include "playground/net/BitStream.h"
#include "playground/net/Quantize.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace playground::game::protocol {
namespace {

constexpr u32 kKindBits = 4;
constexpr u32 kAxisBits = 8;
constexpr f32 kAxisSteps = 127.0f;  // -127..127 sent as 0..254: zero is exact
constexpr u32 kRejectReasonBits = 4;
constexpr u32 kEntityKindBits = 2;
constexpr f32 kTwoPi = 2.0f * std::numbers::pi_v<f32>;
// A position change is sent in the smallest of these that holds it (a
// 2-bit class first), or as the full 16 bits: 7 bits hold +-64 steps (2
// units), 10 bits +-512 (16 units), 13 bits +-4096 (128 units).
constexpr std::array<u32, 3> kOffsetBits{7, 10, 13};
constexpr u32 kOffsetClassBits = 2;
// A rotation change: 7 bits (+-64 steps, 22 degrees) or the full 10.
constexpr u32 kSmallTurnBits = 7;

f32 wrapAngle(f32 radians) {
  const f32 wrapped = std::fmod(radians, kTwoPi);
  return wrapped < 0.0f ? wrapped + kTwoPi : wrapped;
}

u32 axisToWire(f32 value) {
  const f32 safe = std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f;
  return static_cast<u32>(std::lround(safe * kAxisSteps) + static_cast<long>(kAxisSteps));
}

f32 axisFromWire(u32 wire) {
  const auto steps = static_cast<f32>(std::min<u32>(wire, 254u)) - kAxisSteps;
  return steps / kAxisSteps;
}

net::BitWriter begin(MessageKind kind) {
  net::BitWriter out;
  out.writeBits(static_cast<u32>(kind), kKindBits);
  return out;
}

// A reader past the kind, if the packet is of that kind.
std::optional<net::BitReader> open(std::span<const std::byte> packet, MessageKind kind) {
  net::BitReader in(packet);
  if (in.readBits(kKindBits) != static_cast<u32>(kind) || !in.ok()) return std::nullopt;
  return in;
}

void writeExactVec2(net::BitWriter& out, glm::vec2 v) {
  out.writeF32(v.x);
  out.writeF32(v.y);
}

glm::vec2 readExactVec2(net::BitReader& in) {
  const f32 x = in.readF32();
  const f32 y = in.readF32();
  return {x, y};
}

bool byId(const QuantizedEntity& a, const QuantizedEntity& b) { return a.id < b.id; }

// A signed offset in `bits` bits, stored with a bias so it is unsigned.
bool fitsOffset(i32 delta, u32 bits) {
  const i32 bias = 1 << (bits - 1);
  return delta >= -bias && delta < bias;
}

void writeOffset(net::BitWriter& out, i32 delta, u32 bits) {
  out.writeBits(static_cast<u32>(delta + (1 << (bits - 1))), bits);
}

i32 readOffset(net::BitReader& in, u32 bits) {
  return static_cast<i32>(in.readBits(bits)) - (1 << (bits - 1));
}

// One position component, against the baseline's.
void writeComponent(net::BitWriter& out, u16 value, u16 base) {
  const i32 delta = static_cast<i32>(value) - static_cast<i32>(base);
  for (u32 c = 0; c < kOffsetBits.size(); ++c) {
    if (fitsOffset(delta, kOffsetBits[c])) {
      out.writeBits(c, kOffsetClassBits);
      writeOffset(out, delta, kOffsetBits[c]);
      return;
    }
  }
  out.writeBits(kOffsetBits.size(), kOffsetClassBits);
  out.writeBits(value, kPositionBits);
}

u16 readComponent(net::BitReader& in, u16 base) {
  const auto c = static_cast<u32>(in.readBits(kOffsetClassBits));
  if (c >= kOffsetBits.size()) return static_cast<u16>(in.readBits(kPositionBits));
  return static_cast<u16>(std::clamp(static_cast<i32>(base) + readOffset(in, kOffsetBits[c]), 0, 0xFFFF));
}

// A rotation, against the baseline's, the short way round the circle.
void writeTurn(net::BitWriter& out, u16 value, u16 base) {
  constexpr i32 kSteps = 1 << kRotationBits;
  i32 delta = (static_cast<i32>(value) - static_cast<i32>(base)) % kSteps;
  if (delta >= kSteps / 2) delta -= kSteps;
  if (delta < -kSteps / 2) delta += kSteps;
  const bool small = fitsOffset(delta, kSmallTurnBits);
  out.writeBool(small);
  if (small) {
    writeOffset(out, delta, kSmallTurnBits);
  } else {
    out.writeBits(value, kRotationBits);
  }
}

u16 readTurn(net::BitReader& in, u16 base) {
  constexpr i32 kSteps = 1 << kRotationBits;
  if (!in.readBool()) return static_cast<u16>(in.readBits(kRotationBits));
  const i32 turned = (static_cast<i32>(base) + readOffset(in, kSmallTurnBits) + kSteps) % kSteps;
  return static_cast<u16>(turned);
}

}  // namespace

std::string_view describe(RejectReason reason) noexcept {
  switch (reason) {
    case RejectReason::VersionMismatch: return "protocol version mismatch";
    case RejectReason::ServerFull: return "server full";
  }
  return "unknown reason";
}

PlayerInput quantize(const PlayerInput& input) noexcept {
  return {.move = {axisFromWire(axisToWire(input.move.x)), axisFromWire(axisToWire(input.move.y))},
          .buttons = static_cast<u8>(input.buttons & ((1u << kButtonBits) - 1u))};
}

QuantizedEntity quantizeEntity(u32 id, glm::vec2 position, f32 rotation) noexcept {
  return {.id = id,
          .x = static_cast<u16>(net::quantize(position.x, -kWorldExtent, kWorldExtent, kPositionBits)),
          .y = static_cast<u16>(net::quantize(position.y, -kWorldExtent, kWorldExtent, kPositionBits)),
          // The top step would equal 2 pi, which is 0 again: quantize over [0, 2 pi] and wrap on the way back.
          .rotation = static_cast<u16>(net::quantize(wrapAngle(rotation), 0.0f, kTwoPi, kRotationBits))};
}

SnapshotEntity dequantize(const QuantizedEntity& entity) noexcept {
  return {.id = entity.id,
          .position = {net::dequantize(entity.x, -kWorldExtent, kWorldExtent, kPositionBits),
                       net::dequantize(entity.y, -kWorldExtent, kWorldExtent, kPositionBits)},
          .rotation = wrapAngle(net::dequantize(entity.rotation, 0.0f, kTwoPi, kRotationBits))};
}

std::vector<std::byte> encode(const Hello& message) {
  net::BitWriter out = begin(MessageKind::Hello);
  out.writeU16(message.version);
  out.writeString(message.name, kMaxNameLength);
  return out.take();
}

std::vector<std::byte> encode(const Welcome& message) {
  net::BitWriter out = begin(MessageKind::Welcome);
  out.writeU16(message.version);
  out.writeU16(message.tickRate);
  out.writeU32(message.serverTick);
  out.writeVarU32(message.playerId);
  writeExactVec2(out, message.spawnPosition);
  return out.take();
}

std::vector<std::byte> encode(const Reject& message) {
  net::BitWriter out = begin(MessageKind::Reject);
  out.writeBits(static_cast<u32>(message.reason), kRejectReasonBits);
  out.writeU16(message.serverVersion);
  return out.take();
}

std::vector<std::byte> encode(const Spawn& message) {
  net::BitWriter out = begin(MessageKind::Spawn);
  const usize count = std::min<usize>(message.entities.size(), kMaxEntitiesPerMessage);
  out.writeVarU32(static_cast<u32>(count));
  for (usize i = 0; i < count; ++i) {
    out.writeVarU32(message.entities[i].id);
    out.writeBits(static_cast<u32>(message.entities[i].kind), kEntityKindBits);
  }
  return out.take();
}

std::vector<std::byte> encode(const Despawn& message) {
  net::BitWriter out = begin(MessageKind::Despawn);
  const usize count = std::min<usize>(message.ids.size(), kMaxEntitiesPerMessage);
  out.writeVarU32(static_cast<u32>(count));
  for (usize i = 0; i < count; ++i) out.writeVarU32(message.ids[i]);
  return out.take();
}

std::vector<std::byte> encode(const Input& message) {
  net::BitWriter out = begin(MessageKind::Input);
  out.writeU32(message.newestSeq);
  out.writeU32(message.lastSnapshotTick);
  // The newest kMaxInputsPerPacket, if there are more.
  const usize count = std::min<usize>(message.inputs.size(), kMaxInputsPerPacket);
  const usize first = message.inputs.size() - count;
  out.writeBits(count, 4);
  for (usize i = first; i < message.inputs.size(); ++i) {
    out.writeBits(axisToWire(message.inputs[i].move.x), kAxisBits);
    out.writeBits(axisToWire(message.inputs[i].move.y), kAxisBits);
    out.writeBits(message.inputs[i].buttons & ((1u << kButtonBits) - 1u), kButtonBits);
  }
  return out.take();
}

// The wire format of a snapshot (all after the 4-bit kind):
//   tick u32, baseline age varU32 (0: full; else the baseline is tick - age),
//   lastAckedInputSeq u32, has-your-position bit [+ 2 x f32],
//   [delta only: removed count varU32, then each removed id varU32,]
//   listed count varU32, then for each listed entity, in increasing id order:
//     id: 1 bit "previous id + 1", else a varU32 gap past it;
//     in the baseline: position-changed bit [+ x, y each: a 2-bit size,
//       then a 7-, 10- or 13-bit offset, or the full 16 bits],
//       rotation-changed bit [+ small bit, then a 7-bit offset or 10 bits];
//     not in the baseline (or full): x 16, y 16, rotation 10 bits.
std::vector<std::byte> encode(const Snapshot& message, const Snapshot* baseline) {
  std::vector<QuantizedEntity> current = message.entities;
  if (!std::is_sorted(current.begin(), current.end(), byId)) std::sort(current.begin(), current.end(), byId);
  if (current.size() > kMaxSnapshotEntities) current.resize(kMaxSnapshotEntities);
  if (baseline != nullptr && baseline->tick >= message.tick) baseline = nullptr;  // not older: send it full

  net::BitWriter out = begin(MessageKind::Snapshot);
  out.writeU32(message.tick);
  out.writeVarU32(baseline != nullptr ? message.tick - baseline->tick : 0u);
  out.writeU32(message.lastAckedInputSeq);
  out.writeBool(message.yourPosition.has_value());
  if (message.yourPosition) writeExactVec2(out, *message.yourPosition);

  // What changed: walk both lists (sorted by id) side by side.
  std::vector<u32> removed;
  std::vector<std::pair<const QuantizedEntity*, const QuantizedEntity*>> listed;  // {current, its baseline or null}
  const std::vector<QuantizedEntity> none;
  const std::vector<QuantizedEntity>& old = baseline != nullptr ? baseline->entities : none;
  usize i = 0;
  usize j = 0;
  while (i < current.size() || j < old.size()) {
    if (j == old.size() || (i < current.size() && current[i].id < old[j].id)) {
      listed.emplace_back(&current[i++], nullptr);  // new
    } else if (i == current.size() || old[j].id < current[i].id) {
      removed.push_back(old[j++].id);  // gone
    } else {
      if (!(current[i] == old[j])) listed.emplace_back(&current[i], &old[j]);  // changed; unchanged costs nothing
      ++i;
      ++j;
    }
  }

  if (baseline != nullptr) {
    out.writeVarU32(static_cast<u32>(removed.size()));
    for (const u32 id : removed) out.writeVarU32(id);
  }
  out.writeVarU32(static_cast<u32>(listed.size()));
  u64 expected = 0;  // the id that costs one bit
  for (const auto& [entity, base] : listed) {
    out.writeBool(entity->id == expected);
    if (entity->id != expected) out.writeVarU32(static_cast<u32>(entity->id - expected));
    expected = u64{entity->id} + 1;
    if (base == nullptr) {
      out.writeBits(entity->x, kPositionBits);
      out.writeBits(entity->y, kPositionBits);
      out.writeBits(entity->rotation, kRotationBits);
      continue;
    }
    const bool moved = entity->x != base->x || entity->y != base->y;
    out.writeBool(moved);
    if (moved) {
      writeComponent(out, entity->x, base->x);
      writeComponent(out, entity->y, base->y);
    }
    const bool turned = entity->rotation != base->rotation;
    out.writeBool(turned);
    if (turned) writeTurn(out, entity->rotation, base->rotation);
  }
  return out.take();
}

std::vector<std::byte> encode(const CutsceneStart& message) {
  net::BitWriter out = begin(MessageKind::CutsceneStart);
  out.writeVarU32(message.sequenceId);
  out.writeU32(message.startTick);
  out.writeVarU32(message.durationTicks);
  return out.take();
}

std::vector<std::byte> encode(const CutsceneEnd& message) {
  net::BitWriter out = begin(MessageKind::CutsceneEnd);
  out.writeVarU32(message.sequenceId);
  out.writeU32(message.endTick);
  return out.take();
}

std::optional<CutsceneStart> decodeCutsceneStart(std::span<const std::byte> packet) {
  auto in = open(packet, MessageKind::CutsceneStart);
  if (!in) return std::nullopt;
  CutsceneStart message;
  message.sequenceId = in->readVarU32();
  message.startTick = in->readU32();
  message.durationTicks = in->readVarU32();
  if (!in->ok() || message.durationTicks == 0) return std::nullopt;
  return message;
}

std::optional<CutsceneEnd> decodeCutsceneEnd(std::span<const std::byte> packet) {
  auto in = open(packet, MessageKind::CutsceneEnd);
  if (!in) return std::nullopt;
  CutsceneEnd message;
  message.sequenceId = in->readVarU32();
  message.endTick = in->readU32();
  if (!in->ok()) return std::nullopt;
  return message;
}

std::optional<MessageKind> peekKind(std::span<const std::byte> packet) {
  net::BitReader in(packet);
  const auto kind = static_cast<u32>(in.readBits(kKindBits));
  if (!in.ok() || kind < static_cast<u32>(MessageKind::Hello) || kind > static_cast<u32>(MessageKind::CutsceneEnd)) {
    return std::nullopt;
  }
  return static_cast<MessageKind>(kind);
}

std::optional<Hello> decodeHello(std::span<const std::byte> packet) {
  auto in = open(packet, MessageKind::Hello);
  if (!in) return std::nullopt;
  Hello message;
  message.version = in->readU16();
  message.name = in->readString(kMaxNameLength);
  if (!in->ok()) return std::nullopt;
  return message;
}

std::optional<Welcome> decodeWelcome(std::span<const std::byte> packet) {
  auto in = open(packet, MessageKind::Welcome);
  if (!in) return std::nullopt;
  Welcome message;
  message.version = in->readU16();
  message.tickRate = in->readU16();
  message.serverTick = in->readU32();
  message.playerId = in->readVarU32();
  message.spawnPosition = readExactVec2(*in);
  if (!in->ok() || !std::isfinite(message.spawnPosition.x) || !std::isfinite(message.spawnPosition.y)) {
    return std::nullopt;
  }
  return message;
}

std::optional<Reject> decodeReject(std::span<const std::byte> packet) {
  auto in = open(packet, MessageKind::Reject);
  if (!in) return std::nullopt;
  const auto reason = static_cast<u32>(in->readBits(kRejectReasonBits));
  Reject message;
  message.serverVersion = in->readU16();
  if (!in->ok() || reason < static_cast<u32>(RejectReason::VersionMismatch) ||
      reason > static_cast<u32>(RejectReason::ServerFull)) {
    return std::nullopt;
  }
  message.reason = static_cast<RejectReason>(reason);
  return message;
}

std::optional<Spawn> decodeSpawn(std::span<const std::byte> packet) {
  auto in = open(packet, MessageKind::Spawn);
  if (!in) return std::nullopt;
  const u32 count = in->readVarU32();
  if (!in->ok() || count > kMaxEntitiesPerMessage) return std::nullopt;
  Spawn message;
  message.entities.reserve(count);
  for (u32 i = 0; i < count; ++i) {
    const u32 id = in->readVarU32();
    const auto kind = static_cast<u32>(in->readBits(kEntityKindBits));
    if (kind > static_cast<u32>(EntityKind::Player)) return std::nullopt;
    message.entities.push_back({.id = id, .kind = static_cast<EntityKind>(kind)});
  }
  if (!in->ok()) return std::nullopt;
  return message;
}

std::optional<Despawn> decodeDespawn(std::span<const std::byte> packet) {
  auto in = open(packet, MessageKind::Despawn);
  if (!in) return std::nullopt;
  const u32 count = in->readVarU32();
  if (!in->ok() || count > kMaxEntitiesPerMessage) return std::nullopt;
  Despawn message;
  message.ids.reserve(count);
  for (u32 i = 0; i < count; ++i) message.ids.push_back(in->readVarU32());
  if (!in->ok()) return std::nullopt;
  return message;
}

std::optional<Input> decodeInput(std::span<const std::byte> packet) {
  auto in = open(packet, MessageKind::Input);
  if (!in) return std::nullopt;
  Input message;
  message.newestSeq = in->readU32();
  message.lastSnapshotTick = in->readU32();
  const auto count = static_cast<u32>(in->readBits(4));
  // Sequence numbers start at 1, so the oldest, newestSeq - count + 1, must be at least 1.
  if (!in->ok() || count == 0 || count > kMaxInputsPerPacket || message.newestSeq < count) return std::nullopt;
  message.inputs.reserve(count);
  for (u32 i = 0; i < count; ++i) {
    PlayerInput input;
    input.move.x = axisFromWire(static_cast<u32>(in->readBits(kAxisBits)));
    input.move.y = axisFromWire(static_cast<u32>(in->readBits(kAxisBits)));
    input.buttons = static_cast<u8>(in->readBits(kButtonBits));
    message.inputs.push_back(input);
  }
  if (!in->ok()) return std::nullopt;
  return message;
}

std::optional<SnapshotTicks> peekSnapshot(std::span<const std::byte> packet) {
  auto in = open(packet, MessageKind::Snapshot);
  if (!in) return std::nullopt;
  const u32 tick = in->readU32();
  const u32 age = in->readVarU32();
  if (!in->ok() || age > tick) return std::nullopt;
  return SnapshotTicks{.tick = tick, .baselineTick = age == 0 ? 0 : tick - age};
}

std::optional<Snapshot> decodeSnapshot(std::span<const std::byte> packet, const Snapshot* baseline) {
  auto in = open(packet, MessageKind::Snapshot);
  if (!in) return std::nullopt;
  Snapshot message;
  message.tick = in->readU32();
  const u32 age = in->readVarU32();
  if (!in->ok() || age > message.tick) return std::nullopt;
  const bool delta = age != 0;
  message.baselineTick = delta ? message.tick - age : 0;
  if (delta && (baseline == nullptr || baseline->tick != message.baselineTick)) return std::nullopt;  // cannot rebuild it
  message.lastAckedInputSeq = in->readU32();
  if (in->readBool()) {
    const glm::vec2 position = readExactVec2(*in);
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) return std::nullopt;
    message.yourPosition = position;
  }

  // Start from the baseline, without what is gone. (Never size a vector
  // from an unchecked number: counts are checked against the limit first.)
  std::vector<QuantizedEntity> kept;
  if (delta) {
    const u32 removedCount = in->readVarU32();
    if (!in->ok() || removedCount > kMaxSnapshotEntities) return std::nullopt;
    std::vector<u32> removed;
    removed.reserve(removedCount);
    for (u32 r = 0; r < removedCount; ++r) removed.push_back(in->readVarU32());
    if (!in->ok()) return std::nullopt;
    std::sort(removed.begin(), removed.end());
    if (std::adjacent_find(removed.begin(), removed.end()) != removed.end()) return std::nullopt;
    kept.reserve(baseline->entities.size());
    usize matched = 0;
    for (const QuantizedEntity& e : baseline->entities) {
      if (std::binary_search(removed.begin(), removed.end(), e.id)) {
        ++matched;
      } else {
        kept.push_back(e);
      }
    }
    if (matched != removed.size()) return std::nullopt;  // removing something that was not there
  }

  const u32 listedCount = in->readVarU32();
  if (!in->ok() || listedCount > kMaxSnapshotEntities) return std::nullopt;
  std::vector<QuantizedEntity> listed;
  listed.reserve(listedCount);
  u64 expected = 0;
  usize k = 0;  // walks `kept` alongside, both in id order
  for (u32 n = 0; n < listedCount; ++n) {
    const u64 id = in->readBool() ? expected : expected + in->readVarU32();
    if (id > 0xFFFFFFFFull) return std::nullopt;
    expected = id + 1;
    while (k < kept.size() && kept[k].id < id) ++k;
    const QuantizedEntity* base = k < kept.size() && kept[k].id == id ? &kept[k] : nullptr;
    QuantizedEntity e{.id = static_cast<u32>(id)};
    if (base == nullptr) {
      e.x = static_cast<u16>(in->readBits(kPositionBits));
      e.y = static_cast<u16>(in->readBits(kPositionBits));
      e.rotation = static_cast<u16>(in->readBits(kRotationBits));
    } else {
      e.x = base->x;
      e.y = base->y;
      e.rotation = base->rotation;
      if (in->readBool()) {
        e.x = readComponent(*in, base->x);
        e.y = readComponent(*in, base->y);
      }
      if (in->readBool()) e.rotation = readTurn(*in, base->rotation);
    }
    listed.push_back(e);
  }
  if (!in->ok()) return std::nullopt;

  // Merge: the listed ones replace or join the kept ones.
  message.entities.reserve(kept.size() + listed.size());
  usize a = 0;
  usize b = 0;
  while (a < kept.size() || b < listed.size()) {
    if (b == listed.size() || (a < kept.size() && kept[a].id < listed[b].id)) {
      message.entities.push_back(kept[a++]);
    } else {
      if (a < kept.size() && kept[a].id == listed[b].id) ++a;
      message.entities.push_back(listed[b++]);
    }
  }
  if (message.entities.size() > kMaxSnapshotEntities) return std::nullopt;
  return message;
}

}  // namespace playground::game::protocol
