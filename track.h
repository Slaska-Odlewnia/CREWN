#pragma once
#include "util.h"

struct TrackPoint {
    float x, z;     // centerline position
    float tx, tz;   // unit tangent (direction of travel)
    float nx, nz;   // unit RIGHT normal (-tz, tx)
    float curv;     // signed curvature (positive = turning left), 1/m
};

struct TrackLoc {
    int i = 0;            // nearest sample
    float s = 0;          // distance along the lap [0, length)
    float d = 0;          // signed lateral offset, positive = right of the centerline
    float cx = 0, cz = 0; // closest centerline point
    float tx = 0, tz = 1; // tangent there
    float nx = 1, nz = 0; // right normal there
};

class Track {
public:
    std::vector<TrackPoint> p;
    float length = 0, ds = 4.f;
    float halfWidth = 7.5f;   // asphalt half width
    float kerbWidth = 1.4f;
    float barrierOff = 15.f;  // guard rail distance from the centerline
    float minX = 0, maxX = 0, minZ = 0, maxZ = 0;

    int N() const { return (int)p.size(); }

    void build() {
        static const float C[16][2] = {{0, 0},     {0, 170},   {70, 290},  {190, 330}, {300, 290}, {330, 200},
                                       {280, 150}, {190, 100}, {185, 20},  {300, -30}, {420, -110}, {390, -230},
                                       {280, -300}, {150, -280}, {70, -230}, {0, -170}};
        const int n = 16, K = 64;
        auto cr = [](float a, float b, float c, float d, float t) {
            return 0.5f * ((2 * b) + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t * t + (-a + 3 * b - 3 * c + d) * t * t * t);
        };
        std::vector<float> fx, fz;
        for (int i = 0; i < n; i++)
            for (int k = 0; k < K; k++) {
                float t = (float)k / K;
                const float* p0 = C[(i + n - 1) % n]; const float* p1 = C[i];
                const float* p2 = C[(i + 1) % n]; const float* p3 = C[(i + 2) % n];
                fx.push_back(cr(p0[0], p1[0], p2[0], p3[0], t));
                fz.push_back(cr(p0[1], p1[1], p2[1], p3[1], t));
            }
        int M = (int)fx.size();
        std::vector<float> cum(M + 1, 0.f);
        for (int i = 0; i < M; i++) {
            float dx = fx[(i + 1) % M] - fx[i], dz = fz[(i + 1) % M] - fz[i];
            cum[i + 1] = cum[i] + std::sqrt(dx * dx + dz * dz);
        }
        float L = cum[M];
        int cnt = std::max(8, (int)std::floor(L / 4.f + 0.5f));
        ds = L / cnt;
        length = L;
        p.assign(cnt, TrackPoint());
        int seg = 0;
        for (int j = 0; j < cnt; j++) {
            float s = j * ds;
            while (seg < M - 1 && cum[seg + 1] < s) seg++;
            float t = (s - cum[seg]) / std::max(1e-6f, cum[seg + 1] - cum[seg]);
            int a = seg, b = (seg + 1) % M;
            p[j].x = lerpf(fx[a], fx[b], t);
            p[j].z = lerpf(fz[a], fz[b], t);
        }
        for (int j = 0; j < cnt; j++) {
            const TrackPoint& a = p[(j + cnt - 1) % cnt];
            const TrackPoint& b = p[(j + 1) % cnt];
            float dx = b.x - a.x, dz = b.z - a.z, l = std::sqrt(dx * dx + dz * dz);
            p[j].tx = dx / l; p[j].tz = dz / l;
            p[j].nx = -p[j].tz; p[j].nz = p[j].tx;
        }
        for (int j = 0; j < cnt; j++) {
            const TrackPoint& a = p[j];
            const TrackPoint& b = p[(j + 1) % cnt];
            float cr2 = a.tx * b.tz - a.tz * b.tx;  // >0 means turning towards -x when going +z... sign fixed below
            float dot = a.tx * b.tx + a.tz * b.tz;
            float ang = std::atan2(cr2, dot);
            p[j].curv = -ang / ds;  // left turn (heading angle increasing) => positive
        }
        minX = maxX = p[0].x; minZ = maxZ = p[0].z;
        for (auto& q : p) { minX = std::min(minX, q.x); maxX = std::max(maxX, q.x); minZ = std::min(minZ, q.z); maxZ = std::max(maxZ, q.z); }
    }

    // window < 0 => search everything
    TrackLoc locate(float x, float z, int hint, int window) const {
        int n = N();
        int best = 0; float bd = 1e30f;
        if (window < 0 || window * 2 + 1 >= n) {
            for (int i = 0; i < n; i++) {
                float dx = x - p[i].x, dz = z - p[i].z, d2 = dx * dx + dz * dz;
                if (d2 < bd) { bd = d2; best = i; }
            }
        } else {
            for (int k = -window; k <= window; k++) {
                int i = ((hint + k) % n + n) % n;
                float dx = x - p[i].x, dz = z - p[i].z, d2 = dx * dx + dz * dz;
                if (d2 < bd) { bd = d2; best = i; }
            }
        }
        // refine on the two neighbouring segments
        TrackLoc r;
        r.i = best;
        float bestD2 = 1e30f;
        for (int side = -1; side <= 0; side++) {
            int ia = ((best + side) % n + n) % n, ib = (ia + 1) % n;
            const TrackPoint& a = p[ia]; const TrackPoint& b = p[ib];
            float sx = b.x - a.x, sz = b.z - a.z, l2 = sx * sx + sz * sz;
            float t = clampf(((x - a.x) * sx + (z - a.z) * sz) / std::max(1e-6f, l2), 0.f, 1.f);
            float cx = a.x + sx * t, cz = a.z + sz * t;
            float d2 = (x - cx) * (x - cx) + (z - cz) * (z - cz);
            if (d2 < bestD2) {
                bestD2 = d2;
                r.cx = cx; r.cz = cz;
                float tx = lerpf(a.tx, b.tx, t), tz = lerpf(a.tz, b.tz, t);
                float tl = std::sqrt(tx * tx + tz * tz); tx /= tl; tz /= tl;
                r.tx = tx; r.tz = tz; r.nx = -tz; r.nz = tx;
                float s = (ia + t) * ds;
                if (s >= length) s -= length;
                if (s < 0) s += length;
                r.s = s;
            }
        }
        r.d = (x - r.cx) * r.nx + (z - r.cz) * r.nz;
        return r;
    }
};
