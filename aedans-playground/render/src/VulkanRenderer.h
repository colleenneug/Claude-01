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
#include "playground/gpu/Swapchain.h"
#include "playground/render/VulkanRenderer.h"

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

class VulkanRenderer final : public Renderer {
 public:
  [[nodiscard]] static Result<std::unique_ptr<VulkanRenderer>> create(platform::Platform& platform,
                                                                      platform::Window& window,
                                                                      const VulkanRendererDesc& desc);
  ~VulkanRenderer() override;

  [[nodiscard]] std::string_view name() const noexcept override { return "vulkan"; }
  [[nodiscard]] Status onFramebufferResized(platform::Extent2D size) override;
  [[nodiscard]] Status renderFrame(const FrameTime& time, const FrameData& frame) override;

  // For tests and diagnostics.
  [[nodiscard]] const gpu::Context& context() const noexcept { return context_; }
  [[nodiscard]] const gpu::Swapchain& swapchain() const noexcept { return *swapchain_; }
  [[nodiscard]] u64 framesPresented() const noexcept { return framesPresented_; }
  [[nodiscard]] u32 swapchainRecreations() const noexcept { return recreations_; }
  // Room in the largest per-frame sprite buffer, in sprites.
  [[nodiscard]] u32 spriteCapacity() const noexcept;
  [[nodiscard]] u32 lastSpriteCount() const noexcept { return lastSpriteCount_; }
  [[nodiscard]] u32 lastDrawCalls() const noexcept { return lastDrawCalls_; }

 private:
  // The objects one frame in flight needs. The command buffer is freed with
  // the command pool, so it is a plain handle.
  struct Frame {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    gpu::Semaphore imageAcquired;  // signalled when the swapchain image is ours to draw in
    gpu::Fence inFlight;           // signalled when the GPU has finished this frame
    gpu::Buffer sprites;           // this frame's sprite data, persistently mapped
  };

  VulkanRenderer(platform::Window& window, gpu::Context context, const VulkanRendererDesc& desc);

  [[nodiscard]] Status createSwapchain(platform::Extent2D size);
  [[nodiscard]] Status createPipeline(VkFormat colourFormat);
  [[nodiscard]] Status createFrameResources();
  [[nodiscard]] Status writeSprites(Frame& frame, const FrameData& frameData);
  [[nodiscard]] Status recordCommands(VkCommandBuffer commands, u32 imageIndex, const FrameData& frameData,
                                      VkDeviceAddress sprites) const;

  platform::Window& window_;
  VulkanRendererDesc desc_;

  // Creation order; destruction is the reverse. Do not reorder.
  gpu::Context context_;
  std::optional<gpu::Swapchain> swapchain_;
  std::vector<gpu::Semaphore> renderFinished_;  // one per swapchain IMAGE, not per frame (see .cpp)
  gpu::PipelineLayout pipelineLayout_;
  gpu::Pipeline pipeline_;
  VkFormat pipelineFormat_ = VK_FORMAT_UNDEFINED;
  gpu::CommandPool commandPool_;
  std::array<Frame, kFramesInFlight> frames_{};

  u32 frameIndex_ = 0;          // which Frame slot is next
  u64 framesPresented_ = 0;
  u32 recreations_ = 0;
  bool swapchainOutOfDate_ = false;
  u32 lastSpriteCount_ = 0;
  u32 lastDrawCalls_ = 0;
};

}  // namespace playground::render
