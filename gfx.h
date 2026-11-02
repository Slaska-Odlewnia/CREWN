// CREWN - OpenGL helpers (fixed function), 2D drawing and the bitmap font
#pragma once
#include <SDL.h>
#include <SDL_opengl.h>

#include "util.h"

struct Col {
    float r, g, b, a;
    Col(float R = 1, float G = 1, float B = 1, float A = 1) : r(R), g(G), b(B), a(A) {}
    Col withA(float A) const { return Col(r, g, b, A); }
    Col scaled(float s) const { return Col(r * s, g * s, b * s, a); }
};
inline void setCol(const Col& c) { glColor4f(c.r, c.g, c.b, c.a); }

// textures
// ids of the mipmapped world textures, so the filter can be switched at runtime (1990s: no smoothing at all)
inline std::vector<GLuint>& worldTextures() { static std::vector<GLuint> v; return v; }
inline void setRetroFilter(bool retro) {
    for (GLuint id : worldTextures()) {
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, retro ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_NEAREST);
    }
}

inline GLuint uploadTexture(const std::vector<uint8_t>& rgba, int w, int h, bool mip, bool repeat, bool linear) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    std::vector<uint8_t> cur = rgba;
    int cw = w, ch = h, level = 0;
    for (;;) {
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, cw, ch, 0, GL_RGBA, GL_UNSIGNED_BYTE, cur.data());
        if (!mip || (cw == 1 && ch == 1)) break;
        int nw = std::max(1, cw / 2), nh = std::max(1, ch / 2);
        std::vector<uint8_t> nx((size_t)nw * nh * 4);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++)
                for (int c = 0; c < 4; c++) {
                    int x0 = std::min(cw - 1, x * 2), x1 = std::min(cw - 1, x * 2 + 1);
                    int y0 = std::min(ch - 1, y * 2), y1 = std::min(ch - 1, y * 2 + 1);
                    int s = cur[((size_t)y0 * cw + x0) * 4 + c] + cur[((size_t)y0 * cw + x1) * 4 + c] +
                            cur[((size_t)y1 * cw + x0) * 4 + c] + cur[((size_t)y1 * cw + x1) * 4 + c];
                    nx[((size_t)y * nw + x) * 4 + c] = (uint8_t)(s / 4);
                }
        cur.swap(nx);
        cw = nw; ch = nh; level++;
    }
    // chunky "pixelated" magnification, bilinear+mipmapped minification (very 2001)
    GLint mag = linear ? GL_LINEAR : GL_NEAREST;
    GLint mn = mip ? (linear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST) : mag;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mn);
    if (mip && !linear) worldTextures().push_back(id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP);
    return id;
}

// 3D camera helpers (no GLU dependency)
struct CamBasis { V3 pos, right, up, fwd; };

inline void setPerspective(float fovyDeg, float aspect, float n, float f) {
    float t = std::tan(fovyDeg * PI / 360.f) * n;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-t * aspect, t * aspect, -t, t, n, f);
    glMatrixMode(GL_MODELVIEW);
}

inline CamBasis applyLookAt(const V3& eye, const V3& target) {
    CamBasis cb;
    cb.pos = eye;
    cb.fwd = norm3(target - eye);
    V3 r = cross3(cb.fwd, V3(0, 1, 0));
    if (len3(r) < 1e-4f) r = V3(1, 0, 0);
    cb.right = norm3(r);
    cb.up = cross3(cb.right, cb.fwd);
    float m[16] = {cb.right.x, cb.up.x, -cb.fwd.x, 0, cb.right.y, cb.up.y, -cb.fwd.y, 0,
                   cb.right.z, cb.up.z, -cb.fwd.z, 0, 0, 0, 0, 1};
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m);
    glTranslatef(-eye.x, -eye.y, -eye.z);
    return cb;
}

// 2D view: integer-scaled logical pixels so the UI stays crisp at any resolution
struct View2D {
    int winW = 1280, winH = 720, k = 2;
    float W = 640, H = 360;
};
inline View2D makeView(int w, int h) {
    View2D v;
    v.winW = w; v.winH = h;
    v.k = std::max(1, h / 360);
    v.W = (float)w / v.k;
    v.H = (float)h / v.k;
    return v;
}
inline void begin2D(const View2D& v) {
    glViewport(0, 0, v.winW, v.winH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, v.W, v.H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
}

inline void rect(float x, float y, float w, float h, const Col& c) {
    glDisable(GL_TEXTURE_2D);
    setCol(c);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}
inline void rectGradV(float x, float y, float w, float h, const Col& top, const Col& bot) {
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    setCol(top); glVertex2f(x, y); glVertex2f(x + w, y);
    setCol(bot); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}
inline void rectGradH(float x, float y, float w, float h, const Col& l, const Col& r) {
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    setCol(l); glVertex2f(x, y); glVertex2f(x, y + h);
    setCol(r); glVertex2f(x + w, y + h); glVertex2f(x + w, y);
    glEnd();
}
inline void frame(float x, float y, float w, float h, const Col& c, float t = 1) {
    rect(x, y, w, t, c); rect(x, y + h - t, w, t, c);
    rect(x, y, t, h, c); rect(x + w - t, y, t, h, c);
}
inline void line2(float x1, float y1, float x2, float y2, const Col& c, float width) {
    glDisable(GL_TEXTURE_2D);
    glLineWidth(width);
    setCol(c);
    glBegin(GL_LINES);
    glVertex2f(x1, y1); glVertex2f(x2, y2);
    glEnd();
}
inline void disc(float cx, float cy, float r, int seg, const Col& c) {
    glDisable(GL_TEXTURE_2D);
    setCol(c);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= seg; i++) {
        float a = TAU * i / seg;
        glVertex2f(cx + std::cos(a) * r, cy + std::sin(a) * r);
    }
    glEnd();
}


// Bitmap font: classic 5x7 glyphs, plus hand-made Polish letters.
// Two atlases are built: a crisp 1x one (small text) and a smoothed 4x one
// (Scale2x applied twice) for big titles.
namespace fontdata {
// columns, bit0 = top row. ASCII 32..126
static const uint8_t g5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x56,0x20,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x08,0x08,0x2A,0x1C,0x08}};
enum { GLY_S_UP = 95, GLY_S_LO, GLY_L_UP, GLY_L_LO, GLY_A_OG, GLY_COUNT };
constexpr int CELL_W = 8, CELL_H = 12, ROW0 = 3;  // body of a glyph starts at row 3 of the cell
}  // namespace fontdata

class Font {
public:
    GLuint tex1 = 0, tex4 = 0;

    void init() {
        using namespace fontdata;
        const int cols = 16, rows = 8, W = cols * CELL_W, H = rows * CELL_H;  // 128 x 96
        std::vector<uint8_t> bits((size_t)W * H, 0);
        auto put = [&](int idx, int x, int y) {
            int cx = (idx % cols) * CELL_W, cy = (idx / cols) * CELL_H;
            if (x >= 0 && y >= 0 && x < CELL_W && y < CELL_H) bits[(size_t)(cy + y) * W + cx + x] = 255;
        };
        auto body = [&](int idx, const uint8_t* colsData) {
            for (int c = 0; c < 5; c++)
                for (int r = 0; r < 7; r++)
                    if (colsData[c] & (1 << r)) put(idx, c, ROW0 + r);
        };
        for (int i = 0; i < 95; i++) body(i, g5x7[i]);
        // lowercase g with a real descender (the squeezed 7-row version read as a "9")
        {
            static const char* gRows[9] = {".....", ".....", ".###.", "#...#", "#...#", "#...#", ".####", "....#", ".###."};
            int gi = 'g' - 32, cx = (gi % cols) * CELL_W, cy = (gi / cols) * CELL_H;
            for (int y = 0; y < CELL_H; y++) for (int x = 0; x < CELL_W; x++) bits[(size_t)(cy + y) * W + cx + x] = 0;
            for (int r = 0; r < 9; r++) for (int c = 0; c < 5; c++) if (gRows[r][c] == '#') put(gi, c, ROW0 + r);
        }
        // Polish letters
        body(GLY_S_UP, g5x7['S' - 32]); put(GLY_S_UP, 3, 0); put(GLY_S_UP, 2, 1);
        body(GLY_S_LO, g5x7['s' - 32]); put(GLY_S_LO, 3, 2); put(GLY_S_LO, 2, 3);
        static const uint8_t Lbar[5] = {0x10, 0x7F, 0x44, 0x40, 0x40};
        static const uint8_t lbar[5] = {0x00, 0x51, 0x7F, 0x44, 0x00};
        body(GLY_L_UP, Lbar);
        body(GLY_L_LO, lbar);
        body(GLY_A_OG, g5x7['a' - 32]); put(GLY_A_OG, 4, ROW0 + 7); put(GLY_A_OG, 5, ROW0 + 7);

        // 1x atlas, padded to 128x128
        tex1 = makeAtlas(bits, W, H, 1, false);
        // 4x smoothed atlas (Scale2x twice) -> 512x384, padded to 512x512
        std::vector<uint8_t> b2 = scale2x(bits, W, H);
        std::vector<uint8_t> b4 = scale2x(b2, W * 2, H * 2);
        tex4 = makeAtlas(b4, W * 4, H * 4, 4, true);
    }

    static uint32_t decode(const char*& p) {
        unsigned char c = (unsigned char)*p++;
        if (c < 0x80) return c;
        if ((c & 0xE0) == 0xC0 && *p) { uint32_t cp = ((c & 0x1F) << 6) | ((unsigned char)*p++ & 0x3F); return cp; }
        if ((c & 0xF0) == 0xE0 && p[0] && p[1]) {
            uint32_t cp = ((c & 0x0F) << 12) | (((unsigned char)p[0] & 0x3F) << 6) | ((unsigned char)p[1] & 0x3F);
            p += 2; return cp;
        }
        return '?';
    }
    static int glyphIndex(uint32_t cp) {
        using namespace fontdata;
        if (cp >= 32 && cp <= 126) return (int)cp - 32;
        switch (cp) {
            case 0x15A: return GLY_S_UP;
            case 0x15B: return GLY_S_LO;
            case 0x141: return GLY_L_UP;
            case 0x142: return GLY_L_LO;
            case 0x105: return GLY_A_OG;
            case 0xA9: return 'c' - 32;  // (c) sign fallback
            default: return '?' - 32;
        }
    }
    float width(const std::string& s, float scale) const {
        float w = 0;
        const char* p = s.c_str();
        while (*p) { decode(p); w += 6.f * scale; }
        return w > 0 ? w - scale : 0;  // no trailing spacing
    }
    // y is the top of capital letters. shear skews glyphs for an italic look.
    void draw(float x, float y, const std::string& s, float scale, const Col& top, const Col& bot,
              float shear = 0.f) const {
        using namespace fontdata;
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, scale >= 3.f ? tex4 : tex1);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glBegin(GL_QUADS);
        const char* p = s.c_str();
        float cx = x, y0 = y - ROW0 * scale, hh = CELL_H * scale, ww = 6.f * scale;
        float sh = shear * hh;
        float du = 6.f / 128.f, dv = (float)CELL_H / 128.f;
        while (*p) {
            int gi = glyphIndex(decode(p));
            if (gi != 0) {
                float u = (gi % 16) * (CELL_W / 128.f), v = (gi / 16) * (CELL_H / 128.f);
                setCol(top);
                glTexCoord2f(u, v); glVertex2f(cx + sh, y0);
                glTexCoord2f(u + du, v); glVertex2f(cx + ww + sh, y0);
                setCol(bot);
                glTexCoord2f(u + du, v + dv); glVertex2f(cx + ww, y0 + hh);
                glTexCoord2f(u, v + dv); glVertex2f(cx, y0 + hh);
            }
            cx += ww;
        }
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }
    void draw(float x, float y, const std::string& s, float scale, const Col& c) const { draw(x, y, s, scale, c, c); }
    // convenience: shadowed text
    void drawShadow(float x, float y, const std::string& s, float scale, const Col& c, float off = 1.f) const {
        draw(x + off * scale * 0.5f + 0.5f, y + off * scale * 0.5f + 0.5f, s, scale, Col(0, 0, 0, c.a * 0.75f));
        draw(x, y, s, scale, c);
    }
    void drawCentered(float cx, float y, const std::string& s, float scale, const Col& c, bool shadow = true) const {
        float x = cx - width(s, scale) * 0.5f;
        if (shadow) drawShadow(x, y, s, scale, c); else draw(x, y, s, scale, c);
    }
    void drawRight(float rx, float y, const std::string& s, float scale, const Col& c, bool shadow = true) const {
        float x = rx - width(s, scale);
        if (shadow) drawShadow(x, y, s, scale, c); else draw(x, y, s, scale, c);
    }

private:
    static std::vector<uint8_t> scale2x(const std::vector<uint8_t>& s, int w, int h) {
        std::vector<uint8_t> d((size_t)w * 2 * h * 2, 0);
        auto at = [&](int x, int y) -> uint8_t {
            if (x < 0 || y < 0 || x >= w || y >= h) return 0;
            return s[(size_t)y * w + x];
        };
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint8_t P = at(x, y), A = at(x, y - 1), B = at(x + 1, y), C = at(x - 1, y), D = at(x, y + 1);
                uint8_t e0 = P, e1 = P, e2 = P, e3 = P;
                if (C == A && C != D && A != B) e0 = A;
                if (A == B && A != C && B != D) e1 = B;
                if (D == C && D != B && C != A) e2 = C;
                if (B == D && B != A && D != C) e3 = D;
                d[(size_t)(y * 2) * w * 2 + x * 2] = e0;
                d[(size_t)(y * 2) * w * 2 + x * 2 + 1] = e1;
                d[(size_t)(y * 2 + 1) * w * 2 + x * 2] = e2;
                d[(size_t)(y * 2 + 1) * w * 2 + x * 2 + 1] = e3;
            }
        return d;
    }
    static GLuint makeAtlas(const std::vector<uint8_t>& bits, int w, int h, int f, bool linear) {
        int T = 128 * f;
        std::vector<uint8_t> rgba((size_t)T * T * 4, 0);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                size_t o = ((size_t)y * T + x) * 4;
                rgba[o] = rgba[o + 1] = rgba[o + 2] = 255;
                rgba[o + 3] = bits[(size_t)y * w + x];
            }
        for (size_t i = 0; i < rgba.size(); i += 4) if (rgba[i + 3] == 0) rgba[i] = rgba[i + 1] = rgba[i + 2] = 255;
        return uploadTexture(rgba, T, T, false, false, linear);
    }
};
