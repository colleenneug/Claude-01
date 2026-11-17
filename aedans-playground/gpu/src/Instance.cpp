#include "playground/gpu/Instance.h"

#include "playground/core/Log.h"
#include "playground/gpu/VulkanCheck.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <utility>

namespace playground::gpu {
namespace {

using log::Category;

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

std::atomic<u32> gValidationErrors{0};
std::atomic<u32> gValidationWarnings{0};

// Joins C strings for a log line: "a, b, c".
std::string join(std::span<const char* const> names) {
  std::string out;
  for (const char* name : names) {
    if (!out.empty()) out += ", ";
    out += name;
  }
  return out.empty() ? "none" : out;
}

// Called by the validation layer (and the loader) on any thread.
VKAPI_ATTR VkBool32 VKAPI_CALL onDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                                              void* /*userData*/) {
  log::Level level = log::Level::Trace;
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    level = log::Level::Error;
    gValidationErrors.fetch_add(1, std::memory_order_relaxed);
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    level = log::Level::Warn;
    gValidationWarnings.fetch_add(1, std::memory_order_relaxed);
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    level = log::Level::Debug;  // loader chatter: useful when debugging start-up only
  }

  const char* id = (data != nullptr && data->pMessageIdName != nullptr) ? data->pMessageIdName : "-";
  const char* message = (data != nullptr && data->pMessage != nullptr) ? data->pMessage : "(no message)";
  log::print(Category::Gpu, level, "[{}] {}", id, message);

  // VK_FALSE: never abort the call that triggered the message. Aborting
  // changes behaviour between debug and release, which hides bugs.
  return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo() {
  return VkDebugUtilsMessengerCreateInfoEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = &onDebugMessage,
  };
}

bool contains(std::span<const VkExtensionProperties> available, const char* name) {
  return std::ranges::any_of(available, [name](const VkExtensionProperties& e) {
    return std::strcmp(e.extensionName, name) == 0;
  });
}

bool contains(std::span<const VkLayerProperties> available, const char* name) {
  return std::ranges::any_of(available,
                             [name](const VkLayerProperties& l) { return std::strcmp(l.layerName, name) == 0; });
}

}  // namespace

ValidationCounts validationCounts() noexcept {
  return {.errors = gValidationErrors.load(), .warnings = gValidationWarnings.load()};
}

void resetValidationCounts() noexcept {
  gValidationErrors.store(0);
  gValidationWarnings.store(0);
}

Result<Instance> Instance::create(const InstanceDesc& desc) {
  log::info(Category::Gpu, "creating Vulkan instance (validation {})", desc.enableValidation ? "requested" : "off");

  // --- 1. Loader version -----------------------------------------------------
  u32 loaderVersion = VK_API_VERSION_1_0;
  if (Status s = check(vkEnumerateInstanceVersion(&loaderVersion), "vkEnumerateInstanceVersion"); !s) {
    return tl::unexpected(std::move(s.error()));
  }
  log::info(Category::Gpu, "Vulkan loader supports version {}", versionString(loaderVersion));
  if (loaderVersion < kRequiredApiVersion) {
    return fail(Errc::Unsupported,
                "the Vulkan loader on this machine supports {}, but the engine needs {}; update the GPU driver",
                versionString(loaderVersion), versionString(kRequiredApiVersion));
  }

  // --- 2. Extensions ---------------------------------------------------------
  // The usual Vulkan two-call pattern: ask for the count, then fill the array.
  u32 extensionCount = 0;
  if (Status s = check(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr),
                       "vkEnumerateInstanceExtensionProperties");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  std::vector<VkExtensionProperties> availableExtensions(extensionCount);
  if (Status s = check(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data()),
                       "vkEnumerateInstanceExtensionProperties");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  log::debug(Category::Gpu, "{} instance extensions available", availableExtensions.size());

  std::vector<const char*> extensions = desc.requiredExtensions;
  std::vector<const char*> missing;
  for (const char* name : extensions) {
    if (!contains(availableExtensions, name)) missing.push_back(name);
  }
  if (!missing.empty()) {
    return fail(Errc::Unsupported, "the Vulkan driver lacks required instance extension(s): {}", join(missing));
  }

  // --- 3. Validation layer ---------------------------------------------------
  std::vector<const char*> layers;
  bool validation = desc.enableValidation;
  if (validation) {
    u32 layerCount = 0;
    if (Status s = check(vkEnumerateInstanceLayerProperties(&layerCount, nullptr), "vkEnumerateInstanceLayerProperties");
        !s) {
      return tl::unexpected(std::move(s.error()));
    }
    std::vector<VkLayerProperties> availableLayers(layerCount);
    if (Status s = check(vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data()),
                         "vkEnumerateInstanceLayerProperties");
        !s) {
      return tl::unexpected(std::move(s.error()));
    }

    if (!contains(availableLayers, kValidationLayer)) {
      log::warn(Category::Gpu,
                "validation was requested but {} is not installed; continuing WITHOUT validation "
                "(install the Vulkan SDK on Windows, or vulkan-validationlayers on Ubuntu)",
                kValidationLayer);
      validation = false;
    } else if (!contains(availableExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
      log::warn(Category::Gpu, "{} is not available, so validation messages cannot be received; continuing without",
                VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      validation = false;
    } else {
      layers.push_back(kValidationLayer);
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
  }

  // --- 4. Create the instance ------------------------------------------------
  const VkApplicationInfo appInfo{
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = desc.applicationName.c_str(),
      .applicationVersion = desc.applicationVersion,
      .pEngineName = "Aedan's Playground",
      .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
      .apiVersion = kRequiredApiVersion,
  };

  // Chained into pNext, this messenger covers vkCreateInstance and
  // vkDestroyInstance, which the long-lived one below cannot.
  const VkDebugUtilsMessengerCreateInfoEXT creationMessenger = messengerCreateInfo();

  const VkInstanceCreateInfo createInfo{
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = validation ? &creationMessenger : nullptr,
      .pApplicationInfo = &appInfo,
      .enabledLayerCount = static_cast<u32>(layers.size()),
      .ppEnabledLayerNames = layers.data(),
      .enabledExtensionCount = static_cast<u32>(extensions.size()),
      .ppEnabledExtensionNames = extensions.data(),
  };

  Instance instance;
  if (Status s = check(vkCreateInstance(&createInfo, nullptr, &instance.instance_), "vkCreateInstance"); !s) {
    return tl::unexpected(std::move(s.error()));
  }

  // --- 5. Long-lived debug messenger ----------------------------------------
  // An extension function: not exported by the loader, so it is looked up.
  if (validation) {
    const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance.instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (createMessenger == nullptr) {
      return fail(Errc::GpuFailure, "vkCreateDebugUtilsMessengerEXT is missing although {} was enabled",
                  VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    const VkDebugUtilsMessengerCreateInfoEXT info = messengerCreateInfo();
    if (Status s = check(createMessenger(instance.instance_, &info, nullptr, &instance.messenger_),
                         "vkCreateDebugUtilsMessengerEXT");
        !s) {
      return tl::unexpected(std::move(s.error()));  // `instance` destroys the VkInstance on the way out
    }
  }

  log::info(Category::Gpu, "Vulkan instance created: API {}, validation {}", versionString(kRequiredApiVersion),
            validation ? "ON" : "off");
  log::info(Category::Gpu, "  layers: {}", join(layers));
  log::info(Category::Gpu, "  extensions: {}", join(extensions));
  return instance;
}

Instance::Instance(Instance&& other) noexcept
    : instance_(std::exchange(other.instance_, VK_NULL_HANDLE)),
      messenger_(std::exchange(other.messenger_, VK_NULL_HANDLE)) {}

Instance& Instance::operator=(Instance&& other) noexcept {
  if (this != &other) {
    destroy();
    instance_ = std::exchange(other.instance_, VK_NULL_HANDLE);
    messenger_ = std::exchange(other.messenger_, VK_NULL_HANDLE);
  }
  return *this;
}

Instance::~Instance() { destroy(); }

void Instance::destroy() noexcept {
  if (instance_ == VK_NULL_HANDLE) return;
  if (messenger_ != VK_NULL_HANDLE) {
    const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroyMessenger != nullptr) destroyMessenger(instance_, messenger_, nullptr);
    messenger_ = VK_NULL_HANDLE;
  }
  vkDestroyInstance(instance_, nullptr);
  instance_ = VK_NULL_HANDLE;
  log::info(Category::Gpu, "Vulkan instance destroyed");
}

}  // namespace playground::gpu
