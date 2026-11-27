#pragma once
// =============================================================================
//  playground/net/Quantize.h — fewer bits for numbers whose range we know.
//
//  BOUNDED FLOATS: a value known to lie in [min, max] is sent as one of
//  2^bits evenly spaced steps. The worst error is half a step:
//      (max - min) / (2^bits - 1) / 2
//  e.g. [-1000, 1000] in 16 bits: 1.5 cm; in 20 bits: 1 mm. (Past about 20
//  bits for a range that size, f32 itself is the limit: near 1000 floats
//  are 0.00006 apart, so more bits buy nothing.) Values outside
//  the range are clamped to it (and NaN becomes min): the receiver always
//  gets something inside the range it expects.
//
//  ROTATIONS, "SMALLEST THREE": a unit quaternion (x, y, z, w) has
//  x² + y² + z² + w² = 1, so any three components give the fourth, up to
//  its sign. And q and -q are the same rotation, so the sign can be chosen
//  positive. So: drop the LARGEST component (send which one, 2 bits), flip
//  the quaternion so it is positive, send the other three. Those three are
//  each at most 1/sqrt(2) in size (otherwise they would be the largest), so
//  their range is [-0.7072, 0.7072], not [-1, 1]: finer steps for the same
//  bits. At 10 bits each: 32 bits per rotation instead of 128, accurate to
//  about a tenth of a degree.
// =============================================================================

#include "playground/core/Types.h"
#include "playground/net/BitStream.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace playground::net {

// `value` in [min, max] as an integer in [0, 2^bits - 1]; bits 1..32.
[[nodiscard]] u32 quantize(f32 value, f32 min, f32 max, u32 bits) noexcept;
[[nodiscard]] f32 dequantize(u32 steps, f32 min, f32 max, u32 bits) noexcept;
// The worst-case error of quantize + dequantize: half a step.
[[nodiscard]] f32 quantizationError(f32 min, f32 max, u32 bits) noexcept;

void writeQuantized(BitWriter& out, f32 value, f32 min, f32 max, u32 bits);
[[nodiscard]] f32 readQuantized(BitReader& in, f32 min, f32 max, u32 bits) noexcept;

// Each component in the same [min, max] at the same bits.
void writeVec2(BitWriter& out, glm::vec2 value, f32 min, f32 max, u32 bits);
[[nodiscard]] glm::vec2 readVec2(BitReader& in, f32 min, f32 max, u32 bits) noexcept;
void writeVec3(BitWriter& out, glm::vec3 value, f32 min, f32 max, u32 bits);
[[nodiscard]] glm::vec3 readVec3(BitReader& in, f32 min, f32 max, u32 bits) noexcept;

// Smallest-three: 2 + 3 x bitsPerComponent bits (bitsPerComponent 2..30).
// `rotation` need not be normalised: it is normalised first.
void writeQuaternion(BitWriter& out, const glm::quat& rotation, u32 bitsPerComponent = 10);
[[nodiscard]] glm::quat readQuaternion(BitReader& in, u32 bitsPerComponent = 10) noexcept;

}  // namespace playground::net
