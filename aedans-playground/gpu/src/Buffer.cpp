#include "playground/gpu/Buffer.h"

#include "playground/core/Log.h"
#include "playground/gpu/Context.h"
#include "playground/gpu/DeviceHandle.h"
#include "playground/gpu/VulkanCheck.h"

#include <cstring>
#include <format>
#include <string>
#include <utility>

namespace playground::gpu {

using log::Category;

namespace {
constexpr u64 kGpuTimeoutNs = 5'000'000'000;  // 5 s: longer means the GPU hung
}  // namespace

// --- Buffer -------------------------------------------------------------------

Result<Buffer> Buffer::create(const Allocator& allocator, const BufferDesc& desc) {
  if (desc.sizeBytes == 0 || desc.usage == 0) {
    return fail(Errc::InvalidArgument, "buffer \"{}\" needs a non-zero size and usage (got {} bytes, usage 0x{:x})",
                desc.debugName, desc.sizeBytes, desc.usage);
  }

  const VkBufferCreateInfo bufferInfo{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = desc.sizeBytes,
      .usage = desc.usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VmaAllocationCreateInfo allocInfo{};
  if (desc.memory == MemoryKind::HostVisible) {
    // Written by the CPU front to back, never read back; mapped for life.
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
  } else if (desc.memory == MemoryKind::HostReadback) {
    // Read by the CPU, possibly out of order: cached memory, mapped for life.
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
  } else {
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  }

  Buffer buffer;
  buffer.allocator_ = allocator.handle();
  buffer.size_ = desc.sizeBytes;
  VmaAllocationInfo result{};
  if (Status s = check(vmaCreateBuffer(allocator.handle(), &bufferInfo, &allocInfo, &buffer.buffer_,
                                       &buffer.allocation_, &result),
                       "vmaCreateBuffer");
      !s) {
    s.error().message += std::format(" (buffer \"{}\", {} bytes, {})", desc.debugName, desc.sizeBytes,
                                     desc.memory == MemoryKind::DeviceLocal ? "device-local" : "host-visible");
    return tl::unexpected(std::move(s.error()));
  }

  // The name appears in VMA's own leak reports and statistics.
  const std::string name(desc.debugName);
  vmaSetAllocationName(allocator.handle(), buffer.allocation_, name.c_str());

  if (desc.memory != MemoryKind::DeviceLocal) {
    buffer.mapped_ = static_cast<std::byte*>(result.pMappedData);
    if (buffer.mapped_ == nullptr) {
      return fail(Errc::GpuFailure, "host-visible buffer \"{}\" was not mapped", desc.debugName);
    }
  }

  if (desc.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
    const VkBufferDeviceAddressInfo addressInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                                                .buffer = buffer.buffer_};
    buffer.address_ = vkGetBufferDeviceAddress(allocator.device(), &addressInfo);
  }
  return buffer;
}

Buffer::Buffer(Buffer&& other) noexcept
    : allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE)),
      buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)),
      mapped_(std::exchange(other.mapped_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      address_(std::exchange(other.address_, 0)) {}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
  if (this != &other) {
    destroy();
    allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
    buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
    allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
    mapped_ = std::exchange(other.mapped_, nullptr);
    size_ = std::exchange(other.size_, 0);
    address_ = std::exchange(other.address_, 0);
  }
  return *this;
}

Buffer::~Buffer() { destroy(); }

void Buffer::destroy() noexcept {
  if (buffer_ == VK_NULL_HANDLE) return;
  vmaDestroyBuffer(allocator_, buffer_, allocation_);  // also unmaps
  buffer_ = VK_NULL_HANDLE;
  allocation_ = VK_NULL_HANDLE;
  mapped_ = nullptr;
}

std::span<std::byte> Buffer::mapped() const noexcept {
  return mapped_ ? std::span<std::byte>(mapped_, static_cast<usize>(size_)) : std::span<std::byte>{};
}

Status Buffer::write(std::span<const std::byte> bytes, u64 offset) {
  if (mapped_ == nullptr) return fail(Errc::InvalidState, "write() on a buffer the CPU cannot see (device-local)");
  if (offset > size_ || bytes.size() > size_ - offset) {
    return fail(Errc::InvalidArgument, "write of {} bytes at offset {} overruns a {}-byte buffer", bytes.size(),
                offset, size_);
  }
  std::memcpy(mapped_ + offset, bytes.data(), bytes.size());
  return flush(offset, bytes.size());
}

Status Buffer::flush(u64 offset, u64 bytes) {
  if (allocation_ == VK_NULL_HANDLE) return {};
  return check(vmaFlushAllocation(allocator_, allocation_, offset, bytes), "vmaFlushAllocation");
}

Status Buffer::invalidate(u64 offset, u64 bytes) {
  if (allocation_ == VK_NULL_HANDLE) return {};
  return check(vmaInvalidateAllocation(allocator_, allocation_, offset, bytes), "vmaInvalidateAllocation");
}

// --- One-shot GPU work ---------------------------------------------------------

Status submitAndWait(const Context& context, const std::function<void(VkCommandBuffer)>& record) {
  const Device& device = context.device();
  const VkDevice vk = device.handle();

  // A transient pool just for this: simple, and nothing is shared with the
  // renderer's per-frame pools.
  const VkCommandPoolCreateInfo poolInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = device.queueFamilies().graphics,
  };
  VkCommandPool rawPool = VK_NULL_HANDLE;
  if (Status s = check(vkCreateCommandPool(vk, &poolInfo, nullptr, &rawPool), "vkCreateCommandPool"); !s) return s;
  const CommandPool pool(vk, rawPool);

  const VkCommandBufferAllocateInfo allocInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = rawPool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };
  VkCommandBuffer commands = VK_NULL_HANDLE;
  if (Status s = check(vkAllocateCommandBuffers(vk, &allocInfo, &commands), "vkAllocateCommandBuffers"); !s) return s;

  const VkCommandBufferBeginInfo begin{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                       .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  if (Status s = check(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer"); !s) return s;
  record(commands);
  if (Status s = check(vkEndCommandBuffer(commands), "vkEndCommandBuffer"); !s) return s;

  const VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  VkFence rawFence = VK_NULL_HANDLE;
  if (Status s = check(vkCreateFence(vk, &fenceInfo, nullptr, &rawFence), "vkCreateFence"); !s) return s;
  const Fence fence(vk, rawFence);

  const VkCommandBufferSubmitInfo commandInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                              .commandBuffer = commands};
  const VkSubmitInfo2 submit{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                             .commandBufferInfoCount = 1,
                             .pCommandBufferInfos = &commandInfo};
  if (Status s = check(vkQueueSubmit2(device.graphicsQueue(), 1, &submit, rawFence), "vkQueueSubmit2"); !s) return s;

  const VkResult waited = vkWaitForFences(vk, 1, &rawFence, VK_TRUE, kGpuTimeoutNs);
  if (waited == VK_TIMEOUT) return fail(Errc::GpuFailure, "a one-shot GPU command did not finish within 5 s");
  return check(waited, "vkWaitForFences");
  // pool (and its command buffer) and fence are destroyed here, after the wait
}

Status copyBuffer(const Context& context, const Buffer& src, const Buffer& dst, u64 bytes) {
  if (bytes > src.size() || bytes > dst.size()) {
    return fail(Errc::InvalidArgument, "copy of {} bytes from a {}-byte buffer into a {}-byte one", bytes, src.size(),
                dst.size());
  }
  return submitAndWait(context, [&](VkCommandBuffer commands) {
    const VkBufferCopy region{.srcOffset = 0, .dstOffset = 0, .size = bytes};
    vkCmdCopyBuffer(commands, src.handle(), dst.handle(), 1, &region);
  });
}

Result<Buffer> createDeviceLocalBuffer(const Context& context, std::span<const std::byte> data,
                                       VkBufferUsageFlags usage, std::string_view debugName) {
  if (data.empty()) return fail(Errc::InvalidArgument, "device-local buffer \"{}\" with no data", debugName);

  // 1. A host-visible staging buffer, filled by the CPU.
  const std::string stagingName = std::string(debugName) + " (staging)";
  auto staging = Buffer::create(context.allocator(), {.sizeBytes = data.size(),
                                                      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                      .memory = MemoryKind::HostVisible,
                                                      .debugName = stagingName});
  if (!staging) return tl::unexpected(std::move(staging.error()));
  if (Status s = staging->write(data); !s) return tl::unexpected(std::move(s.error()));

  // 2. The device-local destination.
  auto buffer = Buffer::create(context.allocator(), {.sizeBytes = data.size(),
                                                     .usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                     .memory = MemoryKind::DeviceLocal,
                                                     .debugName = debugName});
  if (!buffer) return tl::unexpected(std::move(buffer.error()));

  // 3. The GPU copies across; the staging buffer is freed on return.
  if (Status s = copyBuffer(context, *staging, *buffer, data.size()); !s) return tl::unexpected(std::move(s.error()));
  log::debug(Category::Gpu, "uploaded {} bytes to device-local buffer \"{}\"", data.size(), debugName);
  return buffer;
}

}  // namespace playground::gpu
