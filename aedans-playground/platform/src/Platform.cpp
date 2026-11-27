#include "playground/platform/Platform.h"

#include "playground/core/Log.h"

#include <atomic>
#include <string>

// GLFW_INCLUDE_VULKAN is not defined: the Vulkan queries below need no
// Vulkan headers, and the platform layer does not depend on the SDK.
#include <GLFW/glfw3.h>

namespace playground::platform {
namespace {

using log::Category;

std::atomic<bool> gPlatformAlive{false};

void onGlfwError(int code, const char* description) {
  log::error(Category::Platform, "GLFW error 0x{:05X}: {}", code, description ? description : "(no description)");
}

}  // namespace

Result<std::unique_ptr<Platform>> Platform::create() {
  bool expected = false;
  if (!gPlatformAlive.compare_exchange_strong(expected, true)) {
    return fail(Errc::AlreadyInitialized,
                "a Platform already exists; GLFW is process-global, so only one may be alive at a time");
  }

  // Set before glfwInit so that init failures are logged too.
  glfwSetErrorCallback(&onGlfwError);

  if (glfwInit() != GLFW_TRUE) {
    const char* description = nullptr;
    glfwGetError(&description);
    const std::string reason = description ? description : "unknown error";
    glfwSetErrorCallback(nullptr);
    gPlatformAlive.store(false);
    return fail(Errc::PlatformFailure, "GLFW could not initialise: {}", reason);
  }

  std::unique_ptr<Platform> platform(new Platform());  // its InterruptGuard catches Ctrl+C from here on

  log::info(Category::Platform, "GLFW {} initialised", glfwGetVersionString());

  if (platform->vulkanSupported()) {
    std::string extensions;
    for (const char* name : platform->requiredVulkanInstanceExtensions()) {
      if (!extensions.empty()) extensions += ", ";
      extensions += name;
    }
    log::info(Category::Platform, "Vulkan loader found; surfaces need: {}", extensions);
  } else {
    // Not an error yet: nothing needs Vulkan until a renderer is attached.
    log::warn(Category::Platform, "no Vulkan loader or driver found; a Vulkan renderer will not be able to start");
  }

  return platform;
}

Platform::~Platform() {
  glfwTerminate();
  glfwSetErrorCallback(nullptr);
  gPlatformAlive.store(false);
  log::info(Category::Platform, "GLFW terminated");
}

void Platform::pollEvents() noexcept { glfwPollEvents(); }

void Platform::waitEvents(f64 timeoutSeconds) noexcept {
  if (timeoutSeconds > 0.0) {
    glfwWaitEventsTimeout(timeoutSeconds);
  } else {
    glfwPollEvents();
  }
}

bool Platform::vulkanSupported() const noexcept { return glfwVulkanSupported() == GLFW_TRUE; }

std::span<const char* const> Platform::requiredVulkanInstanceExtensions() const noexcept {
  u32 count = 0;
  const char** names = glfwGetRequiredInstanceExtensions(&count);
  if (names == nullptr) return {};
  return {names, count};
}

std::string_view Platform::versionString() const noexcept { return glfwGetVersionString(); }

bool Platform::interruptRequested() noexcept { return InterruptGuard::interruptRequested(); }

}  // namespace playground::platform
