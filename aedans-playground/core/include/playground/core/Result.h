#pragma once
// =============================================================================
//  playground/core/Result.h — how fallible functions report failure.
//
//  RULE: no exception crosses a subsystem boundary. A function that can fail
//  returns Result<T> (a value or an Error) or Status (nothing, or an Error),
//  and the caller has to look at it.
//
//  An Error's message is a sentence a person can act on, not a code to look
//  up: "no GPU supports descriptor indexing; found Intel HD 4000 (Vulkan
//  1.0)", not "device creation failed". The code is coarse on purpose — it is
//  for the few callers that branch on the kind of failure; everyone else logs
//  the message.
//
//  Moving to C++23 means pointing the two aliases at std::expected and
//  std::unexpected; tl::expected has the same interface.
// =============================================================================

#include "playground/core/Types.h"

#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <tl/expected.hpp>

namespace playground {

enum class Errc : u8 {
  Unknown,
  InvalidArgument,     // the caller passed something unusable
  InvalidState,        // the call is not valid right now (e.g. run after shutdown)
  AlreadyInitialized,  // a once-per-process thing was started twice
  PlatformFailure,     // the OS or windowing system refused
  Unsupported,         // this machine lacks something we require
  IoFailure,           // a file could not be read or written
  RendererFailure,     // the renderer could not produce a frame
  GpuFailure,          // a graphics API call failed (the message names the call)
};

// Named describe(), not toString(): doctest (and other libraries) look up an
// unqualified toString() by argument-dependent lookup to print values, and
// would find this one instead of their own.
[[nodiscard]] constexpr std::string_view describe(Errc code) noexcept {
  switch (code) {
    case Errc::Unknown:            return "unknown";
    case Errc::InvalidArgument:    return "invalid argument";
    case Errc::InvalidState:       return "invalid state";
    case Errc::AlreadyInitialized: return "already initialised";
    case Errc::PlatformFailure:    return "platform failure";
    case Errc::Unsupported:        return "unsupported";
    case Errc::IoFailure:          return "I/O failure";
    case Errc::RendererFailure:    return "renderer failure";
    case Errc::GpuFailure:         return "GPU failure";
  }
  return "unknown";
}

struct Error {
  Errc code = Errc::Unknown;
  std::string message;
};

template <class T>
using Result = tl::expected<T, Error>;

// For functions that produce nothing on success. `return {};` is success.
using Status = Result<void>;

// Builds the error side of a Result:
//   return fail(Errc::InvalidArgument, "window size {}x{} is too small", w, h);
template <class... Args>
[[nodiscard]] tl::unexpected<Error> fail(Errc code, std::format_string<Args...> format, Args&&... args) {
  return tl::unexpected<Error>(Error{
      .code = code,
      .message = std::format(format, std::forward<Args>(args)...),
  });
}

}  // namespace playground
