#include "playground/game/Protocol.h"

#include "playground/net/BitStream.h"
#include "playground/net/Quantize.h"

#include <algorithm>
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

std::vector<std::byte> encode(const Snapshot& message) {
  net::BitWriter out = begin(MessageKind::Snapshot);
  out.writeU32(message.tick);
  out.writeU32(message.lastAckedInputSeq);
  out.writeBool(message.yourPosition.has_value());
  if (message.yourPosition) writeExactVec2(out, *message.yourPosition);
  const usize count = std::min<usize>(message.entities.size(), kMaxSnapshotEntities);
  out.writeVarU32(static_cast<u32>(count));
  for (usize i = 0; i < count; ++i) {
    const SnapshotEntity& e = message.entities[i];
    out.writeVarU32(e.id);
    net::writeVec2(out, e.position, -kWorldExtent, kWorldExtent, kPositionBits);
    // The top step would equal 2 pi, which is 0 again: quantize over [0, 2 pi] and wrap on read.
    net::writeQuantized(out, wrapAngle(e.rotation), 0.0f, kTwoPi, kRotationBits);
  }
  return out.take();
}

std::optional<MessageKind> peekKind(std::span<const std::byte> packet) {
  net::BitReader in(packet);
  const auto kind = static_cast<u32>(in.readBits(kKindBits));
  if (!in.ok() || kind < static_cast<u32>(MessageKind::Hello) || kind > static_cast<u32>(MessageKind::Snapshot)) {
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

std::optional<Snapshot> decodeSnapshot(std::span<const std::byte> packet) {
  auto in = open(packet, MessageKind::Snapshot);
  if (!in) return std::nullopt;
  Snapshot message;
  message.tick = in->readU32();
  message.lastAckedInputSeq = in->readU32();
  if (in->readBool()) {
    const glm::vec2 position = readExactVec2(*in);
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) return std::nullopt;
    message.yourPosition = position;
  }
  const u32 count = in->readVarU32();
  if (!in->ok() || count > kMaxSnapshotEntities) return std::nullopt;  // never size a vector from an unchecked number
  message.entities.reserve(count);
  for (u32 i = 0; i < count; ++i) {
    SnapshotEntity e;
    e.id = in->readVarU32();
    e.position = net::readVec2(*in, -kWorldExtent, kWorldExtent, kPositionBits);
    e.rotation = wrapAngle(net::readQuantized(*in, 0.0f, kTwoPi, kRotationBits));
    message.entities.push_back(e);
  }
  if (!in->ok()) return std::nullopt;
  return message;
}

}  // namespace playground::game::protocol
