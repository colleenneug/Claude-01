#include "Framebuffer.h"

void Framebuffer::create(int w, int h, bool hdr, bool depthTexture) {
  hdr_ = hdr;
  format_ = hdr ? Format::Rgba16f : Format::Rgba8;
  colourOnly_ = false;
  mipmapped_ = false;
  wantDepthTex_ = depthTexture;
  w_ = w; h_ = h;
  glGenFramebuffers(1, &fbo_);
  rebuild();
}

void Framebuffer::createColour(int w, int h, Format format, bool mipmapped) {
  format_ = format;
  hdr_ = format == Format::Rgba16f || format == Format::R16f;
  colourOnly_ = true;
  mipmapped_ = mipmapped;
  wantDepthTex_ = false;
  w_ = w; h_ = h;
  glGenFramebuffers(1, &fbo_);
  rebuild();
}

void Framebuffer::resize(int w, int h) {
  if (w == w_ && h == h_) return;
  w_ = w; h_ = h;
  rebuild();
}

void Framebuffer::rebuild() {
  if (colorTex_) glDeleteTextures(1, &colorTex_);
  if (depthTex_) glDeleteTextures(1, &depthTex_);
  if (depthRbo_) glDeleteRenderbuffers(1, &depthRbo_);
  colorTex_ = depthTex_ = depthRbo_ = 0;

  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

  glGenTextures(1, &colorTex_);
  glBindTexture(GL_TEXTURE_2D, colorTex_);
  GLenum internal = GL_RGBA8, layout = GL_RGBA, type = GL_UNSIGNED_BYTE;
  switch (format_) {
    case Format::Rgba8:   internal = GL_RGBA8;   layout = GL_RGBA; type = GL_UNSIGNED_BYTE; break;
    case Format::Rgba16f: internal = GL_RGBA16F; layout = GL_RGBA; type = GL_FLOAT; break;
    case Format::R8:      internal = GL_R8;      layout = GL_RED;  type = GL_UNSIGNED_BYTE; break;
    case Format::R16f:    internal = GL_R16F;    layout = GL_RED;  type = GL_FLOAT; break;
  }
  glTexImage2D(GL_TEXTURE_2D, 0, internal, w_, h_, 0, layout, type, nullptr);
  if (mipmapped_) glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmapped_ ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);

  if (wantDepthTex_) {
    glGenTextures(1, &depthTex_);
    glBindTexture(GL_TEXTURE_2D, depthTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w_, h_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex_, 0);
  } else if (!colourOnly_) {
    glGenRenderbuffers(1, &depthRbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w_, h_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);
  }

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    std::fprintf(stderr, "[Framebuffer] incomplete: 0x%04x (%dx%d hdr=%d depthTex=%d)\n",
                 status, w_, h_, hdr_, wantDepthTex_);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::bind() const {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glViewport(0, 0, w_, h_);
}

void Framebuffer::bindScreen(int w, int h) {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, w, h);
}

void Framebuffer::destroy() {
  if (colorTex_) glDeleteTextures(1, &colorTex_);
  if (depthTex_) glDeleteTextures(1, &depthTex_);
  if (depthRbo_) glDeleteRenderbuffers(1, &depthRbo_);
  if (fbo_) glDeleteFramebuffers(1, &fbo_);
  colorTex_ = depthTex_ = depthRbo_ = fbo_ = 0;
}
