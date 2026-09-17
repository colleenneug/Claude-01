#!/usr/bin/env bash
# ============================================================
#  Erebus Cradle — install and run the native C++/OpenGL build.
#
#  One command from a fresh checkout: check the toolchain, install
#  what's missing where we know how, build cpp/ and run it.
#
#    bash tools/install-native.sh              # build, then play
#    bash tools/install-native.sh --no-run     # build only
#    bash tools/install-native.sh --serve      # build, then serve the
#                                              # browser build so its
#                                              # LAUNCH GAME plate works
# ============================================================
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

run=1; serve=0
for arg in "$@"; do
  case "$arg" in
    --no-run) run=0 ;;
    --serve)  serve=1; run=0 ;;
    -h|--help) sed -n '2,13p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

say() { printf '\n\033[36m>> %s\033[0m\n' "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }

# ---------- dependencies ----------
# Four libraries and a compiler. This also runs unattended, from the
# launcher's INSTALL button by way of server/server.js, where there is no
# terminal to type a password into: if installing packages would need a
# sudo prompt we can't answer, print the one command a human has to run
# and exit 3 rather than hang on a prompt nobody will ever see.
missing=0
for tool in cmake g++ pkg-config; do
  have "$tool" || { echo "missing: $tool"; missing=1; }
done

if [ "$missing" = 1 ] || ! pkg-config --exists glfw3 glew 2>/dev/null; then
  if have apt-get; then
    dep_cmd="apt-get update && apt-get install -y cmake g++ pkg-config libglfw3-dev libglew-dev libglm-dev libgl1-mesa-dev"
  elif have dnf; then
    dep_cmd="dnf install -y cmake gcc-c++ pkgconf-pkg-config glfw-devel glew-devel glm-devel mesa-libGL-devel"
  elif have pacman; then
    dep_cmd="pacman -S --needed --noconfirm cmake gcc pkgconf glfw glew glm"
  elif have brew; then
    dep_cmd="brew install cmake glfw glew glm"
  else
    cat >&2 <<'MSG'
No package manager I know how to drive (apt / dnf / pacman / brew).
Install these four yourself, then run this script again:

  cmake, a C++17 compiler, glfw3, glew, glm

On Windows, use vcpkg: vcpkg install glfw3 glew glm, then configure
CMake with the vcpkg toolchain file.
MSG
    exit 1
  fi

  # root needs no sudo; a passwordless sudo is fine unattended; anything
  # else needs a person at a keyboard.
  if [ "$(id -u)" = 0 ]; then
    sudo_cmd=""
  elif have brew; then
    sudo_cmd=""                      # Homebrew installs as the user
  elif sudo -n true 2>/dev/null; then
    sudo_cmd="sudo "
  elif [ -t 0 ]; then
    sudo_cmd="sudo "                 # a terminal: it can ask for a password
  else
    echo "DEPS_NEED_PASSWORD" >&2
    echo "Build dependencies are missing and installing them needs a password." >&2
    echo "Run this once in a terminal, then start the install again:" >&2
    echo >&2
    echo "  sudo $dep_cmd" >&2
    exit 3
  fi

  say "installing build dependencies"
  # shellcheck disable=SC2086
  eval "${sudo_cmd}${dep_cmd}"
fi

# ---------- build ----------
say "configuring"
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release

say "building"
cmake --build cpp/build -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

echo
echo "built: cpp/build/erebus_native"

# ---------- run ----------
if [ "$serve" = 1 ]; then
  if ! have node; then echo "node is not installed; run cpp/build/erebus_native directly" >&2; exit 1; fi
  say "serving on http://localhost:8080 — open it and click LAUNCH GAME"
  exec node server/server.js
fi

if [ "$run" = 1 ]; then
  say "launching"
  cd cpp/build
  exec ./erebus_native
fi
