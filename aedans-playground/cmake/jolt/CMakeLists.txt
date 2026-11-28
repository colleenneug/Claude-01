# =============================================================================
#  Jolt Physics (third_party/JoltPhysics, v5.3.0, MIT licence), built as the
#  static library `Jolt` for physics/ only.
#
#  A subdirectory of its own so Jolt.cmake's settings (it sets
#  CMAKE_CXX_STANDARD 17) stay out of the rest of the project. Jolt's own
#  Build/CMakeLists.txt is not used: it rewrites global compiler flags and
#  builds samples and tests we do not want. Our warnings never apply to it,
#  and consumers see its headers as SYSTEM headers (no warnings from them).
#  The sanitizer flags, set globally in cmake/ProjectOptions.cmake, do apply:
#  every translation unit must agree on those.
# =============================================================================
set(PHYSICS_REPO_ROOT ${PROJECT_SOURCE_DIR}/third_party/JoltPhysics)

# The variables Jolt.cmake reads (normally options of Jolt's Build/CMakeLists.txt).
set(CROSS_PLATFORM_DETERMINISTIC ON)  # same results on every CPU and compiler, at a small cost
set(DOUBLE_PRECISION OFF)
set(OBJECT_LAYER_BITS 16)
# The x86-64 baseline every desktop CPU of the last decade has; no AVX, so
# the build runs everywhere (and AVX could not change results anyway, with
# determinism on).
set(USE_SSE4_1 ON)
set(USE_SSE4_2 ON)
set(USE_AVX OFF)
set(USE_AVX2 OFF)
set(USE_AVX512 OFF)
set(USE_LZCNT OFF)
set(USE_TZCNT OFF)
set(USE_F16C OFF)
set(USE_FMADD OFF)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
set(ENABLE_OBJECT_STREAM OFF)
set(CPP_EXCEPTIONS_ENABLED ON)  # our code uses exceptions; one setting for the whole program
set(CPP_RTTI_ENABLED ON)
set(INTERPROCEDURAL_OPTIMIZATION OFF)

include(${PHYSICS_REPO_ROOT}/Jolt/Jolt.cmake)

# Jolt's asserts in debug builds: a broken invariant inside Jolt stops the
# program there (physics/src reports it to our log first).
target_compile_definitions(Jolt PUBLIC "$<$<CONFIG:Debug>:JPH_ENABLE_ASSERTS>")

# Determinism needs the compiler to keep a*b+c as two roundings everywhere,
# Jolt's code and ours that inlines its maths alike (Jolt's Build/ sets this
# globally; here it travels with the target).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(Jolt PUBLIC -ffp-contract=off)
  target_compile_options(Jolt PRIVATE -Wno-stringop-overflow -Wno-psabi)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(Jolt PUBLIC -ffp-model=precise -ffp-contract=off)
endif()

# Consumers include Jolt's headers as system headers: our -Wconversion,
# -Wold-style-cast and friends are for our code, not Jolt's.
get_target_property(joltIncludes Jolt INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(Jolt PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${joltIncludes}")

# UndefinedBehaviorSanitizer's "vptr" check (it needs RTTI, which we keep on
# so physics/src can derive from Jolt classes) trips on a deliberate trick in
# Jolt's own PhysicsSystem.cpp: SimShapeFilterWrapperUnion places a plain
# ShapeFilter where a SimShapeFilterWrapper is declared ("Dirty trick" in
# its comment). Jolt's default build has no RTTI, so the check never runs
# there. Off for Jolt's sources only: every other ASan/UBSan check stays on
# for Jolt, and all of them for our code.
if(PLAYGROUND_SANITIZERS AND NOT MSVC)
  target_compile_options(Jolt PRIVATE -fno-sanitize=vptr)
endif()
