#pragma once
// =============================================================================
//  playground/audio/AudioEngine.h — sound, in 3D, by miniaudio.
//
//  miniaudio is hidden: no miniaudio header appears here (the engine is
//  behind a pointer), and only audio/src is compiled against it.
//
//  OUTPUT, three ways (AudioEngineDesc::output):
//    Device      the sound card. If there is none (a server, a container,
//                a CI machine, an xvfb-run test) or it will not start, the
//                engine falls back to NullDevice and says so in the log:
//                a missing sound card never stops the game.
//    NullDevice  miniaudio's null backend (MA_BACKEND_NULL): a device that
//                plays in real time and throws the sound away. Sounds start,
//                move and finish exactly as with a sound card. The
//                environment variable PLAYGROUND_AUDIO=null forces it.
//    Manual      no device and no audio thread: nothing plays until render()
//                mixes the next frames and hands them back. For tests that
//                listen to what was mixed.
//
//  CLIPS are sounds in memory (32-bit float PCM, mono or stereo; Synth.h
//  makes some). VOICES are clips playing: volume, pitch, looping, and when
//  spatial a position and velocity relative to the LISTENER (setListener),
//  from which miniaudio works out:
//    distance attenuation  the inverse-distance model: full volume within
//                          minDistance, then minDistance / (minDistance +
//                          rolloff * (distance - minDistance)), no quieter
//                          past maxDistance (distanceGain() below);
//    panning               left/right from the direction to the source;
//    Doppler               pitch up while source and listener approach,
//                          down while they part (speed of sound 343.3 m/s).
//  Units: metres, seconds; +y up; the listener looks along `forward`.
//
//  ONE THREAD calls an AudioEngine (miniaudio's own audio thread mixes).
//
//      auto audio = audio::AudioEngine::create({});
//      auto pew = audio->addClip(laser.samples, 1, laser.sampleRate, "laser");
//      audio->setListener({.position = eye, .forward = look});
//      audio->play(*pew, {.position = muzzle});
//      audio->update();  // once a frame: frees finished voices
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>

namespace playground::audio {

enum class Output : u8 { Device, NullDevice, Manual };
[[nodiscard]] std::string_view describe(Output output) noexcept;

struct AudioEngineDesc {
  Output output = Output::Device;
  u32 sampleRate = 48000;
  u32 channels = 2;
};

struct ClipId {
  u32 value = 0;  // 0: none
  [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
  friend constexpr bool operator==(ClipId, ClipId) = default;
};

struct VoiceId {
  u32 value = 0;  // 0: none
  [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
  friend constexpr bool operator==(VoiceId, VoiceId) = default;
};

struct PlayDesc {
  f32 volume = 1.0f;  // linear gain
  f32 pitch = 1.0f;   // 2: an octave up, twice as fast
  bool loop = false;
  bool spatial = true;  // false: the same in both ears at any distance (music, UI, your own gun)
  glm::vec3 position{0.0f};
  glm::vec3 velocity{0.0f};
  f32 minDistance = 1.0f;   // m: full volume this close
  f32 maxDistance = 60.0f;  // m: no quieter beyond this
  f32 rolloff = 1.0f;       // how quickly it fades past minDistance
  u64 owner = 0;            // the caller's tag (an entity): see stopIf()
};

struct Listener {
  glm::vec3 position{0.0f};
  glm::vec3 forward{0.0f, 0.0f, -1.0f};
  glm::vec3 up{0.0f, 1.0f, 0.0f};
  glm::vec3 velocity{0.0f};
};

struct AudioStats {
  u64 voicesStarted = 0;
  usize voicesPlaying = 0;
  usize clips = 0;
};

// The inverse-distance model the engine uses (see the top of this file).
[[nodiscard]] f32 distanceGain(f32 distance, f32 minDistance, f32 maxDistance, f32 rolloff = 1.0f) noexcept;

class AudioEngine {
 public:
  // Fails only if not even the null device (or Manual) can start.
  [[nodiscard]] static Result<std::unique_ptr<AudioEngine>> create(const AudioEngineDesc& desc = {});
  ~AudioEngine();
  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  [[nodiscard]] Output output() const noexcept;  // what it really runs on (after any fallback)
  [[nodiscard]] u32 sampleRate() const noexcept;
  [[nodiscard]] u32 channels() const noexcept;

  // `samples`: interleaved, `channels` (1 or 2) per frame. Copied.
  [[nodiscard]] Result<ClipId> addClip(std::span<const f32> samples, u32 channels, u32 sampleRate,
                                       std::string_view name);
  void removeClip(ClipId clip);  // its voices stop first
  [[nodiscard]] f32 clipSeconds(ClipId clip) const;

  [[nodiscard]] Result<VoiceId> play(ClipId clip, const PlayDesc& desc = {});
  void stop(VoiceId voice);
  [[nodiscard]] bool playing(VoiceId voice) const;  // started and not yet finished or stopped
  void setVoice(VoiceId voice, glm::vec3 position, glm::vec3 velocity = glm::vec3(0.0f));
  void setVoiceVolume(VoiceId voice, f32 volume);
  // Stops every voice whose owner tag `stop(owner)` says to stop.
  void stopIf(const std::function<bool(u64 owner)>& stop);

  void setListener(const Listener& listener);
  void setMasterVolume(f32 volume);

  // Once a frame: frees voices that have finished.
  void update();
  [[nodiscard]] AudioStats stats() const;

  // Output::Manual only: mixes the next `frames` frames and returns them
  // (interleaved, channels() per frame). Errc::InvalidState otherwise.
  [[nodiscard]] Result<std::vector<f32>> render(u32 frames);

 private:
  struct Impl;
  explicit AudioEngine(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

}  // namespace playground::audio
