#pragma once
#include "Gl.h"
#include <string>
#include <unordered_map>

// Compiles and links a GLSL program from files on disk, and caches uniform
// locations by name so setting one per frame is a hash lookup, not a driver
// call to re-resolve it.
class Shader {
public:
  Shader() = default;
  // fragPath may be shared by several shaders (e.g. every post pass uses
  // the same fullscreen-triangle vertex stage).
  bool load(const std::string& vertPath, const std::string& fragPath);
  void use() const;
  GLuint id() const { return program_; }

  void set(const std::string& name, bool v) const;
  void set(const std::string& name, int v) const;
  void set(const std::string& name, float v) const;
  void set(const std::string& name, const glm::vec2& v) const;
  void set(const std::string& name, const glm::vec3& v) const;
  void set(const std::string& name, const glm::vec4& v) const;
  void set(const std::string& name, const glm::mat3& v) const;
  void set(const std::string& name, const glm::mat4& v) const;

private:
  GLint loc(const std::string& name) const;
  GLuint program_ = 0;
  mutable std::unordered_map<std::string, GLint> uniformCache_;
};
