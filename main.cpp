#define _CRT_SECURE_NO_WARNINGS
#include <functional>
#include <string>

#include "gfx.h"
#include "audio.h"
#include "world.h"

// "Śląska Odlewnia" as explicit UTF-8 bytes so the source encoding of the compiler can never break it
static const char* STUDIO = "\xC5\x9A" "l" "\xC4\x85" "ska Odlewnia";

// settings
struct Settings {
    int master = 8, music = 7, sfx = 8;
    bool fullscreen = false;
    int laps = 3;
    float bestLap = 0.f;
};

static std::string settingsFile() {
    std::string base;
    char* p = SDL_GetPrefPath("SlaskaOdlewnia", "CREWN");
    if (p) { base = p; SDL_free(p); }
    return base + "crewn_settings.cfg";
}
static void loadSettings(Settings& s) {
    FILE* f = std::fopen(settingsFile().c_str(), "r");
    if (!f) return;
    char key[64]; float v;
    while (std::fscanf(f, "%63s %f", key, &v) == 2) {
        std::string k = key;
        if (k == "master") s.master = (int)clampf(v, 0, 10);
        else if (k == "music") s.music = (int)clampf(v, 0, 10);
        else if (k == "sfx") s.sfx = (int)clampf(v, 0, 10);
        else if (k == "fullscreen") s.fullscreen = v > 0.5f;
        else if (k == "laps") s.laps = (v == 1 || v == 3 || v == 5 || v == 10) ? (int)v : 3;
        else if (k == "bestlap") s.bestLap = v;
    }
    std::fclose(f);
}
static void saveSettings(const Settings& s) {
    FILE* f = std::fopen(settingsFile().c_str(), "w");
    if (!f) return;
    std::fprintf(f, "master %d\nmusic %d\nsfx %d\nfullscreen %d\nlaps %d\nbestlap %.3f\n", s.master, s.music, s.sfx,
                 s.fullscreen ? 1 : 0, s.laps, s.bestLap);
    std::fclose(f);
}

// game types
enum Scene { SC_SPLASH, SC_TITLE, SC_MENU, SC_OPTIONS, SC_CREDITS, SC_RACE };
enum RacePhase { RP_COUNTDOWN, RP_RACING, RP_FINISHED };

struct Input { float throttle = 0, brake = 0, steer = 0; bool handbrake = false; };

struct Car {
    float x = 0, z = 0, yaw = 0, vx = 0, vz = 0;
    float steer = 0, steerAng = 0, wheelSpin = 0, pitch = 0, roll = 0;
    float rpm = 1000, shiftCool = 0;
    int gear = 1;
    float trav = 0, prevS = 0;
    int idx = 0;
    float slip = 0, offroad = 0, scrape = 0, hitCd = 0, shake = 0, load = 0, brakeAmt = 0;
    bool braking = false, reversing = false, lights = true;
    float topSpeed = 0, distance = 0;
    float fwdSpeed() const { return vx * std::sin(yaw) + vz * std::cos(yaw); }
    float speed() const { return std::sqrt(vx * vx + vz * vz); }
};

struct Skid { float x1, z1, x2, z2, a; };

struct MItem {
    std::function<std::string()> label;
    std::function<void()> activate;
    std::function<void(int)> adjust;
};
struct Menu {
    std::vector<MItem> items;
    int sel = 0;
    std::function<void()> onBack;
    // layout from the last draw (for the mouse)
    float rx = 0, ry = 0, rw = 0, rh = 0;
};

struct Spark { float x, y, vx, vy, life, max; };

// the game
class Game {
public:
    SDL_Window* win = nullptr;
    SDL_GLContext gl = nullptr;
    Font font;
    World world;
    Audio audio;
    Settings set;
    View2D view;
    bool running = true;
    bool selftest = false;
    std::string shotDir;

    Scene scene = SC_SPLASH;
    float sceneT = 0;
    // loading
    std::vector<std::function<void()>> loadSteps;
    int loadDone = 0;
    bool loaded = false;
    float splashFadeOut = -1.f;
    std::vector<Spark> sparks;
    Rng rng{99};
    // title/menu
    float menuT = 0.f;       // 0 = title, 1 = menu
    bool titleDone = false;
    float orbit = 0.7f;
    Menu mainMenu, optMenu, pauseMenu, resultsMenu;
    // race
    RacePhase phase = RP_COUNTDOWN;
    float phaseT = 0;
    Car car;
    Input in;
    bool paused = false;
    float raceTime = 0, lapStart = 0;
    int lapsDone = 0, totalLaps = 3;
    std::vector<float> lapTimes;
    float bestThisRace = 0;
    bool newRecord = false;
    float finishT = 0;
    float msgT = 0; std::string msg; Col msgCol;
    float camYaw = 0, camShake = 0;
    TrackLoc loc;
    float wrongWayT = 0;
    std::vector<Skid> skids; size_t skidHead = 0;
    float lastWheelX[4] = {0}, lastWheelZ[4] = {0}; bool wheelValid[4] = {false};
    float accum = 0;
    bool autopilotOn = false;
    int lastCountdown = 99;
    double timeNow = 0;
    // init  ! init 
    bool init(int argc, char** argv) {
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            if (a == "--selftest") { selftest = true; shotDir = (i + 1 < argc) ? argv[i + 1] : "shots"; }
        }
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }
        loadSettings(set);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
        if (set.fullscreen && !selftest) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        win = SDL_CreateWindow("CREWN", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, flags);
        if (!win) { std::fprintf(stderr, "Window failed: %s\n", SDL_GetError()); return false; }
        SDL_SetWindowMinimumSize(win, 640, 360);
        gl = SDL_GL_CreateContext(win);
        if (!gl) { std::fprintf(stderr, "OpenGL context failed: %s\n", SDL_GetError()); return false; }
        SDL_GL_SetSwapInterval(selftest ? 0 : 1);
        font.init();
        audio.init();
        applyVolumes();
        setupGLState();
        initRetro();
        buildMenus();
        buildLoadSteps();
        if (selftest) { while (!loaded) stepLoad(); }
        return true;
    }

    void shutdown() {
        saveSettings(set);
        audio.shutdown();
        if (gl) SDL_GL_DeleteContext(gl);
        if (win) SDL_DestroyWindow(win);
        SDL_Quit();
    }

    void setupGLState() {
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glShadeModel(GL_SMOOTH);
        glHint(GL_FOG_HINT, GL_NICEST);
        glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_NORMALIZE);
        glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
        float amb[4] = {0.42f, 0.38f, 0.52f, 1};
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
        float dif[4] = {1.15f, 0.82f, 0.56f, 1}, zero[4] = {0, 0, 0, 1};
        glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
        glLightfv(GL_LIGHT0, GL_AMBIENT, zero);
        glLightfv(GL_LIGHT0, GL_SPECULAR, zero);
        glEnable(GL_LIGHT0);
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogf(GL_FOG_START, 45.f);
        glFogf(GL_FOG_END, 400.f);
    }

    void applyVolumes() {
        SynthParams p = sp;
        p.master = set.master / 10.f; p.musicVol = set.music / 10.f; p.sfxVol = set.sfx / 10.f;
        sp = p;
        audio.set(sp);
    }
    SynthParams sp;

    void buildLoadSteps() {
        loadSteps = {
            [this] { world.makeTexturesA(); },
            [this] { world.makeTexturesB(); },
            [this] { world.makeTrack(); },
            [this] { world.makeRoad(); },
            [this] { world.makeScenery(); },
            [this] { world.makeSky(); },
            [this] { world.makeCar(); },
        };
    }
    void stepLoad() {
        if (loaded) return;
        loadSteps[loadDone]();
        loadDone++;
        if (loadDone >= (int)loadSteps.size()) loaded = true;
    }
    float loadProgress() const { return loaded ? 1.f : (float)loadDone / (float)loadSteps.size(); }

    //menus
    void sfx(SfxType t, float a = 1.f) { audio.play(t, a); }

    static std::string volStr(int v) { return std::to_string(v * 10) + "%"; }

    void buildMenus() {
        mainMenu.items = {
            {[] { return std::string("Start Race"); }, [this] { sfx(SFX_SELECT); startRaceScene(); }, nullptr},
            {[] { return std::string("Options"); }, [this] { sfx(SFX_SELECT); scene = SC_OPTIONS; optMenu.sel = 0; }, nullptr},
            {[] { return std::string("Credits"); }, [this] { sfx(SFX_SELECT); scene = SC_CREDITS; sceneT = 0; }, nullptr},
            {[] { return std::string("Exit"); }, [this] { sfx(SFX_BACK); running = false; }, nullptr},
        };
        auto slider = [this](const char* name, int* v) {
            MItem m;
            std::string nm = name;
            m.label = [=] { return nm + "  < " + volStr(*v) + " >"; };
            m.adjust = [this, v](int d) { *v = (int)clampf((float)(*v + d), 0, 10); applyVolumes(); sfx(SFX_MOVE); };
            m.activate = [this, v] { *v = (*v + 1) % 11; applyVolumes(); sfx(SFX_MOVE); };
            return m;
        };
        optMenu.items.push_back(slider("Master Volume", &set.master));
        optMenu.items.push_back(slider("Music Volume", &set.music));
        optMenu.items.push_back(slider("Effects Volume", &set.sfx));
        {
            MItem m;
            m.label = [this] { return std::string("Fullscreen  < ") + (set.fullscreen ? "ON" : "OFF") + " >"; };
            m.adjust = [this](int) { toggleFullscreen(); };
            m.activate = [this] { toggleFullscreen(); };
            optMenu.items.push_back(m);
        }
        {
            MItem m;
            m.label = [this] { return std::string("Race Laps  < ") + std::to_string(set.laps) + " >"; };
            auto cyc = [this](int d) {
                static const int opts[4] = {1, 3, 5, 10};
                int i = 1;
                for (int k = 0; k < 4; k++) if (opts[k] == set.laps) i = k;
                set.laps = opts[((i + d) % 4 + 4) % 4];
                sfx(SFX_MOVE);
            };
            m.adjust = cyc;
            m.activate = [cyc] { cyc(1); };
            optMenu.items.push_back(m);
        }
        optMenu.items.push_back({[] { return std::string("Back"); }, [this] { sfx(SFX_BACK); saveSettings(set); scene = SC_MENU; }, nullptr});
        optMenu.onBack = [this] { sfx(SFX_BACK); saveSettings(set); scene = SC_MENU; };

        pauseMenu.items = {
            {[] { return std::string("Resume"); }, [this] { sfx(SFX_SELECT); paused = false; }, nullptr},
            {[] { return std::string("Restart Race"); }, [this] { sfx(SFX_SELECT); startRace(); }, nullptr},
            {[] { return std::string("Quit to Main Menu"); }, [this] { sfx(SFX_BACK); toMainMenu(); }, nullptr},
        };
        pauseMenu.onBack = [this] { paused = false; };
        resultsMenu.items = {
            {[] { return std::string("Restart Race"); }, [this] { sfx(SFX_SELECT); startRace(); }, nullptr},
            {[] { return std::string("Main Menu"); }, [this] { sfx(SFX_BACK); toMainMenu(); }, nullptr},
        };
    }

    void toggleFullscreen() {
        set.fullscreen = !set.fullscreen;
        if (!selftest) SDL_SetWindowFullscreen(win, set.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        sfx(SFX_MOVE);
    }

    void toMainMenu() {
        scene = SC_MENU; menuT = 1.f; titleDone = true; paused = false;
        SDL_ShowCursor(SDL_ENABLE);
        mainMenu.sel = 0;
    }
    void startRaceScene() {
        scene = SC_RACE;
        SDL_ShowCursor(SDL_DISABLE);
        startRace();
    }

    // menu input
    bool menuKey(Menu& m, SDL_Keycode k) {
        int n = (int)m.items.size();
        switch (k) {
            case SDLK_UP: case SDLK_w: m.sel = (m.sel + n - 1) % n; sfx(SFX_MOVE); return true;
            case SDLK_DOWN: case SDLK_s: m.sel = (m.sel + 1) % n; sfx(SFX_MOVE); return true;
            case SDLK_LEFT: case SDLK_a: if (m.items[m.sel].adjust) m.items[m.sel].adjust(-1); return true;
            case SDLK_RIGHT: case SDLK_d: if (m.items[m.sel].adjust) m.items[m.sel].adjust(+1); return true;
            case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: if (m.items[m.sel].activate) m.items[m.sel].activate(); return true;
            case SDLK_ESCAPE: case SDLK_BACKSPACE: if (m.onBack) m.onBack(); return true;
            default: return false;
        }
    }
    int menuHit(const Menu& m, float mx, float my) const {
        if (mx < m.rx || mx > m.rx + m.rw || my < m.ry) return -1;
        int i = (int)((my - m.ry) / m.rh);
        return (i >= 0 && i < (int)m.items.size()) ? i : -1;
    }
    void menuMouseMove(Menu& m, float mx, float my) {
        int h = menuHit(m, mx, my);
        if (h >= 0 && h != m.sel) { m.sel = h; sfx(SFX_MOVE); }
    }
    void menuClick(Menu& m, float mx, float my) {
        int h = menuHit(m, mx, my);
        if (h >= 0) { m.sel = h; if (m.items[h].activate) m.items[h].activate(); }
    }
    Menu* activeMenu() {
        if (scene == SC_MENU && menuT > 0.7f) return &mainMenu;
        if (scene == SC_OPTIONS) return &optMenu;
        if (scene == SC_RACE) {
            if (paused) return &pauseMenu;
            if (phase == RP_FINISHED && finishT > 2.2f) return &resultsMenu;
        }
        return nullptr;
    }

    //  events
    void handleEvent(const SDL_Event& e) {
        if (e.type == SDL_QUIT) { running = false; return; }
        float scx = 1, scy = 1;
        if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN) {
            int ww, wh; SDL_GetWindowSize(win, &ww, &wh);
            scx = (float)view.winW / std::max(1, ww); scy = (float)view.winH / std::max(1, wh);
        }
        if (e.type == SDL_MOUSEMOTION) {
            float mx = e.motion.x * scx / view.k, my = e.motion.y * scy / view.k;
            if (Menu* m = activeMenu()) menuMouseMove(*m, mx, my);
        }
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            float mx = e.button.x * scx / view.k, my = e.button.y * scy / view.k;
            if (scene == SC_TITLE) { leaveTitle(); return; }
            if (scene == SC_CREDITS) { sfx(SFX_BACK); scene = SC_MENU; return; }
            if (scene == SC_SPLASH) { if (loaded && splashFadeOut < 0) splashFadeOut = 0; return; }
            if (Menu* m = activeMenu()) menuClick(*m, mx, my);
        }
        if (e.type != SDL_KEYDOWN || e.key.repeat) return;
        SDL_Keycode k = e.key.keysym.sym;
        if (k == SDLK_F11 || (k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) { toggleFullscreen(); return; }
        switch (scene) {
            case SC_SPLASH:
                if (loaded && splashFadeOut < 0 && (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_ESCAPE)) splashFadeOut = 0;
                break;
            case SC_TITLE: leaveTitle(); break;
            case SC_MENU:
                if (menuT > 0.7f) {
                    if (k == SDLK_ESCAPE) { sfx(SFX_BACK); menuT = 0.99f; scene = SC_TITLE; titleDone = false; }
                    else menuKey(mainMenu, k);
                }
                break;
            case SC_OPTIONS: menuKey(optMenu, k); break;
            case SC_CREDITS: sfx(SFX_BACK); scene = SC_MENU; break;
            case SC_RACE: raceKey(k); break;
        }
    }
    void leaveTitle() { sfx(SFX_SELECT); scene = SC_MENU; titleDone = true; }

    void raceKey(SDL_Keycode k) {
        if (paused) { menuKey(pauseMenu, k); return; }
        if (phase == RP_FINISHED && finishT > 2.2f) { menuKey(resultsMenu, k); return; }
        if (k == SDLK_ESCAPE || k == SDLK_p) { paused = true; pauseMenu.sel = 0; sfx(SFX_SELECT); SDL_ShowCursor(SDL_ENABLE); }
        else if (k == SDLK_r) { sfx(SFX_SELECT); startRace(); }
        else if (k == SDLK_l) { car.lights = !car.lights; sfx(SFX_MOVE); }
        else if (k == SDLK_t && phase == RP_RACING) resetOnTrack();
    }

    //  race setup
    void startRace() {
        totalLaps = set.laps;
        const Track& T = world.track;
        car = Car();
        int idx = ((T.N() - 3) % T.N() + T.N()) % T.N();
        const TrackPoint& p = T.p[idx];
        car.x = p.x; car.z = p.z; car.yaw = std::atan2(p.tx, p.tz);
        car.idx = idx;
        car.trav = -3.f * T.ds; car.prevS = idx * T.ds;
        car.lights = true;
        loc = T.locate(car.x, car.z, idx, 6);
        phase = RP_COUNTDOWN; phaseT = 0; lastCountdown = 99;
        raceTime = 0; lapStart = 0; lapsDone = 0; lapTimes.clear(); bestThisRace = 0; newRecord = false;
        finishT = 0; paused = false; msgT = 0; wrongWayT = 0;
        camYaw = car.yaw; camShake = 0;
        skids.clear(); skidHead = 0;
        for (int i = 0; i < 4; i++) wheelValid[i] = false;
        SDL_ShowCursor(SDL_DISABLE);
    }
    void resetOnTrack() {
        const Track& T = world.track;
        loc = T.locate(car.x, car.z, car.idx, -1);
        car.x = loc.cx; car.z = loc.cz; car.yaw = std::atan2(loc.tx, loc.tz);
        car.vx = car.vz = 0; car.steer = 0;
        car.prevS = loc.s;
        car.idx = loc.i;
        sfx(SFX_MOVE);
    }

    //  driving physics
    void readInput() {
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        in.throttle = (ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP]) ? 1.f : 0.f;
        in.brake = (ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_DOWN]) ? 1.f : 0.f;
        float st = 0;
        if (ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT]) st += 1.f;
        if (ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT]) st -= 1.f;
        in.steer = st;
        in.handbrake = ks[SDL_SCANCODE_SPACE] != 0;
    }

    // test driver used by --selftest: follows the centre line and brakes for corners
    Input autopilot(float targetCap = 1e9f) {
        const Track& T = world.track;
        Input o;
        int N = T.N();
        float vf = car.fwdSpeed();
        float look = 10.f + std::fabs(vf) * 0.75f;
        int steps = (int)(look / T.ds) + 1;
        const TrackPoint& tp = T.p[(loc.i + steps) % N];
        float desired = std::atan2(tp.x - car.x, tp.z - car.z);
        float err = wrapPi(desired - car.yaw);
        o.steer = clampf(err * 2.2f, -1.f, 1.f);
        float vt = 70.f;
        for (int k = 0; k < 70; k++) {
            const TrackPoint& q = T.p[(loc.i + k) % N];
            float vk = std::min(62.f, std::sqrt(11.f / std::max(std::fabs(q.curv), 1e-4f)));
            float dist = k * T.ds;
            vt = std::min(vt, std::sqrt(vk * vk + 2.f * 13.f * dist));
        }
        vt = std::min(vt, targetCap);
        if (vf < vt - 1.f) o.throttle = 1.f;
        else if (vf > vt + 1.5f) o.brake = clampf((vf - vt) / 6.f, 0.35f, 1.f);
        if (o.brake > 0 && vf < 1.0f) o.brake = 0;
        return o;
    }

    static float gearTop(int g) {
        static const float t[6] = {17.f, 30.f, 42.f, 53.f, 62.f, 72.f};
        return t[std::min(5, std::max(0, g - 1))];
    }

    void physics(float dt, const Input& inp) {
        const Track& T = world.track;
        Car& c = car;
        const float hw = T.halfWidth, kw = T.kerbWidth;
        // steering smoothing
        float rate = std::fabs(inp.steer) > std::fabs(c.steer) ? 5.2f : 8.5f;
        if (inp.steer * c.steer < 0) rate = 10.f;
        c.steer += clampf(inp.steer - c.steer, -rate * dt, rate * dt);

        float sy = std::sin(c.yaw), cy = std::cos(c.yaw);
        float vf = c.vx * sy + c.vz * cy;
        float spd = std::fabs(vf);
        loc = T.locate(c.x, c.z, c.idx, 24);
        c.idx = loc.i;
        float ad = std::fabs(loc.d);
        bool offroad = ad > hw + kw * 0.8f;
        c.offroad += ((offroad ? 1.f : 0.f) - c.offroad) * approachK(10, dt);

        // yaw
        float dir = clampf(vf / 3.f, -1.f, 1.f);
        float yawMax = 2.05f / (1.f + std::pow(spd / 16.f, 1.5f));
        float yawRate = c.steer * yawMax * dir * (inp.handbrake ? 1.45f : 1.f) * (offroad ? 0.85f : 1.f);
        c.yaw += yawRate * dt;
        sy = std::sin(c.yaw); cy = std::cos(c.yaw);
        float rx = -cy, rz = sy;
        vf = c.vx * sy + c.vz * cy;
        float vl = c.vx * rx + c.vz * rz;

        // longitudinal
        const float vmax = 63.f;
        float a = 0; c.braking = false; c.reversing = false;
        if (inp.throttle > 0 && inp.brake <= 0) {
            if (vf > -0.5f) {
                float k = clampf(vf / vmax, 0.f, 1.2f);
                a = 9.2f * (1.f - k * k) * (offroad ? 0.6f : 1.f);
            } else { a = 26.f; c.braking = true; }
        } else if (inp.brake > 0 && inp.throttle <= 0) {
            if (vf > 0.8f) { a = -25.f; c.braking = true; }
            else {
                c.reversing = true;
                float k = clampf(-vf / 14.f, 0.f, 1.f);
                a = -6.5f * (1.f - k * k);
            }
        }
        float vfn = vf + a * dt;
        if (c.braking && vf * vfn < 0) vfn = 0;
        bool coast = inp.throttle <= 0 && inp.brake <= 0;
        float drag = 0.6f + 0.00075f * vfn * vfn + (coast ? 1.7f : 0.f) + (offroad ? 5.5f : 0.f) + (inp.handbrake ? 6.f : 0.f);
        float dv = drag * dt;
        if (std::fabs(vfn) <= dv) vfn = 0;
        else vfn -= (vfn > 0 ? 1.f : -1.f) * dv;

        // lateral grip
        float grip = offroad ? 5.f : 10.f;
        if (inp.handbrake) grip = 1.6f;
        else grip -= 3.2f * clampf(std::fabs(c.steer) * spd / 45.f, 0.f, 1.f);
        float vln = vl * std::exp(-grip * dt);

        float slipBefore = std::fabs(vl);
        c.vx = sy * vfn + rx * vln;
        c.vz = cy * vfn + rz * vln;
        float sp2 = c.speed();
        if (sp2 > vmax + 2) { c.vx *= (vmax + 2) / sp2; c.vz *= (vmax + 2) / sp2; }
        c.x += c.vx * dt; c.z += c.vz * dt;
        c.distance += c.speed() * dt;
        c.topSpeed = std::max(c.topSpeed, c.speed());

        // squeal amount
        float slip = clampf((slipBefore - 1.4f) / 5.f, 0.f, 1.f);
        if (c.braking && spd > 18.f && inp.brake > 0.5f) slip = std::max(slip, clampf((spd - 18.f) / 40.f, 0.f, 0.5f));
        if (inp.throttle > 0.9f && vfn > 0.5f && vfn < 11.f) slip = std::max(slip, 0.55f * (1.f - vfn / 11.f));
        if (inp.handbrake && spd > 8.f) slip = std::max(slip, 0.85f);
        if (offroad) slip *= 0.3f;
        c.slip += (slip - c.slip) * approachK(18, dt);
        c.brakeAmt = c.braking ? 1.f : 0.f;
        c.load = inp.throttle;

        // visual body motion
        float accelLong = (vfn - vf) / std::max(dt, 1e-4f);
        float pT = clampf(-accelLong * 0.0035f, -0.05f, 0.06f);
        float rT = clampf(yawRate * vf * 0.0022f, -0.08f, 0.08f);
        c.pitch += (pT - c.pitch) * approachK(7, dt);
        c.roll += (rT - c.roll) * approachK(7, dt);
        c.steerAng = c.steer * 0.5f;
        c.wheelSpin += vfn / 0.34f * dt;

        // gearbox / rpm (drives the engine sound)
        c.shiftCool -= dt;
        float sp = std::fabs(vfn);
        if (c.reversing || vfn < -0.5f) c.gear = 1;
        else {
            float rpmNow = 1000.f + sp / gearTop(c.gear) * 6200.f;
            if (rpmNow > 6900.f && c.gear < 6 && c.shiftCool <= 0 && inp.throttle > 0) {
                c.gear++; c.shiftCool = 0.35f; sfx(SFX_SHIFT, 0.8f);
            }
            if (c.gear > 1 && sp < gearTop(c.gear - 1) * 0.58f && c.shiftCool <= 0) { c.gear--; c.shiftCool = 0.25f; }
        }
        float rpmT = 1000.f + std::min(sp / gearTop(c.gear), 1.08f) * 6200.f;
        if (sp < 3.f) rpmT = 1000.f + inp.throttle * 4500.f * (1.f - sp / 3.f) + sp / 3.f * (rpmT - 1000.f);
        if (phase == RP_COUNTDOWN) rpmT = 1000.f + inp.throttle * 3800.f;
        c.rpm += (rpmT - c.rpm) * approachK(c.shiftCool > 0 ? 7.f : 16.f, dt);

        collide(dt);

        // lap progress
        TrackLoc l2 = T.locate(c.x, c.z, c.idx, 24);
        loc = l2; c.idx = l2.i;
        float d = l2.s - c.prevS;
        if (d > T.length * 0.5f) d -= T.length;
        if (d < -T.length * 0.5f) d += T.length;
        c.trav += d; c.prevS = l2.s;
        float fdot = (c.vx * l2.tx + c.vz * l2.tz);
        if (fdot < -4.f) wrongWayT += dt; else wrongWayT = std::max(0.f, wrongWayT - dt * 2.f);

        updateSkids(dt, inp);
    }

    void collide(float dt) {
        const Track& T = world.track;
        Car& c = car;
        c.hitCd -= dt;
        float scrape = 0;
        static const float cx[8] = {0.95f, -0.95f, 0.95f, -0.95f, 0.f, 0.f, 0.95f, -0.95f};
        static const float cz[8] = {2.25f, 2.25f, -2.25f, -2.25f, 2.3f, -2.3f, 0.f, 0.f};
        float lim = T.barrierOff - 0.18f;
        for (int pass = 0; pass < 2; pass++)
            for (int k = 0; k < 8; k++) {
                float sy = std::sin(c.yaw), cy = std::cos(c.yaw);
                float wx = c.x + cx[k] * cy + cz[k] * sy, wz = c.z - cx[k] * sy + cz[k] * cy;
                TrackLoc q = T.locate(wx, wz, c.idx, 12);
                float over = std::fabs(q.d) - lim;
                if (over <= 0) continue;
                float sgn = q.d > 0 ? 1.f : -1.f;
                float nx = q.nx * sgn, nz = q.nz * sgn;  // outward
                c.x -= nx * over; c.z -= nz * over;
                float vn = c.vx * nx + c.vz * nz;
                if (vn > 0) {
                    c.vx -= 1.25f * vn * nx; c.vz -= 1.25f * vn * nz;
                    float tvx = c.vx, tvz = c.vz;
                    c.vx = tvx * 0.975f; c.vz = tvz * 0.975f;
                    float rrx = -std::cos(c.yaw), rrz = std::sin(c.yaw);
                    float s = rrx * nx + rrz * nz;
                    float hn = std::sin(c.yaw) * nx + std::cos(c.yaw) * nz;
                    if (hn > 0.1f) c.yaw += (s > 0 ? 1.f : -1.f) * clampf(vn * 0.02f, 0.f, 0.12f);
                    if (vn > 2.5f && c.hitCd <= 0) {
                        sfx(SFX_COLLIDE, clampf(vn / 16.f, 0.25f, 1.2f));
                        c.hitCd = 0.18f;
                        camShake = std::max(camShake, clampf(vn / 20.f, 0.1f, 0.8f));
                    }
                }
                scrape = std::max(scrape, clampf(c.speed() / 30.f, 0.2f, 1.f));
            }
        c.scrape += (scrape - c.scrape) * approachK(20, dt);
    }

    void updateSkids(float dt, const Input& inp) {
        (void)dt; (void)inp;
        Car& c = car;
        bool skid = (c.slip > 0.35f) && c.speed() > 4.f && c.offroad < 0.5f;
        static const float wx[4] = {0.88f, -0.88f, 0.88f, -0.88f};
        static const float wz[4] = {1.38f, 1.38f, -1.32f, -1.32f};
        float sy = std::sin(c.yaw), cy = std::cos(c.yaw);
        for (int i = 0; i < 4; i++) {
            float x = c.x + wx[i] * cy + wz[i] * sy, z = c.z - wx[i] * sy + wz[i] * cy;
            bool use = skid && (i >= 2 || c.braking);
            if (use) {
                if (wheelValid[i]) {
                    Skid s{lastWheelX[i], lastWheelZ[i], x, z, clampf(c.slip, 0.3f, 1.f) * 0.6f};
                    if (skids.size() < 2600) skids.push_back(s);
                    else { skids[skidHead] = s; skidHead = (skidHead + 1) % skids.size(); }
                }
                lastWheelX[i] = x; lastWheelZ[i] = z; wheelValid[i] = true;
            } else wheelValid[i] = false;
        }
    }

    //  race flow
    void showMsg(const std::string& m, const Col& c, float t = 2.6f) { msg = m; msgCol = c; msgT = t; }

    void updateRace(float dt) {
        const Track& T = world.track;
        phaseT += dt;
        camShake = std::max(0.f, camShake - dt * 2.f);
        if (phase == RP_COUNTDOWN) {
            Input idle;
            readInputForPhase(idle);
            physics(dt, Input());  // car held still; engine can rev via rpm in physics
            car.vx = car.vz = 0;
            float t = phaseT - 0.8f;
            int cd = t < 0 ? 4 : (t < 1 ? 3 : (t < 2 ? 2 : (t < 3 ? 1 : 0)));
            if (cd != lastCountdown) {
                lastCountdown = cd;
                if (cd >= 1 && cd <= 3) sfx(SFX_BEEP);
                if (cd == 0) { sfx(SFX_GO); phase = RP_RACING; raceTime = 0; lapStart = 0; showMsg("GO!", Col(0.4f, 1, 0.4f), 1.2f); }
            }
            return;
        }
        Input use;
        if (phase == RP_RACING) {
            use = autopilotOn ? autopilot() : in;
            raceTime += dt;
        } else {  // finished: roll to a stop on the racing line
            finishT += dt;
            use = autopilot(finishT < 0.5f ? 40.f : 0.f);
            if (car.fwdSpeed() < 1.5f) { use.brake = 0; use.throttle = 0; }
        }
        physics(dt, use);
        if (phase == RP_RACING) {
            while (car.trav >= (lapsDone + 1) * T.length) {
                lapsDone++;
                float lt = raceTime - lapStart;
                lapStart = raceTime;
                lapTimes.push_back(lt);
                bool best = bestThisRace == 0 || lt < bestThisRace;
                if (best) bestThisRace = lt;
                if (lapsDone >= totalLaps) { finishRace(); break; }
                sfx(SFX_LAP);
                showMsg("LAP " + std::to_string(lapsDone) + "  " + fmtTime(lt) + (best && lapsDone > 1 ? "  BEST!" : ""), Col(1, 0.85f, 0.3f), 3.f);
            }
        }
        if (msgT > 0) msgT -= dt;
    }
    void readInputForPhase(Input&) {}

    void finishRace() {
        phase = RP_FINISHED; finishT = 0;
        sfx(SFX_FINISH);
        newRecord = false;
        if (bestThisRace > 0 && (set.bestLap <= 0 || bestThisRace < set.bestLap)) { set.bestLap = bestThisRace; newRecord = true; saveSettings(set); }
        showMsg("FINISH!", Col(1, 0.9f, 0.3f), 2.4f);
        resultsMenu.sel = 0;
    }

    //  update
    void update(float dt) {
        timeNow += dt;
        sceneT += dt;
        // audio parameters
        SynthParams p = sp;
        p.master = set.master / 10.f; p.musicVol = set.music / 10.f; p.sfxVol = set.sfx / 10.f;
        p.music = (scene != SC_RACE && scene != SC_SPLASH) ;
        p.engine = false;
        p.lofi = true;
        switch (scene) {
            case SC_SPLASH: updateSplash(dt); break;
            case SC_TITLE: case SC_MENU: case SC_OPTIONS: case SC_CREDITS:
                orbit += dt * 0.22f;
                menuT += ((scene == SC_TITLE || !titleDone ? 0.f : 1.f) - menuT) * approachK(5.f, dt);
                if (scene == SC_TITLE && menuT < 0.001f) menuT = 0;
                break;
            case SC_RACE: {
                if (!paused) {
                    if (!autopilotOn) readInput();
                    updateRace(dt);
                }
                p.engine = !paused;
                p.rpm = car.rpm; p.load = car.load; p.speed = car.speed();
                p.slip = car.slip; p.offroad = car.offroad; p.brake = car.brakeAmt; p.scrape = car.scrape;
                if (phase == RP_FINISHED) { p.load = 0; p.brake = 0; }
                break;
            }
        }
        sp = p;
        audio.set(sp);
        // menu → title transition used by the main menu "back"
        if (scene == SC_MENU && !titleDone) scene = SC_TITLE;
    }

    void updateSplash(float dt) {
        if (!loaded && sceneT > 0.35f) stepLoad();
        if (loaded && splashFadeOut < 0 && sceneT > 4.6f) splashFadeOut = 0;
        if (splashFadeOut >= 0) {
            splashFadeOut += dt;
            if (splashFadeOut > 0.8f) { scene = SC_TITLE; sceneT = 0; menuT = 0; }
        }
        // sparks from the "melt"
        if (rng.f() < dt * 55.f) sparks.push_back({view.W * 0.5f + rng.r(-110, 110), view.H * 0.5f + 30, rng.r(-25, 25), rng.r(-75, -30), 0, rng.r(0.6f, 1.6f)});
        for (auto& s : sparks) { s.x += s.vx * dt; s.y += s.vy * dt; s.vy += 30.f * dt; s.life += dt; }
        sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark& s) { return s.life > s.max; }), sparks.end());
    }

    // ======================================================================
    // rendering
    // ======================================================================
    CamBasis cam;
    float camFov = 62.f;

    void glowBillboard(const V3& p, float size, const Col& c) {
        V3 r = cam.right * size, u = cam.up * size;
        glColor4f(c.r, c.g, c.b, c.a);
        glTexCoord2f(0, 0); glVertex3f(p.x - r.x - u.x, p.y - r.y - u.y, p.z - r.z - u.z);
        glTexCoord2f(1, 0); glVertex3f(p.x + r.x - u.x, p.y + r.y - u.y, p.z + r.z - u.z);
        glTexCoord2f(1, 1); glVertex3f(p.x + r.x + u.x, p.y + r.y + u.y, p.z + r.z + u.z);
        glTexCoord2f(0, 1); glVertex3f(p.x - r.x + u.x, p.y - r.y + u.y, p.z - r.z + u.z);
    }

    static void carToWorld(const Car& c, float lx, float lz, float& wx, float& wz) {
        float s = std::sin(c.yaw), co = std::cos(c.yaw);
        wx = c.x + lx * co + lz * s; wz = c.z - lx * s + lz * co;
    }

    static void rawBox(float cx, float cy, float cz, float sx, float sy, float sz, const Col& c) {
        float x0 = cx - sx / 2, x1 = cx + sx / 2, y0 = cy - sy / 2, y1 = cy + sy / 2, z0 = cz - sz / 2, z1 = cz + sz / 2;
        glColor4f(c.r, c.g, c.b, 1);
        glBegin(GL_QUADS);
        glVertex3f(x0, y0, z1); glVertex3f(x1, y0, z1); glVertex3f(x1, y1, z1); glVertex3f(x0, y1, z1);
        glVertex3f(x1, y0, z0); glVertex3f(x0, y0, z0); glVertex3f(x0, y1, z0); glVertex3f(x1, y1, z0);
        glVertex3f(x0, y1, z0); glVertex3f(x0, y1, z1); glVertex3f(x1, y1, z1); glVertex3f(x1, y1, z0);
        glVertex3f(x0, y0, z0); glVertex3f(x0, y0, z1); glVertex3f(x0, y1, z1); glVertex3f(x0, y1, z0);
        glVertex3f(x1, y0, z1); glVertex3f(x1, y0, z0); glVertex3f(x1, y1, z0); glVertex3f(x1, y1, z1);
        glEnd();
    }

    void drawCar(const Car& c, bool showLights) {
        glEnable(GL_LIGHTING);
        glEnable(GL_TEXTURE_2D);
        glPushMatrix();
        glTranslatef(c.x, 0.06f, c.z);
        glRotatef(c.yaw * 180.f / PI, 0, 1, 0);
        glRotatef(c.roll * 57.2958f, 0, 0, 1);
        glRotatef(c.pitch * 57.2958f, 1, 0, 0);
        glCallList(world.L_body);
        glEnable(GL_TEXTURE_2D);
        for (int i = 0; i < 4; i++) {
            float sx = (i & 1) ? -1.f : 1.f;
            bool front = i < 2;
            glPushMatrix();
            glTranslatef(sx * 0.9f, 0.34f, front ? 1.38f : -1.32f);
            if (front) glRotatef(c.steerAng * 57.2958f, 0, 1, 0);
            glRotatef(std::fmod(c.wheelSpin, TAU) * 57.2958f, 1, 0, 0);
            if (sx < 0) glRotatef(180.f, 0, 1, 0);
            glCallList(world.L_wheel);
            glPopMatrix();
        }
        // emissive lamps
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        Col hl = showLights ? Col(1.0f, 0.96f, 0.78f) : Col(0.55f, 0.56f, 0.6f);
        for (int sx = -1; sx <= 1; sx += 2) {
            rawBox(sx * 0.62f, 0.56f, 2.215f, 0.36f, 0.1f, 0.06f, hl);
            rawBox(sx * 0.36f, 0.5f, 2.255f, 0.14f, 0.05f, 0.03f, showLights ? Col(1, 1, 0.9f) : Col(0.4f, 0.4f, 0.42f));
        }
        Col tl = c.braking ? Col(1.0f, 0.12f, 0.08f) : (showLights ? Col(0.65f, 0.04f, 0.04f) : Col(0.32f, 0.03f, 0.03f));
        for (int sx = -1; sx <= 1; sx += 2) rawBox(sx * 0.6f, 0.7f, -2.215f, 0.52f, 0.12f, 0.05f, tl);
        rawBox(0, 0.71f, -2.22f, 0.5f, 0.04f, 0.04f, tl);
        if (c.reversing) for (int sx = -1; sx <= 1; sx += 2) rawBox(sx * 0.3f, 0.6f, -2.24f, 0.16f, 0.07f, 0.04f, Col(1, 1, 1));
        glPopMatrix();
        glEnable(GL_TEXTURE_2D);
    }

    void drawWorld(const V3& eye, const V3& target, float fov, bool withCarFx) {
        // retro: the 3D scene is drawn at half the internal frame size and blown up with hard pixel edges
        const int div = retro ? 2 : 1;
        const int vw = std::max(1, view.winW / div), vh = std::max(1, view.winH / div);
        float aspect = (float)vw / vh;
        glViewport(0, 0, vw, vh);
        Col hz = horizonCol();
        glClearColor(hz.r, hz.g, hz.b, 1);
        glDepthMask(GL_TRUE);  // begin2D() leaves it off, and glClear ignores the depth buffer while it is off
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        setPerspective(fov, aspect, 1.0f, 950.f);
        cam = applyLookAt(eye, target);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // sky (no depth, no fog, no lighting)
        glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
        glDisable(GL_FOG); glDisable(GL_LIGHTING);
        glPushMatrix();
        glTranslatef(eye.x, eye.y, eye.z);
        glCallList(world.L_sky);
        glEnable(GL_TEXTURE_2D);
        {   // sun
            V3 sd = sunDir();
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glBindTexture(GL_TEXTURE_2D, world.tx.glow);
            glBegin(GL_QUADS);
            V3 sp3 = sd * 540.f;
            CamBasis save = cam;
            glowBillboard(sp3, 170.f, Col(1.0f, 0.55f, 0.25f, 0.55f));
            glowBillboard(sp3, 60.f, Col(1.0f, 0.85f, 0.6f, 0.9f));
            cam = save;
            glEnd();
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        glPopMatrix();

        // opaque world
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
        glEnable(GL_FOG);
        glFogf(GL_FOG_START, retro ? 22.f : 45.f);   // short, thick fog hides the draw distance, like the old days
        glFogf(GL_FOG_END, retro ? 250.f : 400.f);
        float fc[4] = {hz.r, hz.g, hz.b, 1};
        glFogfv(GL_FOG_COLOR, fc);
        glEnable(GL_LIGHTING);
        V3 sd = sunDir();
        float lp[4] = {sd.x, sd.y, sd.z, 0};
        glLightfv(GL_LIGHT0, GL_POSITION, lp);
        glEnable(GL_TEXTURE_2D);
        glColor4f(1, 1, 1, 1);
        glCallList(world.L_ground);
        glCallList(world.L_road);
        glCallList(world.L_scene);
        if (withCarFx || scene != SC_RACE || true) drawCar(car, car.lights);

        // blended: shadows, skid marks
        glDisable(GL_LIGHTING);
        glDepthMask(GL_FALSE);
        glEnable(GL_TEXTURE_2D);
        glCallList(world.L_shadows);
        {
            glBindTexture(GL_TEXTURE_2D, world.tx.shadow);
            glBegin(GL_QUADS);
            float sdx = -sd.x, sdz = -sd.z, sl = std::sqrt(sdx * sdx + sdz * sdz);
            sdx /= sl; sdz /= sl;
            setCol(Col(1, 1, 1, 0.85f));
            g3::groundQuad(car.x + sdx * 0.5f, car.z + sdz * 0.5f, 0.09f, 1.35f, 2.9f, car.yaw);
            glEnd();
        }
        if (!skids.empty()) {
            glDisable(GL_TEXTURE_2D);
            glBegin(GL_QUADS);
            for (const Skid& s : skids) {
                float dx = s.x2 - s.x1, dz = s.z2 - s.z1, l = std::sqrt(dx * dx + dz * dz);
                if (l < 1e-4f || l > 3.f) continue;
                float px = -dz / l * 0.14f, pz = dx / l * 0.14f;
                glColor4f(0.02f, 0.02f, 0.02f, s.a);
                glVertex3f(s.x1 - px, 0.085f, s.z1 - pz); glVertex3f(s.x1 + px, 0.085f, s.z1 + pz);
                glVertex3f(s.x2 + px, 0.085f, s.z2 + pz); glVertex3f(s.x2 - px, 0.085f, s.z2 - pz);
            }
            glEnd();
            glEnable(GL_TEXTURE_2D);
        }

        // additive lights (fog fades to black so glows disappear into the distance)
        float black[4] = {0, 0, 0, 1};
        glFogfv(GL_FOG_COLOR, black);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glBindTexture(GL_TEXTURE_2D, world.tx.glow);
        glCallList(world.L_pools);
        glBindTexture(GL_TEXTURE_2D, world.tx.glow);
        glBegin(GL_QUADS);
        if (car.lights) {
            for (int sx = -1; sx <= 1; sx += 2) {
                float bx, bz;
                carToWorld(car, sx * 0.7f, 13.5f, bx, bz);
                glColor4f(1.0f, 0.93f, 0.7f, 0.5f);
                g3::groundQuad(bx, bz, 0.12f, 3.4f, 12.5f, car.yaw);
            }
        }
        if (car.braking) {
            float bx, bz;
            carToWorld(car, 0, -4.2f, bx, bz);
            glColor4f(1.0f, 0.1f, 0.05f, 0.45f);
            g3::groundQuad(bx, bz, 0.12f, 2.3f, 3.2f, car.yaw);
        }
        glEnd();
        glBegin(GL_QUADS);
        for (const Lamp& l : world.lamps) {
            float dx = l.head.x - eye.x, dz = l.head.z - eye.z, d = std::sqrt(dx * dx + dz * dz);
            if (d > 330.f) continue;
            glowBillboard(l.head, 2.6f, Col(1.0f, 0.82f, 0.5f, sat(1.2f - d / 330.f)));
            glowBillboard(l.head, 0.7f, Col(1, 1, 0.9f, sat(1.3f - d / 330.f)));
        }
        {
            Car& c = car;
            float hy = 0.06f + 0.58f;
            float wx, wz;
            if (c.lights)
                for (int sx = -1; sx <= 1; sx += 2) {
                    carToWorld(c, sx * 0.62f, 2.32f, wx, wz);
                    glowBillboard(V3(wx, hy, wz), 0.8f, Col(1.0f, 0.95f, 0.75f, 0.85f));
                }
            float ta = c.braking ? 1.0f : (c.lights ? 0.38f : 0.12f);
            for (int sx = -1; sx <= 1; sx += 2) {
                carToWorld(c, sx * 0.6f, -2.35f, wx, wz);
                glowBillboard(V3(wx, 0.06f + 0.72f, wz), c.braking ? 0.95f : 0.55f, Col(1.0f, 0.1f, 0.06f, ta));
            }
        }
        glEnd();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glFogfv(GL_FOG_COLOR, fc);
        glDepthMask(GL_TRUE);
        if (retro) blitLowRes(vw, vh);
    }

    //  retro presentation
    // 1) the low-res 3D image is copied into lowTex and drawn pixel-exact (x2) into the 640x360 frame,
    // 2) the HUD is drawn on top at that frame size,
    // 3) the whole frame is read back, snapped to a 6x7x6 (252 colour) palette with an ordered dither
    //    and stretched to the window with nearest-neighbour filtering.
    static constexpr bool retro = true;
    GLuint lowTex = 0, finTex = 0;
    std::vector<uint8_t> pbuf;
    uint8_t qlut[3][16][256];
    int appliedFilter = -1;

    static GLuint makeTarget(int w, int h) {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        return id;
    }
    void initRetro() {
        lowTex = makeTarget(512, 256);
        finTex = makeTarget(1024, 512);
        static const int bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
        static const int levels[3] = {6, 7, 6};
        for (int c = 0; c < 3; c++)
            for (int t = 0; t < 16; t++)
                for (int v = 0; v < 256; v++) {
                    float f = v / 255.f * (levels[c] - 1) + (bayer[t] + 0.5f) / 16.f;
                    int q = std::min(levels[c] - 1, (int)std::floor(f));
                    qlut[c][t][v] = (uint8_t)(q * 255.f / (levels[c] - 1) + 0.5f);
                }
    }
    static void fullscreenQuad(float u1, float v1) {
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1, 0, 1, -1, 1);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_FOG); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(0, 0);
        glTexCoord2f(u1, 0); glVertex2f(1, 0);
        glTexCoord2f(u1, v1); glVertex2f(1, 1);
        glTexCoord2f(0, v1); glVertex2f(0, 1);
        glEnd();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }
    void blitLowRes(int vw, int vh) {
        glBindTexture(GL_TEXTURE_2D, lowTex);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, vw, vh);
        glViewport(0, 0, view.winW, view.winH);
        glColor4f(1, 1, 1, 1);
        fullscreenQuad(vw / 512.f, vh / 256.f);
    }
    void presentRetro(int dw, int dh) {
        const int iw = view.winW, ih = view.winH;
        pbuf.resize((size_t)iw * ih * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, iw, ih, GL_RGB, GL_UNSIGNED_BYTE, pbuf.data());
        for (int y = 0; y < ih; y++) {
            uint8_t* row = &pbuf[(size_t)y * iw * 3];
            for (int x = 0; x < iw; x++) {
                int t = (y & 3) * 4 + (x & 3);
                row[x * 3] = qlut[0][t][row[x * 3]];
                row[x * 3 + 1] = qlut[1][t][row[x * 3 + 1]];
                row[x * 3 + 2] = qlut[2][t][row[x * 3 + 2]];
            }
        }
        glBindTexture(GL_TEXTURE_2D, finTex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, iw, ih, GL_RGB, GL_UNSIGNED_BYTE, pbuf.data());
        glViewport(0, 0, dw, dh);
        glDepthMask(GL_TRUE);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glColor4f(1, 1, 1, 1);
        fullscreenQuad(iw / 1024.f, ih / 512.f);
    }

    //  cameras
    void raceCamera(float dt, V3& eye, V3& target) {
        Car& c = car;
        float spd = c.speed();
        float velYaw = spd > 6.f ? std::atan2(c.vx, c.vz) : c.yaw;
        float tgt = c.yaw + 0.3f * wrapPi(velYaw - c.yaw);
        if (c.fwdSpeed() < -2.f) tgt = c.yaw;
        camYaw += wrapPi(tgt - camYaw) * approachK(6.5f, dt);
        float off = 0.f;
        if (scene == SC_RACE && phase == RP_COUNTDOWN) off = (1.f - smooth01(phaseT / 3.8f)) * 2.6f;
        float yaw = camYaw + off;
        float dist = 7.4f + spd * 0.018f + off * 1.2f, h = 2.5f + spd * 0.011f;
        float sh = camShake * 0.25f;
        eye = V3(c.x - std::sin(yaw) * dist + sh * std::sin((float)timeNow * 70.f), h + sh * std::sin((float)timeNow * 63.f), c.z - std::cos(yaw) * dist);
        target = V3(c.x + std::sin(camYaw) * 4.f, 1.05f, c.z + std::cos(camYaw) * 4.f);
        camFov = 60.f + clampf(spd, 0.f, 65.f) * 0.14f;
    }

    void menuCamera(V3& eye, V3& target) {
        const Track& T = world.track;
        // car parked at the grid, camera orbiting
        float sh = smooth01(menuT);
        float dist = lerpf(10.5f, 6.4f, sh), ang = orbit;
        float h = lerpf(2.6f, 1.5f + 0.5f * std::sin(orbit * 0.7f), sh);
        V3 cp(car.x, 0, car.z);
        eye = V3(cp.x + std::sin(ang) * dist, h, cp.z + std::cos(ang) * dist);
        V3 look(cp.x, 0.8f, cp.z);
        V3 f = norm3(look - eye);
        V3 r = norm3(cross3(f, V3(0, 1, 0)));
        target = look - r * lerpf(0.f, 1.9f, sh);  // car appears on the right side when the menu is open
        camFov = 52.f;
        (void)T;
    }

    //  frame render
    void render() {
        int dw, dh;
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        dw = std::max(1, dw); dh = std::max(1, dh);
        if (loaded && appliedFilter != 1) { setRetroFilter(true); appliedFilter = 1; }
        if (retro) {
            // fixed 360-line internal frame (640x360 at 16:9); width follows the window aspect
            int ih = 360, iw = ((int)(ih * (float)dw / dh * 0.5f + 0.5f)) * 2;
            iw = std::max(480, std::min(1024, iw));
            view = View2D(); view.winW = iw; view.winH = ih; view.k = 1; view.W = (float)iw; view.H = (float)ih;
        } else view = makeView(dw, dh);
        glViewport(0, 0, view.winW, view.winH);
        renderFrame();
        if (retro) presentRetro(dw, dh);
    }
    void renderFrame() {
        if (scene == SC_SPLASH) { renderSplash(); return; }
        V3 eye, target;
        static float lastT = 0;
        float dt = 1.f / 60.f; (void)lastT;
        if (scene == SC_RACE) raceCamera(selftest ? 1.f / 60.f : frameDt, eye, target);
        else {
            ensureMenuCar();
            menuCamera(eye, target);
        }
        (void)dt;
        drawWorld(eye, target, camFov, scene == SC_RACE);
        begin2D(view);
        switch (scene) {
            case SC_TITLE: case SC_MENU: renderTitleMenu(); break;
            case SC_OPTIONS: renderOptions(); break;
            case SC_CREDITS: renderCredits(); break;
            case SC_RACE: renderHUD(); break;
            default: break;
        }
        // fade from black after the splash
        if (scene == SC_TITLE || scene == SC_MENU) {
            float a = 1.f - smooth01(titleFade());
            if (a > 0.001f) rect(0, 0, view.W, view.H, Col(0, 0, 0, a));
        }
    }
    float frameDt = 1.f / 60.f;
    float titleFade() const { return sceneT / 1.2f; }

    void ensureMenuCar() {
        if (menuCarReady) return;
        menuCarReady = true;
        startRace();  // positions the car on the grid; menu uses it as a display model
        SDL_ShowCursor(SDL_ENABLE);
        car.lights = true;
        car.braking = true;
    }
    bool menuCarReady = false;

    //  2D: logo, menus
    void drawLogo(float x, float y, float scale) {
        const std::string t = "CREWN";
        float sh = 0.18f;
        // outline
        float o = scale * 0.55f;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (dx || dy) font.draw(x + dx * o, y + dy * o, t, scale, Col(0.04f, 0.02f, 0.02f, 0.95f), Col(0.04f, 0.02f, 0.02f, 0.95f), sh);
        // drop shadow
        font.draw(x + scale * 0.9f, y + scale * 1.4f, t, scale, Col(0, 0, 0, 0.5f), Col(0, 0, 0, 0.5f), sh);
        // fatten strokes, metallic orange gradient
        float f = scale * 0.32f;
        for (int dy = -1; dy <= 1; dy += 2)
            for (int dx = -1; dx <= 1; dx += 2)
                font.draw(x + dx * f, y + dy * f, t, scale, Col(1.0f, 0.97f, 0.8f), Col(1.0f, 0.42f, 0.05f), sh);
        font.draw(x, y, t, scale, Col(1.0f, 0.97f, 0.8f), Col(1.0f, 0.42f, 0.05f), sh);
    }

    void drawMenu(Menu& m, float x, float y, float w, float rowH, float scale, bool centered) {
        m.rx = x; m.ry = y; m.rw = w; m.rh = rowH;
        for (int i = 0; i < (int)m.items.size(); i++) {
            float yy = y + i * rowH;
            bool sel = i == m.sel;
            std::string label = m.items[i].label ? m.items[i].label() : "";
            float tw = font.width(label, scale);
            float tx = centered ? x + (w - tw) * 0.5f : x + 14.f;
            if (sel) {
                float pulse = 0.78f + 0.22f * std::sin((float)timeNow * 6.f);
                rectGradH(x, yy, w, rowH - 3, Col(1.0f, 0.5f, 0.05f, 0.85f * pulse), Col(1.0f, 0.5f, 0.05f, 0.0f));
                rect(x, yy, 3, rowH - 3, Col(1, 0.85f, 0.4f, 1));
                font.draw(tx + 0.5f, yy + (rowH - 3 - 7 * scale) * 0.5f + 1, label, scale, Col(0, 0, 0, 0.6f));
                font.draw(tx, yy + (rowH - 3 - 7 * scale) * 0.5f, label, scale, Col(1, 1, 1));
            } else {
                font.drawShadow(tx, yy + (rowH - 3 - 7 * scale) * 0.5f, label, scale, Col(0.82f, 0.85f, 0.95f));
            }
        }
    }

    void renderTitleMenu() {
        float sh = smooth01(menuT);
        float W = view.W, H = view.H;
        // vignette
        rectGradV(0, 0, W, 60, Col(0, 0, 0, 0.55f), Col(0, 0, 0, 0));
        rectGradV(0, H - 70, W, 70, Col(0, 0, 0, 0), Col(0, 0, 0, 0.65f));
        float ls = std::max(6.f, std::min(13.f, std::floor(W / 52.f)));
        float lw = font.width("CREWN", ls);
        float lx = lerpf((W - lw) * 0.5f, 34.f, sh);
        float ly = lerpf(H * 0.16f, 26.f, sh);
        float lsc = lerpf(ls, std::max(5.f, ls * 0.62f), sh);
        lw = font.width("CREWN", lsc);
        lx = lerpf((W - font.width("CREWN", ls)) * 0.5f, 34.f, sh);
        drawLogo(lx, ly, lsc);
        font.drawShadow(lx + 4, ly + 7 * lsc + 12, "RACING GAME", 2.f, Col(1.0f, 0.78f, 0.4f).withA(1));
        if (sh < 0.98f) {
            float a = (1.f - sh);
            bool blink = std::fmod((float)timeNow, 1.1f) < 0.7f;
            if (blink) font.drawCentered(W * 0.5f, H * 0.80f, "PRESS ANY KEY", 3.f, Col(1, 1, 1, a));
        }
        if (sh > 0.01f) {
            float mx = 34.f, my = std::max(ly + 7 * lsc + 52.f, H * 0.42f), mw = 250.f, rh = 30.f;
            float slide = (1.f - sh) * 60.f;
            glPushMatrix();
            glTranslatef(-slide, 0, 0);
            drawMenu(mainMenu, mx, my, mw, rh, 2.f, false);
            glPopMatrix();
            mainMenu.rx = mx - slide;
            font.drawShadow(mx, H - 40, "UP/DOWN + ENTER, or use the mouse", 1.f, Col(0.75f, 0.78f, 0.9f, sh));
            font.drawShadow(mx, H - 28, "Best lap: " + (set.bestLap > 0 ? fmtTime(set.bestLap) : std::string("--:--.---")), 1.f, Col(1, 0.8f, 0.4f, sh));
        }
        font.drawRight(W - 10, H - 14, std::string("(c) 2026 ") + STUDIO, 1.f, Col(0.8f, 0.8f, 0.9f, 0.9f));
    }

    void panel(float x, float y, float w, float h) {
        rect(x - 2, y - 2, w + 4, h + 4, Col(0, 0, 0, 0.35f));
        rectGradV(x, y, w, h, Col(0.10f, 0.11f, 0.16f, 0.88f), Col(0.04f, 0.04f, 0.07f, 0.92f));
        rect(x, y, w, 2, Col(1.0f, 0.5f, 0.05f, 1));
        frame(x, y, w, h, Col(0.5f, 0.52f, 0.62f, 0.8f));
    }

    void renderOptions() {
        float W = view.W, H = view.H;
        rect(0, 0, W, H, Col(0, 0, 0, 0.45f));
        float pw = 340, ph = 262, px = (W - pw) * 0.5f, py = (H - ph) * 0.5f;
        panel(px, py, pw, ph);
        font.drawCentered(W * 0.5f, py + 12, "OPTIONS", 3.f, Col(1, 0.75f, 0.3f));
        drawMenu(optMenu, px + 16, py + 48, pw - 32, 26.f, 2.f, false);
        font.drawCentered(W * 0.5f, py + ph - 14, "LEFT / RIGHT to change   ESC to go back", 1.f, Col(0.7f, 0.74f, 0.88f));
    }

    void renderCredits() {
        float W = view.W, H = view.H;
        rect(0, 0, W, H, Col(0, 0, 0, 0.6f));
        float pw = 400, ph = 250, px = (W - pw) * 0.5f, py = (H - ph) * 0.5f;
        panel(px, py, pw, ph);
        float cx = W * 0.5f, y = py + 14;
        font.drawCentered(cx, y, "CREDITS", 3.f, Col(1, 0.75f, 0.3f)); y += 40;
        font.drawCentered(cx, y, "CREWN", 2.f, Col(1, 1, 1)); y += 24;
        font.drawCentered(cx, y, "A game by", 1.f, Col(0.7f, 0.74f, 0.88f)); y += 14;
        font.drawCentered(cx, y, STUDIO, 3.f, Col(0.95f, 0.8f, 0.55f)); y += 36;
        font.drawCentered(cx, y, "Made with C++, SDL2 and OpenGL", 1.f, Col(0.7f, 0.74f, 0.88f)); y += 22;
        font.drawCentered(cx, y, "Thanks for driving!", 2.f, Col(1, 1, 1)); y += 26;
        if (std::fmod((float)timeNow, 1.2f) < 0.8f) font.drawCentered(cx, py + ph - 16, "PRESS ANY KEY", 1.f, Col(1, 0.8f, 0.4f));
    }

    //  splash screen
    void renderSplash() {
        float W, H;
        begin2D(view);
        W = view.W; H = view.H;
        float t = sceneT;
        float fin = smooth01(t / 1.1f);
        float fout = splashFadeOut >= 0 ? smooth01(splashFadeOut / 0.8f) : 0.f;
        glClearColor(0.02f, 0.02f, 0.025f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        // brushed metal
        glEnable(GL_TEXTURE_2D);
        if (world.tx.metal) {
            glBindTexture(GL_TEXTURE_2D, world.tx.metal);
            glColor4f(1, 1, 1, fin);
            glBegin(GL_QUADS);
            float rep = W / 256.f, repy = H / 256.f;
            glTexCoord2f(0, 0); glVertex2f(0, 0);
            glTexCoord2f(rep, 0); glVertex2f(W, 0);
            glTexCoord2f(rep, repy); glVertex2f(W, H);
            glTexCoord2f(0, repy); glVertex2f(0, H);
            glEnd();
        }
        glDisable(GL_TEXTURE_2D);
        // vignette
        float v = fin;
        rectGradH(0, 0, W * 0.35f, H, Col(0, 0, 0, 0.85f * v), Col(0, 0, 0, 0));
        rectGradH(W * 0.65f, 0, W * 0.35f, H, Col(0, 0, 0, 0), Col(0, 0, 0, 0.85f * v));
        rectGradV(0, 0, W, H * 0.3f, Col(0, 0, 0, 0.8f * v), Col(0, 0, 0, 0));
        rectGradV(0, H * 0.7f, W, H * 0.3f, Col(0, 0, 0, 0), Col(0, 0, 0, 0.85f * v));
        float cx = W * 0.5f, cy = H * 0.5f;
        // molten glow
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        float gl = fin * (0.6f + 0.1f * std::sin(t * 3.f));
        rectGradV(cx - 180, cy + 6, 360, 46, Col(1.0f, 0.35f, 0.02f, 0.0f), Col(1.0f, 0.35f, 0.02f, 0.18f * gl));
        rectGradV(cx - 180, cy - 44, 360, 50, Col(1.0f, 0.35f, 0.02f, 0.0f), Col(1.0f, 0.4f, 0.05f, 0.10f * gl));
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // Studio name with the studio label beneath it
        {
            float sc = std::max(2.f, std::min(7.f, std::floor(W * 0.74f / (6.f * 15.f))));
            std::string name = STUDIO;
            std::string studio = "STUDIO";
            float tw = font.width(name, sc);
            float x = cx - tw * 0.5f, y = cy - 4.5f * sc;
            font.draw(x + sc * 0.7f, y + sc * 0.9f, name, sc, Col(0, 0, 0, 0.7f * fin), Col(0, 0, 0, 0.7f * fin));
            // glow behind
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            font.draw(x - sc * 0.3f, y - sc * 0.3f, name, sc, Col(1.0f, 0.4f, 0.05f, 0.28f * fin), Col(1.0f, 0.3f, 0.02f, 0.28f * fin));
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            font.draw(x, y, name, sc, Col(0.97f, 0.98f, 1.0f, fin), Col(0.52f, 0.55f, 0.64f, fin));
            font.draw(x + sc * 0.3f, y + sc * 0.3f, name, sc, Col(0.97f, 0.98f, 1.0f, fin), Col(0.52f, 0.55f, 0.64f, fin));
            // highlight sweep, per glyph
            float sweep = -60.f + std::fmod(std::max(0.f, t - 0.9f), 2.6f) / 2.6f * (tw + 120.f);
            const char* p = name.c_str();
            float gx = x;
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            while (*p) {
                const char* st = p; Font::decode(p);
                std::string ch(st, p - st);
                float d = (gx + 3 * sc - (x + sweep)) / (34.f);
                float a = std::exp(-d * d) * 0.85f * fin;
                if (a > 0.02f) font.draw(gx, y, ch, sc, Col(1, 1, 1, a), Col(1, 0.9f, 0.7f, a * 0.6f));
                gx += 6 * sc;
            }
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            // molten line
            rectGradH(cx - tw * 0.55f, y + 7 * sc + 14, tw * 0.55f, 2, Col(1, 0.45f, 0.05f, 0), Col(1, 0.55f, 0.1f, fin));
            rectGradH(cx, y + 7 * sc + 14, tw * 0.55f, 2, Col(1, 0.55f, 0.1f, fin), Col(1, 0.45f, 0.05f, 0));
            float studioScale = std::max(1.5f, sc * 0.42f);
            font.drawCentered(cx, y + 7 * sc + 28, studio, studioScale, Col(0.78f, 0.81f, 0.9f, fin));
        }
        // sparks
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        for (auto& s : sparks) {
            float a = 1.f - s.life / s.max;
            rect(s.x, s.y, 1.5f, 1.5f, Col(1.0f, 0.55f + 0.3f * a, 0.1f, a * fin));
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // loading indicator
        {
            float bw = 220, bh = 7, bx = cx - bw * 0.5f, by = cy + 78;
            float pr = loadProgress();
            static float shown = 0; shown += (pr - shown) * 0.12f;
            if (selftest) shown = pr;
            rect(bx - 2, by - 2, bw + 4, bh + 4, Col(0.05f, 0.05f, 0.06f, fin));
            frame(bx - 2, by - 2, bw + 4, bh + 4, Col(0.5f, 0.52f, 0.6f, fin));
            rectGradV(bx, by, bw * shown, bh, Col(1.0f, 0.75f, 0.3f, fin), Col(0.9f, 0.3f, 0.02f, fin));
            if (!loaded) {
                int dots = (int)(t * 3.f) % 4;
                font.drawCentered(cx, by + 16, std::string("LOADING") + std::string(dots, '.'), 1.f, Col(0.75f, 0.78f, 0.88f, fin));
            } else {
                font.drawCentered(cx, by + 16, "READY", 1.f, Col(0.75f, 0.78f, 0.88f, fin));
            }
        }
        if (fout > 0) rect(0, 0, W, H, Col(0, 0, 0, fout));
        // Note: the studio screen deliberately says nothing else; all other game text is English.
    }

    //  HUD
    void drawSpeedo(float cx, float cy, float r) {
        float kmh = car.speed() * 3.6f;
        if (car.fwdSpeed() < -0.5f) kmh = car.speed() * 3.6f;
        disc(cx, cy, r + 3, 48, Col(0.02f, 0.02f, 0.03f, 0.7f));
        disc(cx, cy, r, 48, Col(0.07f, 0.08f, 0.11f, 0.85f));
        // rpm ring (segmented)
        float rp = sat((car.rpm - 900.f) / 6500.f);
        for (int i = 0; i < 24; i++) {
            float a0 = (225.f - i * 270.f / 24.f) * PI / 180.f, a1 = (225.f - (i + 0.8f) * 270.f / 24.f) * PI / 180.f;
            bool on = (i + 1) / 24.f <= rp + 0.001f;
            Col c = i > 19 ? Col(1, 0.15f, 0.1f) : (i > 14 ? Col(1, 0.75f, 0.1f) : Col(0.3f, 0.9f, 0.4f));
            if (!on) c = c.scaled(0.18f);
            glDisable(GL_TEXTURE_2D);
            glBegin(GL_QUADS);
            setCol(c);
            float r0 = r - 2, r1 = r + 1.5f;
            glVertex2f(cx + std::cos(a0) * r0, cy - std::sin(a0) * r0);
            glVertex2f(cx + std::cos(a0) * r1, cy - std::sin(a0) * r1);
            glVertex2f(cx + std::cos(a1) * r1, cy - std::sin(a1) * r1);
            glVertex2f(cx + std::cos(a1) * r0, cy - std::sin(a1) * r0);
            glEnd();
        }
        float ri = r - 5.f;
        for (int v = 0; v <= 280; v += 10) {
            float a = (225.f - v * 270.f / 280.f) * PI / 180.f;
            bool major = v % 40 == 0;
            float r0 = ri - (major ? 8.f : 4.f), r1 = ri;
            Col c = v >= 240 ? Col(1, 0.3f, 0.2f) : Col(0.9f, 0.92f, 1.0f);
            line2(cx + std::cos(a) * r0, cy - std::sin(a) * r0, cx + std::cos(a) * r1, cy - std::sin(a) * r1, c, major ? 2.f * view.k : 1.f * view.k);
            if (major) {
                std::string s = std::to_string(v);
                float lr = ri - 15.f;
                font.draw(cx + std::cos(a) * lr - font.width(s, 1.f) * 0.5f, cy - std::sin(a) * lr - 3.5f, s, 1.f, c);
            }
        }
        // needle
        float a = (225.f - clampf(kmh, 0.f, 285.f) * 270.f / 280.f) * PI / 180.f;
        float ca = std::cos(a), sa = std::sin(a);
        glDisable(GL_TEXTURE_2D);
        glBegin(GL_TRIANGLES);
        setCol(Col(1.0f, 0.2f, 0.12f, 1));
        glVertex2f(cx + ca * (ri - 3), cy - sa * (ri - 3));
        glVertex2f(cx - sa * 2.2f - ca * 6, cy - ca * 2.2f + sa * 6);
        glVertex2f(cx + sa * 2.2f - ca * 6, cy + ca * 2.2f + sa * 6);
        glEnd();
        disc(cx, cy, 4.5f, 16, Col(0.15f, 0.15f, 0.18f));
        disc(cx, cy, 2.5f, 12, Col(0.7f, 0.72f, 0.78f));
        // digital readout
        std::string ks = std::to_string((int)(kmh + 0.5f));
        font.drawCentered(cx, cy + r * 0.28f, ks, 3.f, Col(1, 1, 1));
        font.drawCentered(cx, cy + r * 0.28f + 26, "KM/H", 1.f, Col(1.0f, 0.75f, 0.3f));
        std::string g = car.reversing || car.fwdSpeed() < -0.5f ? "R" : (car.speed() < 0.5f ? "N" : std::to_string(car.gear));
        float gx = cx, gy = cy - r * 0.42f;
        rect(gx - 9, gy - 9, 18, 18, Col(0, 0, 0, 0.5f));
        frame(gx - 9, gy - 9, 18, 18, Col(0.6f, 0.62f, 0.7f, 0.7f));
        font.drawCentered(gx, gy - 7, g, 2.f, Col(1, 0.85f, 0.3f), false);
    }

    void drawMinimap(float x, float y, float w, float h) {
        const Track& T = world.track;
        rect(x - 3, y - 3, w + 6, h + 6, Col(0, 0, 0, 0.35f));
        rect(x, y, w, h, Col(0.03f, 0.04f, 0.07f, 0.65f));
        frame(x, y, w, h, Col(0.5f, 0.52f, 0.62f, 0.8f));
        float pad = 8;
        float sx = (w - pad * 2) / (T.maxX - T.minX), sz = (h - pad * 2) / (T.maxZ - T.minZ), s = std::min(sx, sz);
        float ox = x + (w - (T.maxX - T.minX) * s) * 0.5f, oy = y + (h + (T.maxZ - T.minZ) * s) * 0.5f;
        auto MX = [&](float wx) { return ox + (wx - T.minX) * s; };
        auto MY = [&](float wz) { return oy - (wz - T.minZ) * s; };
        for (int pass = 0; pass < 2; pass++) {
            glDisable(GL_TEXTURE_2D);
            glLineWidth(pass == 0 ? 4.f * view.k : 2.f * view.k);
            setCol(pass == 0 ? Col(0, 0, 0, 0.8f) : Col(0.85f, 0.88f, 1.0f, 0.95f));
            glBegin(GL_LINE_LOOP);
            for (size_t i = 0; i < world.minimapX.size(); i++) glVertex2f(MX(world.minimapX[i]), MY(world.minimapZ[i]));
            glEnd();
        }
        const TrackPoint& sp0 = T.p[0];
        float cxm = MX(sp0.x), cym = MY(sp0.z);
        rect(cxm - 4, cym - 1, 8, 2, Col(1, 1, 1, 1));
        float px = MX(car.x), py = MY(car.z);
        disc(px, py, 3.2f, 12, Col(0, 0, 0, 0.9f));
        disc(px, py, 2.2f, 12, Col(1.0f, 0.5f, 0.05f, 1));
        line2(px, py, px + std::sin(car.yaw) * 7, py - std::cos(car.yaw) * 7, Col(1, 0.9f, 0.5f), 1.5f * view.k);
    }

    void renderHUD() {
        float W = view.W, H = view.H;
        // top-left info panel
        float pw = 142, ph = 74, px = 10, py = 10;
        rect(px, py, pw, ph, Col(0.02f, 0.02f, 0.04f, 0.6f));
        rect(px, py, 3, ph, Col(1.0f, 0.5f, 0.05f, 1));
        int shownLap = std::min(totalLaps, std::max(1, lapsDone + 1));
        font.drawShadow(px + 10, py + 6, "LAP " + std::to_string(shownLap) + "/" + std::to_string(totalLaps), 2.f, Col(1, 0.85f, 0.35f));
        font.drawShadow(px + 10, py + 26, "TIME  " + fmtTime(raceTime), 1.f, Col(1, 1, 1));
        float curLap = raceTime - lapStart;
        font.drawShadow(px + 10, py + 38, "LAP   " + fmtTime(phase == RP_COUNTDOWN ? 0.f : curLap), 1.f, Col(0.85f, 0.9f, 1));
        float best = bestThisRace > 0 ? bestThisRace : 0.f;
        font.drawShadow(px + 10, py + 50, "BEST  " + (best > 0 ? fmtTime(best) : std::string("-:--.---")), 1.f, Col(0.5f, 1.0f, 0.55f));
        font.drawShadow(px + 10, py + 62, "LAST  " + (lapTimes.empty() ? std::string("-:--.---") : fmtTime(lapTimes.back())), 1.f, Col(0.8f, 0.8f, 0.9f));
        // minimap
        drawMinimap(W - 112, 10, 102, 112);
        // speedometer
        float r = 62;
        drawSpeedo(W - r - 18, H - r - 16, r);
        // lights indicator
        font.drawShadow(W - 112, 128, car.lights ? "LIGHTS ON  [L]" : "LIGHTS OFF [L]", 1.f, car.lights ? Col(0.7f, 0.9f, 1.0f) : Col(0.6f, 0.6f, 0.65f));
        // controls hint
        if (phase == RP_COUNTDOWN || (phase == RP_RACING && raceTime < 6.f)) {
            font.drawCentered(W * 0.5f, H - 24, "W/UP accelerate   S/DOWN brake & reverse   A/D steer   SPACE handbrake", 1.f, Col(1, 1, 1, 0.9f));
            font.drawCentered(W * 0.5f, H - 12, "L lights   T reset car   R restart   ESC pause", 1.f, Col(0.8f, 0.85f, 1, 0.9f));
        }
        // countdown
        if (phase == RP_COUNTDOWN) {
            float t = phaseT - 0.8f;
            if (t >= 0) {
                int n = 3 - (int)t; float f = t - std::floor(t);
                std::string s = std::to_string(std::max(1, n));
                float sc = 14.f * (1.f + (1.f - f) * 0.35f);
                float a = sat(1.f - f * 0.6f);
                font.draw((W - font.width(s, sc)) * 0.5f + sc * 0.5f, H * 0.28f + sc * 0.5f, s, sc, Col(0, 0, 0, a * 0.6f));
                font.draw((W - font.width(s, sc)) * 0.5f, H * 0.28f, s, sc, Col(1.0f, 0.95f, 0.7f, a), Col(1.0f, 0.45f, 0.05f, a));
            }
            font.drawCentered(W * 0.5f, H * 0.14f, "GET READY", 3.f, Col(1, 1, 1));
        }
        if (msgT > 0 && phase != RP_COUNTDOWN) {
            float a = sat(msgT / 0.4f);
            font.drawCentered(W * 0.5f, H * 0.2f, msg, 3.f, msgCol.withA(a));
        }
        if (wrongWayT > 1.2f && phase == RP_RACING && std::fmod((float)timeNow, 0.7f) < 0.4f)
            font.drawCentered(W * 0.5f, H * 0.34f, "WRONG WAY", 4.f, Col(1, 0.15f, 0.1f));
        if (phase == RP_RACING && car.offroad > 0.6f && car.speed() > 8.f)
            font.drawCentered(W * 0.5f, H * 0.42f, "OFF TRACK", 2.f, Col(1, 0.8f, 0.2f, 0.9f));
        if (phase == RP_FINISHED && finishT <= 2.2f) {
            font.drawCentered(W * 0.5f, H * 0.28f, "RACE COMPLETE", 5.f, Col(1.0f, 0.9f, 0.4f), true);
        }
        if (phase == RP_FINISHED && finishT > 2.2f) renderResults();
        if (paused) {
            rect(0, 0, W, H, Col(0, 0, 0, 0.55f));
            float pw2 = 280, ph2 = 170, ppx = (W - pw2) * 0.5f, ppy = (H - ph2) * 0.5f;
            panel(ppx, ppy, pw2, ph2);
            font.drawCentered(W * 0.5f, ppy + 12, "PAUSED", 3.f, Col(1, 0.75f, 0.3f));
            drawMenu(pauseMenu, ppx + 16, ppy + 54, pw2 - 32, 30.f, 2.f, false);
        }
    }

    void renderResults() {
        float W = view.W, H = view.H;
        rect(0, 0, W, H, Col(0, 0, 0, 0.55f));
        float pw = 360, ph = 100 + 14 * (float)lapTimes.size() + 130, px = (W - pw) * 0.5f, py = std::max(8.f, (H - ph) * 0.5f);
        panel(px, py, pw, ph);
        float cx = W * 0.5f, y = py + 12;
        font.drawCentered(cx, y, "RACE COMPLETE", 3.f, Col(1.0f, 0.85f, 0.35f)); y += 34;
        font.drawCentered(cx, y, "TOTAL TIME", 1.f, Col(0.7f, 0.74f, 0.88f)); y += 12;
        font.drawCentered(cx, y, fmtTime(raceTime), 3.f, Col(1, 1, 1)); y += 32;
        for (size_t i = 0; i < lapTimes.size(); i++) {
            bool b = lapTimes[i] <= bestThisRace + 1e-4f;
            Col c = b ? Col(0.5f, 1.0f, 0.55f) : Col(0.85f, 0.88f, 0.95f);
            font.draw(px + 50, y, "LAP " + std::to_string(i + 1), 1.f, c);
            font.drawRight(px + pw - 50, y, fmtTime(lapTimes[i]) + (b ? "  BEST" : ""), 1.f, c, false);
            y += 14;
        }
        y += 6;
        char buf[96];
        float avg = raceTime > 0 ? car.distance / raceTime * 3.6f : 0.f;
        std::snprintf(buf, sizeof buf, "Top speed %d KM/H    Average %d KM/H", (int)(car.topSpeed * 3.6f + 0.5f), (int)(avg + 0.5f));
        font.drawCentered(cx, y, buf, 1.f, Col(0.8f, 0.84f, 0.95f)); y += 14;
        if (newRecord) {
            if (std::fmod((float)timeNow, 0.8f) < 0.55f) font.drawCentered(cx, y, "NEW LAP RECORD!", 2.f, Col(1.0f, 0.85f, 0.2f));
        } else if (set.bestLap > 0) font.drawCentered(cx, y, "Record lap " + fmtTime(set.bestLap), 1.f, Col(1, 0.8f, 0.4f));
        y += 20;
        drawMenu(resultsMenu, px + 60, y, pw - 120, 28.f, 2.f, true);
    }

    //  main loop + selftest
    void saveShot(const std::string& name) {
        int w, h; SDL_GL_GetDrawableSize(win, &w, &h);
        std::vector<uint8_t> px((size_t)w * h * 3), fl((size_t)w * h * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
        for (int y = 0; y < h; y++) std::memcpy(&fl[(size_t)y * w * 3], &px[(size_t)(h - 1 - y) * w * 3], (size_t)w * 3);
        std::string path = shotDir + "/" + name + ".ppm";
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        std::fwrite(fl.data(), 1, fl.size(), f);
        std::fclose(f);
    }
    void frameShot(const std::string& name) { render(); glFinish(); saveShot(name); }

    int runSelftest() {
        int fails = 0;
        auto check = [&](bool ok, const char* what) { std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what); if (!ok) fails++; };
        // splash
        scene = SC_SPLASH; sceneT = 2.0f; for (int i = 0; i < 40; i++) { updateSplash(0.03f); }
        sceneT = 2.6f; frameShot("01_splash");
        // title
        scene = SC_TITLE; sceneT = 5.f; menuT = 0; titleDone = false; timeNow = 0.2;
        frameShot("02_title");
        scene = SC_MENU; titleDone = true; menuT = 1.f; orbit = 0.9f; frameShot("03_menu");
        scene = SC_OPTIONS; frameShot("04_options");
        scene = SC_CREDITS; frameShot("05_credits");
        // menu logic, driven through the real event handler
        auto key = [&](SDL_Keycode k) { SDL_Event e; SDL_zero(e); e.type = SDL_KEYDOWN; e.key.keysym.sym = k; handleEvent(e); };
        scene = SC_MENU; menuT = 1.f; titleDone = true; mainMenu.sel = 0;
        key(SDLK_DOWN); check(mainMenu.sel == 1, "menu: DOWN moves the selection");
        key(SDLK_RETURN); check(scene == SC_OPTIONS, "menu: Options opens");
        check(optMenu.items.size() == 6, "options: Graphics mode is fixed to 1990s");
        int laps0 = set.laps; optMenu.sel = 4; key(SDLK_RIGHT); check(set.laps != laps0, "options: Race Laps changes");
        set.laps = laps0;
        frameShot("04b_options");
        key(SDLK_ESCAPE); check(scene == SC_MENU, "options: ESC returns to the menu");
        key(SDLK_DOWN); key(SDLK_RETURN); check(scene == SC_CREDITS, "menu: Credits opens");
        key(SDLK_SPACE); check(scene == SC_MENU, "credits: any key returns");
        mainMenu.sel = 3; key(SDLK_RETURN); check(!running, "menu: Exit quits"); running = true;
        mainMenu.sel = 0; key(SDLK_RETURN); check(scene == SC_RACE && phase == RP_COUNTDOWN, "menu: Start Race begins the countdown");
        key(SDLK_ESCAPE); check(paused, "race: ESC pauses"); key(SDLK_ESCAPE); check(!paused, "race: ESC resumes");
        scene = SC_MENU; mainMenu.sel = 0;
        // race: countdown
        startRaceScene();
        for (int i = 0; i < 120 * 2; i++) { update(1.f / 120.f); }
        frameShot("06_countdown");
        for (int i = 0; i < 120 * 3; i++) { update(1.f / 120.f); }
        check(phase == RP_RACING, "countdown reaches GO");
        // drive using autopilot
        autopilotOn = true;
        int shots = 0;
        double t = 0; float lastLapShown = 0;
        int steps = 0, maxSteps = 120 * 60 * 6;
        while (phase != RP_FINISHED && steps < maxSteps) {
            update(1.f / 120.f); steps++; t += 1.0 / 120.0;
            if (steps == 120 * 6 || steps == 120 * 20 || steps == 120 * 33) { frameShot(std::string("07_race_") + std::to_string(shots++)); }
            if (lapsDone > lastLapShown) { lastLapShown = (float)lapsDone; std::printf("lap %d done, lap time %s, trav=%.1f\n", lapsDone, fmtTime(lapTimes.back()).c_str(), car.trav); }
        }
        check(phase == RP_FINISHED, "race finishes (autopilot completes all laps)");
        check((int)lapTimes.size() == totalLaps, "lap count matches setting");
        std::printf("total race time %s, top speed %d km/h, distance %.0f m\n", fmtTime(raceTime).c_str(), (int)(car.topSpeed * 3.6f), car.distance);
        for (int i = 0; i < 120 * 4; i++) update(1.f / 120.f);
        frameShot("08_results");
        check(car.speed() < 3.f, "car rolls to a stop after finish");
        // wall collision test
        autopilotOn = false;
        startRace();
        for (int i = 0; i < 120 * 5; i++) update(1.f / 120.f);
        car.yaw += 1.2f; car.vx = std::sin(car.yaw) * 30; car.vz = std::cos(car.yaw) * 30;
        float maxD = 0;
        for (int i = 0; i < 120 * 3; i++) { Input hold; hold.throttle = 1; physics(1.f / 120.f, hold); maxD = std::max(maxD, std::fabs(loc.d)); }
        std::printf("max lateral offset after driving into barrier: %.2f (barrier at %.1f)\n", maxD, world.track.barrierOff);
        check(maxD < world.track.barrierOff + 0.3f, "barrier keeps the car on the circuit");
        // restart
        startRace();
        check(phase == RP_COUNTDOWN && lapsDone == 0 && raceTime == 0, "restart resets race");
        // paused HUD
        for (int i = 0; i < 120 * 5; i++) update(1.f / 120.f);
        paused = true; pauseMenu.sel = 1; frameShot("09_pause"); paused = false;
        // wrong geometry / night skid test visual
        car.vx = std::sin(car.yaw) * 40; car.vz = std::cos(car.yaw) * 40;
        for (int i = 0; i < 90; i++) { Input h; h.throttle = 0; h.brake = 1; physics(1.f / 120.f, h); }
        frameShot("10_braking");
        std::printf("selftest finished: %d failure(s)\n", fails);
        return fails ? 1 : 0;
    }

    void run() {
        Uint64 last = SDL_GetPerformanceCounter();
        double freq = (double)SDL_GetPerformanceFrequency();
        double acc = 0;
        const double step = 1.0 / 120.0;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) handleEvent(e);
            Uint64 now = SDL_GetPerformanceCounter();
            double dt = (double)(now - last) / freq;
            last = now;
            if (dt > 0.1) dt = 0.1;
            frameDt = (float)dt;
            acc += dt;
            int guard = 0;
            while (acc >= step && guard++ < 20) { update((float)step); acc -= step; }
            render();
            SDL_GL_SwapWindow(win);
        }
    }
};

int main(int argc, char** argv) {
    Game g;
    if (!g.init(argc, argv)) { g.shutdown(); return 1; }
    int rc = 0;
    if (g.selftest) rc = g.runSelftest();
    else g.run();
    g.shutdown();
    return rc;
}
