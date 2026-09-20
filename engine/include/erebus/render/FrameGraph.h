#pragma once
// =============================================================================
//  erebus/render/FrameGraph.h — pass declaration, barriers and aliasing.
//
//  A frame graph is a declarative list of passes with their reads and writes.
//  From it you get three things that are painful to maintain by hand:
//
//    1. AUTOMATIC BARRIERS. The graph knows the last writer and the next
//       reader of every resource, so it emits the transitions. Hand-written
//       barriers are the most common source of "works on NVIDIA, corrupts on
//       AMD".
//    2. TRANSIENT ALIASING. Two passes whose lifetimes do not overlap share
//       memory. On a deferred+post stack this is routinely 30-40% of render
//       target memory.
//    3. CULLING. A pass whose outputs are never read is not executed. Sounds
//       theoretical until a debug view or a disabled feature leaves one behind.
//
//  THE FRAME, IN ORDER
//
//    depth prepass          -> depth               (fills Hi-Z; stops overdraw)
//    cluster assignment     -> light grid          (compute; cluster_assign.comp)
//    shadow cascades        -> cascade atlas       (4 slices, fitted to view)
//    opaque forward         -> HDR colour, motion  (pbr.frag)
//    volumetric inject      -> scattering volume   (volumetric_scatter.comp)
//    volumetric integrate   -> integrated volume
//    transparents           -> HDR colour          (same light grid as opaque)
//    TAA resolve            -> HDR history
//    bloom down/upsample    -> bloom chain
//    composite + tonemap    -> LDR backbuffer      (ACES, then sRGB encode)
//
//  GLOBAL ILLUMINATION slots between shadows and opaque. Two realistic
//  options, both stubbed below:
//
//    DDGI (irradiance probe volumes). A grid of probes, each a small
//    octahedral irradiance map, updated by a few rays per probe per frame and
//    temporally accumulated. Handles fully dynamic lighting, needs ray tracing
//    or a screen-space/voxel fallback for the rays. This is the right target.
//
//    Screen-space GI as the fallback: cheap, no build step, and wrong in
//    exactly the way screen space is always wrong (it cannot light from what
//    is off screen). Acceptable as a floor, not as the plan.
// =============================================================================

#include "erebus/core/Types.h"
#include "erebus/render/RHI.h"

#include <functional>
#include <string>
#include <vector>

namespace erebus::render {

enum class PassKind : u8 { Graphics, Compute, Copy };

struct ResourceAccess {
  TextureHandle texture = TextureHandle::Invalid;
  BufferHandle  buffer  = BufferHandle::Invalid;
  ResourceState state   = ResourceState::Undefined;
};

struct PassDesc {
  std::string name;
  PassKind kind = PassKind::Graphics;
  std::vector<ResourceAccess> reads;
  std::vector<ResourceAccess> writes;
  std::function<void(ICommandList&)> execute;
};

class FrameGraph {
 public:
  void addPass(PassDesc desc) { passes_.push_back(std::move(desc)); }

  // Orders passes, culls unreferenced ones, computes barriers, assigns
  // transient memory. Call once per frame after all passes are declared.
  void compile();

  void execute(ICommandList& cmd);

  void reset() { passes_.clear(); }

 private:
  std::vector<PassDesc> passes_;
  std::vector<u32> executionOrder_;
};

// ---------------------------------------------------------------- pass stubs
// Each of these registers one pass. Implemented in src/render/passes/*.cpp.
// They are declared here so the frame's shape is readable in one place.
void addDepthPrepass(FrameGraph&, TextureHandle depth);
void addClusterAssignPass(FrameGraph&, BufferHandle lights, BufferHandle offsets,
                          BufferHandle indices);
void addShadowCascadePass(FrameGraph&, TextureHandle cascadeAtlas);
void addOpaqueForwardPass(FrameGraph&, TextureHandle hdrColour, TextureHandle motion,
                          TextureHandle depth);
void addVolumetricPasses(FrameGraph&, TextureHandle scatterVolume, TextureHandle integrated);
void addTransparentPass(FrameGraph&, TextureHandle hdrColour, TextureHandle depth);
void addTaaResolvePass(FrameGraph&, TextureHandle hdrColour, TextureHandle motion,
                       TextureHandle history);
void addBloomPass(FrameGraph&, TextureHandle hdrColour, TextureHandle bloomChain);
void addCompositePass(FrameGraph&, TextureHandle hdrColour, TextureHandle bloomChain,
                      TextureHandle integratedVolume, TextureHandle backbuffer);

// Global illumination. Not wired by default; see the header note.
void addDdgiProbeUpdatePass(FrameGraph&, TextureHandle probeIrradiance,
                            TextureHandle probeDepth);
void addDdgiApplyPass(FrameGraph&, TextureHandle hdrColour, TextureHandle probeIrradiance);

}  // namespace erebus::render
