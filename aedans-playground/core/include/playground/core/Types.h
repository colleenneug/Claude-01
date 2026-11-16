#pragma once
// =============================================================================
//  playground/core/Types.h — fixed-width aliases used across the engine.
//
//  Short names for the types that appear in nearly every signature. Sizes are
//  exact, because data that crosses to the GPU or the network must be.
// =============================================================================

#include <cstddef>
#include <cstdint>

namespace playground {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;
using usize = std::size_t;

}  // namespace playground
