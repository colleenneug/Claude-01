// Tests for Vulkan bring-up: instance, surface, GPU selection, device, queues.
//
// They need a display and a Vulkan driver. Without either the executable
// exits with code 77, which CTest reports as skipped. In the cloud container
// they run under `xvfb-run -a` on Mesa's software driver (llvmpipe).
//
// Two tests inject validation messages on purpose, so an "[error]" line
// mentioning "playground-test" in the output is expected.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "playground/core/Log.h"
#include "playground/gpu/Buffer.h"
#include "playground/gpu/Context.h"
#include "playground/gpu/Image.h"
#include "playground/gpu/Sampler.h"
#include "playground/gpu/VulkanCheck.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace playground;

namespace {

constexpr int kSkipExitCode = 77;

// A platform and a small window: what every surface needs.
struct TestWindow {
  std::unique_ptr<platform::Platform> platform;
  std::optional<platform::Window> window;

  TestWindow() {
    auto p = platform::Platform::create();
    REQUIRE_MESSAGE(p.has_value(), (p ? "" : p.error().message));
    platform = std::move(*p);
    auto w = platform::Window::create(*platform, {.title = "gpu_tests", .width = 320, .height = 240});
    REQUIRE_MESSAGE(w.has_value(), (w ? "" : w.error().message));
    window.emplace(std::move(*w));
  }
};

void submitTestMessage(VkInstance instance, VkDebugUtilsMessageSeverityFlagBitsEXT severity) {
  const auto submit = reinterpret_cast<PFN_vkSubmitDebugUtilsMessageEXT>(
      vkGetInstanceProcAddr(instance, "vkSubmitDebugUtilsMessageEXT"));
  REQUIRE(submit != nullptr);
  const VkDebugUtilsMessengerCallbackDataEXT data{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT,
      .pMessageIdName = "playground-test",
      .pMessage = "injected by gpu_tests to prove messages are received; expected",
  };
  submit(instance, severity, VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, &data);
}

}  // namespace

// --- Error reporting ---------------------------------------------------------

TEST_CASE("check() passes success codes and explains failures") {
  CHECK(gpu::check(VK_SUCCESS, "vkAnything").has_value());
  CHECK(gpu::check(VK_SUBOPTIMAL_KHR, "vkQueuePresentKHR").has_value());  // a status, not an error

  const Status lost = gpu::check(VK_ERROR_DEVICE_LOST, "vkQueueSubmit");
  REQUIRE_FALSE(lost.has_value());
  CHECK(lost.error().code == Errc::GpuFailure);
  const std::string& message = lost.error().message;
  CHECK(message.starts_with("vkQueueSubmit failed: VK_ERROR_DEVICE_LOST — the GPU stopped responding"));
  CHECK(message.find("gpu_tests.cpp:") != std::string::npos);

  CHECK(gpu::resultName(static_cast<VkResult>(-12345)) == "VkResult(-12345)");
  CHECK(gpu::versionString(VK_MAKE_API_VERSION(0, 1, 3, 275)) == "1.3.275");
}

// --- Step 1: instance --------------------------------------------------------

TEST_CASE("a missing instance extension is named in the error") {
  const auto instance = gpu::Instance::create({
      .enableValidation = false,
      .requiredExtensions = {"VK_PLAYGROUND_not_a_real_extension"},
  });
  REQUIRE_FALSE(instance.has_value());
  CHECK(instance.error().code == Errc::Unsupported);
  CHECK(instance.error().message.find("VK_PLAYGROUND_not_a_real_extension") != std::string::npos);
}

TEST_CASE("validation messages reach the messenger and are counted") {
  auto instance = gpu::Instance::create({.enableValidation = true});
  REQUIRE(instance.has_value());
  REQUIRE_MESSAGE(instance->validationEnabled(), "the validation layer is not installed on this machine");

  gpu::resetValidationCounts();
  submitTestMessage(instance->handle(), VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
  submitTestMessage(instance->handle(), VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT);
  const gpu::ValidationCounts counts = gpu::validationCounts();
  CHECK(counts.errors == 1);
  CHECK(counts.warnings == 1);
  gpu::resetValidationCounts();  // the injected error must not fail later tests
}

TEST_CASE("validation can be turned off") {
  const auto instance = gpu::Instance::create({.enableValidation = false});
  REQUIRE(instance.has_value());
  CHECK_FALSE(instance->validationEnabled());
}

// --- Steps 1-4 together ------------------------------------------------------

TEST_CASE("full bring-up picks a GPU and creates a device with a graphics queue") {
  gpu::resetValidationCounts();
  TestWindow test;
  {
    auto context = gpu::Context::create(*test.platform, *test.window, {.enableValidation = true});
    REQUIRE_MESSAGE(context.has_value(), (context ? "" : context.error().message));

    CHECK(context->instance() != VK_NULL_HANDLE);
    CHECK(context->surface() != VK_NULL_HANDLE);
    CHECK(context->validationEnabled());

    const gpu::PhysicalDeviceInfo& gpuInfo = context->physicalDevice();
    CHECK(gpuInfo.handle != VK_NULL_HANDLE);
    CHECK_FALSE(gpuInfo.name.empty());
    CHECK(gpuInfo.apiVersion >= gpu::kRequiredApiVersion);
    CHECK(gpuInfo.queues.complete());

    const gpu::Device& device = context->device();
    CHECK(device.handle() != VK_NULL_HANDLE);
    CHECK(device.graphicsQueue() != VK_NULL_HANDLE);
    CHECK(device.presentQueue() != VK_NULL_HANDLE);
    if (device.queueFamilies().shared()) CHECK(device.graphicsQueue() == device.presentQueue());
    CHECK(device.waitIdle().has_value());
  }  // context destroyed here, before the window
  // Errors during teardown (e.g. leaked objects) would be counted here too.
  CHECK(gpu::validationCounts().errors == 0);
}

TEST_CASE("bring-up and teardown can be repeated without validation errors") {
  gpu::resetValidationCounts();
  TestWindow test;
  for (int round = 0; round < 2; ++round) {
    auto context = gpu::Context::create(*test.platform, *test.window, {.enableValidation = true});
    REQUIRE(context.has_value());
  }
  CHECK(gpu::validationCounts().errors == 0);
}

TEST_CASE("a failure names the bring-up step it happened in") {
  TestWindow test;
  // Move the window out: the moved-from one has no native window, so step 2
  // (the surface) must fail cleanly rather than hand GLFW a null pointer.
  platform::Window moved = std::move(*test.window);
  const auto context = gpu::Context::create(*test.platform, *test.window, {.enableValidation = true});
  REQUIRE_FALSE(context.has_value());
  CHECK(context.error().code == Errc::InvalidArgument);
  CHECK(context.error().message.starts_with("step 2 (window surface):"));
}

// --- GPU memory: allocator and buffers -----------------------------------------

TEST_CASE("buffers are counted by the allocator and freed when they die") {
  gpu::resetValidationCounts();
  TestWindow test;
  auto context = gpu::Context::create(*test.platform, *test.window, {.enableValidation = true});
  REQUIRE(context.has_value());
  const gpu::Allocator& allocator = context->allocator();
  CHECK(allocator.stats().allocationCount == 0);
  {
    auto a = gpu::Buffer::create(allocator, {.sizeBytes = 4096, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                             .memory = gpu::MemoryKind::HostVisible, .debugName = "test a"});
    auto b = gpu::Buffer::create(allocator, {.sizeBytes = 1 << 20, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                             .memory = gpu::MemoryKind::DeviceLocal, .debugName = "test b"});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(allocator.stats().allocationCount == 2);
    CHECK(allocator.stats().allocationBytes >= 4096 + (1 << 20));

    gpu::Buffer moved = std::move(*a);  // moving transfers ownership, not a copy
    CHECK(moved.size() == 4096);
    CHECK_FALSE(static_cast<bool>(*a));
    CHECK(allocator.stats().allocationCount == 2);
  }
  CHECK(allocator.stats().allocationCount == 0);  // nothing leaked
  CHECK(gpu::validationCounts().errors == 0);
}

TEST_CASE("a host-visible buffer is persistently mapped and has a GPU address") {
  gpu::resetValidationCounts();
  TestWindow test;
  auto context = gpu::Context::create(*test.platform, *test.window, {.enableValidation = true});
  REQUIRE(context.has_value());

  auto buffer = gpu::Buffer::create(context->allocator(),
                                    {.sizeBytes = 256,
                                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                     .memory = gpu::MemoryKind::HostVisible,
                                     .debugName = "mapped"});
  REQUIRE(buffer.has_value());
  CHECK(buffer->isHostVisible());
  CHECK(buffer->mapped().size() == 256);
  CHECK(buffer->deviceAddress() != 0);

  const std::vector<std::byte> data(64, std::byte{0x5A});
  REQUIRE(buffer->write(data, 32).has_value());
  CHECK(buffer->mapped()[32] == std::byte{0x5A});
  CHECK(buffer->mapped()[95] == std::byte{0x5A});
  CHECK(gpu::validationCounts().errors == 0);
}

TEST_CASE("a staging upload to device-local memory round-trips byte for byte") {
  gpu::resetValidationCounts();
  TestWindow test;
  auto context = gpu::Context::create(*test.platform, *test.window, {.enableValidation = true});
  REQUIRE(context.has_value());

  // 64 KiB of a recognisable pattern.
  std::vector<std::byte> data(64 * 1024);
  for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::byte>((i * 31 + 7) & 0xFF);

  // CPU -> staging -> device-local (the upload path)...
  auto deviceLocal = gpu::createDeviceLocalBuffer(*context, data, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "round trip");
  REQUIRE_MESSAGE(deviceLocal.has_value(), (deviceLocal ? "" : deviceLocal.error().message));
  CHECK_FALSE(deviceLocal->isHostVisible());

  // ...then device-local -> readback buffer -> CPU, to check what arrived.
  auto readback = gpu::Buffer::create(context->allocator(), {.sizeBytes = data.size(),
                                                             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                             .memory = gpu::MemoryKind::HostReadback,
                                                             .debugName = "readback"});
  REQUIRE(readback.has_value());
  REQUIRE(gpu::copyBuffer(*context, *deviceLocal, *readback, data.size()).has_value());
  REQUIRE(readback->invalidate().has_value());
  CHECK(std::memcmp(readback->mapped().data(), data.data(), data.size()) == 0);
  CHECK(gpu::validationCounts().errors == 0);
}

TEST_CASE("buffer misuse is reported, not undefined behaviour") {
  TestWindow test;
  auto context = gpu::Context::create(*test.platform, *test.window, {.enableValidation = true});
  REQUIRE(context.has_value());

  const auto empty = gpu::Buffer::create(context->allocator(), {.sizeBytes = 0, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT});
  REQUIRE_FALSE(empty.has_value());
  CHECK(empty.error().code == Errc::InvalidArgument);

  auto deviceOnly = gpu::Buffer::create(context->allocator(), {.sizeBytes = 64, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                               .memory = gpu::MemoryKind::DeviceLocal, .debugName = "gpu only"});
  REQUIRE(deviceOnly.has_value());
  const std::vector<std::byte> bytes(16);
  const Status cpuWrite = deviceOnly->write(bytes);
  REQUIRE_FALSE(cpuWrite.has_value());
  CHECK(cpuWrite.error().code == Errc::InvalidState);

  auto small = gpu::Buffer::create(context->allocator(), {.sizeBytes = 64, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                          .memory = gpu::MemoryKind::HostVisible, .debugName = "small"});
  REQUIRE(small.has_value());
  const Status overrun = small->write(bytes, 56);  // 16 bytes at 56 > 64
  REQUIRE_FALSE(overrun.has_value());
  CHECK(overrun.error().code == Errc::InvalidArgument);
}

TEST_CASE("a depth image is created, counted by the allocator, and freed") {
  gpu::resetValidationCounts();
  TestWindow test;
  auto context = gpu::Context::create(*test.platform, *test.window, {.enableValidation = true});
  REQUIRE(context.has_value());
  {
    auto depth = gpu::Image::create(context->allocator(), {.width = 256, .height = 128,
                                                           .format = gpu::kDepthFormat,
                                                           .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                                           .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
                                                           .debugName = "test depth"});
    REQUIRE_MESSAGE(depth.has_value(), (depth ? "" : depth.error().message));
    CHECK(depth->view() != VK_NULL_HANDLE);
    CHECK(depth->extent().width == 256);
    CHECK(context->allocator().stats().allocationCount == 1);
  }
  CHECK(context->allocator().stats().allocationCount == 0);

  const auto bad = gpu::Image::create(context->allocator(), {.width = 0, .height = 64, .format = gpu::kDepthFormat,
                                                             .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT});
  REQUIRE_FALSE(bad.has_value());
  CHECK(bad.error().code == Errc::InvalidArgument);
  CHECK(gpu::validationCounts().errors == 0);
}

int main(int argc, char** argv) {
  auto session = log::init({.filePath = {}, .level = log::Level::Warn});
  if (!session) {
    std::fprintf(stderr, "could not start logging: %s\n", session.error().message.c_str());
    return 1;
  }

  {
    auto probe = platform::Platform::create();
    if (!probe) {
      std::printf("skipping gpu_tests: %s\n", probe.error().message.c_str());
      return kSkipExitCode;
    }
    if (!(*probe)->vulkanSupported()) {
      std::printf("skipping gpu_tests: no Vulkan driver on this machine\n");
      return kSkipExitCode;
    }
  }

  doctest::Context context(argc, argv);
  return context.run();
}

// --- Textures ---------------------------------------------------------------------

namespace {

// Copies one mip level of a SHADER_READ_ONLY texture back to the CPU (RGBA8).
std::vector<u8> readTextureLevel(const gpu::Context& context, const gpu::Image& image, u32 level) {
  const u32 width = std::max(1u, image.extent().width >> level);
  const u32 height = std::max(1u, image.extent().height >> level);
  auto readback = gpu::Buffer::create(context.allocator(), {.sizeBytes = u64{width} * height * 4,
                                                            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                            .memory = gpu::MemoryKind::HostReadback,
                                                            .debugName = "test texture readback"});
  REQUIRE(readback.has_value());
  const VkImageSubresourceRange range{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = level,
                                      .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
  const Status copied = gpu::submitAndWait(context, [&](VkCommandBuffer commands) {
    gpu::transitionImage(commands, image.handle(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                         VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, range);
    const VkBufferImageCopy region{
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = level, .baseArrayLayer = 0,
                             .layerCount = 1},
        .imageExtent = {width, height, 1},
    };
    vkCmdCopyImageToBuffer(commands, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback->handle(), 1,
                           &region);
    gpu::transitionImage(commands, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE,
                         VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, range);
  });
  REQUIRE_MESSAGE(copied.has_value(), (copied ? "" : copied.error().message));
  REQUIRE(readback->invalidate().has_value());
  std::vector<u8> pixels(readback->size());
  std::memcpy(pixels.data(), readback->mapped().data(), pixels.size());
  return pixels;
}

// A width x height RGBA image: the left half opaque red, the right half opaque blue.
std::vector<std::byte> redBlueHalves(u32 width, u32 height) {
  std::vector<std::byte> pixels(usize{width} * height * 4);
  for (u32 y = 0; y < height; ++y) {
    for (u32 x = 0; x < width; ++x) {
      const usize i = (usize{y} * width + x) * 4;
      const bool left = x < width / 2;
      pixels[i + 0] = std::byte{static_cast<u8>(left ? 255 : 0)};
      pixels[i + 1] = std::byte{0};
      pixels[i + 2] = std::byte{static_cast<u8>(left ? 0 : 255)};
      pixels[i + 3] = std::byte{255};
    }
  }
  return pixels;
}

}  // namespace

TEST_CASE("a full mip chain goes down to 1x1, counted by the larger side") {
  CHECK(gpu::fullMipCount(1, 1) == 1);
  CHECK(gpu::fullMipCount(2, 1) == 2);
  CHECK(gpu::fullMipCount(256, 256) == 9);
  CHECK(gpu::fullMipCount(300, 20) == 9);
  CHECK(gpu::fullMipCount(64, 32) == 7);
}

TEST_CASE("a texture upload builds its mip chain on the GPU, each level a filtered half of the last") {
  gpu::resetValidationCounts();
  TestWindow test;
  auto context = gpu::Context::create(*test.platform, *test.window, {.enableValidation = true});
  REQUIRE(context.has_value());
  {
    const std::vector<std::byte> pixels = redBlueHalves(64, 32);
    auto texture = gpu::createTexture(*context, {.width = 64, .height = 32, .format = VK_FORMAT_R8G8B8A8_UNORM,
                                                 .pixels = pixels, .debugName = "test halves"});
    REQUIRE_MESSAGE(texture.has_value(), (texture ? "" : texture.error().message));
    CHECK(texture->mipLevels() == 7);  // 64x32, 32x16, ... 2x1, 1x1

    // Level 0 is the upload, byte for byte.
    const std::vector<u8> level0 = readTextureLevel(*context, *texture, 0);
    CHECK(std::memcmp(level0.data(), pixels.data(), pixels.size()) == 0);

    // 2x1: each half averaged on its own, so still pure red and pure blue.
    const std::vector<u8> level5 = readTextureLevel(*context, *texture, 5);
    REQUIRE(level5.size() == 8);
    CHECK(level5[0] == 255);
    CHECK(level5[2] == 0);
    CHECK(level5[4] == 0);
    CHECK(level5[6] == 255);

    // 1x1: the two halves mixed, about 50% each in this linear (UNORM) format.
    const std::vector<u8> level6 = readTextureLevel(*context, *texture, 6);
    CHECK(std::abs(int{level6[0]} - 128) <= 2);
    CHECK(std::abs(int{level6[2]} - 128) <= 2);
    CHECK(level6[3] == 255);
  }
  CHECK(context->allocator().stats().allocationCount == 0);  // staging and texture both freed
  CHECK(gpu::validationCounts().errors == 0);
}

TEST_CASE("an sRGB texture's mips are averaged in linear light, not on the stored bytes") {
  gpu::resetValidationCounts();
  TestWindow test;
  auto context = gpu::Context::create(*test.platform, *test.window, {.enableValidation = true});
  REQUIRE(context.has_value());
  const std::vector<std::byte> pixels = redBlueHalves(2, 1);
  auto texture = gpu::createTexture(*context, {.width = 2, .height = 1, .format = VK_FORMAT_R8G8B8A8_SRGB,
                                               .pixels = pixels, .debugName = "test srgb"});
  REQUIRE(texture.has_value());
  REQUIRE(texture->mipLevels() == 2);
  // Half of full red in linear light is 0.5, which sRGB stores as 188, not 128.
  // Averaging the bytes instead would darken every distant texture.
  const std::vector<u8> mixed = readTextureLevel(*context, *texture, 1);
  CHECK(std::abs(int{mixed[0]} - 188) <= 3);
  CHECK(std::abs(int{mixed[2]} - 188) <= 3);
  CHECK(gpu::validationCounts().errors == 0);
}

TEST_CASE("texture misuse is reported: wrong pixel count, format or size") {
  gpu::resetValidationCounts();
  TestWindow test;
  auto context = gpu::Context::create(*test.platform, *test.window, {.enableValidation = true});
  REQUIRE(context.has_value());
  const std::vector<std::byte> pixels(16 * 4);

  const auto short_ = gpu::createTexture(*context, {.width = 8, .height = 8, .pixels = pixels, .debugName = "short"});
  REQUIRE_FALSE(short_.has_value());
  CHECK(short_.error().code == Errc::InvalidArgument);
  CHECK(short_.error().message.find("needs 256 bytes, got 64") != std::string::npos);

  const auto format = gpu::createTexture(
      *context, {.width = 4, .height = 4, .format = VK_FORMAT_R16G16B16A16_SFLOAT, .pixels = pixels});
  REQUIRE_FALSE(format.has_value());
  CHECK(format.error().code == Errc::InvalidArgument);

  const auto mips = gpu::Image::create(context->allocator(), {.width = 4, .height = 4,
                                                              .format = VK_FORMAT_R8G8B8A8_UNORM,
                                                              .usage = VK_IMAGE_USAGE_SAMPLED_BIT, .mipLevels = 4});
  REQUIRE_FALSE(mips.has_value());  // 4x4 has at most 3 levels
  CHECK(mips.error().message.find("at most 3") != std::string::npos);
  CHECK(context->allocator().stats().allocationCount == 0);
  CHECK(gpu::validationCounts().errors == 0);
}

TEST_CASE("a sampler is created, with anisotropy only when the GPU has it") {
  gpu::resetValidationCounts();
  TestWindow test;
  auto context = gpu::Context::create(*test.platform, *test.window, {.enableValidation = true});
  REQUIRE(context.has_value());
  auto sampler = gpu::createSampler(*context, {.maxAnisotropy = 1000.0f, .debugName = "test sampler"});
  REQUIRE_MESSAGE(sampler.has_value(), (sampler ? "" : sampler.error().message));
  CHECK(sampler->get() != VK_NULL_HANDLE);
  MESSAGE("anisotropy supported: " << context->physicalDevice().samplerAnisotropy << ", up to "
                                   << context->physicalDevice().maxSamplerAnisotropy);
  CHECK(gpu::validationCounts().errors == 0);  // 1000 was clamped, or anisotropy left off
}
