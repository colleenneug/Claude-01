#include "playground/gpu/PhysicalDevice.h"

#include "Requirements.h"
#include "playground/core/Log.h"
#include "playground/gpu/Instance.h"
#include "playground/gpu/VulkanCheck.h"

#include <cstring>
#include <format>
#include <optional>
#include <vector>

namespace playground::gpu {
namespace {

using log::Category;

struct Candidate {
  PhysicalDeviceInfo info;
  std::vector<std::string> rejections;  // empty: suitable
  int score = 0;
};

std::string joinStrings(const std::vector<std::string>& items, std::string_view separator) {
  std::string out;
  for (const std::string& item : items) {
    if (!out.empty()) out += separator;
    out += item;
  }
  return out;
}

int typeScore(VkPhysicalDeviceType type) noexcept {
  switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 1000;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 500;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 200;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:            return 10;
    default:                                     return 0;
  }
}

// The driver's own name and version string. Better than decoding
// VkPhysicalDeviceProperties::driverVersion, whose packing is vendor-specific.
std::string driverDescription(VkPhysicalDevice device, u32 apiVersion) {
  if (apiVersion < VK_API_VERSION_1_2) return "unknown driver (pre-1.2 device)";
  VkPhysicalDeviceDriverProperties driver{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
  VkPhysicalDeviceProperties2 properties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &driver};
  vkGetPhysicalDeviceProperties2(device, &properties);
  return std::format("{} {}", driver.driverName, driver.driverInfo);
}

// Fills `candidate.info.queues`; returns a Status only for failed calls.
Status findQueueFamilies(Candidate& candidate, VkSurfaceKHR surface) {
  const VkPhysicalDevice device = candidate.info.handle;
  u32 count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

  QueueFamilies& found = candidate.info.queues;
  for (u32 index = 0; index < count; ++index) {
    const bool graphics = (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    VkBool32 present = VK_FALSE;
    if (Status s = check(vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &present),
                         "vkGetPhysicalDeviceSurfaceSupportKHR");
        !s) {
      return s;
    }

    // One family that does both is best: no ownership transfers between
    // queues when presenting. Take it as soon as it is seen.
    if (graphics && present == VK_TRUE) {
      found.graphics = index;
      found.present = index;
      return {};
    }
    if (graphics && found.graphics == kNoQueueFamily) found.graphics = index;
    if (present == VK_TRUE && found.present == kNoQueueFamily) found.present = index;
  }
  return {};
}

// Checks one GPU. Every failed requirement is recorded in `rejections`.
Result<Candidate> evaluate(VkPhysicalDevice device, VkSurfaceKHR surface) {
  Candidate candidate;
  candidate.info.handle = device;

  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(device, &properties);
  candidate.info.name = properties.deviceName;
  candidate.info.type = properties.deviceType;
  candidate.info.apiVersion = properties.apiVersion;
  candidate.info.driver = driverDescription(device, properties.apiVersion);
  std::vector<std::string>& rejections = candidate.rejections;

  // Vulkan version. The 1.3 feature structs below may only be queried on a
  // 1.3 device, so a device that fails this is not queried further.
  if (properties.apiVersion < kRequiredApiVersion) {
    rejections.push_back(std::format("supports Vulkan {}, needs {}", versionString(properties.apiVersion),
                                     versionString(kRequiredApiVersion)));
    return candidate;
  }

  // Device extensions.
  u32 extensionCount = 0;
  if (Status s = check(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr),
                       "vkEnumerateDeviceExtensionProperties");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  std::vector<VkExtensionProperties> extensions(extensionCount);
  if (Status s = check(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data()),
                       "vkEnumerateDeviceExtensionProperties");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  bool hasSwapchain = true;
  for (const char* required : detail::kRequiredDeviceExtensions) {
    bool found = false;
    for (const VkExtensionProperties& e : extensions) found = found || std::strcmp(e.extensionName, required) == 0;
    if (!found) {
      rejections.push_back(std::format("missing extension {}", required));
      if (std::strcmp(required, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) hasSwapchain = false;
    }
  }

  // Features.
  detail::FeatureChain supported;
  vkGetPhysicalDeviceFeatures2(device, &supported.core);
  for (const std::string& feature : detail::missingFeatures(supported)) {
    rejections.push_back(std::format("missing feature {}", feature));
  }

  // Queues.
  if (Status s = findQueueFamilies(candidate, surface); !s) return tl::unexpected(std::move(s.error()));
  if (candidate.info.queues.graphics == kNoQueueFamily) rejections.emplace_back("no graphics queue family");
  if (candidate.info.queues.present == kNoQueueFamily) rejections.emplace_back("cannot present to this window");

  // Surface formats and present modes (only meaningful with a swapchain).
  if (hasSwapchain && candidate.info.queues.present != kNoQueueFamily) {
    u32 formatCount = 0;
    u32 modeCount = 0;
    if (Status s = check(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr),
                         "vkGetPhysicalDeviceSurfaceFormatsKHR");
        !s) {
      return tl::unexpected(std::move(s.error()));
    }
    if (Status s = check(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &modeCount, nullptr),
                         "vkGetPhysicalDeviceSurfacePresentModesKHR");
        !s) {
      return tl::unexpected(std::move(s.error()));
    }
    if (formatCount == 0) rejections.emplace_back("no surface formats for this window");
    if (modeCount == 0) rejections.emplace_back("no present modes for this window");
  }

  if (rejections.empty()) {
    candidate.score = typeScore(properties.deviceType) + (candidate.info.queues.shared() ? 50 : 0);
  }
  return candidate;
}

}  // namespace

std::string_view describe(VkPhysicalDeviceType type) noexcept {
  switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU (software)";
    default:                                     return "other";
  }
}

Result<PhysicalDeviceInfo> selectPhysicalDevice(VkInstance instance, VkSurfaceKHR surface) {
  u32 count = 0;
  if (Status s = check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices"); !s) {
    return tl::unexpected(std::move(s.error()));
  }
  if (count == 0) {
    return fail(Errc::Unsupported, "the Vulkan loader found no GPU with a Vulkan driver; install or update the GPU driver");
  }
  std::vector<VkPhysicalDevice> devices(count);
  if (Status s = check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }

  log::info(Category::Gpu, "found {} Vulkan device(s):", count);

  std::optional<Candidate> best;
  std::vector<std::string> summaries;
  for (u32 i = 0; i < count; ++i) {
    auto candidate = evaluate(devices[i], surface);
    if (!candidate) return tl::unexpected(std::move(candidate.error()));

    const PhysicalDeviceInfo& info = candidate->info;
    log::info(Category::Gpu, "  #{} {} — {}, Vulkan {}, driver {}", i, info.name, describe(info.type),
              versionString(info.apiVersion), info.driver);

    if (candidate->rejections.empty()) {
      log::info(Category::Gpu, "     suitable, score {} (graphics queue family {}, present queue family {})",
                candidate->score, info.queues.graphics, info.queues.present);
      if (!best || candidate->score > best->score) best = std::move(*candidate);
    } else {
      const std::string reasons = joinStrings(candidate->rejections, "; ");
      log::warn(Category::Gpu, "     rejected: {}", reasons);
      summaries.push_back(std::format("{} ({})", info.name, reasons));
    }
  }

  if (!best) {
    return fail(Errc::Unsupported, "no GPU meets the engine's requirements: {}", joinStrings(summaries, " | "));
  }

  log::info(Category::Gpu, "selected {} ({})", best->info.name, describe(best->info.type));
  if (best->info.type == VK_PHYSICAL_DEVICE_TYPE_CPU) {
    log::warn(Category::Gpu, "the selected device renders on the CPU: fine for tests, far too slow to play on");
  }
  return std::move(best->info);
}

}  // namespace playground::gpu
