#pragma once
// =============================================================================
//  erebus/render/RHI.h — the render hardware interface.
//
//  WHY AN ABSTRACTION AT ALL. Vulkan and D3D12 agree on the shape of modern
//  rendering (explicit command lists, descriptor tables/sets, explicit
//  barriers, bindless-capable) and disagree on every spelling. One thin layer
//  over the *shape* costs almost nothing and means the frame graph, the
//  material system and every pass above it compile against both. Abstracting
//  further — a "Renderer" class with DrawMesh() — is the mistake: it hides
//  exactly the barrier and residency control that the explicit APIs exist to
//  give you.
//
//  WHAT THIS LAYER OWNS
//    - device, queues, swapchain
//    - resource creation and lifetime (buffers, textures, pipelines)
//    - command recording
//    - synchronisation primitives
//
//  WHAT IT DOES NOT OWN
//    - what to draw, in what order, with what state. That is the frame graph
//      (FrameGraph.h) and the passes above it.
//
//  BINDLESS. The descriptor model here assumes one large descriptor heap /
//  descriptor set of textures indexed by a u32 in the material. Per-draw
//  descriptor sets are the main CPU cost in a naive modern renderer; with
//  bindless, a material is an integer in a push constant. Build this in from
//  the start — retrofitting it means touching every shader.
// =============================================================================

#include "erebus/core/Types.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace erebus::render {

// Opaque handles. A u32 index plus a generation, same reasoning as ecs::Entity:
// a raw pointer to a GPU resource that has been destroyed and its memory
// recycled is a device-lost at best.
enum class BufferHandle   : u64 { Invalid = ~0ull };
enum class TextureHandle  : u64 { Invalid = ~0ull };
enum class PipelineHandle : u64 { Invalid = ~0ull };
enum class SamplerHandle  : u64 { Invalid = ~0ull };

enum class BufferUsage : u32 {
  Vertex   = 1u << 0,
  Index    = 1u << 1,
  Uniform  = 1u << 2,
  Storage  = 1u << 3,
  Indirect = 1u << 4,
  Upload   = 1u << 5,   // host-visible staging
};

enum class TextureFormat : u32 {
  RGBA8Unorm, RGBA8Srgb, RGBA16Float, RG16Float, R11G11B10Float,
  Depth32Float, Depth24Stencil8, BC7Srgb,
};

// Explicit resource states, because both APIs require explicit barriers and a
// layer that guesses them is a layer that will guess wrong under a frame graph
// that reorders passes.
enum class ResourceState : u32 {
  Undefined, VertexBuffer, IndexBuffer, UniformBuffer, ShaderReadOnly,
  UnorderedAccess, RenderTarget, DepthWrite, DepthRead, CopySource,
  CopyDest, Present,
};

struct BufferDesc {
  u64 sizeBytes = 0;
  u32 usage = 0;                 // bitwise-or of BufferUsage
  bool hostVisible = false;
  std::string_view debugName;    // set it. Every one. RenderDoc without names
                                 // is a list of numbered resources.
};

struct TextureDesc {
  u32 width = 1, height = 1, depth = 1;
  u32 mipLevels = 1, arrayLayers = 1;
  TextureFormat format = TextureFormat::RGBA8Unorm;
  bool renderTarget = false;
  bool storage = false;
  std::string_view debugName;
};

// ---------------------------------------------------------------- commands
class ICommandList {
 public:
  virtual ~ICommandList() = default;

  virtual void beginRenderPass(std::span<const TextureHandle> colourTargets,
                               TextureHandle depthTarget) = 0;
  virtual void endRenderPass() = 0;

  virtual void bindPipeline(PipelineHandle pipeline) = 0;
  virtual void pushConstants(const void* data, u32 sizeBytes) = 0;
  virtual void bindVertexBuffer(BufferHandle buffer, u64 offset) = 0;
  virtual void bindIndexBuffer(BufferHandle buffer, u64 offset, bool sixteenBit) = 0;

  virtual void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) = 0;
  virtual void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex,
                           i32 vertexOffset, u32 firstInstance) = 0;
  // GPU-driven submission. The endgame for draw call cost: the culling compute
  // pass writes the argument buffer and the CPU never knows how many draws
  // there were.
  virtual void drawIndexedIndirect(BufferHandle argsBuffer, u64 offset, u32 drawCount) = 0;

  virtual void dispatch(u32 groupsX, u32 groupsY, u32 groupsZ) = 0;

  // Explicit, and deliberately not inferred. A frame graph can compute these
  // automatically from pass dependencies; this is the primitive it emits.
  virtual void barrier(TextureHandle texture, ResourceState from, ResourceState to) = 0;
  virtual void barrier(BufferHandle buffer, ResourceState from, ResourceState to) = 0;

  // GPU timestamps and a debug label. Both cost nothing in release and both
  // are the difference between profiling a frame and staring at it.
  virtual void beginDebugLabel(std::string_view name) = 0;
  virtual void endDebugLabel() = 0;
};

// ---------------------------------------------------------------- device
class IRenderDevice {
 public:
  virtual ~IRenderDevice() = default;

  [[nodiscard]] virtual BufferHandle createBuffer(const BufferDesc&) = 0;
  [[nodiscard]] virtual TextureHandle createTexture(const TextureDesc&) = 0;
  virtual void destroyBuffer(BufferHandle) = 0;
  virtual void destroyTexture(TextureHandle) = 0;

  // Persistently mapped upload. Per-frame data (the frame uniform block, the
  // light buffer) lives in a ring buffer that is mapped once at creation and
  // never unmapped — map/unmap per frame is a measurable cost and, on some
  // drivers, a stall.
  [[nodiscard]] virtual void* mapBuffer(BufferHandle) = 0;

  [[nodiscard]] virtual ICommandList* beginFrame() = 0;
  virtual void endFrame(ICommandList*) = 0;

  // How many frames may be in flight. Two is the right default: three adds a
  // frame of input latency for throughput nobody asked for in a shooter.
  [[nodiscard]] virtual u32 framesInFlight() const = 0;

  virtual void waitIdle() = 0;
};

// Factories. Implementations live in src/render/vulkan and src/render/d3d12;
// which one is built is a CMake option, so a platform never compiles the
// other's headers.
std::unique_ptr<IRenderDevice> createVulkanDevice(void* nativeWindowHandle);
std::unique_ptr<IRenderDevice> createD3D12Device(void* nativeWindowHandle);

}  // namespace erebus::render
