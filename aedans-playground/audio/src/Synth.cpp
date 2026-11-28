#include "playground/audio/Synth.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace playground::audio::synth {
namespace {

constexpr f32 kTwoPi = 2.0f * std::numbers::pi_v<f32>;

// A small, fast, fixed-seed noise source (xorshift32): the same seed, the
// same noise, everywhere.
class Noise {
 public:
  explicit Noise(u32 seed) noexcept : state_(seed == 0 ? 0x9E3779B9u : seed) {}
  f32 next() noexcept {  // uniform in [-1, 1)
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return static_cast<f32>(state_ >> 8) / static_cast<f32>(1u << 23) - 1.0f;
  }

 private:
  u32 state_;
};

usize frameCount(f32 seconds, u32 sampleRate) {
  return static_cast<usize>(std::lround(seconds * static_cast<f32>(sampleRate)));
}

// Scales so the loudest sample is `peak` (silence stays silence).
void normalise(std::vector<f32>& samples, f32 peak) {
  f32 loudest = 0.0f;
  for (const f32 s : samples) loudest = std::max(loudest, std::abs(s));
  if (loudest <= 0.0f) return;
  const f32 scale = peak / loudest;
  for (f32& s : samples) s *= scale;
}

// A few milliseconds of fade-in: no click at the start.
f32 attack(f32 t, f32 seconds = 0.003f) { return t < seconds ? t / seconds : 1.0f; }

}  // namespace

Pcm laser(u32 sampleRate) {
  constexpr f32 kSeconds = 0.25f;
  Pcm out{.samples = std::vector<f32>(frameCount(kSeconds, sampleRate)), .sampleRate = sampleRate};
  const auto rate = static_cast<f32>(sampleRate);
  f32 phase = 0.0f;
  for (usize i = 0; i < out.samples.size(); ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    const f32 hertz = 1800.0f * std::pow(200.0f / 1800.0f, t / kSeconds);  // an exponential sweep: even in pitch
    phase = std::fmod(phase + kTwoPi * hertz / rate, kTwoPi);
    const f32 wave = std::tanh(3.0f * std::sin(phase));  // a softened square: bright, not harsh
    out.samples[i] = wave * attack(t) * std::exp(-t * 9.0f);
  }
  normalise(out.samples, 0.6f);
  return out;
}

Pcm explosion(u32 sampleRate, u32 seed) {
  constexpr f32 kSeconds = 1.2f;
  Pcm out{.samples = std::vector<f32>(frameCount(kSeconds, sampleRate)), .sampleRate = sampleRate};
  const auto rate = static_cast<f32>(sampleRate);
  Noise noise(seed);
  f32 lowPassed = 0.0f;
  f32 phase = 0.0f;
  for (usize i = 0; i < out.samples.size(); ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    // Noise through a one-pole low-pass whose cutoff falls from 6 kHz to
    // 150 Hz: a crack that rumbles away.
    const f32 cutoff = 150.0f + 5850.0f * std::exp(-t * 4.0f);
    const f32 k = 1.0f - std::exp(-kTwoPi * cutoff / rate);
    lowPassed += k * (noise.next() - lowPassed);
    const f32 hertz = 35.0f + 20.0f * std::exp(-t * 5.0f);  // the thump: 55 Hz falling to 35
    phase = std::fmod(phase + kTwoPi * hertz / rate, kTwoPi);
    const f32 thump = std::sin(phase) * std::exp(-t * 6.0f);
    out.samples[i] = attack(t, 0.005f) * (1.6f * lowPassed * std::exp(-t * 3.5f) + 0.8f * thump);
  }
  normalise(out.samples, 0.9f);
  return out;
}

Pcm impact(u32 sampleRate, u32 seed) {
  constexpr f32 kSeconds = 0.15f;
  Pcm out{.samples = std::vector<f32>(frameCount(kSeconds, sampleRate)), .sampleRate = sampleRate};
  const auto rate = static_cast<f32>(sampleRate);
  Noise noise(seed);
  f32 phase = 0.0f;
  for (usize i = 0; i < out.samples.size(); ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    const f32 hertz = 60.0f + 60.0f * std::exp(-t * 40.0f);  // 120 Hz dropping to 60: a knock on wood
    phase = std::fmod(phase + kTwoPi * hertz / rate, kTwoPi);
    const f32 thud = std::sin(phase) * std::exp(-t * 30.0f);
    const f32 click = noise.next() * std::exp(-t * 200.0f);
    out.samples[i] = attack(t, 0.001f) * (0.8f * thud + 0.5f * click);
  }
  normalise(out.samples, 0.9f);
  return out;
}

Pcm hum(u32 sampleRate) {
  // One second: 60, 120 and 180 Hz all fit a whole number of cycles in it,
  // so the end meets the start and the loop has no click.
  Pcm out{.samples = std::vector<f32>(sampleRate), .sampleRate = sampleRate};
  const auto rate = static_cast<f32>(sampleRate);
  for (usize i = 0; i < out.samples.size(); ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    out.samples[i] = 0.2f * std::sin(kTwoPi * 60.0f * t) + 0.08f * std::sin(kTwoPi * 120.0f * t) +
                     0.04f * std::sin(kTwoPi * 180.0f * t);
  }
  return out;
}

Pcm tone(f32 hertz, f32 seconds, u32 sampleRate, f32 amplitude) {
  Pcm out{.samples = std::vector<f32>(frameCount(seconds, sampleRate)), .sampleRate = sampleRate};
  const auto rate = static_cast<f32>(sampleRate);
  for (usize i = 0; i < out.samples.size(); ++i) {
    // The phase from the index, not accumulated: no drift over long tones.
    const f64 cycles = static_cast<f64>(hertz) * static_cast<f64>(i) / static_cast<f64>(rate);
    out.samples[i] = amplitude * static_cast<f32>(std::sin(2.0 * std::numbers::pi * (cycles - std::floor(cycles))));
  }
  return out;
}

}  // namespace playground::audio::synth
