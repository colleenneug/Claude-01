#include "playground/engine/EnvironmentAsset.h"

#include "playground/core/Log.h"

#include <limits>
#include <utility>

#include <stb_image.h>  // declarations only; the code is in GltfImplementation.cpp

namespace playground::engine {

Result<render::HdrImage> decodeHdr(std::span<const std::byte> file, const std::filesystem::path& path) {
  const std::string name = path.generic_string();
  if (file.size() > static_cast<usize>(std::numeric_limits<int>::max())) {
    return fail(Errc::InvalidArgument, "HDR image \"{}\" is too large", name);
  }
  const auto* bytes = reinterpret_cast<const stbi_uc*>(file.data());
  const int size = static_cast<int>(file.size());
  if (!stbi_is_hdr_from_memory(bytes, size)) {
    log::warn(log::Category::Engine, "\"{}\" is not an HDR image: its light is clipped at white, so the lighting "
              "will be flat and dim", name);
  }
  int width = 0;
  int height = 0;
  int channels = 0;
  float* pixels = stbi_loadf_from_memory(bytes, size, &width, &height, &channels, 4);
  if (pixels == nullptr) {
    return fail(Errc::InvalidArgument, "HDR image \"{}\" could not be read: {}", name, stbi_failure_reason());
  }
  render::HdrImage image{.width = static_cast<u32>(width), .height = static_cast<u32>(height)};
  image.rgba.assign(pixels, pixels + static_cast<usize>(width) * static_cast<usize>(height) * 4);
  stbi_image_free(pixels);
  log::debug(log::Category::Engine, "HDR image \"{}\": {}x{}", name, width, height);
  return image;
}

AssetLoader<EnvironmentAsset, render::HdrImage> environmentLoader(render::Renderer& renderer,
                                                                  render::EnvironmentQuality quality) {
  return {
      .decode = [](std::span<const std::byte> file, const std::filesystem::path& path) { return decodeHdr(file, path); },
      .finalize = [&renderer, quality](render::HdrImage&& image, const std::filesystem::path& path)
          -> Result<EnvironmentAsset> {
        if (Status s = renderer.setEnvironment(image, quality); !s) {
          s.error().message += " (environment \"" + path.generic_string() + "\")";
          return tl::unexpected(std::move(s.error()));
        }
        return EnvironmentAsset{.width = image.width, .height = image.height};
      },
  };
}

}  // namespace playground::engine
