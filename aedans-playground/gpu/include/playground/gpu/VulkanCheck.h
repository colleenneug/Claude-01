#pragma once
// =============================================================================
//  playground/gpu/VulkanCheck.h — turning VkResult into something readable.
//
//  Every Vulkan call that can fail returns a VkResult. The rule in this layer
//  is that every one of them is checked, and a failure becomes a Status whose
//  message says three things:
//
//    which call failed     "vkCreateDevice failed"
//    with what             "VK_ERROR_FEATURE_NOT_PRESENT"
//    what that usually     "a feature the engine asked for is not supported
//    means here             by this GPU or driver"
//
//  plus the source line. A bug report from a machine we have never seen then
//  carries its own diagnosis.
//
//    if (Status s = check(vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance"); !s)
//      return tl::unexpected(std::move(s.error()));
// =============================================================================

#include "playground/core/Result.h"

#include <source_location>
#include <string>
#include <string_view>

#include <vulkan/vulkan.h>

namespace playground::gpu {

// "VK_ERROR_DEVICE_LOST". Unknown codes give "VkResult(<number>)".
[[nodiscard]] std::string resultName(VkResult result);

// A one-line hint at the usual cause, or an empty view if there is none.
[[nodiscard]] std::string_view resultHint(VkResult result) noexcept;

// VK_SUCCESS (and the other non-error codes, which are >= 0) is success.
// Anything negative becomes Errc::GpuFailure with a full message.
[[nodiscard]] Status check(VkResult result, std::string_view call,
                           std::source_location where = std::source_location::current());

// "1.3.275" from a packed Vulkan version number.
[[nodiscard]] std::string versionString(u32 packedVersion);

}  // namespace playground::gpu
