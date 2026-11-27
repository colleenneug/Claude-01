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
#include "playground/gpu/VulkanCheck.h"

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
