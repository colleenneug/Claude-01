// ============================================================
//  Framebuffers and image output.
//
//  Colour is kept in linear float, because every shading decision
//  downstream — lighting, fog, tone mapping — is wrong if it is made on
//  display values. sRGB happens once, on the way out.
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "forge/math/Math.hpp"

namespace forge {

class Framebuffer {
public:
    Framebuffer() = default;
    Framebuffer(int width, int height) { resize(width, height); }

    void resize(int width, int height);
    void clearColor(const Color& c);
    void clearDepth(float value = 1.0f);

    int width() const { return width_; }
    int height() const { return height_; }
    float aspect() const { return height_ > 0 ? (float)width_ / (float)height_ : 1.0f; }
    bool valid() const { return width_ > 0 && height_ > 0; }

    Color* colorRow(int y) { return color_.data() + (size_t)y * (size_t)width_; }
    const Color* colorRow(int y) const { return color_.data() + (size_t)y * (size_t)width_; }
    float* depthRow(int y) { return depth_.data() + (size_t)y * (size_t)width_; }

    Color pixel(int x, int y) const;
    void setPixel(int x, int y, const Color& c);
    // Alpha-composites onto what is already there. The UI layer draws
    // through this so panels can be translucent.
    void blend(int x, int y, const Color& c, float alpha);

    const std::vector<Color>& colorBuffer() const { return color_; }
    std::vector<Color>& colorBuffer() { return color_; }
    std::vector<float>& depthBuffer() { return depth_; }

    // 8-bit sRGB, top row first, for handing to a GPU or an encoder.
    std::vector<uint8_t> toRGBA8() const;
    bool savePNG(const std::string& path) const;

    // Average luminance, which the tests use to assert that something was
    // actually drawn rather than eyeballing an image.
    float averageLuminance() const;

private:
    int width_ = 0, height_ = 0;
    std::vector<Color> color_;
    std::vector<float> depth_;
};

// Minimal PNG writer: no dependency, and a screenshot is the most
// direct way to check a renderer is doing what it claims.
bool writePNG(const std::string& path, const uint8_t* rgba, int width, int height);

} // namespace forge
