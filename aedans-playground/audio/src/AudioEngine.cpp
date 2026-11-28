// =============================================================================
//  AudioEngine.cpp — audio::AudioEngine over miniaudio's high-level engine
//  (ma_engine: mixing, resampling, spatialisation). The only file besides
//  MiniaudioImplementation.cpp that includes miniaudio.
//
//  Each VOICE is an ma_audio_buffer reading its clip's samples in place (no
//  copy: a clip outlives its voices, removeClip stops them first) wrapped in
//  an ma_sound, which does the volume, pitch, looping and 3D. Voices live
//  behind unique_ptr: miniaudio keeps pointers to them while they play.
// =============================================================================

#include "playground/audio/AudioEngine.h"

#include "playground/core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>

#include <miniaudio.h>

namespace playground::audio {
namespace {

using log::Category;

bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

struct Clip {
  std::string name;
  std::vector<f32> samples;
  u32 channels = 1;
  u32 sampleRate = 48000;
  u64 frames = 0;
};

struct Voice {
  ma_audio_buffer buffer{};
  ma_sound sound{};
  u32 clip = 0;
  u64 owner = 0;
  bool loop = false;
};

}  // namespace

std::string_view describe(Output output) noexcept {
  switch (output) {
    case Output::Device: return "sound device";
    case Output::NullDevice: return "null device (no sound card: mixed in real time, not heard)";
    case Output::Manual: return "manual (mixed on request)";
  }
  return "?";
}

f32 distanceGain(f32 distance, f32 minDistance, f32 maxDistance, f32 rolloff) noexcept {
  if (minDistance >= maxDistance) return 1.0f;  // as miniaudio: no attenuation rather than a division by zero
  return minDistance / (minDistance + rolloff * (std::clamp(distance, minDistance, maxDistance) - minDistance));
}

struct AudioEngine::Impl {
  Output output = Output::Device;
  ma_context context{};
  bool contextReady = false;
  ma_engine engine{};
  bool engineReady = false;
  std::map<u32, Clip> clips;
  u32 nextClip = 1;
  std::map<u32, std::unique_ptr<Voice>> voices;
  u32 nextVoice = 1;
  u64 voicesStarted = 0;

  Impl() = default;
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  ~Impl() {
    for (auto& [id, voice] : voices) release(*voice);
    voices.clear();
    if (engineReady) ma_engine_uninit(&engine);
    if (contextReady) ma_context_uninit(&context);
  }

  static void release(Voice& voice) {
    ma_sound_uninit(&voice.sound);
    ma_audio_buffer_uninit(&voice.buffer);
  }

  [[nodiscard]] Voice* find(VoiceId id) {
    const auto it = voices.find(id.value);
    return it == voices.end() ? nullptr : it->second.get();
  }

  // One attempt at an engine on `which`; false (and nothing left behind) if it fails.
  bool start(Output which, const AudioEngineDesc& desc, std::string& why) {
    ma_engine_config config = ma_engine_config_init();
    config.channels = desc.channels;
    config.sampleRate = desc.sampleRate;
    if (which == Output::Manual) {
      config.noDevice = MA_TRUE;  // no device, no thread: render() pulls the frames
    } else if (which == Output::NullDevice) {
      const ma_backend backends[] = {ma_backend_null};
      if (const ma_result r = ma_context_init(backends, 1, nullptr, &context); r != MA_SUCCESS) {
        why = ma_result_description(r);
        return false;
      }
      contextReady = true;
      config.pContext = &context;
    }
    if (const ma_result r = ma_engine_init(&config, &engine); r != MA_SUCCESS) {
      why = ma_result_description(r);
      if (contextReady) ma_context_uninit(&context);
      contextReady = false;
      return false;
    }
    engineReady = true;
    output = which;
    // miniaudio's own list of backends ends with the null one: asked for "a
    // sound device" on a machine with none, it quietly picks that. Say so.
    if (which == Output::Device) {
      const ma_device* device = ma_engine_get_device(&engine);
      if (device != nullptr && device->pContext != nullptr && device->pContext->backend == ma_backend_null) {
        output = Output::NullDevice;
        log::warn(Category::Engine, "audio: no sound device found; using the null device (nothing will be heard)");
      }
    }
    return true;
  }
};

Result<std::unique_ptr<AudioEngine>> AudioEngine::create(const AudioEngineDesc& desc) {
  if (desc.channels < 1 || desc.channels > 2 || desc.sampleRate < 8000 || desc.sampleRate > 192000) {
    return fail(Errc::InvalidArgument, "audio: 1 or 2 channels at 8-192 kHz, not {} at {} Hz", desc.channels,
                desc.sampleRate);
  }
  auto impl = std::make_unique<Impl>();  // ma_engine must not move once started: it lives here
  Output wanted = desc.output;
  if (const char* forced = std::getenv("PLAYGROUND_AUDIO"); forced != nullptr && std::string_view(forced) == "null" &&
                                                            wanted == Output::Device) {
    wanted = Output::NullDevice;
    log::info(Category::Engine, "audio: PLAYGROUND_AUDIO=null: using the null device");
  }
  std::string why;
  if (!impl->start(wanted, desc, why)) {
    if (wanted != Output::Device) {
      return fail(Errc::PlatformFailure, "audio: could not start ({}): {}", describe(wanted), why);
    }
    // No sound card (a server, a container, a test machine): keep going, silently.
    log::warn(Category::Engine, "audio: no sound device ({}); using the null device instead", why);
    if (std::string nullWhy; !impl->start(Output::NullDevice, desc, nullWhy)) {
      return fail(Errc::PlatformFailure, "audio: neither a sound device ({}) nor the null device ({}) started", why,
                  nullWhy);
    }
  }
  std::string device = "none";
  if (ma_device* d = ma_engine_get_device(&impl->engine)) device = d->playback.name;
  log::info(Category::Engine, "audio: {} \"{}\", {} Hz, {} channel(s) (miniaudio {})", describe(impl->output),
            device, ma_engine_get_sample_rate(&impl->engine), ma_engine_get_channels(&impl->engine),
            MA_VERSION_STRING);
  return std::unique_ptr<AudioEngine>(new AudioEngine(std::move(impl)));
}

AudioEngine::AudioEngine(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
AudioEngine::~AudioEngine() = default;

Output AudioEngine::output() const noexcept { return impl_->output; }
u32 AudioEngine::sampleRate() const noexcept { return ma_engine_get_sample_rate(&impl_->engine); }
u32 AudioEngine::channels() const noexcept { return ma_engine_get_channels(&impl_->engine); }

Result<ClipId> AudioEngine::addClip(std::span<const f32> samples, u32 channels, u32 sampleRate,
                                    std::string_view name) {
  if (channels < 1 || channels > 2) return fail(Errc::InvalidArgument, "audio: clip \"{}\": 1 or 2 channels", name);
  if (sampleRate < 1000 || sampleRate > 384000) {
    return fail(Errc::InvalidArgument, "audio: clip \"{}\": sample rate {} Hz", name, sampleRate);
  }
  if (samples.empty() || samples.size() % channels != 0) {
    return fail(Errc::InvalidArgument, "audio: clip \"{}\": {} samples is not whole frames of {} channel(s)", name,
                samples.size(), channels);
  }
  if (std::any_of(samples.begin(), samples.end(), [](f32 s) { return !std::isfinite(s); })) {
    return fail(Errc::InvalidArgument, "audio: clip \"{}\" has samples that are not finite numbers", name);
  }
  const u32 id = impl_->nextClip++;
  impl_->clips.emplace(id, Clip{.name = std::string(name), .samples = {samples.begin(), samples.end()},
                                .channels = channels, .sampleRate = sampleRate, .frames = samples.size() / channels});
  return ClipId{id};
}

void AudioEngine::removeClip(ClipId clip) {
  for (auto it = impl_->voices.begin(); it != impl_->voices.end();) {
    if (it->second->clip == clip.value) {
      Impl::release(*it->second);
      it = impl_->voices.erase(it);
    } else {
      ++it;
    }
  }
  impl_->clips.erase(clip.value);
}

f32 AudioEngine::clipSeconds(ClipId clip) const {
  const auto it = impl_->clips.find(clip.value);
  if (it == impl_->clips.end()) return 0.0f;
  return static_cast<f32>(it->second.frames) / static_cast<f32>(it->second.sampleRate);
}

Result<VoiceId> AudioEngine::play(ClipId clipId, const PlayDesc& desc) {
  const auto clip = impl_->clips.find(clipId.value);
  if (clip == impl_->clips.end()) return fail(Errc::InvalidArgument, "audio: no clip {}", clipId.value);
  if (!finite(desc.position) || !finite(desc.velocity) || !std::isfinite(desc.volume) || !(desc.pitch > 0.0f)) {
    return fail(Errc::InvalidArgument, "audio: \"{}\": volume, pitch, position and velocity must be finite (pitch > 0)",
                clip->second.name);
  }
  auto voice = std::make_unique<Voice>();
  ma_audio_buffer_config buffer = ma_audio_buffer_config_init(ma_format_f32, clip->second.channels,
                                                              clip->second.frames, clip->second.samples.data(), nullptr);
  buffer.sampleRate = clip->second.sampleRate;
  if (const ma_result r = ma_audio_buffer_init(&buffer, &voice->buffer); r != MA_SUCCESS) {
    return fail(Errc::IoFailure, "audio: \"{}\": {}", clip->second.name, ma_result_description(r));
  }
  const ma_uint32 flags = desc.spatial ? 0u : static_cast<ma_uint32>(MA_SOUND_FLAG_NO_SPATIALIZATION);
  if (const ma_result r = ma_sound_init_from_data_source(&impl_->engine, &voice->buffer, flags, nullptr, &voice->sound);
      r != MA_SUCCESS) {
    ma_audio_buffer_uninit(&voice->buffer);
    return fail(Errc::IoFailure, "audio: \"{}\": {}", clip->second.name, ma_result_description(r));
  }
  ma_sound& sound = voice->sound;
  ma_sound_set_volume(&sound, desc.volume);
  ma_sound_set_pitch(&sound, desc.pitch);
  ma_sound_set_looping(&sound, desc.loop ? MA_TRUE : MA_FALSE);
  if (desc.spatial) {
    ma_sound_set_attenuation_model(&sound, ma_attenuation_model_inverse);
    ma_sound_set_min_distance(&sound, desc.minDistance);
    ma_sound_set_max_distance(&sound, desc.maxDistance);
    ma_sound_set_rolloff(&sound, desc.rolloff);
    ma_sound_set_position(&sound, desc.position.x, desc.position.y, desc.position.z);
    ma_sound_set_velocity(&sound, desc.velocity.x, desc.velocity.y, desc.velocity.z);
  }
  voice->clip = clipId.value;
  voice->owner = desc.owner;
  voice->loop = desc.loop;
  if (const ma_result r = ma_sound_start(&sound); r != MA_SUCCESS) {
    Impl::release(*voice);
    return fail(Errc::IoFailure, "audio: \"{}\" would not start: {}", clip->second.name, ma_result_description(r));
  }
  const u32 id = impl_->nextVoice++;
  impl_->voices.emplace(id, std::move(voice));
  ++impl_->voicesStarted;
  return VoiceId{id};
}

void AudioEngine::stop(VoiceId id) {
  const auto it = impl_->voices.find(id.value);
  if (it == impl_->voices.end()) return;
  Impl::release(*it->second);
  impl_->voices.erase(it);
}

bool AudioEngine::playing(VoiceId id) const {
  const auto it = impl_->voices.find(id.value);
  return it != impl_->voices.end() && ma_sound_is_playing(&it->second->sound) == MA_TRUE;
}

void AudioEngine::setVoice(VoiceId id, glm::vec3 position, glm::vec3 velocity) {
  Voice* voice = impl_->find(id);
  if (voice == nullptr || !finite(position) || !finite(velocity)) return;
  ma_sound_set_position(&voice->sound, position.x, position.y, position.z);
  ma_sound_set_velocity(&voice->sound, velocity.x, velocity.y, velocity.z);
}

void AudioEngine::setVoiceVolume(VoiceId id, f32 volume) {
  if (Voice* voice = impl_->find(id); voice != nullptr && std::isfinite(volume)) {
    ma_sound_set_volume(&voice->sound, volume);
  }
}

void AudioEngine::stopIf(const std::function<bool(u64 owner)>& stop) {
  for (auto it = impl_->voices.begin(); it != impl_->voices.end();) {
    if (stop(it->second->owner)) {
      Impl::release(*it->second);
      it = impl_->voices.erase(it);
    } else {
      ++it;
    }
  }
}

void AudioEngine::setListener(const Listener& l) {
  if (!finite(l.position) || !finite(l.forward) || !finite(l.up) || !finite(l.velocity)) return;
  ma_engine_listener_set_position(&impl_->engine, 0, l.position.x, l.position.y, l.position.z);
  ma_engine_listener_set_direction(&impl_->engine, 0, l.forward.x, l.forward.y, l.forward.z);
  ma_engine_listener_set_world_up(&impl_->engine, 0, l.up.x, l.up.y, l.up.z);
  ma_engine_listener_set_velocity(&impl_->engine, 0, l.velocity.x, l.velocity.y, l.velocity.z);
}

void AudioEngine::setMasterVolume(f32 volume) {
  if (std::isfinite(volume)) ma_engine_set_volume(&impl_->engine, std::max(volume, 0.0f));
}

void AudioEngine::update() {
  // A one-shot that reached its end stops itself; free it. (Loops play on.)
  for (auto it = impl_->voices.begin(); it != impl_->voices.end();) {
    Voice& voice = *it->second;
    if (!voice.loop && ma_sound_is_playing(&voice.sound) == MA_FALSE) {
      Impl::release(voice);
      it = impl_->voices.erase(it);
    } else {
      ++it;
    }
  }
}

AudioStats AudioEngine::stats() const {
  return {.voicesStarted = impl_->voicesStarted, .voicesPlaying = impl_->voices.size(), .clips = impl_->clips.size()};
}

Result<std::vector<f32>> AudioEngine::render(u32 frames) {
  if (impl_->output != Output::Manual) {
    return fail(Errc::InvalidState, "audio: render() is for Output::Manual (this engine has a {})",
                describe(impl_->output));
  }
  std::vector<f32> out(static_cast<usize>(frames) * channels());
  ma_uint64 read = 0;
  if (const ma_result r = ma_engine_read_pcm_frames(&impl_->engine, out.data(), frames, &read); r != MA_SUCCESS) {
    return fail(Errc::IoFailure, "audio: mixing failed: {}", ma_result_description(r));
  }
  return out;
}

}  // namespace playground::audio
