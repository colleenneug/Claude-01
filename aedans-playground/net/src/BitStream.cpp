#include "playground/net/BitStream.h"

#include "playground/core/Assert.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace playground::net {

// --- BitWriter -------------------------------------------------------------------------

void BitWriter::writeBits(u64 value, u32 count) {
  PG_ASSERT(count <= 64, "writeBits: at most 64 bits at a time");
  count = std::min(count, 64u);
  if (count < 64) {
    PG_ASSERT((value >> count) == 0, "writeBits: the value does not fit in the bits given");
    value &= (u64{1} << count) - 1;
  }
  // Up to eight bits at a time: the rest of the current byte, or less.
  while (count > 0) {
    const usize byteIndex = bits_ / 8;
    const u32 offset = static_cast<u32>(bits_ % 8);
    if (byteIndex == bytes_.size()) bytes_.push_back(std::byte{0});
    const u32 take = std::min(8u - offset, count);
    const u32 chunk = static_cast<u32>(value & ((u64{1} << take) - 1));
    bytes_[byteIndex] |= static_cast<std::byte>(chunk << offset);
    value >>= take;
    count -= take;
    bits_ += take;
  }
}

void BitWriter::writeI32(i32 value) { writeU32(std::bit_cast<u32>(value)); }

void BitWriter::writeF32(f32 value) { writeU32(std::bit_cast<u32>(value)); }

void BitWriter::writeVarU32(u32 value) {
  do {
    const u32 group = value & 0x7Fu;
    value >>= 7;
    writeBits(group | (value != 0 ? 0x80u : 0u), 8);
  } while (value != 0);
}

void BitWriter::writeString(std::string_view text, u32 maxLength) {
  const usize length = std::min<usize>(text.size(), maxLength);
  writeVarU32(static_cast<u32>(length));
  for (usize i = 0; i < length; ++i) writeU8(static_cast<u8>(text[i]));
}

void BitWriter::writeBytes(std::span<const std::byte> bytes) {
  for (const std::byte b : bytes) writeU8(std::to_integer<u8>(b));
}

void BitWriter::alignToByte() {
  if (bits_ % 8 != 0) writeBits(0, 8 - static_cast<u32>(bits_ % 8));
}

std::vector<std::byte> BitWriter::take() noexcept {
  bits_ = 0;
  return std::exchange(bytes_, {});
}

// --- BitReader -------------------------------------------------------------------------

void BitReader::fail() noexcept {
  failed_ = true;
  position_ = data_.size() * 8;  // nothing more can be read
}

u64 BitReader::readBits(u32 count) noexcept {
  if (count > 64 || failed_ || count > bitsLeft()) {
    fail();
    return 0;
  }
  u64 value = 0;
  u32 shift = 0;
  while (count > 0) {
    const usize byteIndex = position_ / 8;
    const u32 offset = static_cast<u32>(position_ % 8);
    const u32 take = std::min(8u - offset, count);
    const u32 chunk = (std::to_integer<u32>(data_[byteIndex]) >> offset) & ((1u << take) - 1);
    value |= u64{chunk} << shift;
    shift += take;
    count -= take;
    position_ += take;
  }
  return value;
}

i32 BitReader::readI32() noexcept { return std::bit_cast<i32>(readU32()); }

f32 BitReader::readF32() noexcept { return std::bit_cast<f32>(readU32()); }

u32 BitReader::readVarU32() noexcept {
  u32 value = 0;
  for (u32 group = 0; group < 5; ++group) {
    const u32 byte = static_cast<u32>(readBits(8));
    if (failed_) return 0;
    const u32 payload = byte & 0x7Fu;
    if (group == 4 && payload > 0x0Fu) break;  // would not fit in 32 bits
    value |= payload << (7 * group);
    if ((byte & 0x80u) == 0) return value;
  }
  fail();  // too long: malformed
  return 0;
}

std::string BitReader::readString(u32 maxLength) {
  const u32 length = readVarU32();
  if (failed_ || length > maxLength || usize{length} * 8 > bitsLeft()) {
    fail();
    return {};
  }
  std::string text(length, '\0');
  for (u32 i = 0; i < length; ++i) text[i] = static_cast<char>(readU8());
  return text;
}

void BitReader::readBytes(std::span<std::byte> out) noexcept {
  if (out.size() * 8 > bitsLeft()) {
    std::fill(out.begin(), out.end(), std::byte{0});
    fail();
    return;
  }
  for (std::byte& b : out) b = std::byte{readU8()};
}

void BitReader::alignToByte() noexcept {
  const u32 offset = static_cast<u32>(position_ % 8);
  if (offset != 0) position_ += 8 - offset;  // never past the end: the last byte is whole
}

}  // namespace playground::net
