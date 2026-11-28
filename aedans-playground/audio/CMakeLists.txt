# audio: sound in 3D (audio::AudioEngine) by miniaudio, and sound effects
# made from maths (audio::synth). miniaudio is PRIVATE: only audio/src
# includes it. Depends on core and glm only.
add_library(playground_audio STATIC
  src/AudioEngine.cpp
  src/MiniaudioImplementation.cpp
  src/Synth.cpp)
add_library(playground::audio ALIAS playground_audio)
add_library(engine_audio ALIAS playground_audio)  # the name in the audio plan

target_include_directories(playground_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(playground_audio
  PUBLIC  playground::core glm::glm
  PRIVATE playground_miniaudio)

playground_configure_target(playground_audio)
# Third-party code: built without our warning flags.
if(MSVC)
  set_source_files_properties(src/MiniaudioImplementation.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
  set_source_files_properties(src/MiniaudioImplementation.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
