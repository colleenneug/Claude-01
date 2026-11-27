#pragma once
// =============================================================================
//  render/src/VulkanRenderer.h — the Vulkan renderer's class (internal).
//
//  Private to the render layer: other code uses createVulkanRenderer() and
//  sees only render::Renderer. The tests include this header to look inside
//  (swapchain size, frame counts).
//
//  OWNERSHIP AND DESTRUCTION ORDER
//    Members are declared in the order they are created, and C++ destroys
//    them in reverse: per-frame objects, then the command pool, pipeline,
//    swapchain, and last the gpu::Context (device, surface, instance). Every
//    object must die before the device it was made from, so the Context is
//    declared first. The destructor waits for the GPU to go idle before any
//    of that happens.
// =============================================================================

#include "playground/gpu/Buffer.h"
#include "playground/gpu/Context.h"
#include "playground/gpu/DeviceHandle.h"
#include "playground/gpu/Image.h"
#include "playground/gpu/Sampler.h"
#include "playground/gpu/Swapchain.h"
#include "playground/render/Resources.h"
#include "playground/render/VulkanRenderer.h"

#include "Mesh.h"
#include "ResourcePool.h"

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace playground::render {

// How many frames the CPU may be ahead of the GPU. Two lets the CPU record
// frame N+1 while the GPU draws frame N; more adds latency for little gain.
inline constexpr u32 kFramesInFlight = 2;

// Sprite buffers start with room for this many and double when outgrown.
inline constexpr u32 kInitialSpriteCapacity = 1024;
// A hard ceiling, so a runaway spawn fails with a clear message instead of
// asking the driver for gigabytes: 4M sprites x 48 bytes = 192 MiB per frame.
inline constexpr u32 kMaxSprites = 1u << 22;

// Mesh draw lists start with room for this many draws and double when outgrown.
inline constexpr u32 kInitialMeshDrawCapacity = 256;
inline constexpr u32 kMaxMeshDraws = 1u << 20;
inline constexpr u32 kMaxMeshes = 1u << 16;
// Size of the bindless texture array (descriptor set 1). Vulkan guarantees
// at least 500,000 update-after-bind samplers per stage on any GPU with
// descriptor indexing, so this is far inside every limit.
inline constexpr u32 kMaxTextures = 4096;
// The first slots of the array hold the defaults a material falls back to.
inline constexpr u32 kWhiteTexture = 0;       // base colour and metallic-roughness: factors apply unchanged
inline constexpr u32 kFlatNormalTexture = 1;  // normal map: (0.5, 0.5, 1) = straight out of the surface
inline constexpr u32 kDefaultTextureCount = 2;

// One frame copied back to the CPU (requestCapture / takeCapture): for pixel
// tests and screenshots. Always RGBA, 8 bits per channel, top row first.
struct CapturedFrame {
  u32 width = 0;
  u32 height = 0;
  std::vector<u8> rgba;  // width * height * 4

  struct Pixel {
    u8 r, g, b, a;
  };
  [[nodiscard]] Pixel at(u32 x, u32 y) const noexcept {
    const usize i = (static_cast<usize>(y) * width + x) * 4;
    return {rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
  }
};

class VulkanRenderer final : public Renderer {
 public:
  [[nodiscard]] static Result<std::unique_ptr<VulkanRenderer>> create(platform::Platform& platform,
                                                                      platform::Window& window,
                                                                      const VulkanRendererDesc& desc);
  ~VulkanRenderer() override;

  [[nodiscard]] std::string_view name() const noexcept override { return "vulkan"; }
  [[nodiscard]] Status onFramebufferResized(platform::Extent2D size) override;
  [[nodiscard]] Status renderFrame(const FrameTime& time, const FrameData& frame) override;

  [[nodiscard]] Result<MeshHandle> createMesh(const MeshData& mesh, std::string_view debugName) override;
  [[nodiscard]] Result<TextureHandle> createTexture(const ImageData& image, TextureUsage usage,
                                                    std::string_view debugName) override;
  void destroyMesh(MeshHandle mesh) noexcept override;
  void destroyTexture(TextureHandle texture) noexcept override;

  // For tests and diagnostics.
  [[nodiscard]] const gpu::Context& context() const noexcept { return context_; }
  [[nodiscard]] const gpu::Swapchain& swapchain() const noexcept { return *swapchain_; }
  [[nodiscard]] u64 framesPresented() const noexcept { return framesPresented_; }
  [[nodiscard]] u32 swapchainRecreations() const noexcept { return recreations_; }
  // Room in the largest per-frame sprite buffer, in sprites.
  [[nodiscard]] u32 spriteCapacity() const noexcept;
  [[nodiscard]] u32 lastSpriteCount() const noexcept { return lastSpriteCount_; }
  [[nodiscard]] u32 lastDrawCalls() const noexcept { return lastDrawCalls_; }
  [[nodiscard]] const gpu::Image& depthBuffer() const noexcept { return depth_; }
  [[nodiscard]] u32 lastMeshDrawCount() const noexcept { return lastMeshDraws_; }
  // Live resources (textures: not counting the built-in defaults), and those
  // destroyed but waiting for in-flight frames to finish.
  [[nodiscard]] usize meshCount() const noexcept { return meshes_.liveCount(); }
  [[nodiscard]] usize textureCount() const noexcept { return textures_.liveCount() - kDefaultTextureCount; }
  [[nodiscard]] usize retiredResourceCount() const noexcept {
    return meshes_.retiredCount() + textures_.retiredCount();
  }

  // Copies the next rendered frame back to the CPU; takeCapture() waits for
  // the GPU and returns it. Errc::Unsupported if this window's swapchain
  // cannot be copied from; Errc::InvalidState if no frame was captured.
  void requestCapture() noexcept { captureRequested_ = true; }
  [[nodiscard]] Result<CapturedFrame> takeCapture();

 private:
  // The objects one frame in flight needs. The command buffer is freed with
  // the command pool, so it is a plain handle.
  // One mesh draw, as recorded: which buffers to bind. The draw's data
  // (transform, material) is entry i of the frame's meshDraws buffer.
  struct MeshCommand {
    VkBuffer vertices = VK_NULL_HANDLE;
    VkBuffer indices = VK_NULL_HANDLE;
    u32 indexCount = 0;
  };

  struct Frame {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    gpu::Semaphore imageAcquired;  // signalled when the swapchain image is ours to draw in
    gpu::Fence inFlight;           // signalled when the GPU has finished this frame
    gpu::Buffer sprites;           // this frame's sprite data, persistently mapped
    u32 opaqueCount = 0;           // sprites [0, opaqueCount) are opaque...
    u32 blendedCount = 0;          // ...the next blendedCount are translucent, back to front
    gpu::Buffer meshDraws;         // this frame's MeshDrawGpu[], persistently mapped
    std::vector<MeshCommand> meshCommands;  // one per entry of meshDraws, in order
    gpu::Buffer uniforms;          // this frame's FrameUniforms (camera, lights), persistently mapped
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;  // points the shader at `uniforms`; freed with the pool
    gpu::Buffer capture;           // a copy of the frame for takeCapture(), when requested
    bool recordCapture = false;    // copy THIS submission's image into `capture`
    bool hasCapture = false;       // `capture` holds the newest finished capture (kept when the slot is reused)
    VkExtent2D captureExtent{};
    VkFormat captureFormat = VK_FORMAT_UNDEFINED;
  };

  VulkanRenderer(platform::Window& window, gpu::Context context, const VulkanRendererDesc& desc);

  [[nodiscard]] Status createDescriptorLayouts();
  [[nodiscard]] Status createTextureResources();
  [[nodiscard]] Status createSwapchain(platform::Extent2D size);
  [[nodiscard]] Status createDepthBuffer(VkExtent2D extent);
  [[nodiscard]] Status writeFrameUniforms(Frame& frame, const FrameData& frameData);
  [[nodiscard]] Status prepareCapture(Frame& frame);
  [[nodiscard]] Status createPipeline(VkFormat colourFormat);
  [[nodiscard]] Status createFrameResources();
  [[nodiscard]] Status writeSprites(Frame& frame, const FrameData& frameData);
  [[nodiscard]] Status writeMeshes(Frame& frame, const FrameData& frameData);
  [[nodiscard]] Result<TextureHandle> addTexture(gpu::Image image, std::string_view debugName);
  [[nodiscard]] u32 textureSlot(TextureHandle texture, u32 fallback) noexcept;
  [[nodiscard]] Status recordCommands(const Frame& frame, u32 imageIndex) const;

  platform::Window& window_;
  VulkanRendererDesc desc_;

  // Creation order; destruction is the reverse. Do not reorder.
  gpu::Context context_;
  std::optional<gpu::Swapchain> swapchain_;
  gpu::Image depth_;                            // one depth buffer, rebuilt with the swapchain
  std::vector<gpu::Semaphore> renderFinished_;  // one per swapchain IMAGE, not per frame (see .cpp)
  gpu::DescriptorSetLayout frameSetLayout_;     // set 0: the per-frame uniform buffer
  gpu::DescriptorSetLayout textureSetLayout_;   // set 1: every texture (bindless array)
  gpu::PipelineLayout pipelineLayout_;          // shared by all pipelines: sets 0 and 1, one 8-byte address
  gpu::Pipeline meshPipeline_;     // lit, textured meshes: back faces culled, depth test + write
  gpu::Pipeline opaquePipeline_;   // sprites: depth test + depth write
  gpu::Pipeline blendedPipeline_;  // sprites: depth test, NO depth write (translucent, sorted back to front)
  VkFormat pipelineFormat_ = VK_FORMAT_UNDEFINED;
  gpu::CommandPool commandPool_;
  gpu::DescriptorPool descriptorPool_;
  std::array<Frame, kFramesInFlight> frames_{};
  gpu::Sampler sampler_;                        // one for every texture: trilinear, repeat, anisotropic
  gpu::DescriptorPool textureDescriptorPool_;   // update-after-bind: textures are added while frames are in flight
  VkDescriptorSet textureSet_ = VK_NULL_HANDLE;  // freed with its pool
  // Declared last: destroyed first, while the device still exists.
  ResourcePool<gpu::Image, TextureHandle> textures_{kMaxTextures, "texture"};
  ResourcePool<Mesh, MeshHandle> meshes_{kMaxMeshes, "mesh"};

  u32 frameIndex_ = 0;          // which Frame slot is next
  u64 framesPresented_ = 0;
  u32 recreations_ = 0;
  bool swapchainOutOfDate_ = false;
  u32 lastSpriteCount_ = 0;
  u32 lastDrawCalls_ = 0;
  u32 lastMeshDraws_ = 0;
  bool captureRequested_ = false;
  std::vector<u32> blendedOrder_;  // scratch for sorting translucent sprites; reused every frame
};

}  // namespace playground::render
