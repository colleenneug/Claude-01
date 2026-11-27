#pragma once
// =============================================================================
//  playground/game/Protocol.h — what the game's server and clients say to
//  each other (protocol version 2).
//
//  Every message starts with a 4-bit kind:
//
//    client -> server
//      Hello     reliable, once, first thing: the protocol version and a
//                name. A server answers Welcome, or Reject and goodbye.
//      Input     unreliable, every 120 Hz tick: the newest input's sequence
//                number and the last few inputs (oldest first), so one lost
//                packet loses nothing: the next carries the same inputs again.
//
//    server -> client
//      Welcome   reliable: the version, the tick rate, the server's tick,
//                the id of the entity this client now controls and exactly
//                where it starts.
//      Reject    reliable, then goodbye: why (another protocol version...).
//      Spawn     reliable: entities that now exist (id and kind). A client
//                draws only entities it has been told about.
//      Despawn   reliable: entities that are gone.
//      Snapshot  unreliable, 20 a second, one per client: the server tick,
//                the newest of THIS client's inputs the server has applied
//                (lastAckedInputSeq) and where that left its player, exactly
//                (two f32: the client checks its prediction against it),
//                then every entity's id, position and rotation QUANTIZED
//                (net/Quantize.h):
//                  position   2 x 16 bits over [-1024, 1024]: 1.6 cm steps
//                  rotation   10 bits over [0, 2 pi): 0.35 degree steps
//
//  Decoding never trusts the packet: a short, truncated, oversized or
//  wrong-kind one gives std::nullopt (net::BitReader's ok() check), never
//  garbage, and no count read from a packet sizes anything before it has
//  been checked against its limit.
// =============================================================================

#include "playground/core/Types.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec2.hpp>

namespace playground::game {

enum class EntityKind : u8 { Ball = 0, Player = 1 };

// One tick of a player's controls. `move` components are in [-1, 1].
struct PlayerInput {
  glm::vec2 move{0.0f};
  u8 buttons = 0;  // 4 bits: Fire = 1 (not used by the simulation yet)

  friend bool operator==(const PlayerInput&, const PlayerInput&) = default;
};

namespace protocol {

inline constexpr u16 kVersion = 2;
inline constexpr u16 kDefaultPort = 27015;
inline constexpr u16 kTickRate = 120;
inline constexpr u32 kSnapshotEveryTicks = 6;        // 120 Hz / 6 = 20 snapshots a second
inline constexpr u32 kMaxSnapshotEntities = 128;     // keeps a snapshot under one network packet (~1 KB)
inline constexpr u32 kMaxEntitiesPerMessage = 256;   // Spawn / Despawn lists
inline constexpr u32 kMaxInputsPerPacket = 8;        // the redundancy: 8 ticks (67 ms) of input in each packet
inline constexpr u32 kMaxNameLength = 32;
inline constexpr f32 kWorldExtent = 1024.0f;         // positions in [-1024, 1024]
inline constexpr u32 kPositionBits = 16;
inline constexpr u32 kRotationBits = 10;
inline constexpr u32 kButtonBits = 4;

enum class MessageKind : u8 { Hello = 1, Welcome, Reject, Spawn, Despawn, Input, Snapshot };

enum class RejectReason : u8 { VersionMismatch = 1, ServerFull = 2 };
[[nodiscard]] std::string_view describe(RejectReason reason) noexcept;

struct Hello {
  u16 version = kVersion;
  std::string name;  // at most kMaxNameLength bytes
};

struct Welcome {
  u16 version = kVersion;
  u16 tickRate = kTickRate;
  u32 serverTick = 0;
  u32 playerId = 0;              // the entity this client controls
  glm::vec2 spawnPosition{0.0f};  // exact
};

struct Reject {
  RejectReason reason = RejectReason::VersionMismatch;
  u16 serverVersion = kVersion;
};

struct SpawnEntry {
  u32 id = 0;
  EntityKind kind = EntityKind::Ball;
  friend bool operator==(const SpawnEntry&, const SpawnEntry&) = default;
};

struct Spawn {
  std::vector<SpawnEntry> entities;
};

struct Despawn {
  std::vector<u32> ids;
};

struct Input {
  u32 newestSeq = 0;                // inputs.back() has this sequence number, the one before it newestSeq - 1...
  std::vector<PlayerInput> inputs;  // oldest first; 1..kMaxInputsPerPacket
};

struct SnapshotEntity {
  u32 id = 0;
  glm::vec2 position{0.0f};
  f32 rotation = 0.0f;  // radians
};

struct Snapshot {
  u32 tick = 0;
  u32 lastAckedInputSeq = 0;              // 0: none of this client's inputs applied yet
  std::optional<glm::vec2> yourPosition;  // this client's player after that input, exactly
  std::vector<SnapshotEntity> entities;
};

// What the server would see of `input`: each axis rounded to 1/127 (0 stays
// exactly 0), buttons to 4 bits. The client predicts with this, so its
// prediction uses the very same numbers as the server.
[[nodiscard]] PlayerInput quantize(const PlayerInput& input) noexcept;

[[nodiscard]] std::vector<std::byte> encode(const Hello& message);
[[nodiscard]] std::vector<std::byte> encode(const Welcome& message);
[[nodiscard]] std::vector<std::byte> encode(const Reject& message);
[[nodiscard]] std::vector<std::byte> encode(const Spawn& message);
[[nodiscard]] std::vector<std::byte> encode(const Despawn& message);
[[nodiscard]] std::vector<std::byte> encode(const Input& message);
[[nodiscard]] std::vector<std::byte> encode(const Snapshot& message);

[[nodiscard]] std::optional<MessageKind> peekKind(std::span<const std::byte> packet);
[[nodiscard]] std::optional<Hello> decodeHello(std::span<const std::byte> packet);
[[nodiscard]] std::optional<Welcome> decodeWelcome(std::span<const std::byte> packet);
[[nodiscard]] std::optional<Reject> decodeReject(std::span<const std::byte> packet);
[[nodiscard]] std::optional<Spawn> decodeSpawn(std::span<const std::byte> packet);
[[nodiscard]] std::optional<Despawn> decodeDespawn(std::span<const std::byte> packet);
[[nodiscard]] std::optional<Input> decodeInput(std::span<const std::byte> packet);
[[nodiscard]] std::optional<Snapshot> decodeSnapshot(std::span<const std::byte> packet);

}  // namespace protocol
}  // namespace playground::game
