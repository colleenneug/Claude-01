// The code of the two header-only libraries the glTF loader uses, compiled
// once, here, without our warning flags (they are third-party; see
// engine/CMakeLists.txt). TINYGLTF_NO_STB_IMAGE and TINYGLTF_NO_STB_IMAGE_WRITE
// come from the playground_gltf target, so every file agrees on them:
// tinygltf does not call stb itself; Gltf.cpp gives it an image decoder.
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
