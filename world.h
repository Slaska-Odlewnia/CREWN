// CREWN - builds all static geometry into OpenGL display lists (fixed-function, low-poly, 2001 style)
#pragma once
#include "gfx.h"
#include "textures.h"
#include "track.h"

// dusk palette -------------------------------------------------------------
inline Col horizonCol() { return Col(0.86f, 0.55f, 0.42f, 1); }
inline V3 sunDir() { return norm3(V3(-0.55f, 0.26f, 0.50f)); }

struct Tx {
    GLuint asphalt = 0, grass = 0, gravel = 0, kerb = 0, rail = 0, foliage = 0, bark = 0, wall1 = 0, wall2 = 0, roof = 0,
           seats = 0, wheel = 0, glow = 0, shadow = 0, checker = 0, banner = 0, metal = 0, white = 0;
};
struct Lamp { V3 head; };

// text blit into an image using the 5x7 font (ASCII only) for the in-world banner
inline Img makeBanner() {
    Img im(256, 32);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 256; x++) {
            float k = 22 + y * 0.6f;
            im.set(x, y, k * 0.6f, k * 0.7f, k * 1.5f);
        }
    for (int x = 0; x < 256; x++) {
        im.set(x, 0, 255, 120, 20); im.set(x, 1, 255, 120, 20); im.set(x, 2, 255, 120, 20);
        im.set(x, 29, 255, 120, 20); im.set(x, 30, 255, 120, 20); im.set(x, 31, 255, 120, 20);
    }
    for (int i = 0; i < 2; i++)  // chequered ends
        for (int y = 4; y < 28; y++)
            for (int x = 0; x < 16; x++) {
                bool w = (((x / 4) + (y / 4)) % 2) == 0;
                int px = i == 0 ? x : 255 - x;
                im.set(px, y, w ? 240 : 15, w ? 240 : 15, w ? 240 : 15);
            }
    const char* text = "CREWN START/FINISH";
    int len = (int)std::strlen(text), sc = 2, totalW = len * 6 * sc - sc, x0 = (256 - totalW) / 2, y0 = 9;
    for (int ci = 0; ci < len; ci++) {
        const uint8_t* g = fontdata::g5x7[text[ci] - 32];
        for (int c = 0; c < 5; c++)
            for (int r = 0; r < 7; r++)
                if (g[c] & (1 << r))
                    for (int dy = 0; dy < sc; dy++)
                        for (int dx = 0; dx < sc; dx++) {
                            int px = x0 + ci * 6 * sc + c * sc + dx, py = y0 + r * sc + dy;
                            im.set(px, py, 255, ci < 5 ? 160.f : 255.f, ci < 5 ? 40.f : 255.f);
                        }
    }
    return im;
}

namespace g3 {
inline void tquad(const V3& a, const V3& b, const V3& c, const V3& d, float u0, float v0, float u1, float v1) {
    V3 n = norm3(cross3(b - a, d - a));
    glNormal3f(n.x, n.y, n.z);
    glTexCoord2f(u0, v0); glVertex3f(a.x, a.y, a.z);
    glTexCoord2f(u1, v0); glVertex3f(b.x, b.y, b.z);
    glTexCoord2f(u1, v1); glVertex3f(c.x, c.y, c.z);
    glTexCoord2f(u0, v1); glVertex3f(d.x, d.y, d.z);
}
inline void yawXZ(float lx, float lz, float yaw, float& wx, float& wz) {
    float c = std::cos(yaw), s = std::sin(yaw);
    wx = lx * c + lz * s; wz = -lx * s + lz * c;
}
// Axis box with base-centre (cx,cy,cz), rotated by yaw. Sides use sideTex, top uses topTex.
inline void box(float cx, float cy, float cz, float sx, float sy, float sz, float yaw, GLuint sideTex, GLuint topTex,
                const Col& col, float tileM = 8.f) {
    auto P = [&](int ix, int iy, int iz) {
        float lx = (ix ? 0.5f : -0.5f) * sx, lz = (iz ? 0.5f : -0.5f) * sz, wx, wz;
        yawXZ(lx, lz, yaw, wx, wz);
        return V3(cx + wx, cy + (iy ? sy : 0.f), cz + wz);
    };
    setCol(col);
    glBindTexture(GL_TEXTURE_2D, sideTex);
    glBegin(GL_QUADS);
    tquad(P(0, 0, 0), P(1, 0, 0), P(1, 1, 0), P(0, 1, 0), 0, 0, sx / tileM, sy / tileM);
    tquad(P(1, 0, 1), P(0, 0, 1), P(0, 1, 1), P(1, 1, 1), 0, 0, sx / tileM, sy / tileM);
    tquad(P(0, 0, 1), P(0, 0, 0), P(0, 1, 0), P(0, 1, 1), 0, 0, sz / tileM, sy / tileM);
    tquad(P(1, 0, 0), P(1, 0, 1), P(1, 1, 1), P(1, 1, 0), 0, 0, sz / tileM, sy / tileM);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, topTex);
    glBegin(GL_QUADS);
    tquad(P(0, 1, 0), P(1, 1, 0), P(1, 1, 1), P(0, 1, 1), 0, 0, sx / tileM, sz / tileM);
    glEnd();
}
// body of revolution around Y. profile = (radius, height) bottom -> top.
inline void lathe(float cx, float cy, float cz, const float (*prof)[2], int n, int seg, float scale, float yscale,
                  const Col& col, float uTiles = 2.f, float vTiles = 1.f) {
    setCol(col);
    glBegin(GL_QUADS);
    for (int i = 0; i + 1 < n; i++)
        for (int k = 0; k < seg; k++) {
            float a0 = TAU * k / seg, a1 = TAU * (k + 1) / seg;
            float r0 = prof[i][0] * scale, y0 = prof[i][1] * yscale, r1 = prof[i + 1][0] * scale, y1 = prof[i + 1][1] * yscale;
            V3 A(cx + std::cos(a0) * r0, cy + y0, cz + std::sin(a0) * r0);
            V3 B(cx + std::cos(a1) * r0, cy + y0, cz + std::sin(a1) * r0);
            V3 C(cx + std::cos(a1) * r1, cy + y1, cz + std::sin(a1) * r1);
            V3 D(cx + std::cos(a0) * r1, cy + y1, cz + std::sin(a0) * r1);
            V3 nrm = norm3(cross3(B - A, D - A));
            if (len3(cross3(B - A, D - A)) < 1e-6f) nrm = norm3(cross3(B - A, C - A));
            // make sure the normal faces outwards
            V3 mid((A.x + C.x) * 0.5f - cx, 0, (A.z + C.z) * 0.5f - cz);
            if (dot3(nrm, mid) < 0) nrm = nrm * -1.f;
            glNormal3f(nrm.x, nrm.y, nrm.z);
            float u0 = uTiles * k / seg, u1 = uTiles * (k + 1) / seg, v0 = vTiles * i / (n - 1), v1 = vTiles * (i + 1) / (n - 1);
            glTexCoord2f(u0, v0); glVertex3f(A.x, A.y, A.z);
            glTexCoord2f(u1, v0); glVertex3f(B.x, B.y, B.z);
            glTexCoord2f(u1, v1); glVertex3f(C.x, C.y, C.z);
            glTexCoord2f(u0, v1); glVertex3f(D.x, D.y, D.z);
        }
    glEnd();
}
// flat ground-aligned quad (shadow blobs, light pools)
inline void groundQuad(float cx, float cz, float y, float hx, float hz, float yaw) {
    float c = std::cos(yaw), s = std::sin(yaw);
    auto V = [&](float lx, float lz, float u, float v) {
        glTexCoord2f(u, v);
        glVertex3f(cx + lx * c + lz * s, y, cz - lx * s + lz * c);
    };
    glNormal3f(0, 1, 0);
    V(-hx, -hz, 0, 0); V(hx, -hz, 1, 0); V(hx, hz, 1, 1); V(-hx, hz, 0, 1);
}
}  // namespace g3

struct CarSec { float z, y0, y1, hb, ht; };

class World {
public:
    Track track;
    Tx tx;
    GLuint L_ground = 0, L_road = 0, L_scene = 0, L_shadows = 0, L_pools = 0, L_sky = 0, L_body = 0, L_wheel = 0;
    std::vector<Lamp> lamps;
    std::vector<float> minimapX, minimapZ;
    int startIdx = 0;

    // --------------------------------------------------------------- textures
    void makeTexturesA() {
        auto up = [](const Img& im, bool mip, bool repeat, bool lin) { return uploadTexture(im.d, im.w, im.h, mip, repeat, lin); };
        tx.asphalt = up(tex::asphalt(), true, true, false);
        tx.grass = up(tex::grass(), true, true, false);
        tx.gravel = up(tex::gravel(), true, true, false);
        tx.kerb = up(tex::kerb(), true, true, false);
        tx.rail = up(tex::rail(), true, true, false);
        tx.foliage = up(tex::foliage(), true, true, false);
        tx.bark = up(tex::bark(), true, true, false);
    }
    void makeTexturesB() {
        auto up = [](const Img& im, bool mip, bool repeat, bool lin) { return uploadTexture(im.d, im.w, im.h, mip, repeat, lin); };
        tx.wall1 = up(tex::wall1(), true, true, false);
        tx.wall2 = up(tex::wall2(), true, true, false);
        tx.roof = up(tex::roof(), true, true, false);
        tx.seats = up(tex::seats(), true, true, false);
        tx.wheel = up(tex::wheel(), true, false, false);
        tx.glow = up(tex::glow(), false, false, true);
        tx.shadow = up(tex::shadow(), false, false, true);
        tx.checker = up(tex::checker(), true, true, false);
        tx.banner = up(makeBanner(), false, false, false);
        tx.metal = up(tex::metalBg(), false, true, true);
        Img w(2, 2); w.fill(255, 255, 255);
        tx.white = up(w, false, true, false);
    }

    // --------------------------------------------------------------- track
    void makeTrack() {
        track.build();
        for (int i = 0; i < track.N(); i += 3) { minimapX.push_back(track.p[i].x); minimapZ.push_back(track.p[i].z); }
    }

    float distToTrack(float x, float z) const {
        float best = 1e30f;
        for (auto& q : track.p) {
            float dx = x - q.x, dz = z - q.z;
            best = std::min(best, dx * dx + dz * dz);
        }
        return std::sqrt(best);
    }

    // --------------------------------------------------------------- ground, road, rails
    void makeRoad() {
        const Track& T = track;
        const int N = T.N();
        const float hw = T.halfWidth, kw = T.kerbWidth, bo = T.barrierOff;
        float tile = T.length / std::max(1, (int)std::floor(T.length / 12.f + 0.5f));
        std::vector<float> ac(N, 0.f);
        for (int i = 0; i < N; i++) {
            float s = 0;
            for (int k = -6; k <= 6; k++) s += std::fabs(T.p[((i + k) % N + N) % N].curv);
            ac[i] = s / 13.f;
        }
        std::vector<char> kerbOn(N), gravOn(N, 0);
        for (int i = 0; i < N; i++) kerbOn[i] = ac[i] > 1.f / 230.f;
        for (int i = 0; i < N; i++)
            for (int k = -10; k <= 10; k++) if (kerbOn[((i + k) % N + N) % N]) { gravOn[i] = 1; break; }

        // ground: subdivided so per-vertex fog implementations behave
        L_ground = glGenLists(1);
        glNewList(L_ground, GL_COMPILE);
        {
            glBindTexture(GL_TEXTURE_2D, tx.grass);
            setCol(Col(1, 1, 1));
            float x0 = T.minX - 1200, x1 = T.maxX + 1200, z0 = T.minZ - 1200, z1 = T.maxZ + 1200;
            const int G = 48;
            glBegin(GL_QUADS);
            glNormal3f(0, 1, 0);
            for (int a = 0; a < G; a++)
                for (int b = 0; b < G; b++) {
                    float xa = lerpf(x0, x1, (float)a / G), xb = lerpf(x0, x1, (float)(a + 1) / G);
                    float za = lerpf(z0, z1, (float)b / G), zb = lerpf(z0, z1, (float)(b + 1) / G);
                    float s = 1.f / 9.f;
                    glTexCoord2f(xa * s, za * s); glVertex3f(xa, 0, za);
                    glTexCoord2f(xb * s, za * s); glVertex3f(xb, 0, za);
                    glTexCoord2f(xb * s, zb * s); glVertex3f(xb, 0, zb);
                    glTexCoord2f(xa * s, zb * s); glVertex3f(xa, 0, zb);
                }
            glEnd();
        }
        glEndList();

        L_road = glGenLists(1);
        glNewList(L_road, GL_COMPILE);
        {
            // gravel run-off
            glBindTexture(GL_TEXTURE_2D, tx.gravel);
            setCol(Col(1, 1, 1));
            glBegin(GL_QUADS);
            glNormal3f(0, 1, 0);
            for (int i = 0; i < N; i++) {
                if (!gravOn[i]) continue;
                const TrackPoint& a = T.p[i]; const TrackPoint& b = T.p[(i + 1) % N];
                for (int sd = -1; sd <= 1; sd += 2) {
                    float o0 = sd * (hw + kw - 0.2f), o1 = sd * bo;
                    float va = i * T.ds / 6.f, vb = (i + 1) * T.ds / 6.f;
                    glTexCoord2f(0, va); glVertex3f(a.x + a.nx * o0, 0.02f, a.z + a.nz * o0);
                    glTexCoord2f(1.6f, va); glVertex3f(a.x + a.nx * o1, 0.02f, a.z + a.nz * o1);
                    glTexCoord2f(1.6f, vb); glVertex3f(b.x + b.nx * o1, 0.02f, b.z + b.nz * o1);
                    glTexCoord2f(0, vb); glVertex3f(b.x + b.nx * o0, 0.02f, b.z + b.nz * o0);
                }
            }
            glEnd();
            // asphalt
            glBindTexture(GL_TEXTURE_2D, tx.asphalt);
            glBegin(GL_QUADS);
            glNormal3f(0, 1, 0);
            for (int i = 0; i < N; i++) {
                const TrackPoint& a = T.p[i]; const TrackPoint& b = T.p[(i + 1) % N];
                float va = i * T.ds / tile, vb = (i + 1) * T.ds / tile;
                glTexCoord2f(0, va); glVertex3f(a.x - a.nx * hw, 0.05f, a.z - a.nz * hw);
                glTexCoord2f(1, va); glVertex3f(a.x + a.nx * hw, 0.05f, a.z + a.nz * hw);
                glTexCoord2f(1, vb); glVertex3f(b.x + b.nx * hw, 0.05f, b.z + b.nz * hw);
                glTexCoord2f(0, vb); glVertex3f(b.x - b.nx * hw, 0.05f, b.z - b.nz * hw);
            }
            glEnd();
            // kerbs
            glBindTexture(GL_TEXTURE_2D, tx.kerb);
            glBegin(GL_QUADS);
            glNormal3f(0, 1, 0);
            for (int i = 0; i < N; i++) {
                if (!kerbOn[i]) continue;
                const TrackPoint& a = T.p[i]; const TrackPoint& b = T.p[(i + 1) % N];
                for (int sd = -1; sd <= 1; sd += 2) {
                    float o0 = sd * (hw - 0.05f), o1 = sd * (hw + kw);
                    float va = i * T.ds / 3.f, vb = (i + 1) * T.ds / 3.f;
                    glTexCoord2f(0, va); glVertex3f(a.x + a.nx * o0, 0.075f, a.z + a.nz * o0);
                    glTexCoord2f(1, va); glVertex3f(a.x + a.nx * o1, 0.075f, a.z + a.nz * o1);
                    glTexCoord2f(1, vb); glVertex3f(b.x + b.nx * o1, 0.075f, b.z + b.nz * o1);
                    glTexCoord2f(0, vb); glVertex3f(b.x + b.nx * o0, 0.075f, b.z + b.nz * o0);
                }
            }
            glEnd();
            // start / finish chequered line
            {
                const TrackPoint& c = T.p[startIdx];
                glBindTexture(GL_TEXTURE_2D, tx.checker);
                glBegin(GL_QUADS);
                glNormal3f(0, 1, 0);
                float h = 0.75f;
                glTexCoord2f(0, 0); glVertex3f(c.x - c.nx * hw - c.tx * h, 0.07f, c.z - c.nz * hw - c.tz * h);
                glTexCoord2f(hw * 2 / 3.f, 0); glVertex3f(c.x + c.nx * hw - c.tx * h, 0.07f, c.z + c.nz * hw - c.tz * h);
                glTexCoord2f(hw * 2 / 3.f, 0.5f); glVertex3f(c.x + c.nx * hw + c.tx * h, 0.07f, c.z + c.nz * hw + c.tz * h);
                glTexCoord2f(0, 0.5f); glVertex3f(c.x - c.nx * hw + c.tx * h, 0.07f, c.z - c.nz * hw + c.tz * h);
                glEnd();
            }
            // guard rails
            glBindTexture(GL_TEXTURE_2D, tx.rail);
            setCol(Col(1, 1, 1));
            glBegin(GL_QUADS);
            for (int i = 0; i < N; i++) {
                const TrackPoint& a = T.p[i]; const TrackPoint& b = T.p[(i + 1) % N];
                for (int sd = -1; sd <= 1; sd += 2) {
                    float o = sd * bo;
                    V3 pa(a.x + a.nx * o, 0.25f, a.z + a.nz * o), pb(b.x + b.nx * o, 0.25f, b.z + b.nz * o);
                    V3 qa(pa.x, 1.0f, pa.z), qb(pb.x, 1.0f, pb.z);
                    float ua = i * T.ds / 4.f, ub = (i + 1) * T.ds / 4.f;
                    g3::tquad(pa, pb, qb, qa, ua, 0, ub, 1);
                }
            }
            glEnd();
            // posts
            for (int i = 0; i < N; i += 3) {
                const TrackPoint& a = T.p[i];
                for (int sd = -1; sd <= 1; sd += 2) {
                    float o = sd * (bo + 0.15f);
                    g3::box(a.x + a.nx * o, 0, a.z + a.nz * o, 0.16f, 1.05f, 0.16f, std::atan2(a.tx, a.tz), tx.white, tx.white, Col(0.25f, 0.26f, 0.3f));
                }
            }
        }
        glEndList();
    }

    // --------------------------------------------------------------- scenery
    void makeScenery() {
        const Track& T = track;
        const int N = T.N();
        Rng rng(2002);
        struct Tree { float x, z, s; int kind; Col tint; };
        std::vector<Tree> trees;
        struct Disc { float x, z, r; };
        std::vector<Disc> keepOut;

        // big structures -------------------------------------------------
        L_scene = glGenLists(1);
        L_shadows = glGenLists(1);
        std::vector<std::pair<V3, V3>> bshadows;  // centre, half extents (unused rotation) for blob shadows
        glNewList(L_scene, GL_COMPILE);
        setCol(Col(1, 1, 1));
        {
            auto pt = [&](int idx) { return T.p[((idx % N) + N) % N]; };
            // grandstand on the outside (right) of the start straight
            {
                int ci = startIdx + 22;
                const TrackPoint& c = pt(ci);
                float yaw = std::atan2(c.tx, c.tz);
                float off = 27.f, len = 96.f;
                float bx = c.x + c.nx * off, bz = c.z + c.nz * off;
                // steps rise away from the track
                for (int s = 0; s < 6; s++) {
                    float depth = 3.0f, h = 1.3f * (s + 1);
                    float o = off + s * depth + depth * 0.5f;
                    g3::box(c.x + c.nx * o, 0, c.z + c.nz * o, depth, h, len, yaw, tx.wall2, tx.seats, Col(0.9f, 0.9f, 0.9f), 10.f);
                }
                // canopy
                float oc = off + 9.f;
                for (int sx = -1; sx <= 1; sx += 2)
                    for (int k = 0; k < 2; k++) {
                        float lo = (k == 0 ? off + 2.f : off + 16.f);
                        float lx = sx * (len * 0.5f - 1.5f);
                        g3::box(c.x + c.nx * lo + c.tx * lx, 0, c.z + c.nz * lo + c.tz * lx, 0.4f, 11.f, 0.4f, yaw, tx.rail, tx.rail, Col(0.8f, 0.8f, 0.85f));
                    }
                g3::box(c.x + c.nx * oc, 11.f, c.z + c.nz * oc, 17.f, 0.5f, len + 2.f, yaw, tx.roof, tx.roof, Col(0.75f, 0.75f, 0.8f), 6.f);
                keepOut.push_back({bx, bz, 60});
                bshadows.push_back({V3(c.x + c.nx * (off + 8), 0, c.z + c.nz * (off + 8)), V3(len * 0.5f, 0, 14)});
            }
            // pit building + race control tower on the infield (left) side
            {
                int ci = startIdx - 10;
                const TrackPoint& c = pt(ci);
                float yaw = std::atan2(c.tx, c.tz);
                float off = -30.f;
                g3::box(c.x + c.nx * off, 0, c.z + c.nz * off, 11.f, 5.2f, 120.f, yaw, tx.wall1, tx.roof, Col(1, 0.97f, 0.95f), 12.f);
                g3::box(c.x + c.nx * (off + 5.5f), 3.6f, c.z + c.nz * (off + 5.5f), 4.f, 0.35f, 120.f, yaw, tx.roof, tx.roof, Col(0.5f, 0.55f, 0.7f), 6.f);  // awning
                const TrackPoint& t = pt(startIdx + 6);
                float yt = std::atan2(t.tx, t.tz);
                g3::box(t.x + t.nx * (off - 3.f), 0, t.z + t.nz * (off - 3.f), 7.f, 15.f, 7.f, yt, tx.wall1, tx.roof, Col(0.95f, 0.95f, 1), 10.f);
                g3::box(t.x + t.nx * (off - 3.f), 15.f, t.z + t.nz * (off - 3.f), 9.f, 3.f, 9.f, yt, tx.wall1, tx.roof, Col(0.55f, 0.65f, 0.85f), 9.f);
                keepOut.push_back({c.x + c.nx * off, c.z + c.nz * off, 75});
                bshadows.push_back({V3(c.x + c.nx * (off + 2), 0, c.z + c.nz * (off + 2)), V3(60, 0, 8)});
            }
            // start / finish gantry
            {
                const TrackPoint& c = T.p[startIdx];
                float yaw = std::atan2(c.tx, c.tz);
                float half = T.halfWidth + 2.4f;
                for (int sd = -1; sd <= 1; sd += 2)
                    g3::box(c.x + c.nx * half * sd, 0, c.z + c.nz * half * sd, 0.8f, 9.6f, 0.8f, yaw, tx.rail, tx.rail, Col(0.7f, 0.72f, 0.8f));
                g3::box(c.x, 9.f, c.z, half * 2 + 0.8f, 1.1f, 1.0f, yaw, tx.rail, tx.rail, Col(0.6f, 0.62f, 0.7f));
                glBindTexture(GL_TEXTURE_2D, tx.banner);
                setCol(Col(1, 1, 1));
                glBegin(GL_QUADS);
                float bh = (half * 2.f) / 8.f;
                for (int side = -1; side <= 1; side += 2) {
                    float t = 0.55f * side;
                    V3 a(c.x - c.nx * half + c.tx * t, 9.0f - bh * 0.1f, c.z - c.nz * half + c.tz * t);
                    V3 b(c.x + c.nx * half + c.tx * t, 9.0f - bh * 0.1f, c.z + c.nz * half + c.tz * t);
                    V3 cc(b.x, 9.0f + bh * 0.9f, b.z), d(a.x, 9.0f + bh * 0.9f, a.z);
                    if (side > 0) g3::tquad(b, a, d, cc, 0, 1, 1, 0);  // flip so text reads correctly from the approach direction
                    else g3::tquad(a, b, cc, d, 0, 1, 1, 0);
                }
                glEnd();
            }
            // houses / sheds in the countryside
            struct HouseSpec { float x, z, yaw, w, d, h; int kind; };
            std::vector<HouseSpec> houses;
            for (int attempt = 0; attempt < 900 && (int)houses.size() < 14; attempt++) {
                float x = rng.r(T.minX - 130, T.maxX + 130), z = rng.r(T.minZ - 130, T.maxZ + 130);
                if (distToTrack(x, z) < 48.f) continue;
                bool bad = false;
                for (auto& k : keepOut) if (std::hypot(x - k.x, z - k.z) < k.r) bad = true;
                for (auto& h : houses) if (std::hypot(x - h.x, z - h.z) < 40.f) bad = true;
                if (bad) continue;
                HouseSpec h{x, z, rng.r(0, TAU), rng.r(7, 13), rng.r(6, 9), rng.r(4.5f, 7.f), (int)(rng.u() % 2)};
                houses.push_back(h);
                keepOut.push_back({x, z, 18});
            }
            for (auto& h : houses) {
                g3::box(h.x, 0, h.z, h.w, h.h, h.d, h.yaw, h.kind ? tx.wall2 : tx.wall1, tx.roof, Col(1, 0.97f, 0.95f), 8.f);
                // gable roof
                float c = std::cos(h.yaw), s = std::sin(h.yaw);
                auto W = [&](float lx, float lz, float y) { return V3(h.x + lx * c + lz * s, y, h.z - lx * s + lz * c); };
                float rh = 2.4f, ov = 0.5f, hw2 = h.w * 0.5f + ov, hd2 = h.d * 0.5f + ov;
                setCol(Col(1, 0.9f, 0.85f));
                glBindTexture(GL_TEXTURE_2D, tx.roof);
                glBegin(GL_QUADS);
                g3::tquad(W(-hw2, -hd2, h.h), W(hw2, -hd2, h.h), W(hw2, 0, h.h + rh), W(-hw2, 0, h.h + rh), 0, 0, 3, 1);
                g3::tquad(W(hw2, hd2, h.h), W(-hw2, hd2, h.h), W(-hw2, 0, h.h + rh), W(hw2, 0, h.h + rh), 0, 0, 3, 1);
                glEnd();
                glBindTexture(GL_TEXTURE_2D, h.kind ? tx.wall2 : tx.wall1);
                glBegin(GL_TRIANGLES);
                for (int sd = -1; sd <= 1; sd += 2) {
                    V3 a = W(sd * h.w * 0.5f, -h.d * 0.5f, h.h), b = W(sd * h.w * 0.5f, h.d * 0.5f, h.h), t = W(sd * h.w * 0.5f, 0, h.h + rh);
                    V3 n = norm3(cross3(b - a, t - a));
                    glNormal3f(n.x, n.y, n.z);
                    glTexCoord2f(0, 0); glVertex3f(a.x, a.y, a.z);
                    glTexCoord2f(1, 0); glVertex3f(b.x, b.y, b.z);
                    glTexCoord2f(0.5f, 0.3f); glVertex3f(t.x, t.y, t.z);
                }
                glEnd();
                bshadows.push_back({V3(h.x, 0, h.z), V3(h.w * 0.7f, 0, h.d * 0.7f)});
            }

            // street lamps behind the guard rails --------------------------
            lamps.clear();
            int lampStep = 10;
            for (int i = 3; i < N; i += lampStep) {
                const TrackPoint& a = T.p[i];
                int sd = ((i / lampStep) % 2) ? 1 : -1;
                float o = sd * (T.barrierOff + 2.2f);
                float bx = a.x + a.nx * o, bz = a.z + a.nz * o;
                float yaw = std::atan2(a.tx, a.tz);
                g3::box(bx, 0, bz, 0.28f, 9.f, 0.28f, yaw, tx.white, tx.white, Col(0.32f, 0.34f, 0.38f));
                float dx = -a.nx * sd, dz = -a.nz * sd, armYaw = std::atan2(dx, dz);
                float ax = bx + dx * 1.3f, az = bz + dz * 1.3f;
                g3::box(bx + dx * 0.65f, 8.9f, bz + dz * 0.65f, 0.2f, 0.2f, 1.4f, armYaw, tx.white, tx.white, Col(0.32f, 0.34f, 0.38f));
                g3::box(ax, 8.75f, az, 0.55f, 0.2f, 1.1f, armYaw, tx.white, tx.white, Col(0.2f, 0.2f, 0.22f));
                lamps.push_back({V3(ax, 8.65f, az)});
            }

            // trees -------------------------------------------------------
            for (int attempt = 0; attempt < 9000 && (int)trees.size() < 1250; attempt++) {
                float x = rng.r(T.minX - 160, T.maxX + 160), z = rng.r(T.minZ - 160, T.maxZ + 160);
                float d = distToTrack(x, z);
                if (d < 22.f) continue;
                bool bad = false;
                for (auto& k : keepOut) if (std::hypot(x - k.x, z - k.z) < k.r) { bad = true; break; }
                if (bad) continue;
                float fn = fbm(x * 0.33f + 400, z * 0.33f + 400, 4096, 4096, 7);
                if (fn < 0.47f && rng.f() > 0.05f) continue;
                if (d > 60 && rng.f() < 0.35f) continue;
                Tree t;
                t.x = x; t.z = z; t.s = rng.r(0.8f, 1.5f);
                t.kind = (rng.u() % 100) < 58 ? 0 : 1;
                float pick = rng.f();
                if (t.kind == 1 && pick < 0.28f) t.tint = Col(1.9f, 1.15f, 0.5f);       // autumn orange
                else if (t.kind == 1 && pick < 0.5f) t.tint = Col(1.45f, 1.35f, 0.65f);  // yellow green
                else { float g = rng.r(0.85f, 1.25f); t.tint = Col(g * 0.9f, g, g * 0.9f); }
                trees.push_back(t);
            }
            static const float pine1[2][2] = {{2.7f, 1.4f}, {0.f, 4.4f}};
            static const float pine2[2][2] = {{2.1f, 3.0f}, {0.f, 6.0f}};
            static const float pine3[2][2] = {{1.4f, 4.9f}, {0.f, 7.9f}};
            static const float broad[5][2] = {{0.f, 2.6f}, {2.3f, 3.7f}, {2.7f, 5.3f}, {1.7f, 6.9f}, {0.f, 7.8f}};
            glBindTexture(GL_TEXTURE_2D, tx.foliage);
            for (auto& t : trees) {
                if (t.kind == 0) {
                    g3::lathe(t.x, 0, t.z, pine1, 2, 5, t.s, t.s, t.tint, 2, 1);
                    g3::lathe(t.x, 0, t.z, pine2, 2, 5, t.s, t.s, t.tint, 2, 1);
                    g3::lathe(t.x, 0, t.z, pine3, 2, 5, t.s, t.s, t.tint, 2, 1);
                } else {
                    g3::lathe(t.x, 0, t.z, broad, 5, 6, t.s, t.s, t.tint, 2, 2);
                }
            }
            glBindTexture(GL_TEXTURE_2D, tx.bark);
            static const float trunk[2][2] = {{0.28f, 0.f}, {0.2f, 3.2f}};
            for (auto& t : trees) g3::lathe(t.x, 0, t.z, trunk, 2, 4, t.s, t.s, Col(1, 1, 1), 2, 1);
        }
        glEndList();

        // blob shadows (alpha blended list)
        glNewList(L_shadows, GL_COMPILE);
        glBindTexture(GL_TEXTURE_2D, tx.shadow);
        glBegin(GL_QUADS);
        V3 sd = sunDir();
        float shx = -sd.x, shz = -sd.z, sl = std::sqrt(shx * shx + shz * shz);
        shx /= sl; shz /= sl;
        float yawS = std::atan2(shx, shz);
        for (auto& t : trees) {
            float len = (t.kind == 0 ? 6.5f : 5.0f) * t.s;
            setCol(Col(1, 1, 1, 0.55f));
            g3::groundQuad(t.x + shx * len * 0.55f, t.z + shz * len * 0.55f, 0.03f, 1.7f * t.s, len * 0.9f, yawS);
        }
        for (auto& b : bshadows) {
            float hx = b.second.x, hz = b.second.z;
            setCol(Col(1, 1, 1, 0.5f));
            g3::groundQuad(b.first.x + shx * 8, b.first.z + shz * 8, 0.03f, std::max(hx, hz) * 0.85f, std::max(hx, hz) * 0.85f, yawS);
        }
        glEnd();
        glEndList();

        // light pools of the street lamps (additive list)
        L_pools = glGenLists(1);
        glNewList(L_pools, GL_COMPILE);
        glBindTexture(GL_TEXTURE_2D, tx.glow);
        glBegin(GL_QUADS);
        for (int li = 0; li < (int)lamps.size(); li++) {
            const V3& h = lamps[li].head;
            TrackLoc loc = T.locate(h.x, h.z, 0, -1);
            float sdir = loc.d > 0 ? -1.f : 1.f;  // towards the road
            float px = h.x + loc.nx * sdir * 9.f, pz = h.z + loc.nz * sdir * 9.f;
            setCol(Col(1.0f, 0.78f, 0.45f, 0.55f));
            g3::groundQuad(px, pz, 0.11f, 11.f, 11.f, 0);
        }
        glEnd();
        glEndList();
    }

    // --------------------------------------------------------------- sky
    void makeSky() {
        L_sky = glGenLists(1);
        glNewList(L_sky, GL_COMPILE);
        glDisable(GL_TEXTURE_2D);
        const Col hor = horizonCol(), mid(0.46f, 0.34f, 0.60f), top(0.08f, 0.12f, 0.34f), sunC(1.0f, 0.62f, 0.26f);
        V3 sd = sunDir();
        float sh = std::sqrt(sd.x * sd.x + sd.z * sd.z);
        const int seg = 24;
        const float elev[7] = {-0.2f, 0.0f, 0.07f, 0.2f, 0.45f, 0.75f, 1.0f};  // fraction of 90 degrees
        const float R = 600.f;
        auto colorAt = [&](float e, float az) {
            float t = clampf(e, 0.f, 1.f);
            Col c;
            if (t < 0.18f) { float k = t / 0.18f; c = Col(lerpf(hor.r, mid.r, k), lerpf(hor.g, mid.g, k), lerpf(hor.b, mid.b, k)); }
            else { float k = sat((t - 0.18f) / 0.82f); k = std::sqrt(k); c = Col(lerpf(mid.r, top.r, k), lerpf(mid.g, top.g, k), lerpf(mid.b, top.b, k)); }
            float cs = std::cos(az) * sd.x / sh + std::sin(az) * sd.z / sh;  // alignment with sun azimuth
            float boost = std::pow(std::max(0.f, cs), 3.f) * std::pow(1.f - t, 2.5f);
            c.r = lerpf(c.r, sunC.r, boost * 0.85f); c.g = lerpf(c.g, sunC.g, boost * 0.8f); c.b = lerpf(c.b, sunC.b, boost * 0.7f);
            return c;
        };
        glBegin(GL_QUADS);
        for (int i = 0; i < 6; i++)
            for (int k = 0; k < seg; k++) {
                float a0 = TAU * k / seg, a1 = TAU * (k + 1) / seg;
                auto vtx = [&](int ring, float az) {
                    float e = elev[ring] * PI * 0.5f;
                    Col c = colorAt(elev[ring], az);
                    glColor3f(c.r, c.g, c.b);
                    glVertex3f(std::cos(az) * std::cos(e) * R, std::sin(e) * R, std::sin(az) * std::cos(e) * R);
                };
                vtx(i, a0); vtx(i, a1); vtx(i + 1, a1); vtx(i + 1, a0);
            }
        glEnd();
        // two layers of distant hills
        Rng rng(77);
        for (int layer = 0; layer < 2; layer++) {
            float r = layer == 0 ? 560.f : 470.f;
            Col peak = layer == 0 ? Col(0.34f, 0.26f, 0.44f) : Col(0.16f, 0.13f, 0.26f);
            const int n = 72;
            std::vector<float> h(n + 1);
            for (int i = 0; i < n; i++) {
                float a = TAU * i / n;
                h[i] = 18.f + 55.f * fbm(a * 120.f + layer * 77, 5.f, 1 << 20, 1 << 20, 99 + layer) + rng.r(0, 18);
                h[i] *= layer == 0 ? 1.5f : 1.0f;
            }
            h[n] = h[0];
            glBegin(GL_QUADS);
            for (int i = 0; i < n; i++) {
                float a0 = TAU * i / n, a1 = TAU * (i + 1) / n;
                glColor3f(hor.r * 0.9f, hor.g * 0.8f, hor.b * 0.85f);
                glVertex3f(std::cos(a0) * r, -10, std::sin(a0) * r);
                glVertex3f(std::cos(a1) * r, -10, std::sin(a1) * r);
                glColor3f(peak.r, peak.g, peak.b);
                glVertex3f(std::cos(a1) * r, h[i + 1], std::sin(a1) * r);
                glVertex3f(std::cos(a0) * r, h[i], std::sin(a0) * r);
            }
            glEnd();
        }
        glEnable(GL_TEXTURE_2D);
        glEndList();
    }

    // --------------------------------------------------------------- the car
    // lofted body made of cross sections; front is +Z
    static void loft(const CarSec* s, int n, const Col& c, bool topOnly = false, float topHW = -1.f, float dy = 0.f, bool caps = true) {
        setCol(c);
        float cy = 0.7f;
        auto quad = [&](V3 a, V3 b, V3 cc, V3 d) {
            V3 nrm = norm3(cross3(b - a, d - a));
            V3 ctr((a.x + cc.x) * 0.5f, (a.y + cc.y) * 0.5f - cy, (a.z + cc.z) * 0.5f);
            ctr.z = 0;
            if (dot3(nrm, ctr) < 0) nrm = nrm * -1.f;
            glNormal3f(nrm.x, nrm.y, nrm.z);
            glVertex3f(a.x, a.y, a.z); glVertex3f(b.x, b.y, b.z); glVertex3f(cc.x, cc.y, cc.z); glVertex3f(d.x, d.y, d.z);
        };
        glBegin(GL_QUADS);
        for (int i = 0; i + 1 < n; i++) {
            const CarSec& a = s[i]; const CarSec& b = s[i + 1];
            float ta = topHW > 0 ? topHW : a.ht, tb = topHW > 0 ? topHW : b.ht;
            V3 aTL(-ta, a.y1 + dy, a.z), aTR(ta, a.y1 + dy, a.z), bTL(-tb, b.y1 + dy, b.z), bTR(tb, b.y1 + dy, b.z);
            quad(aTL, aTR, bTR, bTL);
            if (topOnly) continue;
            V3 aBL(-a.hb, a.y0, a.z), aBR(a.hb, a.y0, a.z), bBL(-b.hb, b.y0, b.z), bBR(b.hb, b.y0, b.z);
            quad(aBR, bBR, bTR, aTR);
            quad(aBL, aTL, bTL, bBL);
        }
        if (!topOnly && caps) {
            const CarSec& a = s[0]; const CarSec& b = s[n - 1];
            quad(V3(-a.hb, a.y0, a.z), V3(a.hb, a.y0, a.z), V3(a.ht, a.y1, a.z), V3(-a.ht, a.y1, a.z));
            quad(V3(b.hb, b.y0, b.z), V3(-b.hb, b.y0, b.z), V3(-b.ht, b.y1, b.z), V3(b.ht, b.y1, b.z));
        }
        glEnd();
    }

    void makeCar() {
        L_body = glGenLists(1);
        glNewList(L_body, GL_COMPILE);
        glDisable(GL_TEXTURE_2D);
        const Col paint(0.80f, 0.06f, 0.07f), glass(0.06f, 0.09f, 0.14f), dark(0.05f, 0.05f, 0.06f), white(0.93f, 0.93f, 0.95f),
            chrome(0.62f, 0.64f, 0.70f);
        static const CarSec lower[] = {
            {-2.20f, 0.34f, 0.86f, 0.80f, 0.76f}, {-1.90f, 0.30f, 0.98f, 0.92f, 0.88f}, {-0.90f, 0.28f, 0.96f, 0.94f, 0.90f},
            {0.90f, 0.28f, 0.91f, 0.94f, 0.90f},  {1.60f, 0.30f, 0.76f, 0.91f, 0.85f},  {2.10f, 0.32f, 0.60f, 0.82f, 0.74f},
            {2.26f, 0.34f, 0.50f, 0.72f, 0.64f}};
        loft(lower, 7, paint);
        static const CarSec cabin[] = {{-1.40f, 0.95f, 1.06f, 0.74f, 0.62f}, {-0.80f, 0.95f, 1.30f, 0.80f, 0.64f},
                                       {0.10f, 0.95f, 1.30f, 0.80f, 0.64f}, {0.98f, 0.93f, 1.00f, 0.82f, 0.76f}};
        loft(cabin, 4, glass);
        static const CarSec roofS[] = {{-0.76f, 0.f, 1.30f, 0.f, 0.58f}, {0.06f, 0.f, 1.30f, 0.f, 0.58f}};
        loft(roofS, 2, paint, true, 0.58f, 0.012f);
        // twin racing stripes on hood, roof and deck
        static const CarSec top[] = {{-2.2f, 0, 0.86f, 0, 0}, {-1.9f, 0, 0.98f, 0, 0}, {-0.9f, 0, 0.96f, 0, 0}, {0.9f, 0, 0.91f, 0, 0}, {1.6f, 0, 0.76f, 0, 0}, {2.1f, 0, 0.60f, 0, 0}, {2.26f, 0, 0.50f, 0, 0}};
        for (int sx = -1; sx <= 1; sx += 2) {
            glPushMatrix(); glTranslatef(sx * 0.2f, 0, 0);
            loft(top, 7, white, true, 0.07f, 0.014f);
            glPopMatrix();
        }
        // trim: skirts, splitter, diffuser, wing, exhausts
        auto B = [&](float cx, float cy2, float cz, float sx, float sy, float sz, const Col& c) {
            g3::box(cx, cy2, cz, sx, sy, sz, 0, tx.white, tx.white, c);
        };
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tx.white);
        B(0.93f, 0.2f, 0.0f, 0.1f, 0.16f, 3.0f, dark); B(-0.93f, 0.2f, 0.0f, 0.1f, 0.16f, 3.0f, dark);
        B(0.f, 0.25f, 2.22f, 1.55f, 0.07f, 0.34f, dark);                      // front splitter
        B(0.f, 0.22f, -2.15f, 1.40f, 0.14f, 0.3f, dark);                      // diffuser
        B(0.f, 1.16f, -2.02f, 1.72f, 0.05f, 0.42f, paint);                    // wing
        B(0.f, 1.2f, -2.02f, 1.72f, 0.025f, 0.43f, white);
        B(0.82f, 1.03f, -2.02f, 0.05f, 0.24f, 0.46f, dark); B(-0.82f, 1.03f, -2.02f, 0.05f, 0.24f, 0.46f, dark);
        B(0.5f, 0.88f, -1.92f, 0.07f, 0.28f, 0.1f, dark); B(-0.5f, 0.88f, -1.92f, 0.07f, 0.28f, 0.1f, dark);
        B(0.38f, 0.30f, -2.28f, 0.14f, 0.12f, 0.14f, chrome); B(-0.38f, 0.30f, -2.28f, 0.14f, 0.12f, 0.14f, chrome);
        B(0.86f, 0.95f, 0.80f, 0.12f, 0.1f, 0.2f, paint); B(-0.86f, 0.95f, 0.80f, 0.12f, 0.1f, 0.2f, paint);   // mirrors
        B(0.f, 0.43f, 2.26f, 0.5f, 0.12f, 0.03f, dark);                        // front plate area
        B(0.f, 0.48f, -2.24f, 0.5f, 0.13f, 0.03f, white);                      // rear plate
        glEndList();

        // wheel: axis along X, outer face at +X
        L_wheel = glGenLists(1);
        glNewList(L_wheel, GL_COMPILE);
        const float R = 0.34f, W2 = 0.13f;
        const int seg = 8;   // octagonal wheels, very 1997
        glBindTexture(GL_TEXTURE_2D, tx.white);
        setCol(Col(0.07f, 0.07f, 0.08f));
        glBegin(GL_QUADS);
        for (int k = 0; k < seg; k++) {
            float a0 = TAU * k / seg, a1 = TAU * (k + 1) / seg;
            float y0 = std::cos(a0) * R, z0 = std::sin(a0) * R, y1 = std::cos(a1) * R, z1 = std::sin(a1) * R;
            float am = (a0 + a1) * 0.5f;
            glNormal3f(0, std::cos(am), std::sin(am));
            glVertex3f(-W2, y0, z0); glVertex3f(W2, y0, z0); glVertex3f(W2, y1, z1); glVertex3f(-W2, y1, z1);
        }
        glEnd();
        glBindTexture(GL_TEXTURE_2D, tx.wheel);
        setCol(Col(1, 1, 1));
        glBegin(GL_TRIANGLE_FAN);
        glNormal3f(1, 0, 0);
        glTexCoord2f(0.5f, 0.5f); glVertex3f(W2 + 0.005f, 0, 0);
        for (int k = 0; k <= seg; k++) {
            float a = TAU * k / seg;
            glTexCoord2f(0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a));
            glVertex3f(W2 + 0.005f, std::cos(a) * R, std::sin(a) * R);
        }
        glEnd();
        glBindTexture(GL_TEXTURE_2D, tx.white);
        setCol(Col(0.04f, 0.04f, 0.045f));
        glBegin(GL_TRIANGLE_FAN);
        glNormal3f(-1, 0, 0);
        glVertex3f(-W2 - 0.005f, 0, 0);
        for (int k = 0; k <= seg; k++) { float a = TAU * k / seg; glVertex3f(-W2 - 0.005f, std::cos(a) * R, std::sin(a) * R); }
        glEnd();
        glEndList();
    }
};
