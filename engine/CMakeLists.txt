# ============================================================
#  Forge Engine
#
#  Two renderer backends. The software rasteriser has no
#  dependencies at all and is always built, which is what lets the
#  engine run and be tested on a machine with no GPU and no display.
#  The OpenGL backend is built when GLFW and OpenGL are found.
# ============================================================
cmake_minimum_required(VERSION 3.16)
project(Forge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(FORGE_BUILD_TESTS "Build the test suite" ON)
option(FORGE_BUILD_EDITOR "Build the editor" ON)
option(FORGE_BUILD_SAMPLES "Build the sample game" ON)
option(FORGE_ENABLE_OPENGL "Build the OpenGL backend when GLFW is available" ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------
#  The engine library
# ---------------------------------------------------------------
file(GLOB_RECURSE FORGE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE FORGE_HEADERS CONFIGURE_DEPENDS "include/*.hpp")

# The GL backend is added conditionally below, so keep it out of the glob.
list(FILTER FORGE_SOURCES EXCLUDE REGEX "src/render/OpenGLBackend\\.cpp$")

add_library(forge STATIC ${FORGE_SOURCES} ${FORGE_HEADERS})
target_include_directories(forge PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

find_package(Threads QUIET)
if(Threads_FOUND)
    target_link_libraries(forge PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------
#  OpenGL backend (optional)
# ---------------------------------------------------------------
set(FORGE_HAS_OPENGL OFF)
if(FORGE_ENABLE_OPENGL)
    find_package(OpenGL QUIET)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GLFW QUIET glfw3)
    endif()
    if(OPENGL_FOUND AND GLFW_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/render/OpenGLBackend.cpp")
        set(FORGE_HAS_OPENGL ON)
        target_sources(forge PRIVATE src/render/OpenGLBackend.cpp)
        target_compile_definitions(forge PUBLIC FORGE_HAS_OPENGL=1)
        target_include_directories(forge PUBLIC ${GLFW_INCLUDE_DIRS})
        target_link_libraries(forge PUBLIC ${GLFW_LIBRARIES} OpenGL::GL)
        target_link_directories(forge PUBLIC ${GLFW_LIBRARY_DIRS})
        message(STATUS "Forge: OpenGL backend enabled")
    else()
        message(STATUS "Forge: OpenGL backend disabled (need OpenGL and glfw3); "
                       "the software renderer still builds")
    endif()
endif()

# ---------------------------------------------------------------
#  Applications
# ---------------------------------------------------------------
if(FORGE_BUILD_EDITOR)
    add_executable(forge-editor apps/forge-editor/main.cpp)
    target_link_libraries(forge-editor PRIVATE forge)

    add_executable(forge-player apps/forge-player/main.cpp)
    target_link_libraries(forge-player PRIVATE forge)
endif()

if(FORGE_BUILD_SAMPLES)
    add_executable(forge-sample samples/skyforge/main.cpp)
    target_link_libraries(forge-sample PRIVATE forge)
endif()

# ---------------------------------------------------------------
#  Tests
# ---------------------------------------------------------------
if(FORGE_BUILD_TESTS)
    enable_testing()
    foreach(suite Math Core Scene Render)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/Test${suite}.cpp")
            add_executable(test-${suite} tests/Test${suite}.cpp)
            target_link_libraries(test-${suite} PRIVATE forge)
            target_include_directories(test-${suite} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
            add_test(NAME ${suite} COMMAND test-${suite})
        endif()
    endforeach()
endif()
