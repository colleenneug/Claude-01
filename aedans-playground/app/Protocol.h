#pragma once
// =============================================================================
//  app/Protocol.h — what the demo's server and client say to each other.
//
//  Every message starts with a 4-bit kind. Two kinds so far:
//
//    Welcome   reliable, once, when a client connects: the protocol version
//              (a client with another version should leave), the tick rate,
//              the server's current tick, how many entities it simulates.
//    Snapshot  unreliable, 20 times a second: the tick, then for each entity
//              its id, position and rotation, QUANTIZED (net/Quantize.h):
//                position   2 x 16 bits over [-1024, 1024]: 1.6 cm steps
//                rotation   10 bits over [0, 2 pi): 0.35 degree steps
//              about 6 bytes an entity instead of 16. Lost ones are fine:
//              the next arrives 50 ms later.
//
//  Decoding never trusts the packet: a short, truncated or wrong-kind one
//  gives std::nullopt (net::BitReader's ok() check), never garbage.
// =============================================================================

#include "playground/core/Types.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <glm/vec2.hpp>

namespace playground::app::protocol {

inline constexpr u16 kVersion = 1;
inline constexpr u16 kDefaultPort = 27015;
inline constexpr u32 kSnapshotEveryTicks = 6;      // 120 Hz / 6 = 20 snapshots a second
inline constexpr u32 kMaxSnapshotEntities = 128;   // keeps a snapshot under one network packet (~1 KB)
inline constexpr f32 kWorldExtent = 1024.0f;       // positions in [-1024, 1024]
inline constexpr u32 kPositionBits = 16;
inline constexpr u32 kRotationBits = 10;

enum class MessageKind : u8 { Welcome = 1, Snapshot = 2 };

struct Welcome {
  u16 version = kVersion;
  u16 tickRate = 120;
  u32 tick = 0;
  u32 entityCount = 0;
};

struct SnapshotEntity {
  u32 id = 0;
  glm::vec2 position{0.0f};
  f32 rotation = 0.0f;  // radians
};

struct Snapshot {
  u32 tick = 0;
  std::vector<SnapshotEntity> entities;
};

[[nodiscard]] std::vector<std::byte> encode(const Welcome& welcome);
[[nodiscard]] std::vector<std::byte> encode(const Snapshot& snapshot);

[[nodiscard]] std::optional<MessageKind> peekKind(std::span<const std::byte> packet);
[[nodiscard]] std::optional<Welcome> decodeWelcome(std::span<const std::byte> packet);
[[nodiscard]] std::optional<Snapshot> decodeSnapshot(std::span<const std::byte> packet);

}  // namespace playground::app::protocol
