#pragma once
// =============================================================================
//  playground/engine/EnvironmentAsset.h — HDR panoramas as the scene's sky
//  and light, through the AssetManager.
//
//  A Radiance .hdr file (the usual format for photographed skies, e.g. from
//  Poly Haven) holds floating-point light values, far brighter than 1 where
//  the sun is. Loading it:
//    decode    worker thread: decodeHdr() reads it with stb_image into a
//              render::HdrImage (linear RGBA floats, equirectangular);
//    finalize  main thread: the renderer bakes it into its lighting maps
//              (Renderer::setEnvironment) and draws it as the sky.
//
//      assets.registerType<engine::EnvironmentAsset>(engine::environmentLoader(renderer));
//      auto sky = assets.load<engine::EnvironmentAsset>("skies/meadow.hdr");
//
//  Until it is ready, the renderer keeps its current environment (the
//  default procedural sky). Unloading does not restore the old one: the
//  environment is whatever was set last.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/engine/Assets.h"
#include "playground/render/Environment.h"
#include "playground/render/Renderer.h"

#include <cstddef>
#include <filesystem>
#include <span>

namespace playground::engine {

// What was installed: the panorama's size, for the log.
struct EnvironmentAsset {
  u32 width = 0;
  u32 height = 0;
};

// A .hdr (or any image stb_image reads; 8-bit images are converted to
// linear light, which is dim for lighting) as linear RGBA floats.
// Errc::InvalidArgument naming the file and stb's reason if it cannot be
// read. Thread-safe: runs on worker threads.
[[nodiscard]] Result<render::HdrImage> decodeHdr(std::span<const std::byte> file, const std::filesystem::path& path);

// decodeHdr, then renderer.setEnvironment(panorama, quality). The renderer
// must outlive the asset manager's use of this loader.
[[nodiscard]] AssetLoader<EnvironmentAsset, render::HdrImage> environmentLoader(
    render::Renderer& renderer, render::EnvironmentQuality quality = render::kDetailedEnvironment);

}  // namespace playground::engine
