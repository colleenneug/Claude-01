#include "Shader.h"
#include <fstream>
#include <sstream>
#include <vector>

namespace {

std::string readFile(const std::string& path) {
  std::ifstream f(path, std::ios::in | std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "[Shader] cannot open %s\n", path.c_str());
    return "";
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// GLSL has no #include, and the sky is needed by three programs (the sky
// pass itself, the capture that turns it into the light probe, and the
// forward shader's fog). Three hand-kept copies of one function is three
// chances for the reflection of the sky to disagree with the sky. So a line
// of the form
//
//   #include "common/sky.glsl"
//
// is replaced by that file, resolved against the including file's own
// directory. One level is all anything here needs, but nesting costs
// nothing, so it recurses with a depth guard rather than silently stopping.
std::string readShader(const std::string& path, int depth = 0) {
  std::string src = readFile(path);
  if (src.empty() || depth > 4) return src;
  std::string dir;
  size_t slash = path.find_last_of("/\\");
  if (slash != std::string::npos) dir = path.substr(0, slash + 1);

  std::istringstream in(src);
  std::ostringstream out;
  std::string line;
  while (std::getline(in, line)) {
    size_t at = line.find_first_not_of(" \t");
    if (at != std::string::npos && line.compare(at, 8, "#include") == 0) {
      size_t q0 = line.find('"', at), q1 = line.find('"', q0 == std::string::npos ? q0 : q0 + 1);
      if (q0 != std::string::npos && q1 != std::string::npos) {
        out << readShader(dir + line.substr(q0 + 1, q1 - q0 - 1), depth + 1) << "\n";
        continue;
      }
    }
    out << line << "\n";
  }
  return out.str();
}

GLuint compile(GLenum stage, const std::string& src, const std::string& tag) {
  GLuint s = glCreateShader(stage);
  const char* csrc = src.c_str();
  glShaderSource(s, 1, &csrc, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
    std::vector<char> log(len > 0 ? len : 1);
    glGetShaderInfoLog(s, len, nullptr, log.data());
    std::fprintf(stderr, "[Shader] compile failed (%s):\n%s\n", tag.c_str(), log.data());
  }
  return s;
}

}  // namespace

bool Shader::load(const std::string& vertPath, const std::string& fragPath) {
  std::string vsrc = readShader(vertPath);
  std::string fsrc = readShader(fragPath);
  if (vsrc.empty() || fsrc.empty()) return false;

  GLuint vs = compile(GL_VERTEX_SHADER, vsrc, vertPath);
  GLuint fs = compile(GL_FRAGMENT_SHADER, fsrc, fragPath);

  program_ = glCreateProgram();
  glAttachShader(program_, vs);
  glAttachShader(program_, fs);
  glLinkProgram(program_);

  GLint ok = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &len);
    std::vector<char> log(len > 0 ? len : 1);
    glGetProgramInfoLog(program_, len, nullptr, log.data());
    std::fprintf(stderr, "[Shader] link failed (%s + %s):\n%s\n",
                 vertPath.c_str(), fragPath.c_str(), log.data());
  }
  glDeleteShader(vs);
  glDeleteShader(fs);
  uniformCache_.clear();
  return ok != 0;
}

void Shader::use() const { glUseProgram(program_); }

GLint Shader::loc(const std::string& name) const {
  auto it = uniformCache_.find(name);
  if (it != uniformCache_.end()) return it->second;
  GLint l = glGetUniformLocation(program_, name.c_str());
  uniformCache_[name] = l;
  return l;
}

void Shader::set(const std::string& name, bool v) const { glUniform1i(loc(name), v ? 1 : 0); }
void Shader::set(const std::string& name, int v) const { glUniform1i(loc(name), v); }
void Shader::set(const std::string& name, float v) const { glUniform1f(loc(name), v); }
void Shader::set(const std::string& name, const glm::vec2& v) const { glUniform2fv(loc(name), 1, glm::value_ptr(v)); }
void Shader::set(const std::string& name, const glm::vec3& v) const { glUniform3fv(loc(name), 1, glm::value_ptr(v)); }
void Shader::set(const std::string& name, const glm::vec4& v) const { glUniform4fv(loc(name), 1, glm::value_ptr(v)); }
void Shader::set(const std::string& name, const glm::mat3& v) const { glUniformMatrix3fv(loc(name), 1, GL_FALSE, glm::value_ptr(v)); }
void Shader::set(const std::string& name, const glm::mat4& v) const { glUniformMatrix4fv(loc(name), 1, GL_FALSE, glm::value_ptr(v)); }
void Shader::setArray(const std::string& name, const glm::vec4* v, int count) const {
  if (count > 0) glUniform4fv(loc(name), count, glm::value_ptr(v[0]));
}
void Shader::setArray(const std::string& name, const glm::vec3* v, int count) const {
  if (count > 0) glUniform3fv(loc(name), count, glm::value_ptr(v[0]));
}
