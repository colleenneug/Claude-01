#include "Audio.h"

#include "miniaudio.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

namespace {

constexpr int kRate = 48000;
constexpr float kTau = 6.2831853f;
constexpr int kVoices = 48;

using Buffer = std::vector<float>;

// ---------------------------------------------------------------- synthesis
//
// Everything below writes a mono buffer at kRate. The building blocks are
// deliberately few — noise, a sine, a saw, an envelope, a one-pole filter —
// and each effect is a handful of them added together, so a sound that is
// wrong is a line to change rather than a file to re-record.

struct Rng {
  uint32_t s = 0x9e3779b9u;
  float next() {   // -1..1
    s = s * 1664525u + 1013904223u;
    return (float)(s >> 8) / 8388608.0f - 1.0f;
  }
};

Buffer make(float seconds) { return Buffer((size_t)std::max(1.0f, seconds * kRate), 0.0f); }

// Attack then exponential decay to silence by the end of the buffer.
float env(float t, float attack, float decay) {
  if (t < attack) return attack > 0.0f ? t / attack : 1.0f;
  return std::exp(-(t - attack) / std::max(0.001f, decay));
}

struct OnePole {   // low-pass; high-pass is the input minus this
  float y = 0.0f;
  float run(float x, float cutoff) {
    const float a = 1.0f - std::exp(-kTau * cutoff / kRate);
    y += a * (x - y);
    return y;
  }
};

// A gunshot is a burst of filtered noise for the crack, a falling sine for
// the body, and a longer low tail for the room. The silhouettes differ in
// those three numbers more than anything else.
Buffer gunshot(float len, float crackCut, float body, float bodyDecay, float tail, float seed) {
  Buffer b = make(len);
  Rng r;
  r.s += (uint32_t)(seed * 1000.0f);
  OnePole lp, lp2;
  float ph = 0.0f;
  for (size_t i = 0; i < b.size(); i++) {
    const float t = (float)i / kRate;
    const float n = r.next();
    const float crack = lp.run(n, crackCut) * env(t, 0.0005f, 0.018f) * 1.6f;
    const float f = body * (1.0f + 2.5f * std::exp(-t / 0.012f));
    ph += kTau * f / kRate;
    const float thump = std::sin(ph) * env(t, 0.001f, bodyDecay);
    const float room = lp2.run(n, 900.0f) * env(t, 0.004f, tail) * 0.9f;
    b[i] = crack + thump * 0.9f + room;
  }
  return b;
}

Buffer tone(float len, float f0, float f1, float attack, float decay, int wave = 0, float vol = 1.0f) {
  Buffer b = make(len);
  float ph = 0.0f;
  for (size_t i = 0; i < b.size(); i++) {
    const float t = (float)i / kRate;
    const float k = t / len;
    const float f = f0 * std::pow(f1 / f0, k);
    ph += f / kRate;
    ph -= std::floor(ph);
    float s;
    switch (wave) {
      case 1: s = ph < 0.5f ? 0.6f : -0.6f; break;        // square
      case 2: s = (ph * 2.0f - 1.0f) * 0.7f; break;       // saw
      case 3: s = 1.0f - 4.0f * std::fabs(ph - 0.5f); break;  // triangle
      default: s = std::sin(ph * kTau); break;
    }
    b[i] = s * env(t, attack, decay) * vol;
  }
  return b;
}

Buffer noise(float len, float lowCut, float highCut, float attack, float decay, float vol = 1.0f,
             uint32_t seed = 7) {
  Buffer b = make(len);
  Rng r;
  r.s = seed;
  OnePole lo, hi;
  for (size_t i = 0; i < b.size(); i++) {
    const float t = (float)i / kRate;
    const float n = r.next();
    const float band = lo.run(n, highCut) - hi.run(n, lowCut);
    b[i] = band * env(t, attack, decay) * vol * 2.0f;
  }
  return b;
}

// a + b, starting b `at` seconds in. Grows a to fit.
void mix(Buffer& a, const Buffer& b, float at = 0.0f, float vol = 1.0f) {
  const size_t off = (size_t)(at * kRate);
  if (a.size() < off + b.size()) a.resize(off + b.size(), 0.0f);
  for (size_t i = 0; i < b.size(); i++) a[off + i] += b[i] * vol;
}

// Notes one after another: an arpeggio, a fanfare, a chime.
Buffer notes(std::initializer_list<float> freqs, float step, float each, int wave, float vol) {
  Buffer b;
  float at = 0.0f;
  for (float f : freqs) {
    mix(b, tone(each, f, f, 0.004f, each * 0.35f, wave, vol), at);
    mix(b, tone(each, f * 2.0f, f * 2.0f, 0.004f, each * 0.2f, 0, vol * 0.25f), at);
    at += step;
  }
  return b;
}

// Loops need to join without a click: whole cycles of every partial over
// the loop's length, and noise crossfaded into its own start.
Buffer hum(float len, std::initializer_list<std::pair<float, float>> partials, float noiseVol,
           float noiseCut) {
  Buffer b = make(len);
  for (auto [f, v] : partials) {
    const float cycles = std::round(f * len);
    for (size_t i = 0; i < b.size(); i++) b[i] += std::sin(kTau * cycles * (float)i / (float)b.size()) * v;
  }
  if (noiseVol > 0.0f) {
    // Noise a quarter longer than the loop, its overhang faded into the
    // loop's head: the last sample then runs straight on into the first.
    const size_t N = b.size(), fade = N / 4;
    Buffer n = noise((float)(N + fade) / kRate, 20.0f, noiseCut, 0.0f, 1e6f, noiseVol, 99);
    for (size_t i = 0; i < N; i++) {
      float v = n[i];
      if (i < fade) {
        const float k = (float)i / (float)fade;
        v = n[i] * k + n[N + i] * (1.0f - k);
      }
      b[i] += v;
    }
  }
  return b;
}

void normalise(Buffer& b, float peak) {
  float m = 0.0f;
  for (float v : b) m = std::max(m, std::fabs(v));
  if (m < 1e-6f) return;
  for (float& v : b) v *= peak / m;
}

std::array<Buffer, (size_t)Sfx::Count> synthesiseAll() {
  std::array<Buffer, (size_t)Sfx::Count> s;
  auto set = [&](Sfx id, Buffer b, float peak) {
    normalise(b, peak);
    s[(size_t)id] = std::move(b);
  };

  // ---- weapons
  set(Sfx::FirePistol, gunshot(0.32f, 5200.0f, 150.0f, 0.04f, 0.10f, 1), 0.70f);
  set(Sfx::FireRifle, gunshot(0.42f, 4200.0f, 110.0f, 0.05f, 0.16f, 2), 0.75f);
  set(Sfx::FireCarbine, gunshot(0.36f, 4800.0f, 125.0f, 0.045f, 0.12f, 3), 0.72f);
  set(Sfx::FireSmg, gunshot(0.22f, 6000.0f, 170.0f, 0.025f, 0.06f, 4), 0.55f);
  set(Sfx::FireShotgun, gunshot(0.70f, 2600.0f, 70.0f, 0.09f, 0.28f, 5), 0.90f);
  {
    Buffer b = gunshot(0.9f, 3800.0f, 95.0f, 0.07f, 0.40f, 6);
    mix(b, noise(0.05f, 3000.0f, 12000.0f, 0.0f, 0.01f, 0.8f, 61));   // the supersonic crack
    set(Sfx::FireMarksman, b, 0.88f);
  }
  {
    // An induction weapon does not burn powder: a rising whine, a snap.
    Buffer b = tone(0.30f, 2400.0f, 380.0f, 0.001f, 0.09f, 2, 0.9f);
    mix(b, tone(0.30f, 1200.0f, 190.0f, 0.001f, 0.12f, 0, 0.6f));
    mix(b, noise(0.12f, 400.0f, 7000.0f, 0.0f, 0.02f, 0.5f, 71));
    set(Sfx::FireInduction, b, 0.62f);
  }
  {
    Buffer b = noise(0.06f, 1500.0f, 9000.0f, 0.0f, 0.008f, 1.0f, 11);
    mix(b, tone(0.05f, 1800.0f, 1700.0f, 0.0f, 0.01f, 1, 0.3f));
    set(Sfx::DryFire, b, 0.40f);
  }
  {
    Buffer b = noise(0.10f, 800.0f, 6000.0f, 0.0f, 0.015f, 1.0f, 12);
    mix(b, tone(0.10f, 520.0f, 400.0f, 0.0f, 0.02f, 1, 0.4f), 0.01f);
    mix(b, noise(0.18f, 200.0f, 2500.0f, 0.02f, 0.05f, 0.4f, 13), 0.05f);
    set(Sfx::ReloadOut, b, 0.45f);
  }
  {
    Buffer b = noise(0.08f, 900.0f, 7000.0f, 0.0f, 0.012f, 1.0f, 14);
    mix(b, tone(0.08f, 700.0f, 650.0f, 0.0f, 0.015f, 1, 0.5f));
    mix(b, noise(0.08f, 1200.0f, 9000.0f, 0.0f, 0.01f, 1.0f, 15), 0.16f);   // the slide
    mix(b, tone(0.08f, 900.0f, 850.0f, 0.0f, 0.012f, 1, 0.4f), 0.16f);
    set(Sfx::ReloadIn, b, 0.50f);
  }
  {
    Buffer b = noise(0.25f, 300.0f, 4000.0f, 0.01f, 0.05f, 0.8f, 16);
    mix(b, tone(0.06f, 600.0f, 560.0f, 0.0f, 0.012f, 1, 0.4f), 0.14f);
    set(Sfx::WeaponSwap, b, 0.42f);
  }

  // ---- your rounds landing
  set(Sfx::Hit, tone(0.07f, 1900.0f, 1600.0f, 0.0f, 0.02f, 3, 1.0f), 0.30f);
  {
    Buffer b = tone(0.14f, 2800.0f, 2600.0f, 0.0f, 0.04f, 0, 1.0f);
    mix(b, tone(0.14f, 4200.0f, 4100.0f, 0.0f, 0.03f, 0, 0.5f));
    set(Sfx::Headshot, b, 0.38f);
  }
  {
    Buffer b = tone(0.09f, 1400.0f, 1400.0f, 0.0f, 0.03f, 3, 1.0f);
    mix(b, tone(0.16f, 2100.0f, 2100.0f, 0.0f, 0.05f, 3, 1.0f), 0.07f);
    set(Sfx::Kill, b, 0.40f);
  }
  {
    Buffer b = noise(0.25f, 1500.0f, 9000.0f, 0.0f, 0.02f, 0.6f, 21);
    for (float f : {1230.0f, 1870.0f, 2650.0f}) mix(b, tone(0.4f, f, f * 0.98f, 0.0f, 0.09f, 0, 0.3f));
    set(Sfx::HitMetal, b, 0.40f);
  }

  // ---- hostiles
  {
    Buffer b = tone(0.22f, 2200.0f, 500.0f, 0.001f, 0.07f, 1, 0.7f);
    mix(b, tone(0.22f, 1100.0f, 260.0f, 0.001f, 0.08f, 0, 0.6f));
    set(Sfx::DroneShot, b, 0.55f);
  }
  {
    // A thrall is a person, or was: a ragged formant over a growl.
    Buffer b = make(0.6f);
    Rng r;
    OnePole lp;
    float ph = 0.0f;
    for (size_t i = 0; i < b.size(); i++) {
      const float t = (float)i / kRate;
      const float f = 210.0f + 140.0f * std::sin(t * 9.0f) - 160.0f * t;
      ph += kTau * f / kRate;
      const float buzz = std::sin(ph) + 0.5f * std::sin(ph * 2.03f) + 0.3f * std::sin(ph * 3.1f);
      b[i] = (buzz * 0.6f + lp.run(r.next(), 1800.0f) * 1.2f) * env(t, 0.03f, 0.22f);
    }
    set(Sfx::ThrallShriek, b, 0.55f);
  }
  {
    Buffer b = tone(0.5f, 420.0f, 90.0f, 0.002f, 0.14f, 2, 0.9f);
    mix(b, noise(0.4f, 60.0f, 1500.0f, 0.002f, 0.12f, 0.8f, 31));
    mix(b, tone(0.5f, 70.0f, 45.0f, 0.002f, 0.18f, 0, 0.8f));
    set(Sfx::WardenShot, b, 0.72f);
  }
  set(Sfx::MarauderShot, gunshot(0.4f, 3400.0f, 100.0f, 0.06f, 0.14f, 32), 0.62f);
  {
    Buffer b;
    for (int k = 0; k < 7; k++) {
      mix(b, noise(0.03f, 2500.0f, 11000.0f, 0.0f, 0.006f, 0.8f, 40 + (uint32_t)k), k * 0.028f);
    }
    set(Sfx::ScarabSkitter, b, 0.40f);
  }
  {
    Buffer b = tone(1.3f, 60.0f, 28.0f, 0.004f, 0.45f, 0, 1.0f);
    mix(b, noise(1.2f, 20.0f, 500.0f, 0.004f, 0.35f, 1.0f, 33));
    mix(b, noise(0.2f, 600.0f, 4000.0f, 0.0f, 0.04f, 0.5f, 34));
    set(Sfx::ColossusSlam, b, 0.95f);
  }
  {
    // The Conductor sings: a minor chord of detuned saws swelling in.
    Buffer b = make(1.6f);
    for (float f : {110.0f, 130.8f, 164.8f, 220.0f}) {
      mix(b, tone(1.6f, f, f * 1.01f, 0.4f, 0.7f, 2, 0.5f));
      mix(b, tone(1.6f, f * 1.006f, f * 0.995f, 0.45f, 0.7f, 2, 0.4f));
    }
    set(Sfx::ConductorChord, b, 0.70f);
  }
  {
    Buffer b = tone(0.35f, 300.0f, 900.0f, 0.05f, 0.12f, 2, 0.5f);
    mix(b, noise(0.3f, 200.0f, 3000.0f, 0.05f, 0.1f, 0.3f, 35));
    set(Sfx::HostileWake, b, 0.40f);
  }
  {
    Buffer b = noise(0.5f, 80.0f, 2200.0f, 0.002f, 0.12f, 1.0f, 36);
    mix(b, tone(0.4f, 180.0f, 60.0f, 0.005f, 0.12f, 0, 0.8f));
    set(Sfx::DeathOrganic, b, 0.55f);
  }
  {
    Buffer b = noise(0.8f, 40.0f, 3500.0f, 0.001f, 0.22f, 1.0f, 37);
    mix(b, tone(0.6f, 140.0f, 40.0f, 0.001f, 0.16f, 0, 1.0f));
    mix(b, tone(0.3f, 1600.0f, 300.0f, 0.0f, 0.06f, 1, 0.3f));
    set(Sfx::DeathMech, b, 0.70f);
  }
  {
    Buffer b = noise(1.6f, 30.0f, 3000.0f, 0.001f, 0.45f, 1.0f, 38);
    mix(b, tone(1.2f, 90.0f, 30.0f, 0.001f, 0.35f, 0, 1.0f));
    for (int k = 0; k < 6; k++) {   // glass and fins coming down after
      mix(b, noise(0.08f, 3000.0f, 12000.0f, 0.0f, 0.02f, 0.5f, 50 + (uint32_t)k), 0.15f + 0.11f * k);
    }
    set(Sfx::StructureDown, b, 0.90f);
  }
  {
    Buffer b = noise(2.6f, 25.0f, 2500.0f, 0.002f, 0.8f, 1.0f, 39);
    mix(b, tone(2.4f, 70.0f, 22.0f, 0.002f, 0.7f, 0, 1.0f));
    mix(b, tone(2.0f, 440.0f, 110.0f, 0.1f, 0.5f, 2, 0.3f), 0.3f);
    set(Sfx::BossDeath, b, 1.0f);
  }

  // ---- you
  set(Sfx::Footstep, noise(0.09f, 90.0f, 1600.0f, 0.002f, 0.025f, 1.0f, 60), 0.22f);
  {
    Buffer b = noise(0.2f, 200.0f, 3000.0f, 0.01f, 0.05f, 0.6f, 61);
    mix(b, tone(0.15f, 180.0f, 260.0f, 0.005f, 0.04f, 0, 0.5f));
    set(Sfx::Jump, b, 0.30f);
  }
  {
    Buffer b = noise(0.18f, 50.0f, 1200.0f, 0.001f, 0.05f, 1.0f, 62);
    mix(b, tone(0.15f, 110.0f, 60.0f, 0.001f, 0.05f, 0, 0.9f));
    set(Sfx::Land, b, 0.42f);
  }
  set(Sfx::Slide, noise(0.6f, 400.0f, 5000.0f, 0.05f, 0.2f, 1.0f, 63), 0.32f);
  {
    Buffer b = tone(0.3f, 160.0f, 70.0f, 0.001f, 0.08f, 0, 1.0f);
    mix(b, noise(0.25f, 100.0f, 2000.0f, 0.001f, 0.06f, 0.7f, 64));
    set(Sfx::Hurt, b, 0.55f);
  }
  {
    Buffer b = tone(1.4f, 440.0f, 110.0f, 0.01f, 0.5f, 2, 0.6f);
    mix(b, tone(1.4f, 70.0f, 40.0f, 0.01f, 0.5f, 0, 0.8f));
    set(Sfx::Downed, b, 0.7f);
  }
  set(Sfx::Revive, notes({330.0f, 440.0f, 554.0f, 660.0f}, 0.08f, 0.3f, 3, 0.7f), 0.55f);
  set(Sfx::Heal, notes({523.0f, 784.0f}, 0.07f, 0.25f, 0, 0.7f), 0.38f);
  {
    Buffer b = noise(0.4f, 1500.0f, 10000.0f, 0.0f, 0.08f, 1.0f, 65);
    mix(b, tone(0.4f, 1800.0f, 300.0f, 0.0f, 0.1f, 2, 0.4f));
    set(Sfx::ShieldBreak, b, 0.55f);
  }
  {
    Buffer b = tone(0.8f, 220.0f, 880.0f, 0.05f, 0.4f, 3, 0.7f);
    mix(b, tone(0.8f, 330.0f, 1320.0f, 0.06f, 0.4f, 0, 0.4f));
    mix(b, noise(0.7f, 2000.0f, 9000.0f, 0.1f, 0.3f, 0.2f, 66));
    set(Sfx::AbilityBarrier, b, 0.6f);
  }
  {
    Buffer b = tone(0.7f, 3000.0f, 80.0f, 0.0f, 0.2f, 1, 0.6f);
    mix(b, noise(0.7f, 50.0f, 6000.0f, 0.0f, 0.18f, 0.9f, 67));
    mix(b, tone(0.7f, 90.0f, 40.0f, 0.0f, 0.25f, 0, 0.8f));
    set(Sfx::AbilityBreach, b, 0.75f);
  }
  {
    Buffer b = noise(0.45f, 300.0f, 8000.0f, 0.12f, 0.08f, 1.0f, 68);
    mix(b, tone(0.45f, 300.0f, 2400.0f, 0.1f, 0.1f, 0, 0.5f));
    set(Sfx::AbilityPhase, b, 0.55f);
  }
  set(Sfx::AbilityReady, notes({880.0f, 1320.0f}, 0.06f, 0.18f, 3, 0.6f), 0.28f);

  // ---- things on the floor
  {
    Buffer b = noise(0.06f, 1000.0f, 8000.0f, 0.0f, 0.01f, 1.0f, 70);
    mix(b, noise(0.06f, 1000.0f, 8000.0f, 0.0f, 0.01f, 1.0f, 71), 0.09f);
    mix(b, tone(0.12f, 1200.0f, 1200.0f, 0.0f, 0.03f, 3, 0.4f), 0.09f);
    set(Sfx::PickupAmmo, b, 0.40f);
  }
  set(Sfx::PickupHealth, notes({587.0f, 740.0f, 880.0f}, 0.06f, 0.25f, 0, 0.7f), 0.40f);
  {
    Buffer b = noise(0.1f, 600.0f, 6000.0f, 0.0f, 0.02f, 1.0f, 72);
    mix(b, noise(0.12f, 1500.0f, 9000.0f, 0.0f, 0.015f, 1.0f, 73), 0.14f);
    mix(b, notes({660.0f, 990.0f}, 0.06f, 0.2f, 3, 0.5f), 0.22f);
    set(Sfx::PickupWeapon, b, 0.5f);
  }
  set(Sfx::PickupCore, notes({784.0f, 988.0f, 1175.0f, 1568.0f}, 0.055f, 0.3f, 3, 0.7f), 0.48f);
  {
    Buffer b = notes({1047.0f, 1319.0f, 1568.0f, 2093.0f}, 0.04f, 0.5f, 0, 0.5f);
    mix(b, noise(0.8f, 4000.0f, 12000.0f, 0.05f, 0.3f, 0.25f, 74));
    set(Sfx::LootDrop, b, 0.5f);
  }

  // ---- the mission
  set(Sfx::ObjectiveDone, notes({523.0f, 659.0f, 784.0f}, 0.09f, 0.4f, 3, 0.8f), 0.52f);
  set(Sfx::HoldTick, tone(0.05f, 1500.0f, 1500.0f, 0.0f, 0.012f, 1, 0.5f), 0.22f);
  {
    Buffer b = notes({392.0f, 523.0f, 659.0f, 784.0f, 1047.0f}, 0.12f, 0.7f, 2, 0.5f);
    mix(b, tone(1.4f, 131.0f, 131.0f, 0.02f, 0.6f, 0, 0.6f), 0.48f);
    set(Sfx::MissionComplete, b, 0.62f);
  }
  set(Sfx::MissionFailed, notes({392.0f, 330.0f, 262.0f, 196.0f}, 0.2f, 0.7f, 2, 0.6f), 0.55f);
  {
    Buffer b = tone(2.2f, 55.0f, 55.0f, 0.6f, 0.8f, 2, 0.8f);
    mix(b, tone(2.2f, 82.4f, 82.4f, 0.7f, 0.8f, 2, 0.6f));
    mix(b, noise(2.0f, 30.0f, 400.0f, 0.5f, 0.7f, 0.6f, 75));
    set(Sfx::BossSpawn, b, 0.85f);
  }
  {
    Buffer b = notes({262.0f, 330.0f, 392.0f, 523.0f}, 0.14f, 0.6f, 2, 0.5f);
    mix(b, notes({523.0f, 659.0f, 784.0f, 1047.0f}, 0.0f, 1.6f, 3, 0.4f), 0.6f);
    set(Sfx::Promotion, b, 0.7f);
  }
  {
    Buffer b = noise(0.12f, 800.0f, 4000.0f, 0.0f, 0.04f, 0.5f, 76);
    mix(b, tone(0.06f, 1760.0f, 1760.0f, 0.0f, 0.02f, 1, 0.5f), 0.03f);
    mix(b, tone(0.06f, 2349.0f, 2349.0f, 0.0f, 0.02f, 1, 0.5f), 0.09f);
    set(Sfx::Comms, b, 0.30f);
  }
  set(Sfx::Cutscene, tone(1.2f, 110.0f, 82.0f, 0.3f, 0.4f, 2, 0.5f), 0.4f);

  // ---- menus and people
  set(Sfx::UiMove, tone(0.03f, 1250.0f, 1250.0f, 0.0f, 0.008f, 3, 1.0f), 0.18f);
  set(Sfx::UiSelect, notes({880.0f, 1175.0f}, 0.045f, 0.12f, 3, 0.8f), 0.30f);
  set(Sfx::UiBack, notes({880.0f, 660.0f}, 0.045f, 0.12f, 3, 0.8f), 0.26f);
  {
    Buffer b = tone(0.22f, 140.0f, 130.0f, 0.0f, 0.1f, 1, 0.8f);
    mix(b, tone(0.22f, 147.0f, 137.0f, 0.0f, 0.1f, 1, 0.8f));
    set(Sfx::UiDeny, b, 0.30f);
  }
  set(Sfx::Buy, notes({1319.0f, 1760.0f, 2093.0f}, 0.05f, 0.25f, 0, 0.7f), 0.36f);
  set(Sfx::Sell, notes({1760.0f, 1319.0f}, 0.05f, 0.2f, 0, 0.7f), 0.32f);
  set(Sfx::Talk, tone(0.05f, 420.0f, 380.0f, 0.003f, 0.015f, 1, 0.6f), 0.14f);
  set(Sfx::BountyDone, notes({659.0f, 880.0f, 1109.0f, 1319.0f}, 0.07f, 0.35f, 3, 0.7f), 0.46f);

  // ---- ships and stations
  set(Sfx::Boost, noise(1.0f, 100.0f, 3000.0f, 0.1f, 0.4f, 1.0f, 80), 0.55f);
  {
    Buffer b = tone(0.5f, 90.0f, 50.0f, 0.001f, 0.15f, 0, 1.0f);
    mix(b, noise(0.4f, 60.0f, 2000.0f, 0.001f, 0.08f, 0.8f, 81));
    mix(b, noise(1.0f, 2000.0f, 9000.0f, 0.15f, 0.35f, 0.35f, 82), 0.3f);   // seal and hiss
    set(Sfx::Dock, b, 0.65f);
  }
  {
    Buffer b = noise(0.8f, 2000.0f, 9000.0f, 0.02f, 0.25f, 0.4f, 83);
    mix(b, tone(0.4f, 50.0f, 90.0f, 0.02f, 0.15f, 0, 0.8f), 0.3f);
    set(Sfx::Undock, b, 0.6f);
  }
  {
    Buffer b = noise(0.7f, 150.0f, 2500.0f, 0.05f, 0.25f, 0.7f, 84);
    mix(b, tone(0.12f, 300.0f, 260.0f, 0.0f, 0.03f, 1, 0.4f), 0.55f);
    set(Sfx::Door, b, 0.42f);
  }
  {
    Buffer b = tone(1.8f, 80.0f, 900.0f, 0.4f, 0.4f, 2, 0.5f);
    mix(b, noise(1.8f, 200.0f, 7000.0f, 0.5f, 0.4f, 0.5f, 85));
    set(Sfx::Warp, b, 0.6f);
  }
  return s;
}

std::array<Buffer, (size_t)Loop::Count> synthesiseLoops() {
  std::array<Buffer, (size_t)Loop::Count> l;
  l[(size_t)Loop::Engine] = hum(2.0f, {{55.0f, 0.5f}, {110.0f, 0.3f}, {165.0f, 0.12f}, {57.0f, 0.3f}}, 0.25f, 500.0f);
  l[(size_t)Loop::Uplink] = hum(2.0f, {{220.0f, 0.25f}, {330.0f, 0.15f}, {441.0f, 0.12f}, {8.0f, 0.0f}}, 0.08f, 3000.0f);
  {
    // Heartbeat: lub-dub once a second. One cycle, so the loop is the beat.
    Buffer b = make(1.0f);
    mix(b, tone(0.15f, 70.0f, 45.0f, 0.005f, 0.04f, 0, 1.0f), 0.0f);
    mix(b, tone(0.15f, 60.0f, 40.0f, 0.005f, 0.04f, 0, 0.8f), 0.22f);
    b.resize((size_t)kRate);
    l[(size_t)Loop::Heartbeat] = b;
  }
  l[(size_t)Loop::MissionAmbience] = hum(4.0f, {{41.0f, 0.2f}, {61.5f, 0.12f}, {82.0f, 0.08f}}, 0.35f, 300.0f);
  l[(size_t)Loop::StationHum] = hum(3.0f, {{60.0f, 0.3f}, {120.0f, 0.2f}, {180.0f, 0.08f}, {240.0f, 0.04f}}, 0.1f, 1200.0f);
  for (Buffer& b : l) normalise(b, 0.5f);
  return l;
}

// ---------------------------------------------------------------- mixing

struct Voice {
  const Buffer* buf = nullptr;
  double pos = 0.0;
  float step = 1.0f;
  float gainL = 0.0f, gainR = 0.0f;
};

struct LoopState {
  const Buffer* buf = nullptr;
  double pos = 0.0;
  float vol = 0.0f, pitch = 1.0f;          // where it is
  float targetVol = 0.0f, targetPitch = 1.0f;  // where it is going
};

struct State {
  std::array<Buffer, (size_t)Sfx::Count> sounds;
  std::array<Buffer, (size_t)Loop::Count> loopBufs;
  std::array<Voice, kVoices> voices;
  std::array<LoopState, (size_t)Loop::Count> loops;
  std::mutex lock;
  ma_device device;
  bool deviceUp = false;
  bool ready = false;
  glm::vec3 ear{0.0f}, forward{0.0f, 0.0f, -1.0f};
  std::atomic<int> plays{0};
  std::array<std::atomic<int>, (size_t)Sfx::Count> perSound{};
  std::array<double, (size_t)Sfx::Count> lastPlayed{};   // in callback frames, for de-duplication
  uint64_t clock = 0;
  std::string name = "none";
};

State* g = nullptr;

void callback(ma_device* dev, void* out, const void*, ma_uint32 frames) {
  State* st = (State*)dev->pUserData;
  float* o = (float*)out;
  std::fill(o, o + frames * 2, 0.0f);
  std::lock_guard<std::mutex> guard(st->lock);
  for (Voice& v : st->voices) {
    if (!v.buf) continue;
    const Buffer& b = *v.buf;
    for (ma_uint32 i = 0; i < frames; i++) {
      const size_t p = (size_t)v.pos;
      if (p + 1 >= b.size()) { v.buf = nullptr; break; }
      const float f = (float)(v.pos - (double)p);
      const float x = b[p] * (1.0f - f) + b[p + 1] * f;
      o[i * 2] += x * v.gainL;
      o[i * 2 + 1] += x * v.gainR;
      v.pos += v.step;
    }
  }
  for (LoopState& l : st->loops) {
    if (!l.buf || (l.vol < 1e-4f && l.targetVol < 1e-4f)) continue;
    const Buffer& b = *l.buf;
    const double n = (double)b.size();
    for (ma_uint32 i = 0; i < frames; i++) {
      l.vol += (l.targetVol - l.vol) * 0.0015f;
      l.pitch += (l.targetPitch - l.pitch) * 0.0015f;
      const size_t p = (size_t)l.pos;
      const size_t q = (p + 1) % b.size();
      const float f = (float)(l.pos - (double)p);
      const float x = (b[p] * (1.0f - f) + b[q] * f) * l.vol;
      o[i * 2] += x;
      o[i * 2 + 1] += x;
      l.pos += l.pitch;
      while (l.pos >= n) l.pos -= n;
    }
  }
  // Soft limit: a dozen guns at once should get louder, not crackle.
  for (ma_uint32 i = 0; i < frames * 2; i++) o[i] = std::tanh(o[i] * 0.9f);
  st->clock += frames;
}

void start(Sfx s, float volume, float pitch, float pan, bool positional) {
  if (!g) return;
  g->plays++;
  g->perSound[(size_t)s]++;
  if (!g->deviceUp || volume <= 0.002f) return;
  const Buffer& b = g->sounds[(size_t)s];
  if (b.empty()) return;
  std::lock_guard<std::mutex> guard(g->lock);
  // The same sound landing several times in one buffer (a shotgun's eight
  // pellets all hitting) is one louder sound, not eight stacked copies.
  const double now = (double)g->clock;
  if (now - g->lastPlayed[(size_t)s] < 1.0f && !positional) return;
  g->lastPlayed[(size_t)s] = now;
  Voice* slot = nullptr;
  size_t oldest = 0;
  double furthest = -1.0;
  for (size_t i = 0; i < g->voices.size(); i++) {
    if (!g->voices[i].buf) { slot = &g->voices[i]; break; }
    const double done = g->voices[i].pos / (double)g->voices[i].buf->size();
    if (done > furthest) { furthest = done; oldest = i; }
  }
  if (!slot) slot = &g->voices[oldest];   // steal the one nearest its end
  slot->buf = &b;
  slot->pos = 0.0;
  slot->step = std::clamp(pitch, 0.25f, 4.0f);
  const float l = std::sqrt(0.5f * (1.0f - pan)), r = std::sqrt(0.5f * (1.0f + pan));
  slot->gainL = volume * l * 1.41f;
  slot->gainR = volume * r * 1.41f;
}

}  // namespace

namespace audio {

bool init() {
  if (g) return g->deviceUp;
  g = new State();
  g->sounds = synthesiseAll();
  g->loopBufs = synthesiseLoops();
  for (size_t i = 0; i < g->loops.size(); i++) g->loops[i].buf = &g->loopBufs[i];
  g->ready = true;
  for (double& t : g->lastPlayed) t = -1e9;

  const char* mute = std::getenv("EREBUS_MUTE");
  if (mute && *mute && *mute != '0') {
    std::printf("[Audio] muted (EREBUS_MUTE); %d sounds synthesised\n", soundCount());
    return false;
  }
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.sampleRate = kRate;
  cfg.dataCallback = callback;
  cfg.pUserData = g;
  if (ma_device_init(nullptr, &cfg, &g->device) != MA_SUCCESS) {
    std::printf("[Audio] no output device - playing silent; %d sounds synthesised\n", soundCount());
    return false;
  }
  if (ma_device_start(&g->device) != MA_SUCCESS) {
    ma_device_uninit(&g->device);
    std::printf("[Audio] output device would not start - playing silent\n");
    return false;
  }
  g->deviceUp = true;
  g->name = g->device.playback.name;
  std::printf("[Audio] %d sounds synthesised, playing on '%s'\n", soundCount(), g->name.c_str());
  return true;
}

void shutdown() {
  if (!g) return;
  if (g->deviceUp) ma_device_uninit(&g->device);
  delete g;
  g = nullptr;
}

bool live() { return g && g->deviceUp; }

void setListener(const glm::vec3& pos, const glm::vec3& forward) {
  if (!g) return;
  g->ear = pos;
  glm::vec3 f(forward.x, 0.0f, forward.z);
  if (glm::length(f) > 1e-4f) g->forward = glm::normalize(f);
}

void play(Sfx s, float volume, float pitch) { start(s, volume, pitch, 0.0f, false); }

void playAt(Sfx s, const glm::vec3& at, float volume, float pitch) {
  if (!g) return;
  glm::vec3 d = at - g->ear;
  const float dist = glm::length(d);
  // Inverse distance past a couple of metres, and nothing past eighty: a
  // fight three rooms away on an expedition should not be in your ears.
  float gain = volume / (1.0f + std::max(0.0f, dist - 2.0f) * 0.12f);
  if (dist > 80.0f) gain = 0.0f;
  float pan = 0.0f;
  if (dist > 0.5f) {
    const glm::vec3 right = glm::normalize(glm::cross(g->forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    pan = std::clamp(glm::dot(glm::normalize(glm::vec3(d.x, 0.0f, d.z) + glm::vec3(1e-5f)), right), -1.0f, 1.0f) * 0.8f;
  }
  start(s, gain, pitch, pan, true);
}

void setLoop(Loop l, float volume, float pitch) {
  if (!g) return;
  std::lock_guard<std::mutex> guard(g->lock);
  LoopState& s = g->loops[(size_t)l];
  s.targetVol = std::clamp(volume, 0.0f, 1.0f);
  s.targetPitch = std::clamp(pitch, 0.25f, 4.0f);
}

void stopLoops() {
  if (!g) return;
  std::lock_guard<std::mutex> guard(g->lock);
  for (LoopState& s : g->loops) s.targetVol = 0.0f;
}

int playCount() { return g ? g->plays.load() : 0; }
int playCount(Sfx s) { return g ? g->perSound[(size_t)s].load() : 0; }
int soundCount() {
  if (!g) return 0;
  int n = 0;
  for (const Buffer& b : g->sounds) n += b.empty() ? 0 : 1;
  return n;
}
std::string deviceName() { return g ? g->name : "none"; }

}  // namespace audio
