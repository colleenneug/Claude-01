#pragma once
// =============================================================================
//  playground/net/BitStream.h — packing values into as few bits as they need.
//
//  Network packets should be small: a position that only ever lies between
//  -1000 and 1000 to a millimetre needs 21 bits, not a 32-bit float, and a
//  flag needs 1 bit, not a byte. BitWriter appends values of any width,
//  1 to 64 bits, one after another with no gaps; BitReader reads them back
//  in the same order.
//
//  SAFE BY CONSTRUCTION
//    - Values are moved bit by bit with shifts and masks, never by casting
//      a struct over the buffer: the layout is the same on every CPU (no
//      endianness, padding or alignment surprises) and the sanitizers have
//      nothing to complain about. Bit order: least significant first,
//      filling each byte from its lowest bit.
//    - Floats travel as their exact IEEE-754 bits (std::bit_cast).
//    - A reader NEVER reads past its data. Asking for more than is left (a
//      truncated or hostile packet) returns zeros and sets a sticky failure
//      flag: decode a whole message, then check ok() once.
//
//      BitWriter out;
//      out.writeBits(kind, 4);
//      out.writeVarU32(entityId);
//      out.writeF32(health);
//      host.send(peer, Packet(Channel::Reliable, out.take()));
//
//      BitReader in(packet.bytes());
//      const u32 kind = static_cast<u32>(in.readBits(4));
//      ...
//      if (!in.ok()) { drop the packet: it was too short }
// =============================================================================

#include "playground/core/Types.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace playground::net {

class BitWriter {
 public:
  BitWriter() = default;

  // The low `count` bits of `value` (0..64). Higher bits of `value` must be
  // zero (asserted in debug builds; masked off in release).
  void writeBits(u64 value, u32 count);
  void writeBool(bool value) { writeBits(value ? 1 : 0, 1); }
  void writeU8(u8 value) { writeBits(value, 8); }
  void writeU16(u16 value) { writeBits(value, 16); }
  void writeU32(u32 value) { writeBits(value, 32); }
  void writeU64(u64 value) { writeBits(value, 64); }
  void writeI32(i32 value);  // two's complement, 32 bits
  void writeF32(f32 value);  // the exact IEEE-754 bits
  // Small numbers in fewer bytes: 7 bits per byte-sized group, plus a
  // "more follows" bit. 0..127 take 8 bits; the largest u32, 40.
  void writeVarU32(u32 value);
  // `text` as a length (VarU32) then its bytes. Longer than maxLength:
  // truncated (the reader enforces the same limit).
  void writeString(std::string_view text, u32 maxLength = 1024);
  void writeBytes(std::span<const std::byte> bytes);
  // Pads with zero bits to the next whole byte.
  void alignToByte();

  [[nodiscard]] usize bitCount() const noexcept { return bits_; }
  [[nodiscard]] usize byteCount() const noexcept { return bytes_.size(); }
  [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }
  // Hands over the buffer and starts empty again.
  [[nodiscard]] std::vector<std::byte> take() noexcept;

 private:
  std::vector<std::byte> bytes_;
  usize bits_ = 0;
};

class BitReader {
 public:
  explicit BitReader(std::span<const std::byte> data) noexcept : data_(data) {}

  // The next `count` bits (0..64). Past the end: 0, and ok() turns false.
  [[nodiscard]] u64 readBits(u32 count) noexcept;
  [[nodiscard]] bool readBool() noexcept { return readBits(1) != 0; }
  [[nodiscard]] u8 readU8() noexcept { return static_cast<u8>(readBits(8)); }
  [[nodiscard]] u16 readU16() noexcept { return static_cast<u16>(readBits(16)); }
  [[nodiscard]] u32 readU32() noexcept { return static_cast<u32>(readBits(32)); }
  [[nodiscard]] u64 readU64() noexcept { return readBits(64); }
  [[nodiscard]] i32 readI32() noexcept;
  [[nodiscard]] f32 readF32() noexcept;
  // More than five groups (more than 32 bits) is malformed: 0, and fails.
  [[nodiscard]] u32 readVarU32() noexcept;
  // A length over maxLength, or past the end, fails (returns "").
  [[nodiscard]] std::string readString(u32 maxLength = 1024);
  // Fills `out` completely, or fails (and leaves it zeroed).
  void readBytes(std::span<std::byte> out) noexcept;
  void alignToByte() noexcept;

  // False once anything was read past the end or found malformed. Sticky.
  [[nodiscard]] bool ok() const noexcept { return !failed_; }
  [[nodiscard]] usize bitsLeft() const noexcept { return data_.size() * 8 - position_; }
  [[nodiscard]] usize bitPosition() const noexcept { return position_; }

 private:
  void fail() noexcept;

  std::span<const std::byte> data_;
  usize position_ = 0;  // in bits
  bool failed_ = false;
};

}  // namespace playground::net
