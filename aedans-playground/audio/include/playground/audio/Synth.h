#pragma once
// =============================================================================
//  playground/audio/Synth.h — sound effects made from maths, not files.
//
//  So the demo (and the tests) can play something at once, with no .wav on
//  disk. Every one is mono 32-bit float PCM in [-1, 1], and deterministic:
//  the same call gives the same samples, bit for bit (noise comes from a
//  fixed-seed generator, not from the clock).
//
//    laser      a "pew": a bright tone sweeping down from 1.8 kHz to 200 Hz
//               in a quarter of a second, fading out
//    explosion  a burst of noise, low-passed more and more as it fades over
//               1.2 s, with a deep thump under it
//    impact     a short knock: a low thud and a click of noise, 0.15 s
//    hum        a steady 1 s loop of mains hum (60 Hz and harmonics) that
//               loops without a click
//    tone       a plain sine wave (tests measure it)
// =============================================================================

#include "playground/core/Types.h"

#include <vector>

namespace playground::audio {

struct Pcm {
  std::vector<f32> samples;  // mono
  u32 sampleRate = 48000;
  [[nodiscard]] f32 seconds() const noexcept {
    return static_cast<f32>(samples.size()) / static_cast<f32>(sampleRate);
  }
};

namespace synth {
[[nodiscard]] Pcm laser(u32 sampleRate = 48000);
[[nodiscard]] Pcm explosion(u32 sampleRate = 48000, u32 seed = 1);
[[nodiscard]] Pcm impact(u32 sampleRate = 48000, u32 seed = 2);
[[nodiscard]] Pcm hum(u32 sampleRate = 48000);
[[nodiscard]] Pcm tone(f32 hertz, f32 seconds, u32 sampleRate = 48000, f32 amplitude = 0.5f);
}  // namespace synth

}  // namespace playground::audio
