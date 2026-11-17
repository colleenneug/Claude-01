#include "playground/gpu/VulkanCheck.h"

#include <filesystem>
#include <format>
#include <string>

namespace playground::gpu {

std::string resultName(VkResult result) {
  switch (result) {
#define PG_CASE(code) \
  case code:          \
    return #code;
    PG_CASE(VK_SUCCESS)
    PG_CASE(VK_NOT_READY)
    PG_CASE(VK_TIMEOUT)
    PG_CASE(VK_EVENT_SET)
    PG_CASE(VK_EVENT_RESET)
    PG_CASE(VK_INCOMPLETE)
    PG_CASE(VK_SUBOPTIMAL_KHR)
    PG_CASE(VK_ERROR_OUT_OF_HOST_MEMORY)
    PG_CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY)
    PG_CASE(VK_ERROR_INITIALIZATION_FAILED)
    PG_CASE(VK_ERROR_DEVICE_LOST)
    PG_CASE(VK_ERROR_MEMORY_MAP_FAILED)
    PG_CASE(VK_ERROR_LAYER_NOT_PRESENT)
    PG_CASE(VK_ERROR_EXTENSION_NOT_PRESENT)
    PG_CASE(VK_ERROR_FEATURE_NOT_PRESENT)
    PG_CASE(VK_ERROR_INCOMPATIBLE_DRIVER)
    PG_CASE(VK_ERROR_TOO_MANY_OBJECTS)
    PG_CASE(VK_ERROR_FORMAT_NOT_SUPPORTED)
    PG_CASE(VK_ERROR_FRAGMENTED_POOL)
    PG_CASE(VK_ERROR_UNKNOWN)
    PG_CASE(VK_ERROR_OUT_OF_POOL_MEMORY)
    PG_CASE(VK_ERROR_INVALID_EXTERNAL_HANDLE)
    PG_CASE(VK_ERROR_FRAGMENTATION)
    PG_CASE(VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS)
    PG_CASE(VK_PIPELINE_COMPILE_REQUIRED)
    PG_CASE(VK_ERROR_SURFACE_LOST_KHR)
    PG_CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR)
    PG_CASE(VK_ERROR_OUT_OF_DATE_KHR)
    PG_CASE(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR)
    PG_CASE(VK_ERROR_VALIDATION_FAILED_EXT)
#undef PG_CASE
    default:
      return std::format("VkResult({})", static_cast<int>(result));
  }
}

std::string_view resultHint(VkResult result) noexcept {
  switch (result) {
    case VK_ERROR_OUT_OF_HOST_MEMORY:
      return "the driver ran out of system memory";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
      return "the GPU ran out of video memory";
    case VK_ERROR_INITIALIZATION_FAILED:
      return "the driver could not initialise; often a broken or half-installed driver";
    case VK_ERROR_DEVICE_LOST:
      return "the GPU stopped responding (a hang, a driver reset, or the device was removed)";
    case VK_ERROR_LAYER_NOT_PRESENT:
      return "a requested layer is not installed (validation layers come with the Vulkan SDK)";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
      return "a requested extension is not supported by this driver";
    case VK_ERROR_FEATURE_NOT_PRESENT:
      return "a requested feature is not supported by this GPU or driver";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
      return "no installed driver supports the requested Vulkan version; update the GPU driver";
    case VK_ERROR_SURFACE_LOST_KHR:
      return "the window's surface is gone (the window was destroyed first?)";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
      return "the window already has a graphics context; it must be created with GLFW_NO_API";
    case VK_ERROR_OUT_OF_DATE_KHR:
      return "the swapchain no longer matches the window and must be recreated";
    default:
      return {};
  }
}

Status check(VkResult result, std::string_view call, std::source_location where) {
  if (result >= VK_SUCCESS) return {};  // VK_SUCCESS and the non-error status codes

  const std::string_view hint = resultHint(result);
  // Just the file name: full build paths make every line twice as long.
  const std::string file = std::filesystem::path(where.file_name()).filename().string();
  return fail(Errc::GpuFailure, "{} failed: {}{}{} [{}:{}]", call, resultName(result), hint.empty() ? "" : " — ", hint,
              file, where.line());
}

std::string versionString(u32 packedVersion) {
  return std::format("{}.{}.{}", VK_API_VERSION_MAJOR(packedVersion), VK_API_VERSION_MINOR(packedVersion),
                     VK_API_VERSION_PATCH(packedVersion));
}

}  // namespace playground::gpu
