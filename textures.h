#pragma once
#include "gfx.h"

struct Img {
    int w, h;
    std::vector<uint8_t> d;
    Img(int W, int H) : w(W), h(H), d((size_t)W * H * 4, 0) {}
    static uint8_t c8(float v) { return (uint8_t)clampf(v, 0.f, 255.f); }
    void set(int x, int y, float r, float g, float b, float a = 255.f) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        uint8_t* p = &d[((size_t)y * w + x) * 4];
        p[0] = c8(r); p[1] = c8(g); p[2] = c8(b); p[3] = c8(a);
    }
    void fill(float r, float g, float b, float a = 255.f) {
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) set(x, y, r, g, b, a);
    }
    void getRGB(int x, int y, float& r, float& g, float& b) const {
        x = ((x % w) + w) % w; y = ((y % h) + h) % h;
        const uint8_t* p = &d[((size_t)y * w + x) * 4];
        r = p[0]; g = p[1]; b = p[2];
    }
    void rectFill(int x0, int y0, int x1, int y1, float r, float g, float b, float a = 255.f) {
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) set(x, y, r, g, b, a);
    }
};

// tileable value noise
inline float vtile(float x, float y, int cell, int w, int h, uint32_t seed) {
    cell = std::max(1, cell);
    float fx = x / cell, fy = y / cell;
    int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
    float tx = fx - ix, ty = fy - iy;
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
    int pw = std::max(1, w / cell), ph = std::max(1, h / cell);
    auto g = [&](int a, int b) { a = ((a % pw) + pw) % pw; b = ((b % ph) + ph) % ph; return nz(a, b, seed); };
    return lerpf(lerpf(g(ix, iy), g(ix + 1, iy), tx), lerpf(g(ix, iy + 1), g(ix + 1, iy + 1), tx), ty);
}
inline float fbm(float x, float y, int w, int h, uint32_t seed) {
    return 0.5f * vtile(x, y, 16, w, h, seed) + 0.3f * vtile(x, y, 8, w, h, seed + 1) +
           0.2f * vtile(x, y, 4, w, h, seed + 2);
}

namespace tex {

inline Img asphalt() {  // u across the road, v along it (tile = ~12 m)
    Img im(128, 128);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            float u = x / 128.f;
            float n = fbm((float)x, (float)y, 128, 128, 11);
            float base = 58 + n * 30 + (nz(x, y, 5) - 0.5f) * 14;
            if (nz(x, y, 9) > 0.975f) base += 45;               // light aggregate
            if (nz(x, y, 12) < 0.02f) base -= 18;               // dark pits
            float wear = (u > 0.2f && u < 0.36f) || (u > 0.64f && u < 0.8f) ? -9.f : 0.f;  // rubbered-in lines
            base += wear;
            float r = base, g = base, b = base * 1.04f;
            bool edge = (u > 0.035f && u < 0.062f) || (u > 0.938f && u < 0.965f);
            if (edge) {
                float w = 215 + (nz(x, y, 21) - 0.5f) * 40;
                r = g = b = w;
            }
            im.set(x, y, r, g, b);
        }
    // a couple of repair patches
    for (int p = 0; p < 3; p++) {
        int px = 14 + p * 40, py = 20 + p * 37;
        for (int y = py; y < py + 18; y++) for (int x = px; x < px + 26; x++) {
            float r, g, b; im.getRGB(x, y, r, g, b);
            if (x > 8 && x < 118) im.set(x, y, r * 0.78f, g * 0.78f, b * 0.8f);
        }
    }
    return im;
}
inline Img grass() {
    Img im(64, 64);
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        float n = fbm((float)x, (float)y, 64, 64, 31);
        float blade = nz(x, y, 33);
        float r = 46 + n * 30, g = 98 + n * 52, b = 34 + n * 20;
        if (blade > 0.88f) { r += 18; g += 28; }
        if (blade < 0.1f) { r -= 14; g -= 22; b -= 8; }
        im.set(x, y, r, g, b);
    }
    return im;
}
inline Img gravel() {
    Img im(64, 64);
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        float n = nz(x, y, 41), m = fbm((float)x, (float)y, 64, 64, 43);
        float v = 150 + m * 40 + (n - 0.5f) * 60;
        im.set(x, y, v * 1.0f, v * 0.9f, v * 0.7f);
    }
    return im;
}
inline Img kerb() {  // 8 wide (across), 32 tall (along): 2 stripes
    Img im(8, 32);
    for (int y = 0; y < 32; y++) for (int x = 0; x < 8; x++) {
        float k = (nz(x, y, 51) - 0.5f) * 18;
        if (y < 16) im.set(x, y, 205 + k, 28 + k * 0.4f, 28 + k * 0.4f);
        else im.set(x, y, 228 + k, 228 + k, 224 + k);
    }
    return im;
}
inline Img rail() {  // galvanised guard rail
    Img im(64, 32);
    for (int y = 0; y < 32; y++) for (int x = 0; x < 64; x++) {
        float v = 150 + (nz(x, y, 61) - 0.5f) * 14;
        if (y >= 4 && y < 9) v += 55;          // highlight
        else if (y >= 9 && y < 12) v -= 30;
        else if (y >= 20 && y < 25) v += 35;
        else if (y >= 25 && y < 28) v -= 40;
        float r = v, g = v * 1.01f, b = v * 1.06f;
        if ((x % 32) >= 14 && (x % 32) < 18 && y >= 13 && y < 17) { r = 230; g = 120; b = 20; }  // reflectors
        if (x % 16 == 0 || x % 16 == 1) { r *= 0.8f; g *= 0.8f; b *= 0.8f; }
        im.set(x, y, r, g, b);
    }
    return im;
}
inline Img foliage() {
    Img im(64, 64);
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        float n = fbm((float)x, (float)y, 64, 64, 71);
        float s = nz(x, y, 73);
        float r = 22 + n * 28, g = 62 + n * 50, b = 24 + n * 18;
        if (s > 0.9f) { r += 25; g += 38; }
        if (s < 0.12f) { r -= 12; g -= 22; }
        im.set(x, y, r, g, b);
    }
    return im;
}
inline Img bark() {
    Img im(16, 16);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        float s = (nz(x, 0, 81) - 0.5f) * 40 + (nz(x, y, 82) - 0.5f) * 14;
        im.set(x, y, 84 + s, 58 + s * 0.7f, 38 + s * 0.4f);
    }
    return im;
}
inline Img wall1() {  // office / pit building with lit windows
    Img im(64, 64);
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        float v = 158 + (nz(x, y, 91) - 0.5f) * 22 + fbm((float)x, (float)y, 64, 64, 92) * 18;
        im.set(x, y, v, v * 0.98f, v * 0.93f);
    }
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 4; col++) {
            int x0 = 3 + col * 15, y0 = 6 + row * 20;
            bool lit = nz(col, row, 95) > 0.45f;
            for (int y = y0; y < y0 + 12; y++) for (int x = x0; x < x0 + 10; x++) {
                if (lit) { float f = 1.f - (y - y0) * 0.02f; im.set(x, y, 255 * f, 205 * f, 110 * f); }
                else im.set(x, y, 28, 40, 62 + (y - y0) * 3);
            }
            for (int x = x0 - 1; x <= x0 + 10; x++) { im.set(x, y0 - 1, 90, 90, 90); im.set(x, y0 + 12, 90, 90, 90); }
        }
    return im;
}
inline Img wall2() {  // brick
    Img im(64, 64);
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        int row = y / 8; int off = (row % 2) * 8;
        bool mortar = (y % 8 == 0) || ((x + off) % 16 == 0);
        float k = (nz((x + off) / 16, row, 101) - 0.5f) * 40 + (nz(x, y, 102) - 0.5f) * 14;
        if (mortar) im.set(x, y, 170, 165, 155);
        else im.set(x, y, 150 + k, 70 + k * 0.5f, 52 + k * 0.4f);
    }
    return im;
}
inline Img roof() {
    Img im(32, 32);
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
        float k = (nz(x / 4, y / 4, 111) - 0.5f) * 24 + (nz(x, y, 112) - 0.5f) * 10;
        float shade = (y % 4 == 0) ? -22.f : 0.f;
        im.set(x, y, 112 + k + shade, 62 + k * 0.6f + shade, 52 + k * 0.5f + shade);
    }
    return im;
}
inline Img seats() {
    Img im(64, 16);
    static const float pal[6][3] = {{200, 40, 40}, {40, 70, 190}, {230, 200, 50}, {235, 235, 235}, {50, 50, 60}, {230, 110, 30}};
    for (int y = 0; y < 16; y++) for (int x = 0; x < 64; x++) {
        int gx = x / 3, gy = y / 4;
        int c = hash2(gx, gy, 121) % 6;
        float k = 0.8f + nz(x, y, 122) * 0.3f;
        if (y % 4 == 3) { im.set(x, y, 60, 60, 65); continue; }
        im.set(x, y, pal[c][0] * k, pal[c][1] * k, pal[c][2] * k);
    }
    return im;
}
inline Img wheel() {  // alloy wheel face, 32x32
    Img im(32, 32);
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
        float dx = x - 15.5f, dy = y - 15.5f;
        float r = std::sqrt(dx * dx + dy * dy), a = std::atan2(dy, dx);
        float R, G, B;
        if (r > 14.5f) { R = G = B = 14; }
        else if (r > 12.5f) { R = G = B = 200; }  // rim lip
        else if (r > 4.5f) {
            float sp = std::fabs(std::sin(a * 2.5f));  // 5 spokes
            if (sp > 0.55f) { R = G = B = 195 - r * 3; }
            else { R = G = B = 22; if (r > 6 && r < 10) { R = 150; G = 30; B = 30; } }  // red caliper through gaps
        }
        else if (r > 2.f) { R = G = B = 150; }
        else { R = G = B = 60; }
        im.set(x, y, R, G, B);
    }
    return im;
}
inline Img glow() {
    Img im(64, 64);
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        float dx = (x - 31.5f) / 31.5f, dy = (y - 31.5f) / 31.5f;
        float r = std::sqrt(dx * dx + dy * dy);
        float a = sat(1.f - r); a = a * a;
        im.set(x, y, 255, 255, 255, a * 255);
    }
    return im;
}
inline Img shadow() {
    Img im(32, 32);
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
        float dx = (x - 15.5f) / 15.5f, dy = (y - 15.5f) / 15.5f;
        float r = std::sqrt(dx * dx + dy * dy);
        float a = sat((1.f - r) * 2.2f); a = std::min(a, 0.8f);
        im.set(x, y, 0, 0, 0, a * 255);
    }
    return im;
}
inline Img checker() {
    Img im(32, 32);
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
        bool w = ((x / 8) + (y / 8)) % 2 == 0;
        float k = (nz(x, y, 131) - 0.5f) * 14;
        im.set(x, y, (w ? 235 : 20) + k, (w ? 235 : 20) + k, (w ? 235 : 22) + k);
    }
    return im;
}
inline Img metalBg() {  // dark brushed steel for the studio splash
    Img im(256, 256);
    for (int y = 0; y < 256; y++) for (int x = 0; x < 256; x++) {
        float streak = vtile((float)x * 0.12f, (float)y * 6.f, 8, 256, 256, 141) * 0.6f + nz(0, y, 142) * 0.4f;
        float v = 9 + streak * 13 + (nz(x, y, 143) - 0.5f) * 3;
        im.set(x, y, v, v * 1.02f, v * 1.08f);
    }
    return im;
}
inline Img sky() {  // not used for the dome (vertex colors) - kept for the menu vignette
    Img im(4, 4); im.fill(0, 0, 0, 0); return im;
}
}  // namespace tex
