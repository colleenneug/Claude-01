// Tests for the audio library and the ECS audio system: starting with no
// sound card (manual mixing and miniaudio's null device), a voice's life
// (play, loop, stop, finish, freed), refusals, and the 3D maths checked
// against what was actually mixed: distance attenuation, left/right
// panning, and Doppler pitch. Also the procedural sound effects, and
// audioSystem following entities.
//
// No window, no GPU, no sound card needed. The null-device tests run
// miniaudio's real-time audio thread: also run under ThreadSanitizer.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/audio/AudioEngine.h"
#include "playground/audio/Synth.h"
#include "playground/ecs/World.h"
#include "playground/engine/Audio.h"
#include "playground/engine/Components.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

#include <glm/gtc/quaternion.hpp>

using namespace playground;
using audio::AudioEngine;
using audio::Output;

namespace {

constexpr u32 kRate = 48000;

std::unique_ptr<AudioEngine> manualEngine() {
  auto engine = AudioEngine::create({.output = Output::Manual, .sampleRate = kRate, .channels = 2});
  REQUIRE_MESSAGE(engine.has_value(), (engine ? "" : engine.error().message));
  return std::move(*engine);
}

audio::ClipId addPcm(AudioEngine& engine, const audio::Pcm& pcm, const char* name) {
  auto clip = engine.addClip(pcm.samples, 1, pcm.sampleRate, name);
  REQUIRE_MESSAGE(clip.has_value(), (clip ? "" : clip.error().message));
  return *clip;
}

std::vector<f32> render(AudioEngine& engine, u32 frames) {
  auto out = engine.render(frames);
  REQUIRE(out.has_value());
  return std::move(*out);
}

// Root mean square of one channel (0 left, 1 right) of interleaved stereo.
f64 rms(const std::vector<f32>& stereo, int channel) {
  f64 sum = 0.0;
  usize n = 0;
  for (usize i = static_cast<usize>(channel); i < stereo.size(); i += 2, ++n) sum += f64{stereo[i]} * stereo[i];
  return n == 0 ? 0.0 : std::sqrt(sum / static_cast<f64>(n));
}

// Frequency from upward zero crossings of one channel.
f64 frequency(const std::vector<f32>& stereo, int channel) {
  u32 crossings = 0;
  f32 previous = stereo[static_cast<usize>(channel)];
  for (usize i = static_cast<usize>(channel) + 2; i < stereo.size(); i += 2) {
    if (previous < 0.0f && stereo[i] >= 0.0f) ++crossings;
    previous = stereo[i];
  }
  return static_cast<f64>(crossings) / (static_cast<f64>(stereo.size() / 2) / kRate);
}

}  // namespace

// --- Starting -------------------------------------------------------------------------

TEST_CASE("manual output starts with no device at all") {
  auto engine = manualEngine();
  CHECK(engine->output() == Output::Manual);
  CHECK(engine->sampleRate() == kRate);
  CHECK(engine->channels() == 2);
  const auto silence = render(*engine, 480);
  CHECK(silence.size() == 960);
  CHECK(rms(silence, 0) == 0.0);
}

TEST_CASE("the null device plays in real time with no sound card; a one-shot finishes and is freed") {
  auto engine = AudioEngine::create({.output = Output::NullDevice});
  REQUIRE_MESSAGE(engine.has_value(), (engine ? "" : engine.error().message));
  CHECK((*engine)->output() == Output::NullDevice);
  const auto clip = addPcm(**engine, audio::synth::tone(440.0f, 0.05f), "beep");
  auto voice = (*engine)->play(clip, {.spatial = false});
  REQUIRE(voice.has_value());
  CHECK((*engine)->playing(*voice));
  CHECK((*engine)->stats().voicesPlaying == 1);
  bool finished = false;
  for (int i = 0; i < 100 && !finished; ++i) {  // up to a second, for a 50 ms clip
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    (*engine)->update();
    finished = (*engine)->stats().voicesPlaying == 0;
  }
  CHECK(finished);
  CHECK_FALSE((*engine)->playing(*voice));
  CHECK((*engine)->stats().voicesStarted == 1);
  // And render() is for manual output only.
  const auto refused = (*engine)->render(16);
  REQUIRE_FALSE(refused.has_value());
  CHECK(refused.error().code == Errc::InvalidState);
}

TEST_CASE("asking for the sound card: the device if there is one, else the null device, never a failure") {
  ::setenv("PLAYGROUND_AUDIO", "null", 1);  // as CI and the tests do: never touch a real sound card here
  auto engine = AudioEngine::create({.output = Output::Device});
  ::unsetenv("PLAYGROUND_AUDIO");
  REQUIRE_MESSAGE(engine.has_value(), (engine ? "" : engine.error().message));
  CHECK((*engine)->output() == Output::NullDevice);
  CHECK_FALSE(AudioEngine::create({.channels = 6}).has_value());
}

// --- A voice's life ----------------------------------------------------------------------

TEST_CASE("a one-shot plays to its end; a loop plays on until stopped; removing a clip stops its voices") {
  auto engine = manualEngine();
  const auto shortClip = addPcm(*engine, audio::synth::tone(300.0f, 0.1f), "short");
  const auto loopClip = addPcm(*engine, audio::synth::hum(), "hum");
  CHECK(engine->clipSeconds(shortClip) == doctest::Approx(0.1f));
  auto once = engine->play(shortClip, {.spatial = false});
  auto loop = engine->play(loopClip, {.volume = 0.5f, .loop = true, .spatial = false});
  REQUIRE(once.has_value());
  REQUIRE(loop.has_value());
  CHECK(engine->playing(*once));
  CHECK(rms(render(*engine, 2400), 0) > 0.05);  // 50 ms: both sounding

  (void)render(*engine, 9600);  // 200 ms more: the one-shot (100 ms) is over
  engine->update();
  CHECK_FALSE(engine->playing(*once));
  CHECK(engine->playing(*loop));
  CHECK(engine->stats().voicesPlaying == 1);

  (void)render(*engine, kRate * 2);  // two more seconds: the 1 s loop is still going
  CHECK(engine->playing(*loop));
  engine->stop(*loop);
  CHECK_FALSE(engine->playing(*loop));
  CHECK(rms(render(*engine, 4800), 0) < 1e-4);  // quiet again

  auto again = engine->play(loopClip, {.loop = true, .spatial = false});
  REQUIRE(again.has_value());
  engine->removeClip(loopClip);
  CHECK_FALSE(engine->playing(*again));
  CHECK_FALSE(engine->play(loopClip).has_value());  // gone
  engine->stop(*again);  // a stale voice: harmless
}

TEST_CASE("stopIf stops the voices of chosen owners only") {
  auto engine = manualEngine();
  const auto clip = addPcm(*engine, audio::synth::hum(), "hum");
  auto a = engine->play(clip, {.loop = true, .owner = 7});
  auto b = engine->play(clip, {.loop = true, .owner = 8});
  auto c = engine->play(clip, {.loop = true});
  engine->stopIf([](u64 owner) { return owner == 7; });
  CHECK_FALSE(engine->playing(*a));
  CHECK(engine->playing(*b));
  CHECK(engine->playing(*c));
}

TEST_CASE("bad clips and plays are refused with a reason") {
  auto engine = manualEngine();
  const std::vector<f32> three{0.1f, 0.2f, 0.3f};
  CHECK_FALSE(engine->addClip(three, 2, kRate, "odd").has_value());  // not whole stereo frames
  CHECK_FALSE(engine->addClip(three, 3, kRate, "3 channels").has_value());
  CHECK_FALSE(engine->addClip({}, 1, kRate, "empty").has_value());
  const std::vector<f32> nan{0.0f, NAN};
  CHECK_FALSE(engine->addClip(nan, 1, kRate, "nan").has_value());
  CHECK_FALSE(engine->play(audio::ClipId{99}).has_value());
  const auto clip = addPcm(*engine, audio::synth::tone(440.0f, 0.1f), "tone");
  CHECK_FALSE(engine->play(clip, {.pitch = 0.0f}).has_value());
  CHECK_FALSE(engine->play(clip, {.position = {NAN, 0.0f, 0.0f}}).has_value());
}

// --- 3D: what is heard ---------------------------------------------------------------

TEST_CASE("distance: the formula, and the loudness actually mixed follows it") {
  CHECK(audio::distanceGain(0.5f, 1.0f, 100.0f) == 1.0f);   // inside minDistance: full
  CHECK(audio::distanceGain(1.0f, 1.0f, 100.0f) == 1.0f);
  CHECK(audio::distanceGain(4.0f, 1.0f, 100.0f) == doctest::Approx(0.25f));
  CHECK(audio::distanceGain(500.0f, 1.0f, 100.0f) == doctest::Approx(0.01f));  // capped at maxDistance
  CHECK(audio::distanceGain(9.0f, 1.0f, 100.0f, 0.5f) == doctest::Approx(1.0f / 5.0f));
  CHECK(audio::distanceGain(9.0f, 5.0f, 5.0f) == 1.0f);  // a degenerate range: no attenuation

  auto engine = manualEngine();
  const auto clip = addPcm(*engine, audio::synth::tone(440.0f, 1.0f), "tone");
  engine->setListener({});  // at the origin, looking down -z
  f64 nearest = 0.0;
  for (const f32 distance : {1.0f, 2.0f, 4.0f, 8.0f}) {
    CAPTURE(distance);
    auto voice = engine->play(clip, {.loop = true, .position = {0.0f, 0.0f, -distance}, .minDistance = 1.0f,
                                     .maxDistance = 100.0f});
    REQUIRE(voice.has_value());
    (void)render(*engine, 4800);  // let gain smoothing settle
    const f64 loudness = rms(render(*engine, 9600), 0);
    if (distance == 1.0f) nearest = loudness;
    CHECK(loudness / nearest == doctest::Approx(audio::distanceGain(distance, 1.0f, 100.0f)).epsilon(0.03));
    engine->stop(*voice);
  }
  CHECK(nearest > 0.05);
}

TEST_CASE("panning: a sound on the right is louder in the right ear, and the other way round") {
  auto engine = manualEngine();
  const auto clip = addPcm(*engine, audio::synth::tone(440.0f, 1.0f), "tone");
  engine->setListener({});
  for (const f32 side : {1.0f, -1.0f}) {
    CAPTURE(side);
    auto voice = engine->play(clip, {.loop = true, .position = {5.0f * side, 0.0f, 0.0f}});
    REQUIRE(voice.has_value());
    (void)render(*engine, 4800);
    const auto mixed = render(*engine, 9600);
    const f64 left = rms(mixed, 0);
    const f64 right = rms(mixed, 1);
    if (side > 0.0f) {
      CHECK(right > 2.0 * left);
    } else {
      CHECK(left > 2.0 * right);
    }
    engine->stop(*voice);
  }
  // Turn the listener round (now facing +z): the same source swaps ears.
  engine->setListener({.forward = {0.0f, 0.0f, 1.0f}});
  auto voice = engine->play(clip, {.loop = true, .position = {5.0f, 0.0f, 0.0f}});
  (void)render(*engine, 4800);
  const auto mixed = render(*engine, 9600);
  CHECK(rms(mixed, 0) > 2.0 * rms(mixed, 1));
  engine->stop(*voice);
}

TEST_CASE("Doppler: a sound coming closer is higher, going away lower, by c / (c - v)") {
  constexpr f64 kSpeedOfSound = 343.3;
  constexpr f32 kSpeed = 40.0f;
  auto engine = manualEngine();
  const auto clip = addPcm(*engine, audio::synth::tone(440.0f, 2.0f), "tone");
  engine->setListener({});
  struct Case {
    f32 towardsListener;  // + approaching, - receding
    f64 expectedHertz;
  };
  for (const Case c : {Case{0.0f, 440.0}, Case{kSpeed, 440.0 * kSpeedOfSound / (kSpeedOfSound - kSpeed)},
                       Case{-kSpeed, 440.0 * kSpeedOfSound / (kSpeedOfSound + kSpeed)}}) {
    CAPTURE(c.towardsListener);
    // 20 m in front, moving along z (towards the listener at the origin is +z).
    auto voice = engine->play(clip, {.loop = true, .position = {0.0f, 0.0f, -20.0f},
                                     .velocity = {0.0f, 0.0f, c.towardsListener}, .maxDistance = 1000.0f});
    REQUIRE(voice.has_value());
    (void)render(*engine, 4800);
    const f64 heard = frequency(render(*engine, kRate / 2), 0);
    CHECK(heard == doctest::Approx(c.expectedHertz).epsilon(0.01));
    engine->stop(*voice);
  }
}

// --- Sound effects from maths -------------------------------------------------------------

TEST_CASE("the generated effects: the right length, in range, never silent, the same every time") {
  const audio::Pcm laser = audio::synth::laser();
  const audio::Pcm boom = audio::synth::explosion();
  const audio::Pcm knock = audio::synth::impact();
  const audio::Pcm hum = audio::synth::hum();
  CHECK(laser.seconds() == doctest::Approx(0.25f));
  CHECK(boom.seconds() == doctest::Approx(1.2f));
  CHECK(knock.seconds() == doctest::Approx(0.15f));
  CHECK(hum.seconds() == doctest::Approx(1.0f));
  for (const audio::Pcm* pcm : {&laser, &boom, &knock, &hum}) {
    f32 peak = 0.0f;
    for (const f32 s : pcm->samples) {
      REQUIRE(std::isfinite(s));
      peak = std::max(peak, std::abs(s));
    }
    CHECK(peak <= 1.0f);
    CHECK(peak > 0.25f);
  }
  CHECK(audio::synth::explosion().samples == boom.samples);  // deterministic: fixed-seed noise
  CHECK(audio::synth::explosion(48000, 99).samples != boom.samples);
  // The laser sweeps down: more zero crossings at the start than at the end.
  auto crossings = [&](usize from, usize to) {
    u32 n = 0;
    for (usize i = from + 1; i < to; ++i) n += (laser.samples[i - 1] < 0.0f && laser.samples[i] >= 0.0f) ? 1u : 0u;
    return n;
  };
  const usize tenth = laser.samples.size() / 10;
  CHECK(crossings(0, tenth) > 3 * crossings(laser.samples.size() - tenth, laser.samples.size()));
  // The hum loops seamlessly: its last sample leads into its first.
  CHECK(std::abs(hum.samples.back() - hum.samples.front()) < 0.01f);
}

// --- The ECS side ---------------------------------------------------------------------

TEST_CASE("audioSystem: play on spawn, follow the entity, stop with it; the listener follows the camera") {
  auto engine = manualEngine();
  const auto hum = addPcm(*engine, audio::synth::hum(), "hum");
  const auto beep = addPcm(*engine, audio::synth::tone(500.0f, 0.05f), "beep");
  ecs::World world;
  const ecs::Entity ears = world.create();
  world.add<engine::Transform3D>(ears, engine::Transform3D{});
  world.add<engine::AudioListener>(ears);
  const ecs::Entity lamp = world.create();
  world.add<engine::Transform3D>(lamp, engine::Transform3D{.position = {3.0f, 0.0f, 0.0f}});
  world.add<engine::AudioSource>(lamp, engine::AudioSource{.clip = hum, .loop = true});
  const ecs::Entity button = world.create();
  world.add<engine::Transform3D>(button, engine::Transform3D{});
  world.add<engine::AudioSource>(button, engine::AudioSource{.clip = beep, .spatial = false, .playOnSpawn = false});

  engine::audioSystem(world, *engine, 1.0f / 60.0f);
  const auto& lampSound = world.get<engine::AudioSource>(lamp);
  REQUIRE(lampSound.voice.valid());
  CHECK(engine->playing(lampSound.voice));
  CHECK_FALSE(world.get<engine::AudioSource>(button).voice.valid());  // waits to be asked
  (void)render(*engine, 4800);
  const auto onTheRight = render(*engine, 4800);
  CHECK(rms(onTheRight, 1) > 2.0 * rms(onTheRight, 0));

  // The listener turns round: the lamp is now on its left.
  world.get<engine::Transform3D>(ears).rotation = glm::angleAxis(3.14159265f, glm::vec3(0.0f, 1.0f, 0.0f));
  engine::audioSystem(world, *engine, 1.0f / 60.0f);
  (void)render(*engine, 4800);
  const auto onTheLeft = render(*engine, 4800);
  CHECK(rms(onTheLeft, 0) > 2.0 * rms(onTheLeft, 1));

  // Asked to play: it plays, once, and is let go when done.
  world.get<engine::AudioSource>(button).play = true;
  engine::audioSystem(world, *engine, 1.0f / 60.0f);
  CHECK(world.get<engine::AudioSource>(button).voice.valid());
  CHECK_FALSE(world.get<engine::AudioSource>(button).play);
  (void)render(*engine, 4800);
  engine::audioSystem(world, *engine, 1.0f / 60.0f);
  CHECK_FALSE(world.get<engine::AudioSource>(button).voice.valid());

  // The lamp goes: so does its hum (a loop would otherwise play forever).
  const audio::VoiceId hummed = lampSound.voice;
  world.destroy(lamp);
  world.flush();
  engine::audioSystem(world, *engine, 1.0f / 60.0f);
  CHECK_FALSE(engine->playing(hummed));
  CHECK(engine->stats().voicesPlaying == 0);
}

TEST_CASE("owner tags round-trip entities, and 0 is no owner") {
  const ecs::Entity e{.index = 12, .generation = 3};
  CHECK(engine::ownerTag(e) != 0);
  CHECK(engine::ownerEntity(engine::ownerTag(e)) == e);
  CHECK(engine::ownerEntity(engine::ownerTag({.index = 0, .generation = 0})) == ecs::Entity{.index = 0, .generation = 0});
}
