#include "Framebuffer.h"

void Framebuffer::create(int w, int h, bool hdr, bool depthTexture) {
  hdr_ = hdr;
  wantDepthTex_ = depthTexture;
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
  glTexImage2D(GL_TEXTURE_2D, 0, hdr_ ? GL_RGBA16F : GL_RGBA8, w_, h_, 0, GL_RGBA,
               hdr_ ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
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
  } else {
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
