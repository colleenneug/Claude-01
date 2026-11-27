#include "playground/net/Quantize.h"

#include "playground/core/Assert.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace playground::net {
namespace {

constexpr f32 kSmallestThreeRange = 0.70710678f + 1e-4f;  // 1 / sqrt(2), with room for rounding

f64 maxSteps(u32 bits) noexcept { return static_cast<f64>((u64{1} << bits) - 1); }

}  // namespace

u32 quantize(f32 value, f32 min, f32 max, u32 bits) noexcept {
  PG_ASSERT(bits >= 1 && bits <= 32, "quantize: 1 to 32 bits");
  PG_ASSERT(max > min, "quantize: an empty range");
  bits = std::clamp(bits, 1u, 32u);
  if (!(max > min) || std::isnan(value)) return 0;
  const f64 t = std::clamp((static_cast<f64>(value) - min) / (static_cast<f64>(max) - min), 0.0, 1.0);
  return static_cast<u32>(std::llround(t * maxSteps(bits)));
}

f32 dequantize(u32 steps, f32 min, f32 max, u32 bits) noexcept {
  bits = std::clamp(bits, 1u, 32u);
  const f64 t = std::min(static_cast<f64>(steps) / maxSteps(bits), 1.0);
  return static_cast<f32>(min + t * (static_cast<f64>(max) - min));
}

f32 quantizationError(f32 min, f32 max, u32 bits) noexcept {
  return static_cast<f32>((static_cast<f64>(max) - min) / maxSteps(std::clamp(bits, 1u, 32u)) / 2.0);
}

void writeQuantized(BitWriter& out, f32 value, f32 min, f32 max, u32 bits) {
  out.writeBits(quantize(value, min, max, bits), bits);
}

f32 readQuantized(BitReader& in, f32 min, f32 max, u32 bits) noexcept {
  return dequantize(static_cast<u32>(in.readBits(bits)), min, max, bits);
}

void writeVec2(BitWriter& out, glm::vec2 value, f32 min, f32 max, u32 bits) {
  writeQuantized(out, value.x, min, max, bits);
  writeQuantized(out, value.y, min, max, bits);
}

glm::vec2 readVec2(BitReader& in, f32 min, f32 max, u32 bits) noexcept {
  const f32 x = readQuantized(in, min, max, bits);
  const f32 y = readQuantized(in, min, max, bits);
  return {x, y};
}

void writeVec3(BitWriter& out, glm::vec3 value, f32 min, f32 max, u32 bits) {
  for (int i = 0; i < 3; ++i) writeQuantized(out, value[i], min, max, bits);
}

glm::vec3 readVec3(BitReader& in, f32 min, f32 max, u32 bits) noexcept {
  glm::vec3 v{0.0f};
  for (int i = 0; i < 3; ++i) v[i] = readQuantized(in, min, max, bits);
  return v;
}

void writeQuaternion(BitWriter& out, const glm::quat& rotation, u32 bitsPerComponent) {
  PG_ASSERT(bitsPerComponent >= 2 && bitsPerComponent <= 30, "writeQuaternion: 2 to 30 bits per component");
  const f32 length = glm::length(rotation);
  const glm::quat q = length > 1e-12f ? rotation / length : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  // glm::quat's components in a fixed order: x, y, z, w.
  std::array<f32, 4> c{q.x, q.y, q.z, q.w};
  u32 largest = 0;
  for (u32 i = 1; i < 4; ++i) {
    if (std::fabs(c[i]) > std::fabs(c[largest])) largest = i;
  }
  const f32 sign = c[largest] < 0.0f ? -1.0f : 1.0f;  // q and -q: the same rotation
  out.writeBits(largest, 2);
  for (u32 i = 0; i < 4; ++i) {
    if (i == largest) continue;
    writeQuantized(out, c[i] * sign, -kSmallestThreeRange, kSmallestThreeRange, bitsPerComponent);
  }
}

glm::quat readQuaternion(BitReader& in, u32 bitsPerComponent) noexcept {
  const u32 largest = static_cast<u32>(in.readBits(2));
  std::array<f32, 4> c{};
  f32 sum = 0.0f;
  for (u32 i = 0; i < 4; ++i) {
    if (i == largest) continue;
    c[i] = readQuantized(in, -kSmallestThreeRange, kSmallestThreeRange, bitsPerComponent);
    sum += c[i] * c[i];
  }
  c[largest] = std::sqrt(std::max(0.0f, 1.0f - sum));
  const glm::quat q(c[3], c[0], c[1], c[2]);  // glm::quat(w, x, y, z)
  const f32 length = glm::length(q);
  return length > 1e-12f ? q / length : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

}  // namespace playground::net
