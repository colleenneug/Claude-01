#pragma once
// =============================================================================
//  erebus/render/ClusteredLighting.h — froxel light binning.
//
//  THE PROBLEM
//  Forward shading with a loop over every light is O(pixels x lights) and dies
//  somewhere around eight lights. Deferred fixes the light count but costs a
//  fat G-buffer, makes MSAA painful, and makes per-material variation (cloth,
//  skin, clearcoat) awkward because the G-buffer has to encode every parameter
//  any material might want.
//
//  CLUSTERED FORWARD (Forward+) is the modern answer and is what this
//  blueprint builds around. Divide the view frustum into a 3D grid of
//  "froxels" (frustum voxels), assign every light to the froxels it touches
//  once per frame in a compute pass, and have the pixel shader look up only
//  the lights in its own froxel. Cost becomes O(pixels x lights-per-froxel),
//  which is typically 2-8 rather than hundreds.
//
//  WHY IT BEATS DEFERRED HERE
//    - MSAA works, because shading happens at the forward rasterisation.
//    - Transparency uses the same light list as opaques. In deferred,
//      transparents need a whole second lighting path, and emissive particle
//      effects — the thing that makes a firefight read — are transparent.
//    - Per-material BRDF variation is free: it is still a forward shader.
//  The cost is that you need a depth prepass to avoid shading overdraw, which
//  you want anyway.
//
//  THE GRID
//  X and Y divide the screen uniformly. Z is divided EXPONENTIALLY:
//
//      slice(z) = floor( log(z) * (numSlices / log(far/near))
//                      - numSlices * log(near) / log(far/near) )
//
//  Uniform Z slices would put almost every froxel in the far distance where
//  there is nothing, and lump the entire near field — where the player's own
//  muzzle flash, shields and grenades are — into one slice. The exponential
//  distribution matches how depth precision and screen-space light density
//  actually behave.
//
//  A 16 x 9 x 24 grid is the usual starting point: 3456 froxels, one
//  workgroup each, and a light list that fits comfortably in a structured
//  buffer.
// =============================================================================

#include "erebus/core/Types.h"

#include <vector>

namespace erebus::render {

// GPU-side light record. 32 bytes, deliberately: two per cache line, and it
// maps to a std430 structured buffer with no padding surprises. View-space
// position, because the cluster assignment compute shader works in view space
// and converting per light per froxel would be wasted work.
struct alignas(16) GpuLight {
  f32 positionViewSpace[3] = {0, 0, 0};
  f32 radius = 10.0f;
  f32 colour[3] = {1, 1, 1};
  f32 intensity = 1.0f;
};
static_assert(sizeof(GpuLight) == 32, "GpuLight must match the std430 layout in cluster_assign.comp");

struct ClusterGridConfig {
  u32 tilesX = 16;
  u32 tilesY = 9;
  u32 slicesZ = 24;
  f32 nearPlane = 0.1f;
  f32 farPlane  = 500.0f;
  // Hard cap per froxel. Overflow is dropped, brightest-first, rather than
  // grown: a variable-length list means an unbounded allocation in a compute
  // shader, and dropping the dimmest light in an over-full froxel is a defect
  // nobody can see.
  u32 maxLightsPerCluster = 64;
};

// The CPU-side mirror of the grid. Everything here also exists on the GPU; the
// CPU copy is for the reference implementation, for tests, and for the
// fallback path on hardware without compute.
class ClusterGrid {
 public:
  explicit ClusterGrid(ClusterGridConfig cfg = {});

  [[nodiscard]] u32 clusterCount() const noexcept {
    return cfg_.tilesX * cfg_.tilesY * cfg_.slicesZ;
  }
  [[nodiscard]] const ClusterGridConfig& config() const noexcept { return cfg_; }

  // Which Z slice a view-space depth falls in. The exponential mapping above;
  // shared with the shader, which must use the identical formula or lights
  // land in a slice the pixel does not read.
  [[nodiscard]] u32 sliceForDepth(f32 viewDepth) const noexcept;

  // Precomputed scale/bias so the shader can do the same mapping with one
  // multiply-add instead of two logs:
  //      slice = floor(log2(z) * scale + bias)
  void depthSliceScaleBias(f32& outScale, f32& outBias) const noexcept;

  // Reference CPU binning. The shipping path is the compute shader
  // (shaders/cluster_assign.comp); this exists to validate it — a binning bug
  // shows up as lights popping at froxel boundaries, which is very hard to
  // debug on the GPU and trivial to unit-test here.
  void assign(const std::vector<GpuLight>& lights, f32 verticalFovRadians, f32 aspect);

  // Flattened output, matching the two GPU buffers:
  //   clusterOffsets[i]  = { firstIndex, count } into clusterIndices
  //   clusterIndices[..] = indices into the light array
  [[nodiscard]] const std::vector<u32>& clusterOffsets() const noexcept { return offsets_; }
  [[nodiscard]] const std::vector<u32>& clusterIndices() const noexcept { return indices_; }

 private:
  ClusterGridConfig cfg_;
  std::vector<u32> offsets_;   // 2 entries per cluster: offset, count
  std::vector<u32> indices_;
};

}  // namespace erebus::render
