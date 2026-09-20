#pragma once
// =============================================================================
//  erebus/core/Types.h — foundational value types and alignment contracts.
//
//  Everything downstream of this header assumes two things, and both of them
//  are load-bearing for performance rather than style preferences:
//
//  1. HOT DATA IS 16-BYTE ALIGNED. SIMD loads of a float4 from an unaligned
//     address are either a fault or a silent two-load penalty depending on
//     the instruction selected. Transform storage (ecs/Components.h) is
//     allocated through AlignedAllocator so a `movaps` is always legal.
//
//  2. HOT DATA IS TRIVIALLY COPYABLE. The ECS moves components between dense
//     array slots on removal (swap-and-pop). If a component has a non-trivial
//     move constructor that becomes a per-element call instead of a memcpy,
//     which is the single easiest way to turn an O(n) system into a profiler
//     mystery. Storage static_asserts this; see ComponentStorage.
//
//  C++20. We use concepts rather than SFINAE throughout because the error
//  messages are the difference between a five-minute fix and an afternoon.
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <concepts>
#include <memory>
#include <new>
#include <type_traits>

namespace erebus {

// ---------------------------------------------------------------- scalar types
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;

// Seconds. A distinct type because mixing "seconds" and "milliseconds" in a
// fixed-timestep integrator produces a simulation that is wrong by exactly
// 1000x and still runs, which is the worst possible failure mode.
struct Seconds {
  f32 value = 0.0f;
  constexpr explicit Seconds(f32 v = 0.0f) noexcept : value(v) {}
  constexpr operator f32() const noexcept { return value; }
};

// ---------------------------------------------------------------- vector math
// Deliberately small and POD. This is not a math library — link glm, DirectXMath
// or your own; these exist so the headers below are self-contained and so the
// alignment contract above is expressed in the type system.
//
// Vec3 is 16 bytes, not 12. The padding is intentional: an array of 12-byte
// vectors puts three quarters of its elements on unaligned addresses, and the
// w lane is free storage for whatever a system wants to pack alongside.
struct alignas(16) Vec3 {
  f32 x = 0.0f, y = 0.0f, z = 0.0f, _pad = 0.0f;

  constexpr Vec3() noexcept = default;
  constexpr Vec3(f32 x_, f32 y_, f32 z_) noexcept : x(x_), y(y_), z(z_) {}

  constexpr Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
  constexpr Vec3 operator-(const Vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
  constexpr Vec3 operator*(f32 s) const noexcept { return {x * s, y * s, z * s}; }
  constexpr Vec3 operator-() const noexcept { return {-x, -y, -z}; }
  constexpr Vec3& operator+=(const Vec3& o) noexcept { x += o.x; y += o.y; z += o.z; return *this; }
  constexpr Vec3& operator-=(const Vec3& o) noexcept { x -= o.x; y -= o.y; z -= o.z; return *this; }
  constexpr Vec3& operator*=(f32 s) noexcept { x *= s; y *= s; z *= s; return *this; }
};

constexpr f32 dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline f32 length(const Vec3& v) noexcept { return std::sqrt(dot(v, v)); }
constexpr f32 lengthSq(const Vec3& v) noexcept { return dot(v, v); }

// Guarded normalise. An unguarded one is how a zero-length velocity vector
// becomes a NaN position, and a NaN position propagates through the transform
// hierarchy into the depth buffer, where it is very hard to trace back.
inline Vec3 normalize(const Vec3& v, const Vec3& fallback = Vec3{0, 0, -1}) noexcept {
  const f32 len2 = lengthSq(v);
  if (len2 < 1e-12f) return fallback;
  return v * (1.0f / std::sqrt(len2));
}

constexpr Vec3 lerp(const Vec3& a, const Vec3& b, f32 t) noexcept {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// Column-major 4x4, matching GLSL and both Vulkan and D3D12 when the shader
// side is authored column-major. Packed so it can be memcpy'd straight into a
// uniform/constant buffer with no per-element conversion.
struct alignas(16) Mat4 {
  f32 m[16] = {1, 0, 0, 0,
               0, 1, 0, 0,
               0, 0, 1, 0,
               0, 0, 0, 1};
};

// ---------------------------------------------------------------- allocation
// A minimal aligned allocator so std::vector<T, AlignedAllocator<T>> honours
// alignas(16) on T. Before C++17's aligned new this needed platform ifdefs;
// it no longer does, but the vector default allocator still does not respect
// over-aligned types on every standard library, so this stays explicit.
template <typename T, std::size_t Alignment = 16>
struct AlignedAllocator {
  using value_type = T;
  static constexpr std::size_t alignment =
      Alignment > alignof(T) ? Alignment : alignof(T);

  AlignedAllocator() noexcept = default;
  template <typename U>
  explicit AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

  [[nodiscard]] T* allocate(std::size_t n) {
    if (n == 0) return nullptr;
    return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{alignment}));
  }
  void deallocate(T* p, std::size_t) noexcept {
    ::operator delete(p, std::align_val_t{alignment});
  }
  template <typename U>
  struct rebind { using other = AlignedAllocator<U, Alignment>; };

  bool operator==(const AlignedAllocator&) const noexcept { return true; }
};

// ---------------------------------------------------------------- concepts
// What the ECS requires of a component. Enforced at the point of registration
// so the diagnostic names the offending component rather than the inside of a
// container.
template <typename T>
concept Component =
    std::is_trivially_copyable_v<T> &&
    std::is_default_constructible_v<T> &&
    (alignof(T) <= 64);   // beyond a cache line, storage should be bespoke

}  // namespace erebus
