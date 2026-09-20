#include "erebus/render/ClusteredLighting.h"

#include <algorithm>
#include <cmath>

namespace erebus::render {

ClusterGrid::ClusterGrid(ClusterGridConfig cfg) : cfg_(cfg) {
  offsets_.assign(static_cast<std::size_t>(clusterCount()) * 2, 0);
}

void ClusterGrid::depthSliceScaleBias(f32& outScale, f32& outBias) const noexcept {
  // slice = log2(z) * scale + bias, derived from
  //   slice = numSlices * log(z/near) / log(far/near)
  const f32 logRatio = std::log2(cfg_.farPlane / cfg_.nearPlane);
  outScale = static_cast<f32>(cfg_.slicesZ) / logRatio;
  outBias  = -(static_cast<f32>(cfg_.slicesZ) * std::log2(cfg_.nearPlane) / logRatio);
}

u32 ClusterGrid::sliceForDepth(f32 viewDepth) const noexcept {
  if (viewDepth <= cfg_.nearPlane) return 0;
  f32 scale = 0.0f, bias = 0.0f;
  depthSliceScaleBias(scale, bias);
  const i32 slice = static_cast<i32>(std::floor(std::log2(viewDepth) * scale + bias));
  return static_cast<u32>(std::clamp<i32>(slice, 0, static_cast<i32>(cfg_.slicesZ) - 1));
}

void ClusterGrid::assign(const std::vector<GpuLight>& lights, f32 verticalFovRadians, f32 aspect) {
  const u32 count = clusterCount();
  offsets_.assign(static_cast<std::size_t>(count) * 2, 0);
  indices_.clear();

  // Per-cluster buckets. The GPU version writes into a preallocated
  // maxLightsPerCluster-strided buffer with an atomic counter per cluster;
  // this CPU reference builds vectors because it is the readable version and
  // correctness, not throughput, is its job.
  std::vector<std::vector<u32>> buckets(count);

  const f32 tanHalfV = std::tan(verticalFovRadians * 0.5f);
  const f32 tanHalfH = tanHalfV * aspect;

  for (u32 li = 0; li < lights.size(); ++li) {
    const GpuLight& L = lights[li];
    // View space, right-handed, camera looking down -Z. Depth is therefore
    // positive -Z; getting this sign wrong bins every light into slice 0,
    // which looks like "clustering does nothing" rather than like a bug.
    const f32 lz = -L.positionViewSpace[2];
    const f32 r  = L.radius;
    if (lz + r <= cfg_.nearPlane || lz - r >= cfg_.farPlane) continue;

    const u32 z0 = sliceForDepth(std::max(cfg_.nearPlane, lz - r));
    const u32 z1 = sliceForDepth(std::min(cfg_.farPlane, lz + r));

    for (u32 z = z0; z <= z1; ++z) {
      // The screen-space extent of the sphere at this slice's near depth.
      // Using the slice's own depth rather than the light's centre depth is
      // what stops a large light near the camera from being clipped out of
      // the tiles it actually covers further back.
      f32 scale = 0.0f, bias = 0.0f;
      depthSliceScaleBias(scale, bias);
      const f32 sliceNear = std::exp2((static_cast<f32>(z) - bias) / scale);
      const f32 refDepth = std::max(cfg_.nearPlane, std::min(std::max(sliceNear, lz - r), lz));

      // Half-extent of the sphere projected at refDepth, in NDC.
      const f32 halfX = r / (refDepth * tanHalfH);
      const f32 halfY = r / (refDepth * tanHalfV);
      const f32 cx = L.positionViewSpace[0] / (refDepth * tanHalfH);
      const f32 cy = L.positionViewSpace[1] / (refDepth * tanHalfV);

      auto ndcToTile = [](f32 ndc, u32 tiles) -> i32 {
        return static_cast<i32>(std::floor((ndc * 0.5f + 0.5f) * static_cast<f32>(tiles)));
      };
      const i32 x0 = std::clamp(ndcToTile(cx - halfX, cfg_.tilesX), 0, static_cast<i32>(cfg_.tilesX) - 1);
      const i32 x1 = std::clamp(ndcToTile(cx + halfX, cfg_.tilesX), 0, static_cast<i32>(cfg_.tilesX) - 1);
      const i32 y0 = std::clamp(ndcToTile(cy - halfY, cfg_.tilesY), 0, static_cast<i32>(cfg_.tilesY) - 1);
      const i32 y1 = std::clamp(ndcToTile(cy + halfY, cfg_.tilesY), 0, static_cast<i32>(cfg_.tilesY) - 1);

      for (i32 y = y0; y <= y1; ++y) {
        for (i32 x = x0; x <= x1; ++x) {
          const u32 idx = (z * cfg_.tilesY + static_cast<u32>(y)) * cfg_.tilesX + static_cast<u32>(x);
          auto& bucket = buckets[idx];
          if (bucket.size() < cfg_.maxLightsPerCluster) bucket.push_back(li);
          // Overflow is dropped. See the note on maxLightsPerCluster: a
          // dropped 65th light in one froxel is invisible; a dynamically
          // grown list in a compute shader is not implementable.
        }
      }
    }
  }

  // Flatten.
  for (u32 c = 0; c < count; ++c) {
    offsets_[c * 2 + 0] = static_cast<u32>(indices_.size());
    offsets_[c * 2 + 1] = static_cast<u32>(buckets[c].size());
    indices_.insert(indices_.end(), buckets[c].begin(), buckets[c].end());
  }
}

}  // namespace erebus::render
