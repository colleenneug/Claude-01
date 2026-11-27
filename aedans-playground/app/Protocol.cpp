#include "Protocol.h"

#include "playground/net/BitStream.h"
#include "playground/net/Quantize.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace playground::app::protocol {
namespace {

constexpr u32 kKindBits = 4;
constexpr f32 kTwoPi = 2.0f * std::numbers::pi_v<f32>;

f32 wrapAngle(f32 radians) {
  const f32 wrapped = std::fmod(radians, kTwoPi);
  return wrapped < 0.0f ? wrapped + kTwoPi : wrapped;
}

}  // namespace

std::vector<std::byte> encode(const Welcome& welcome) {
  net::BitWriter out;
  out.writeBits(static_cast<u32>(MessageKind::Welcome), kKindBits);
  out.writeU16(welcome.version);
  out.writeU16(welcome.tickRate);
  out.writeU32(welcome.tick);
  out.writeVarU32(welcome.entityCount);
  return out.take();
}

std::vector<std::byte> encode(const Snapshot& snapshot) {
  net::BitWriter out;
  out.writeBits(static_cast<u32>(MessageKind::Snapshot), kKindBits);
  out.writeU32(snapshot.tick);
  const usize count = std::min<usize>(snapshot.entities.size(), kMaxSnapshotEntities);
  out.writeVarU32(static_cast<u32>(count));
  for (usize i = 0; i < count; ++i) {
    const SnapshotEntity& e = snapshot.entities[i];
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
  if (!in.ok()) return std::nullopt;
  if (kind == static_cast<u32>(MessageKind::Welcome)) return MessageKind::Welcome;
  if (kind == static_cast<u32>(MessageKind::Snapshot)) return MessageKind::Snapshot;
  return std::nullopt;
}

std::optional<Welcome> decodeWelcome(std::span<const std::byte> packet) {
  net::BitReader in(packet);
  if (in.readBits(kKindBits) != static_cast<u32>(MessageKind::Welcome)) return std::nullopt;
  Welcome welcome;
  welcome.version = in.readU16();
  welcome.tickRate = in.readU16();
  welcome.tick = in.readU32();
  welcome.entityCount = in.readVarU32();
  if (!in.ok()) return std::nullopt;
  return welcome;
}

std::optional<Snapshot> decodeSnapshot(std::span<const std::byte> packet) {
  net::BitReader in(packet);
  if (in.readBits(kKindBits) != static_cast<u32>(MessageKind::Snapshot)) return std::nullopt;
  Snapshot snapshot;
  snapshot.tick = in.readU32();
  const u32 count = in.readVarU32();
  if (!in.ok() || count > kMaxSnapshotEntities) return std::nullopt;  // never size a vector from an unchecked number
  snapshot.entities.reserve(count);
  for (u32 i = 0; i < count; ++i) {
    SnapshotEntity e;
    e.id = in.readVarU32();
    e.position = net::readVec2(in, -kWorldExtent, kWorldExtent, kPositionBits);
    e.rotation = wrapAngle(net::readQuantized(in, 0.0f, kTwoPi, kRotationBits));
    snapshot.entities.push_back(e);
  }
  if (!in.ok()) return std::nullopt;
  return snapshot;
}

}  // namespace playground::app::protocol
