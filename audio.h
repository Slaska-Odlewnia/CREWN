// CREWN - fully procedural audio: engine, tyres, collisions, UI sounds and a sequenced menu track.
// The Synth class has no SDL dependency (so it can be tested offline); Audio wraps it in an SDL device.
#pragma once
#include "util.h"

enum SfxType {
    SFX_COLLIDE, SFX_BEEP, SFX_GO, SFX_MOVE, SFX_SELECT, SFX_BACK, SFX_LAP, SFX_FINISH, SFX_SHIFT, SFX_COUNT
};

struct SynthParams {
    float rpm = 900.f;     // engine rpm
    float load = 0.f;      // throttle 0..1
    float speed = 0.f;     // m/s
    float slip = 0.f;      // tyre squeal 0..1
    float offroad = 0.f;   // 0..1 rumble on grass/gravel
    float brake = 0.f;     // 0..1
    float scrape = 0.f;    // barrier scraping 0..1
    bool engine = false;   // engine / race ambience audible
    bool music = false;    // menu music audible
    bool lofi = false;     // 1990s sound card: 22 kHz sample-and-hold, 8-bit quantisation
    float master = 0.8f, musicVol = 0.7f, sfxVol = 0.8f;
};

class Synth {
public:
    SynthParams p;
    int sr = 44100;

    void init(int sampleRate) {
        sr = sampleRate;
        const int T = 2048;
        wavDark.resize(T); wavBright.resize(T);
        float ph[12];
        for (int h = 0; h < 12; h++) ph[h] = 0.7f * h * h * 0.37f;
        for (int i = 0; i < T; i++) {
            float x = TAU * i / T, d = 0, b = 0;
            for (int h = 1; h <= 12; h++) {
                d += std::sin(h * x + ph[h - 1]) / std::pow((float)h, 1.9f);
                b += std::sin(h * x + ph[h - 1]) / std::pow((float)h, 0.95f);
            }
            wavDark[i] = d * 0.9f; wavBright[i] = b * 0.45f;
        }
        for (auto& v : voices) v.on = false;
        engPhase = subPhase = 0; musicSample = 0; lastStep = -1;
    }

    void trigger(SfxType t, float amp = 1.f) {
        Voice* slot = nullptr;
        for (auto& v : voices) if (!v.on) { slot = &v; break; }
        if (!slot) slot = &voices[0];
        slot->on = true; slot->type = t; slot->t = 0; slot->amp = amp; slot->ph = 0;
    }

    // interleaved stereo float
    void render(float* out, int frames) {
        const float dt = 1.f / sr;
        const float vol = p.master;
        for (int i = 0; i < frames; i++) {
            float L = 0, R = 0;
            // ---------------- race ambience ----------------
            float eg = p.engine ? 1.f : 0.f;
            engGain += (eg - engGain) * 0.0008f;
            if (engGain > 0.001f) {
                float rpmTarget = p.rpm;
                rpmS += (rpmTarget - rpmS) * 0.0035f;
                float f0 = rpmS / 60.f * 3.f;  // V6: 3 combustion events per revolution
                engPhase += f0 * dt; engPhase -= std::floor(engPhase);
                subPhase += f0 * 0.5f * dt; subPhase -= std::floor(subPhase);
                float jitter = 1.f + (white() * 0.004f);
                float ld = lerpf(0.25f, 1.f, p.load);
                int ix = (int)(engPhase * jitter * 2048.f) & 2047;
                float w = lerpf(wavDark[ix], wavBright[ix], ld * 0.85f);
                float sub = std::sin(TAU * subPhase) * 0.35f;
                float intake = lp1(intakeS, white(), 0.08f) * (0.10f + p.load * 0.28f);
                float e = w + sub + intake;
                float cutoff = 0.05f + clampf((rpmS + 600.f) / 9000.f, 0.f, 1.f) * 0.30f + p.load * 0.18f;
                engLP += (e - engLP) * cutoff;
                engLP2 += (engLP - engLP2) * (cutoff * 1.4f);
                float eng = engLP2 * (0.55f + 0.45f * ld) * 0.62f;
                // tyres
                float sq = p.slip;
                float sqS = lp1(sqSmooth, sq, 0.002f);
                float n = white();
                float tyre = bandpass(sqF1, n, 0.17f + sqS * 0.07f, 0.15f) * 1.3f +
                             bandpass(sqF2, n, 0.31f + sqS * 0.06f, 0.10f) * 0.9f;
                tyre *= std::pow(sqS, 1.2f) * (0.7f + 0.3f * std::sin(TAU * 11.f * (float)musicSample / sr));
                // wind
                float sp = clampf(p.speed / 70.f, 0.f, 1.f);
                float wind = lp1(windS, white(), 0.02f + sp * 0.12f) * sp * sp * 1.1f;
                // rough ground
                float rough = lp1(roughS, std::fabs(white()) - 0.5f, 0.06f) * p.offroad * clampf(p.speed / 30.f, 0.f, 1.f) * 2.4f;
                // barrier scrape
                float sc = bandpass(scF, white(), 0.24f, 0.18f) * 1.6f + std::sin(TAU * 880.f * (float)musicSample / sr) * 0.05f * white();
                sc *= p.scrape;
                // brakes: faint hiss + squeal at low speed
                float br = hp1(brHP, white()) * p.brake * clampf(p.speed / 25.f, 0.f, 1.f) * 0.12f;
                float brq = std::sin(TAU * 1750.f * (float)musicSample / sr) * p.brake * (p.speed > 4 && p.speed < 22 ? 0.025f : 0.f);
                float amb = (eng + tyre * 0.32f + wind * 0.5f + rough * 0.5f + sc * 0.5f + br + brq) * engGain * p.sfxVol;
                L += amb; R += amb;
            }
            // ---------------- one-shot sfx ----------------
            float sfx = 0;
            for (auto& v : voices) if (v.on) sfx += voice(v, dt);
            sfx *= p.sfxVol;
            L += sfx; R += sfx;
            // ---------------- music ----------------
            float mg = p.music ? 1.f : 0.f;
            musicGain += (mg - musicGain) * 0.0004f;
            if (musicGain > 0.001f) {
                float mL = 0, mR = 0;
                music(mL, mR);
                L += mL * musicGain * p.musicVol; R += mR * musicGain * p.musicVol;
            }
            musicSample++;
            L *= vol * 0.9f; R *= vol * 0.9f;
            L = softclip(L); R = softclip(R);
            if (p.lofi) {
                if (crushCnt++ & 1) { L = holdL; R = holdR; } else { holdL = L; holdR = R; }
                L = std::floor(L * 127.f + 0.5f) / 127.f; R = std::floor(R * 127.f + 0.5f) / 127.f;
            }
            out[i * 2] = L; out[i * 2 + 1] = R;
        }
    }

private:
    struct Voice { bool on = false; SfxType type = SFX_BEEP; float t = 0, amp = 1, ph = 0; };
    Voice voices[24];
    std::vector<float> wavDark, wavBright;
    float engPhase = 0, subPhase = 0, rpmS = 900, engLP = 0, engLP2 = 0, engGain = 0, musicGain = 0;
    float intakeS = 0, sqSmooth = 0, windS = 0, roughS = 0, brHP = 0;
    struct SVF { float lo = 0, bp = 0; };
    SVF sqF1, sqF2, scF;
    uint32_t rs = 12345;
    uint64_t musicSample = 0;
    int lastStep = -1;
    uint32_t crushCnt = 0;
    float holdL = 0, holdR = 0;

    static float softclip(float x) { return std::tanh(x); }
    float white() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return ((rs >> 8) * (1.f / 8388608.f)) - 1.f; }
    static float lp1(float& s, float x, float a) { s += (x - s) * a; return s; }
    static float hp1(float& s, float x) { s += (x - s) * 0.35f; return x - s; }
    static float bandpass(SVF& f, float x, float freq, float damp) {
        f.lo += freq * f.bp;
        float hi = x - f.lo - damp * 2.f * f.bp;
        f.bp += freq * hi;
        return f.bp;
    }
    static float tri(float ph) { ph -= std::floor(ph); return 4.f * std::fabs(ph - 0.5f) - 1.f; }
    static float sqr(float ph, float pw = 0.5f) { ph -= std::floor(ph); return ph < pw ? 1.f : -1.f; }

    float voice(Voice& v, float dt) {
        float t = v.t, a = v.amp, o = 0, dur = 0.5f;
        switch (v.type) {
            case SFX_COLLIDE: {
                dur = 0.8f;
                float f = 42.f + 95.f * std::exp(-t * 20.f);
                v.ph += f * dt;
                o = std::sin(TAU * v.ph) * std::exp(-t * 6.f) * 0.9f;
                o += lp1(colLP, white(), 0.25f) * std::exp(-t * 11.f) * 0.9f;
                o += (std::sin(TAU * 311.f * t) + 0.7f * std::sin(TAU * 523.f * t) + 0.5f * std::sin(TAU * 871.f * t) +
                      0.3f * std::sin(TAU * 1337.f * t)) * std::exp(-t * 13.f) * 0.12f;
                break;
            }
            case SFX_BEEP: dur = 0.22f; o = std::sin(TAU * 523.25f * t) * std::min(1.f, t * 200.f) * std::exp(-t * 6.f) * 0.5f; break;
            case SFX_GO:
                dur = 0.9f;
                o = (std::sin(TAU * 1046.5f * t) + 0.4f * std::sin(TAU * 2093.f * t)) * std::min(1.f, t * 200.f) * std::exp(-t * 3.2f) * 0.45f;
                break;
            case SFX_MOVE: dur = 0.06f; o = sqr(880.f * t, 0.4f) * std::exp(-t * 45.f) * 0.18f; break;
            case SFX_SELECT: {
                dur = 0.2f;
                float f = t < 0.07f ? 660.f : 990.f;
                o = sqr(f * t, 0.5f) * 0.16f * std::exp(-t * 12.f);
                break;
            }
            case SFX_BACK: {
                dur = 0.16f;
                float f = t < 0.06f ? 520.f : 390.f;
                o = sqr(f * t, 0.5f) * 0.15f * std::exp(-t * 14.f);
                break;
            }
            case SFX_LAP: {
                dur = 0.6f;
                static const float n[3] = {659.25f, 783.99f, 987.77f};
                int k = std::min(2, (int)(t / 0.1f));
                float lt = t - k * 0.1f;
                o = tri(n[k] * t) * std::exp(-lt * 9.f) * 0.35f + (t > 0.3f ? tri(987.77f * t) * std::exp(-(t - 0.3f) * 6.f) * 0.2f : 0.f);
                break;
            }
            case SFX_FINISH: {
                dur = 2.2f;
                static const float n[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
                int k = std::min(3, (int)(t / 0.17f));
                float lt = t - k * 0.17f;
                o = tri(n[k] * t) * std::exp(-lt * 5.f) * 0.3f;
                if (t > 0.68f) {
                    float ft = t - 0.68f;
                    o += (tri(523.25f * t) + tri(659.25f * t) + tri(783.99f * t) + tri(1046.5f * t)) * 0.12f * std::exp(-ft * 1.8f);
                }
                break;
            }
            case SFX_SHIFT: dur = 0.08f; o = hp1(v.ph, white()) * std::exp(-t * 50.f) * 0.25f; break;
            default: break;
        }
        v.t += dt;
        if (v.t > dur) v.on = false;
        return o * a;
    }
    float colLP = 0;

    // ------------------------------------------------------------------ music
    // 4 bar loop (Am F C G), 118 BPM, driving four-on-the-floor with arpeggio and delay
    float kickT = 9, snT = 9, hatT = 9, hatOpen = 0, bassT = 9, bassF = 55, leadT = 9, leadF = 440, bassPh = 0, kickPh = 0,
          padPh[9] = {0}, leadPh = 0, bassLP = 0, padLP = 0, hhS = 0, snS = 0;
    float padF[3] = {220, 261.6f, 329.6f};
    std::vector<float> dlL = std::vector<float>(44100 * 2, 0.f), dlR = std::vector<float>(44100 * 2, 0.f);
    int dlPos = 0;

    static float midi(float n) { return 440.f * std::pow(2.f, (n - 69.f) / 12.f); }

    void onStep(int step) {
        static const int roots[4] = {45, 41, 48, 43};            // A2 F2 C3 G2
        static const int chords[4][3] = {{57, 60, 64}, {53, 57, 60}, {60, 64, 67}, {55, 59, 62}};
        static const int bassPat[16] = {0, -99, 12, -99, 0, -99, 12, -99, 0, -99, 12, -99, 0, 7, 10, -99};
        static const int arp[16] = {0, 1, 2, 1, 2, 3, 2, 1, 0, 1, 2, 1, 2, 3, 4, 3};
        int bar = (step / 16) % 8, st = step % 16, ch = bar % 4;
        if (st % 4 == 0) { kickT = 0; kickPh = 0; }
        if (st == 4 || st == 12) snT = 0;
        if (st % 2 == 0) { hatT = 0; hatOpen = (st % 4 == 2) ? 1.f : 0.f; }
        if (bassPat[st] > -50) {
            bassT = 0; bassF = midi((float)(roots[ch] + bassPat[st]));
        }
        if (st == 0) for (int k = 0; k < 3; k++) padF[k] = midi((float)chords[ch][k]);
        bool leadOn = bar >= 4 || (st % 4 == 0) || (st % 8 == 6);
        if (leadOn) {
            int a = arp[st];
            int note = a < 3 ? chords[ch][a] + 12 : (a == 3 ? chords[ch][0] + 24 : chords[ch][1] + 24);
            leadF = midi((float)note); leadT = 0;
        }
    }

    void music(float& oL, float& oR) {
        const float stepLen = sr * 60.f / (118.f * 4.f);
        int step = (int)((double)musicSample / stepLen);
        if (step != lastStep) { lastStep = step; onStep(step); }
        const float dt = 1.f / sr;
        float m = 0, sideL = 0, sideR = 0;
        // kick
        if (kickT < 0.4f) {
            float f = 45.f + 120.f * std::exp(-kickT * 28.f);
            kickPh += f * dt;
            m += std::sin(TAU * kickPh) * std::exp(-kickT * 9.f) * 0.9f;
            kickT += dt;
        }
        // snare
        if (snT < 0.3f) {
            float n = white();
            float hp = n - lp1(snS, n, 0.3f);
            m += (hp * 0.55f + std::sin(TAU * 190.f * snT) * 0.4f * std::exp(-snT * 30.f)) * std::exp(-snT * 16.f) * 0.55f;
            snT += dt;
        }
        // hat
        if (hatT < 0.4f) {
            float n = white();
            float hp = n - lp1(hhS, n, 0.6f);
            float e = std::exp(-hatT * (hatOpen > 0.5f ? 22.f : 70.f));
            sideR += hp * e * (hatOpen > 0.5f ? 0.16f : 0.1f);
            hatT += dt;
        }
        // bass (saw through an enveloped low-pass)
        if (bassT < 0.5f) {
            bassPh += bassF * dt; bassPh -= std::floor(bassPh);
            float saw = bassPh * 2.f - 1.f;
            float a = 0.02f + 0.20f * std::exp(-bassT * 14.f);
            bassLP += (saw - bassLP) * a;
            m += bassLP * std::exp(-bassT * 2.8f) * 0.55f;
            bassT += dt;
        }
        // pad
        float pad = 0;
        for (int k = 0; k < 3; k++) {
            for (int d = 0; d < 3; d++) {
                float& ph = padPh[k * 3 + d];
                ph += padF[k] * (1.f + (d - 1) * 0.004f) * dt; ph -= std::floor(ph);
                pad += ph * 2.f - 1.f;
            }
        }
        padLP += (pad - padLP) * 0.04f;
        m += padLP * 0.05f;
        // lead + ping-pong delay
        float lead = 0;
        if (leadT < 0.4f) {
            leadPh += leadF * dt; leadPh -= std::floor(leadPh);
            lead = (sqr(leadPh, 0.3f) * 0.6f + tri(leadPh) * 0.4f) * std::exp(-leadT * 8.f) * 0.17f;
            leadT += dt;
        }
        int dlen = (int)(stepLen * 3.f);
        int rp = (dlPos - dlen + (int)dlL.size()) % (int)dlL.size();
        float dL = dlL[rp], dR = dlR[rp];
        dlL[dlPos] = lead * 0.7f + dR * 0.42f;
        dlR[dlPos] = dL * 0.42f;
        dlPos = (dlPos + 1) % (int)dlL.size();
        oL = (m + lead * 0.8f + sideL + dL * 0.8f) * 0.8f;
        oR = (m + lead * 0.4f + sideR + dR * 0.8f) * 0.8f;
    }
};

// ---------------------------------------------------------------------------
// SDL wrapper (compiled out for offline tests)
// ---------------------------------------------------------------------------
#ifdef SDL_h_
class Audio {
public:
    Synth synth;
    SDL_AudioDeviceID dev = 0;
    bool ok = false;

    bool init() {
        SDL_AudioSpec want, have;
        SDL_zero(want);
        want.freq = 44100; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 1024;
        want.callback = &Audio::cb; want.userdata = this;
        synth.init(44100);
        dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (!dev) { std::fprintf(stderr, "Audio unavailable: %s\n", SDL_GetError()); return false; }
        if (have.freq != 44100) synth.init(have.freq);
        ok = true;
        SDL_PauseAudioDevice(dev, 0);
        return true;
    }
    void shutdown() { if (dev) SDL_CloseAudioDevice(dev); dev = 0; ok = false; }
    void set(const SynthParams& p) {
        if (!ok) return;
        SDL_LockAudioDevice(dev); synth.p = p; SDL_UnlockAudioDevice(dev);
    }
    void play(SfxType t, float amp = 1.f) {
        if (!ok) return;
        SDL_LockAudioDevice(dev); synth.trigger(t, amp); SDL_UnlockAudioDevice(dev);
    }
private:
    static void SDLCALL cb(void* ud, Uint8* stream, int len) {
        Audio* a = (Audio*)ud;
        a->synth.render((float*)stream, len / (int)(sizeof(float) * 2));
    }
};
#endif
