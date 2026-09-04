#include "forge/render/Framebuffer.hpp"

#include <cstdio>
#include <cstring>

namespace forge {

void Framebuffer::resize(int w, int h) {
    width_ = std::max(0, w);
    height_ = std::max(0, h);
    const size_t n = (size_t)width_ * (size_t)height_;
    color_.assign(n, Color{0, 0, 0, 1});
    depth_.assign(n, 1.0f);
}

void Framebuffer::clearColor(const Color& c) {
    for (Color& p : color_) p = c;
}

void Framebuffer::clearDepth(float value) {
    for (float& d : depth_) d = value;
}

Color Framebuffer::pixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return Color{0, 0, 0, 0};
    return color_[(size_t)y * (size_t)width_ + (size_t)x];
}

void Framebuffer::setPixel(int x, int y, const Color& c) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    color_[(size_t)y * (size_t)width_ + (size_t)x] = c;
}

void Framebuffer::blend(int x, int y, const Color& c, float alpha) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    if (alpha <= 0.0f) return;
    Color& dst = color_[(size_t)y * (size_t)width_ + (size_t)x];
    if (alpha >= 1.0f) { dst = Color{c.r, c.g, c.b, 1.0f}; return; }
    dst.r = lerpf(dst.r, c.r, alpha);
    dst.g = lerpf(dst.g, c.g, alpha);
    dst.b = lerpf(dst.b, c.b, alpha);
}

std::vector<uint8_t> Framebuffer::toRGBA8() const {
    std::vector<uint8_t> out((size_t)width_ * (size_t)height_ * 4);
    for (size_t i = 0; i < color_.size(); ++i) {
        const Color& c = color_[i];
        // The one place linear becomes display-referred.
        out[i * 4 + 0] = (uint8_t)clampf(std::round(linearToSrgb(c.r) * 255.0f), 0.0f, 255.0f);
        out[i * 4 + 1] = (uint8_t)clampf(std::round(linearToSrgb(c.g) * 255.0f), 0.0f, 255.0f);
        out[i * 4 + 2] = (uint8_t)clampf(std::round(linearToSrgb(c.b) * 255.0f), 0.0f, 255.0f);
        out[i * 4 + 3] = (uint8_t)clampf(std::round(c.a * 255.0f), 0.0f, 255.0f);
    }
    return out;
}

float Framebuffer::averageLuminance() const {
    if (color_.empty()) return 0.0f;
    double sum = 0.0;
    for (const Color& c : color_) sum += 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
    return (float)(sum / (double)color_.size());
}

bool Framebuffer::savePNG(const std::string& path) const {
    if (!valid()) return false;
    return writePNG(path, toRGBA8().data(), width_, height_);
}

// -----------------------------------------------------------------
//  PNG
//
//  Deflate has a "stored" block type that compresses nothing. Using it
//  means a complete, valid PNG without a compressor — the files are
//  larger than they need to be, which for a screenshot is a fine trade
//  against carrying zlib.
// -----------------------------------------------------------------

namespace {

uint32_t crc32Of(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
    static uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        built = true;
    }
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

void pushBE32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v >> 24));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)v);
}

void pushChunk(std::vector<uint8_t>& out, const char tag[4], const std::vector<uint8_t>& data) {
    pushBE32(out, (uint32_t)data.size());
    std::vector<uint8_t> body;
    body.reserve(4 + data.size());
    body.insert(body.end(), tag, tag + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    pushBE32(out, crc32Of(body.data(), body.size()) ^ 0xFFFFFFFFu);
}

} // namespace

bool writePNG(const std::string& path, const uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return false;

    // Raw scanlines, each prefixed with filter type 0 (none).
    std::vector<uint8_t> raw;
    raw.reserve((size_t)height * ((size_t)width * 4 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const uint8_t* row = rgba + (size_t)y * (size_t)width * 4;
        raw.insert(raw.end(), row, row + (size_t)width * 4);
    }

    // zlib stream: 2-byte header, stored deflate blocks, Adler-32.
    std::vector<uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    const size_t kMaxBlock = 65535;
    for (size_t off = 0; off < raw.size(); off += kMaxBlock) {
        const size_t n = std::min(kMaxBlock, raw.size() - off);
        z.push_back(off + n >= raw.size() ? 1 : 0);
        z.push_back((uint8_t)(n & 0xFF));
        z.push_back((uint8_t)(n >> 8));
        z.push_back((uint8_t)(~n & 0xFF));
        z.push_back((uint8_t)((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + (long)off, raw.begin() + (long)(off + n));
    }
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    pushBE32(z, (b << 16) | a);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    std::vector<uint8_t> ihdr;
    pushBE32(ihdr, (uint32_t)width);
    pushBE32(ihdr, (uint32_t)height);
    ihdr.push_back(8);    // bit depth
    ihdr.push_back(6);    // colour type: RGBA
    ihdr.push_back(0);    // deflate
    ihdr.push_back(0);    // adaptive filtering
    ihdr.push_back(0);    // no interlace
    pushChunk(png, "IHDR", ihdr);
    pushChunk(png, "IDAT", z);
    pushChunk(png, "IEND", {});

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const size_t written = std::fwrite(png.data(), 1, png.size(), f);
    std::fclose(f);
    return written == png.size();
}

} // namespace forge
