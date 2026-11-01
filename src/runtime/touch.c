/* On-screen touch controls (iOS, or SH2_TOUCH with SH2_GLES to test them on Linux): the touch controls
 * standard of the other ports (jk-files docs/HOUSE_STYLE.md section 3), drawn with the Jedi Knight port's
 * renderer (touchUiDraw.c, touchUiRender_gl.c).
 *
 * The controls are a virtual SDL gamepad, pressing the game's default pad bindings (dinput.c's pad_defaults);
 * a real controller, once connected, takes over and the overlay hides. Touches arrive on the main thread
 * (ios.c's event watch), the overlay is drawn on the game thread over each presented frame (touch_draw).
 * Sizes are in points (the window's units on iOS), times in milliseconds.
 *
 *   left 42%     the move stick, where the thumb lands (radius 60, 30% dead zone), and the Jedi Knight port's
 *                run marker (a double arrow) up to 100 pt above it: pushed 70 pt toward it, James runs
 *   elsewhere    a drag looks (the right stick: the search view); a quick tap is a mouse click where the game
 *                shows its cursor (dinput.c), else ACTION
 *   bottom right FIRE (aim and attack), AIM and ACTION round it; a drag on any of them looks too
 *   top left     MAP, ITEMS, the flashlight (its picture lit while the light is on)
 *                                      top right  MENU, the FPS readout left of it, CAMERA (when on) further left
 *   MENU         a tap pauses (or skips: the pad's Back); held 0.45 s, the tray opens under it:
 *                SENS, GYRO, OPACITY, FPS, CAMERA, CHEAT, CONTROLS, and the app's version below
 * While the game shows its mouse cursor (its menus), only the top row shows and a touch is a click. */
#if defined(SH2_IOS) || defined(SH2_TOUCH)
#include "rt.h"
#include "../d3d8/gl.h"
#include "touchUiDraw.h"
#include "touchUiRender_gl.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>

#ifndef SH2_VERSION_LABEL
#define SH2_VERSION_LABEL "dev"
#endif

/* ---- the standard's numbers ---- */
#define IDLE_ALPHA   0.65f  /* an untouched button; a touched one is at 1 */
#define SLOP         6.0f   /* a touch this near a button counts */
#define STICK_R      60.0f
#define KNOB_R       24.0f
#define STICK_DEAD   0.30f
#define STICK_SIDE   0.42f  /* of the width */
#define RUN_R        15.0f  /* the run marker (Jedi Knight's touchUi.c TU_RUN_*) */
#define RUN_DIST     100.0f
#define RUN_MIN_DIST 76.0f
#define RUN_ON       70.0f  /* pushed this far toward it, running; under RUN_OFF, walking again */
#define RUN_OFF      60.0f
#define RUN_CONE     30.0f  /* degrees either side of the marker (RUN_CONE_OFF while running) */
#define RUN_CONE_OFF 40.0f
#define LOOK_R       60.0f  /* a look drag this long is the right stick's full tilt */
#define FIRE_R       42.0f
#define ARC_DIST     115.0f
#define TOP_R        22.0f
#define TRAY_R       20.0f
#define TRAY_STEP    50.0f
#define MENU_HOLD    450
#define CHEAT_HOLD   500  /* the cheat's button, as the other ports': a hold, so a stray touch can't flip it */
#define TAP_MS       300
#define DRAG_LOOK    10.0f  /* a cluster button slid this far looks too */
#define FPS_PERIOD   500

/* the pad, numbered as SDL's buttons; the triggers are axes */
enum { P_A, P_B, P_X, P_Y, P_BACK, P_START, P_RT, N_PAD };
enum { B_FIRE, B_AIM, B_ACTION, B_LIGHT, B_MAP, B_ITEMS, B_MENU, B_CAMERA, B_CHEAT, N_BTN };
static const char *const btn_label[N_BTN] = {"FIRE", "AIM", "ACTION", "", "MAP", "ITEMS", "MENU", "CAMERA", ""};
static const int btn_cluster[N_BTN] = {1, 1, 1, 0, 0, 0, 0, 0, 0};  /* in the right thumb's cluster: drags look */
enum { T_SENS, T_GYRO, T_OPACITY, T_FPS, T_CAMERA, T_CHEAT, T_CONTROLS, N_TRAY };

enum { GYRO_OFF, GYRO_TOUCH, GYRO_ALWAYS };
static const float sens_steps[] = {1.0f, 1.5f, 2.0f, 3.0f};
static const float opacity_steps[] = {1.0f, 0.8f, 0.6f, 0.4f, 0.2f};

#define MAX_FINGERS 10
enum { F_NONE, F_BUTTON, F_STICK, F_LOOK, F_MENU, F_TRAY, F_TAP };

typedef struct { float x, y, r; } Circle;

static struct {
    pthread_mutex_t mx;
    SDL_Joystick *joy;              /* the virtual pad, opened so its state can be set */
    SDL_JoystickID joy_id;
    float w, h, k;                  /* the window in points, and pixels per point */
    int pw, ph;                     /* in pixels */
    int visible;                    /* no real controller attached */
    atomic_int cursor;              /* the game shows its mouse cursor (its menus): set by touch_draw */
    struct {
        SDL_FingerID id;
        int kind, idx;              /* idx: the button or tray button */
        float ox, oy, x, y, travel;
        Uint32 t0;                  /* SDL_GetTicks when it went down */
        int off;                    /* slid off a hold button: it does nothing any more */
    } f[MAX_FINGERS];
    int held[N_BTN];
    int tray;                       /* the tray is open */
    int tap_a;                      /* a quick tap off the controls confirms (touch_set_tap_confirm) */
    Uint32 a_release, back_release; /* a tap's A, MENU's Back: held until then (0: not held) */
    int tap_pending;                /* a quick tap the game has not taken yet (touch_take_tap) */
    float tap_x, tap_y;             /* where, in pixels */
    Uint32 tap_at;
    /* settings, kept in touch.cfg */
    int gyro, sens, opacity, show_fps, camera, classic;
    int shot;                       /* CAMERA was tapped: touch_draw saves the frame */
    SDL_Sensor *gyro_sensor;
    float gyro_x, gyro_y;           /* the latest tilt, as right-stick tilt */
    float fps;
    int sound;                      /* CHEAT's sound to play once T.mx is let go: 1 off, 2 on (0 none) */
    int runs;                       /* the stick is pushed onto the run marker */
    int run_marker;                 /* placed (there was room above the stick) */
    float run_x, run_y, run_deg;    /* where, and its direction from the stick (degrees, counter-clockwise) */
    atomic_int light;               /* the flashlight is on (touch_draw reads the game) */
} T = {.mx = PTHREAD_MUTEX_INITIALIZER, .tap_a = 1, .sens = 1, .visible = 1};

/* ---- settings ---- */
static void save_settings(void) {
    FILE *f = fopen("touch.cfg", "w");
    if (!f) return;
    fprintf(f, "gyro=%d\nsens=%d\nopacity=%d\nfps=%d\ncamera=%d\nclassic=%d\n", T.gyro, T.sens, T.opacity, T.show_fps,
            T.camera, T.classic);
    fclose(f);
}
static void load_settings(void) {
    FILE *f = fopen("touch.cfg", "r");
    char line[64];
    while (f && fgets(line, sizeof line, f)) {
        int v;
        if (sscanf(line, "gyro=%d", &v) == 1 && v >= 0 && v <= 2) T.gyro = v;
        else if (sscanf(line, "sens=%d", &v) == 1 && v >= 0 && v < 4) T.sens = v;
        else if (sscanf(line, "opacity=%d", &v) == 1 && v >= 0 && v < 5) T.opacity = v;
        else if (sscanf(line, "fps=%d", &v) == 1) T.show_fps = !!v;
        else if (sscanf(line, "camera=%d", &v) == 1) T.camera = !!v;
        else if (sscanf(line, "classic=%d", &v) == 1) T.classic = !!v;
    }
    if (f) fclose(f);
}

/* ---- layout, in points ---- */
static float inset_x(void) { return T.w > T.h * 1.8f ? 48.0f : 20.0f; }  /* rounded corners, the camera housing */

static Circle button_at(int b) {
    float W = T.w, H = T.h, ix = inset_x(), top = 14.0f + TOP_R;
    float fx = W - ix - FIRE_R - 6.0f, fy = H - 18.0f - FIRE_R;
    switch (b) {
    case B_FIRE: return (Circle){fx, fy, FIRE_R};
    /* round FIRE: AIM to the left, ACTION up and to the left */
    case B_AIM: return (Circle){fx - ARC_DIST, fy + 4.0f, 29.0f};
    case B_ACTION: return (Circle){fx - ARC_DIST * 0.62f, fy - ARC_DIST * 0.85f, 31.0f};
    case B_LIGHT: return (Circle){ix + TOP_R + 2 * (2 * TOP_R + 14.0f), top, TOP_R};
    case B_MAP: return (Circle){ix + TOP_R, top, TOP_R};
    case B_ITEMS: return (Circle){ix + TOP_R + 2 * TOP_R + 14.0f, top, TOP_R};
    case B_MENU: return (Circle){W - ix - TOP_R, top, TOP_R};
    case B_CHEAT: return (Circle){W - ix - TOP_R - 2 * TOP_R - 90.0f, top, TOP_R};
    case B_CAMERA: return (Circle){W - ix - TOP_R - 4 * TOP_R - 104.0f, top, TOP_R};
    }
    return (Circle){0, 0, 0};
}
static Circle tray_at(int t) {
    Circle m = button_at(B_MENU);
    return (Circle){m.x - (N_TRAY - 1 - t) * TRAY_STEP, m.y + TOP_R + 16.0f + TRAY_R, TRAY_R};
}
static int shown(int b) {
    if (b == B_CAMERA) return T.camera;
    if (b == B_MAP || b == B_ITEMS || b == B_MENU || b == B_LIGHT || b == B_CHEAT) return 1;
    return !atomic_load(&T.cursor);  /* the cluster: only in play */
}

/* The nearest shown button within its radius and SLOP of (x, y), or -1. */
static int button_hit(float x, float y) {
    int best = -1;
    float bd = 1e9f;
    for (int b = 0; b < N_BTN; b++) {
        if (!shown(b)) continue;
        Circle c = button_at(b);
        float d = hypotf(x - c.x, y - c.y) - c.r;
        if (d < SLOP && d < bd) bd = d, best = b;
    }
    return best;
}
static int tray_hit(float x, float y) {
    for (int t = 0; t < N_TRAY; t++) {
        Circle c = tray_at(t);
        if (hypotf(x - c.x, y - c.y) < c.r + SLOP) return t;
    }
    return -1;
}

/* ---- the run marker, as the Jedi Knight port places it (tu_PlaceRunMarker): straight above the stick, as far
 * as 100 pt (no nearer than 76) where it is clear of the top and of every button ---- */
static void place_run_marker(float ox, float oy) {
    T.run_marker = T.runs = 0;
    float x = fmaxf(ox, inset_x() + 4 + RUN_R), min_y = 4 + RUN_R;
    x = fminf(x, T.w - inset_x() - 4 - RUN_R);
    for (float h = RUN_DIST; h >= RUN_MIN_DIST; h -= 2) {
        float y = oy - h;
        int clear = y >= min_y;
        for (int b = 0; b < N_BTN && clear; b++) {
            if (!shown(b)) continue;
            Circle c = button_at(b);
            clear = hypotf(x - c.x, y - c.y) >= c.r + RUN_R + 4;
        }
        if (!clear) continue;
        T.run_marker = 1, T.run_x = x, T.run_y = y;
        T.run_deg = atan2f(h, x - ox) * 57.29578f;
        return;
    }
}
/* Pushed far enough toward the marker, and within its cone (wider once running, so it doesn't flicker). */
static int on_run_marker(float px, float py, float ox, float oy) {
    if (!T.run_marker) return 0;
    float dx = px - ox, dy = oy - py, d = hypotf(dx, dy);
    if (d < (T.runs ? RUN_OFF : RUN_ON)) return 0;
    float off = fabsf(fmodf(atan2f(dy, dx) * 57.29578f - T.run_deg + 540.0f, 360.0f) - 180.0f);
    return off <= (T.runs ? RUN_CONE_OFF : RUN_CONE);
}

/* ---- the pad ---- */
static void set_button(int sdl_button, int on) { SDL_JoystickSetVirtualButton(T.joy, sdl_button, on); }

/* Stick tilt from a finger's offset: dead zone, rescaled to reach full at the edge. */
static void stick_tilt(float dx, float dy, float reach, float dead, float *ox, float *oy) {
    float len = hypotf(dx, dy) / reach;
    if (len < dead) { *ox = *oy = 0; return; }
    float s = (len > 1 ? 1 : (len - dead) / (1 - dead)) / (len * reach);
    *ox = dx * s, *oy = dy * s;
}

static int thumb_down(void) {
    for (int i = 0; i < MAX_FINGERS; i++)
        if (T.f[i].kind == F_STICK || T.f[i].kind == F_LOOK || (T.f[i].kind == F_BUTTON && btn_cluster[T.f[i].idx])) return 1;
    return 0;
}

/* Pushes the fingers' state into the virtual pad. Caller holds T.mx. */
static void apply(void) {
    if (!T.joy) return;
    int pad[N_PAD] = {0}, held[N_BTN] = {0};
    float lx = 0, ly = 0, rx = 0, ry = 0;
    int runs = 0;
    for (int i = 0; i < MAX_FINGERS; i++) {
        float dx = T.f[i].x - T.f[i].ox, dy = T.f[i].y - T.f[i].oy, tx, ty;
        switch (T.f[i].kind) {
        case F_STICK:
            stick_tilt(dx, dy, STICK_R, STICK_DEAD, &tx, &ty);
            lx = tx, ly = ty;
            if (on_run_marker(T.f[i].x, T.f[i].y, T.f[i].ox, T.f[i].oy)) runs = 1, pad[P_X] = 1;  /* run: hold X */
            break;
        case F_LOOK:
            stick_tilt(dx, dy, LOOK_R, 0.05f, &tx, &ty);
            rx += tx, ry += ty;
            break;
        case F_BUTTON:
            held[T.f[i].idx] = 1;
            if (btn_cluster[T.f[i].idx] && T.f[i].travel > DRAG_LOOK) {
                stick_tilt(dx, dy, LOOK_R, 0.05f, &tx, &ty);
                rx += tx, ry += ty;
            }
            break;
        case F_MENU: held[B_MENU] = !T.f[i].off; break;
        }
    }
    if (T.gyro == GYRO_ALWAYS || (T.gyro == GYRO_TOUCH && thumb_down())) rx += T.gyro_x, ry += T.gyro_y;
    rx = rx > 1 ? 1 : rx < -1 ? -1 : rx;
    ry = ry > 1 ? 1 : ry < -1 ? -1 : ry;
    if (held[B_FIRE]) pad[P_RT] = pad[P_A] = 1;  /* FIRE: aim and attack */
    if (held[B_AIM]) pad[P_RT] = 1;
    if (held[B_ACTION]) pad[P_A] = 1;
    if (held[B_LIGHT]) pad[P_B] = 1;
    if (held[B_MAP]) pad[P_Y] = 1;
    if (held[B_ITEMS]) pad[P_START] = 1;
    T.runs = runs;
    if (T.a_release) pad[P_A] = 1;
    if (T.back_release) pad[P_BACK] = 1;
    memcpy(T.held, held, sizeof held);
    set_button(SDL_CONTROLLER_BUTTON_A, pad[P_A]);
    set_button(SDL_CONTROLLER_BUTTON_B, pad[P_B]);
    set_button(SDL_CONTROLLER_BUTTON_X, pad[P_X]);
    set_button(SDL_CONTROLLER_BUTTON_Y, pad[P_Y]);
    set_button(SDL_CONTROLLER_BUTTON_BACK, pad[P_BACK]);
    set_button(SDL_CONTROLLER_BUTTON_START, pad[P_START]);
    /* a trigger rests at the axis's bottom: SDL maps -32768..32767 to 0..32767, so 0 would read as half pulled */
    SDL_JoystickSetVirtualAxis(T.joy, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, pad[P_RT] ? 32767 : -32768);
    SDL_JoystickSetVirtualAxis(T.joy, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
    SDL_JoystickSetVirtualAxis(T.joy, SDL_CONTROLLER_AXIS_LEFTX, (Sint16)(lx * 32767));
    SDL_JoystickSetVirtualAxis(T.joy, SDL_CONTROLLER_AXIS_LEFTY, (Sint16)(ly * 32767));
    SDL_JoystickSetVirtualAxis(T.joy, SDL_CONTROLLER_AXIS_RIGHTX, (Sint16)(rx * 32767));
    SDL_JoystickSetVirtualAxis(T.joy, SDL_CONTROLLER_AXIS_RIGHTY, (Sint16)(ry * 32767));
}

/* Real controllers attached (anything but our own pad): the overlay shows only without one. */
static void count_pads(void) {
    int real = 0;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i) && SDL_JoystickGetDeviceInstanceID(i) != T.joy_id) real++;
    if (T.visible != !real) rt_log("touch: %s", real ? "a controller is attached, touch controls hidden" : "touch controls shown");
    T.visible = !real;
    if (real) memset(T.f, 0, sizeof T.f), T.tray = 0;
}

static void measure(void) {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(rt_window, &ww, &wh);
    SDL_GL_GetDrawableSize(rt_window, &T.pw, &T.ph);
    if (ww <= 0 || wh <= 0) ww = T.pw, wh = T.ph;
    T.w = ww, T.h = wh, T.k = ww > 0 ? (float)T.pw / ww : 1;
}

/* Main thread, once the window is open: the virtual pad. Every SDL_GameControllerButton and axis, in SDL's
 * order, so SDL maps it as a standard gamepad and button i of the pad is SDL button i. */
void touch_init(void) {
    /* recursive: should an SDL call made while T.mx is held still post an event back into touch_event, it carries
     * on instead of hanging the main thread (touches would stop reaching the game) */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&T.mx, &attr);
    pthread_mutexattr_destroy(&attr);
    load_settings();
    controls_set_classic(T.classic);
    SDL_VirtualJoystickDesc d;
    SDL_zero(d);
    d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes = SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons = SDL_CONTROLLER_BUTTON_DPAD_RIGHT + 1;
    d.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1;
    d.button_mask = (1u << d.nbuttons) - 1;
    d.name = "Touch controls";
    int idx = SDL_JoystickAttachVirtualEx(&d);
    if (idx < 0 || !(T.joy = SDL_JoystickOpen(idx))) { rt_log("touch: no virtual pad: %s", SDL_GetError()); return; }
    T.joy_id = SDL_JoystickInstanceID(T.joy);
    measure();
    count_pads();
    if (SDL_InitSubSystem(SDL_INIT_SENSOR) == 0)
        for (int i = 0; i < SDL_NumSensors() && !T.gyro_sensor; i++)
            if (SDL_SensorGetDeviceType(i) == SDL_SENSOR_GYRO) T.gyro_sensor = SDL_SensorOpen(i);
    apply();  /* the triggers' resting value: a new virtual pad starts every axis at 0, half pulled for a trigger */
}

/* Is this SDL joystick index our own pad (dinput.c uses it to prefer a real one)? */
int touch_is_virtual_pad(int device_index) {
    return T.joy && SDL_JoystickGetDeviceInstanceID(device_index) == T.joy_id;
}

/* ---- the tray ---- */
/* Caller holds T.mx. The sound is played after it is let go (play_sound): opening the audio device posts SDL
 * events, which come straight back into touch_event through ios.c's event watch. */
static void toggle_cheat(void) {
    int on = !cheat_get();
    cheat_set(on);
    T.sound = on ? 2 : 1;
}
static void play_sound(void) {  /* without T.mx */
    pthread_mutex_lock(&T.mx);
    int s = T.sound;
    T.sound = 0;
    pthread_mutex_unlock(&T.mx);
    if (s) touch_sound_cheat(s == 2);
}

static void use_tray(int t) {
    switch (t) {
    case T_SENS: T.sens = (T.sens + 1) % 4; break;
    case T_GYRO: if (T.gyro_sensor) T.gyro = (T.gyro + 1) % 3; break;
    case T_OPACITY: T.opacity = (T.opacity + 1) % 5; break;
    case T_FPS: T.show_fps = !T.show_fps; T.tray = 0; break;
    case T_CAMERA: T.camera = !T.camera; T.tray = 0; break;
    case T_CHEAT: toggle_cheat(); T.tray = 0; break;
    case T_CONTROLS: T.classic = !T.classic; controls_set_classic(T.classic); break;
    }
    save_settings();
}

/* A quick tap off the controls: the game takes it as a mouse click where a menu shows its cursor (dinput.c),
 * else as A (touch_tick, if nothing took it). */
static void tap(float x, float y) {
    if (!T.tap_a) return;
    T.tap_pending = 1;
    T.tap_x = x * T.k, T.tap_y = y * T.k;
    T.tap_at = SDL_GetTicks();
}

/* Main thread, for every SDL event (ios.c's event watch): touches and controllers coming and going. */
void touch_event(const SDL_Event *e) {
    /* Only what the controls need: nothing else takes T.mx (sensor readings, audio devices and the rest arrive
     * here too, some posted by SDL calls made while T.mx is held) */
    switch (e->type) {
    case SDL_CONTROLLERDEVICEADDED: case SDL_CONTROLLERDEVICEREMOVED: case SDL_JOYDEVICEADDED: case SDL_JOYDEVICEREMOVED:
    case SDL_WINDOWEVENT: case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP: break;
    default: return;
    }
    pthread_mutex_lock(&T.mx);
    switch (e->type) {
    case SDL_CONTROLLERDEVICEADDED: case SDL_CONTROLLERDEVICEREMOVED: case SDL_JOYDEVICEADDED: case SDL_JOYDEVICEREMOVED:
        count_pads();
        apply();
        break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) measure();
        break;
    case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP: {
        if (!T.visible || !T.w) break;
        float x = e->tfinger.x * T.w, y = e->tfinger.y * T.h;
        int slot = -1, free_slot = -1;
        for (int i = 0; i < MAX_FINGERS; i++) {
            if (T.f[i].kind != F_NONE && T.f[i].id == e->tfinger.fingerId) slot = i;
            if (T.f[i].kind == F_NONE && free_slot < 0) free_slot = i;
        }
        if (e->type == SDL_FINGERDOWN && slot < 0 && free_slot >= 0) {
            int kind = F_TAP, idx = 0, b;
            if (T.tray) {  /* the tray's buttons; a touch anywhere else closes it, and does nothing more */
                int t = tray_hit(x, y);
                kind = F_TRAY, idx = t;
                if (t >= 0) use_tray(t);
                else T.tray = 0;
            } else if ((b = button_hit(x, y)) >= 0) {
                kind = b == B_MENU ? F_MENU : F_BUTTON, idx = b;
                if (b == B_CAMERA) T.shot = 1;
            } else if (!atomic_load(&T.cursor)) {
                kind = x < T.w * STICK_SIDE ? F_STICK : F_LOOK;
                for (int i = 0; i < MAX_FINGERS; i++)  /* a second finger on the stick's side looks */
                    if (kind == F_STICK && T.f[i].kind == F_STICK) kind = F_LOOK;
                if (kind == F_STICK) place_run_marker(x, y);
            }
            T.f[free_slot] = (typeof(T.f[0])){e->tfinger.fingerId, kind, idx, x, y, x, y, 0, SDL_GetTicks(), 0};
        } else if (slot >= 0 && e->type == SDL_FINGERMOTION) {
            float d = hypotf(x - T.f[slot].ox, y - T.f[slot].oy);
            if (d > T.f[slot].travel) T.f[slot].travel = d;
            T.f[slot].x = x, T.f[slot].y = y;
            if (T.f[slot].kind == F_MENU || (T.f[slot].kind == F_BUTTON && T.f[slot].idx == B_CHEAT)) {
                Circle c = button_at(T.f[slot].idx);
                if (hypotf(x - c.x, y - c.y) > c.r + SLOP * 2) T.f[slot].off = 1;  /* slid off: neither tap nor tray */
            }
        } else if (slot >= 0 && e->type == SDL_FINGERUP) {
            int quick = SDL_GetTicks() - T.f[slot].t0 < TAP_MS && T.f[slot].travel < 0.04f * T.h + SLOP;
            if (T.f[slot].kind == F_MENU && !T.f[slot].off) T.back_release = SDL_GetTicks() + 120;  /* a tap: pause */
            if (quick && (T.f[slot].kind == F_LOOK || T.f[slot].kind == F_STICK || T.f[slot].kind == F_TAP))
                tap(T.f[slot].ox, T.f[slot].oy);
            T.f[slot].kind = F_NONE;
        }
        apply();
        break;
    }
    }
    pthread_mutex_unlock(&T.mx);
    play_sound();
}

/* Main thread, every pump (ios.c): MENU's hold, the presses a tap left held, and the gyro. */
void touch_tick(void) {
    pthread_mutex_lock(&T.mx);
    Uint32 now = SDL_GetTicks();
    int changed = 0;
    for (int i = 0; i < MAX_FINGERS; i++)
        if (T.f[i].kind == F_BUTTON && T.f[i].idx == B_CHEAT && !T.f[i].off && now - T.f[i].t0 >= CHEAT_HOLD) {
            toggle_cheat();
            T.f[i].off = 1;  /* once a hold */
        }
    for (int i = 0; i < MAX_FINGERS; i++)
        if (T.f[i].kind == F_MENU && !T.f[i].off && now - T.f[i].t0 >= MENU_HOLD) {
            T.tray = 1;
            T.f[i].kind = F_TRAY, T.f[i].idx = -1;  /* held: the tray, not the pause */
            changed = 1;
        }
    if (T.tap_pending && SDL_TICKS_PASSED(now, T.tap_at + 150)) {  /* a tap nothing took confirms with A */
        T.tap_pending = 0;
        T.a_release = now + 90;
        changed = 1;
    }
    if (T.a_release && SDL_TICKS_PASSED(now, T.a_release)) T.a_release = 0, changed = 1;
    if (T.back_release && SDL_TICKS_PASSED(now, T.back_release)) T.back_release = 0, changed = 1;
    if (T.gyro_sensor) {
        float g[3] = {0, 0, 0};  /* (SDL_PumpEvents reads the sensor: calling SDL_SensorUpdate here would post an
                                    * event into touch_event while T.mx is held, and the main thread would hang) */
        if (T.gyro != GYRO_OFF && !T.tray && !SDL_SensorGetData(T.gyro_sensor, g, 3)) {
            /* landscape: turning the phone left or right is about its long axis, tipping it about its short one;
             * radians a second, nothing under the dead zone, 1.5 rad/s for full tilt at SENS 1.5 */
            int flip = SDL_GetDisplayOrientation(0) == SDL_ORIENTATION_LANDSCAPE_FLIPPED ? -1 : 1;
            float yaw = -g[0] * flip, pitch = -g[1] * flip, s = sens_steps[T.sens] / 1.5f;
            T.gyro_x = fabsf(yaw) < 0.026f ? 0 : yaw * s;
            T.gyro_y = fabsf(pitch) < 0.026f ? 0 : pitch * s;
        } else
            T.gyro_x = T.gyro_y = 0;
        changed = 1;
    }
    if (changed) apply();
    pthread_mutex_unlock(&T.mx);
    play_sound();
}

/* Game thread (the mouse's GetDeviceData): takes a pending tap, in pixels of the drawable. */
int touch_take_tap(float *x, float *y, int *w, int *h) {
    pthread_mutex_lock(&T.mx);
    int got = T.tap_pending;
    if (got) { *x = T.tap_x, *y = T.tap_y, *w = T.pw, *h = T.ph; T.tap_pending = 0; }
    pthread_mutex_unlock(&T.mx);
    return got;
}

/* Game thread: a tap the game had no cursor for confirms, as A. */
void touch_press_a(void) {
    pthread_mutex_lock(&T.mx);
    T.a_release = SDL_GetTicks() + 90;
    apply();
    pthread_mutex_unlock(&T.mx);
}

/* The launcher handles its own taps (it knows where its items are): it turns tap-to-confirm off meanwhile. */
void touch_set_tap_confirm(int on) {
    pthread_mutex_lock(&T.mx);
    T.tap_a = on;
    pthread_mutex_unlock(&T.mx);
}

/* For the tests: a control's center, in pixels (b < 0: tray button -b-1). */
int touch_running(float *x, float *y) {
    pthread_mutex_lock(&T.mx);
    int r = T.runs;
    *x = T.run_x * T.k, *y = T.run_y * T.k;
    pthread_mutex_unlock(&T.mx);
    return r;
}

void touch_control_center(int b, float *x, float *y) {
    pthread_mutex_lock(&T.mx);
    Circle c = b >= 0 ? button_at(b) : tray_at(-b - 1);
    *x = c.x * T.k, *y = c.y * T.k;
    pthread_mutex_unlock(&T.mx);
}

/* ---- the overlay (game thread) ---- */
static touchUiDrawList dl;
static const touchUiColor blue = {0.47f, 0.78f, 1.0f, 1.0f};  /* #78C7FF: holds and lit toggles */

static void draw_button(Circle c, const char *label, int held, int lit, float alpha, float size) {
    dl.alpha = alpha * (held ? 1.0f : IDLE_ALPHA);
    touchUiDraw_Circle(&dl, c.x, c.y, c.r, held ? touchUi_White(0.35f) : touchUi_Black(0.35f), 2.0f,
                       lit ? blue : touchUi_White(0.85f));
    touchUiDraw_Text(&dl, label, c.x, c.y, size, 1.0f, lit ? blue : touchUi_White(0.95f));
}

/* LIGHT: a flashlight, its lens to the upper right; on, its beam is out and it is lit blue. */
static void draw_flashlight(Circle c, int held, int on, float alpha) {
    dl.alpha = alpha * (held ? 1.0f : IDLE_ALPHA);
    touchUiDraw_Circle(&dl, c.x, c.y, c.r, on ? touchUi_Rgba(0.47f, 0.78f, 1.0f, 0.35f) : held ? touchUi_White(0.35f) : touchUi_Black(0.35f),
                       2.0f, on ? blue : touchUi_White(0.85f));
    touchUiColor ink = on ? touchUi_White(1.0f) : touchUi_White(0.9f);
    /* drawn pointing right, then turned 35 degrees up: the handle, the wider head, the lens */
    float ca = cosf(-0.61f), sa = sinf(-0.61f);
    #define P(px, py) c.x + (px) * ca - (py) * sa, c.y + (px) * sa + (py) * ca
    float body[] = {P(-11, -2.6f), P(1, -2.6f), P(1, 2.6f), P(-11, 2.6f)};
    float head[] = {P(1, -2.6f), P(6, -5.5f), P(6, 5.5f), P(1, 2.6f)};
    touchUiDraw_Polygon(&dl, body, 4, ink);
    touchUiDraw_Polygon(&dl, head, 4, ink);
    touchUiDraw_Line(&dl, P(7.2f, -5.5f), P(7.2f, 5.5f), 1.6f, on ? blue : touchUi_White(0.6f));
    if (on)  /* the beam */
        for (int i = -1; i <= 1; i++) touchUiDraw_Line(&dl, P(10, i * 4.0f), P(15, i * 7.0f), 1.6f, blue);
    else     /* the switch */
        touchUiDraw_Circle(&dl, P(-5, 0), 1.3f, touchUi_Black(0.8f), 0, touchUi_Clear());
    #undef P
}

/* CHEAT: Pyramid Head's helmet, seen from the front, an edge toward us: the great iron pyramid, its two faces
 * (the right one in shadow), the edge down the middle, the rim jutting out over the shoulders. While on, lit
 * the red of rust and blood. */
static void draw_pyramid(Circle c, int held, int on, float alpha) {
    touchUiColor red = touchUi_Rgba(0.75f, 0.10f, 0.08f, 1.0f);
    dl.alpha = alpha * (held ? 1.0f : IDLE_ALPHA);
    touchUiDraw_Circle(&dl, c.x, c.y, c.r, on ? touchUi_Rgba(0.55f, 0.05f, 0.04f, 0.45f) : held ? touchUi_White(0.35f) : touchUi_Black(0.35f),
                       2.0f, on ? red : touchUi_White(0.85f));
    float x = c.x, y = c.y;
    touchUiColor lit = on ? touchUi_Rgba(0.95f, 0.85f, 0.8f, 1.0f) : touchUi_White(0.92f);
    touchUiColor dark = on ? touchUi_Rgba(0.62f, 0.14f, 0.11f, 1.0f) : touchUi_White(0.55f);
    touchUiColor body = on ? touchUi_Rgba(0.35f, 0.05f, 0.04f, 1.0f) : touchUi_White(0.35f);
    /* the shoulders it sits on */
    float shoulders[] = {x - 9, y + 6, x + 9, y + 6, x + 16, y + 15, x - 16, y + 15};
    touchUiDraw_Polygon(&dl, shoulders, 4, body);
    /* the helmet: wide and squat, its front edge a little left of the middle; the lit face, the shadowed one */
    float left[] = {x + 1, y - 14, x - 15, y + 6, x - 2, y + 10};
    float right[] = {x + 1, y - 14, x - 2, y + 10, x + 15, y + 5};
    touchUiDraw_Polygon(&dl, left, 3, lit);
    touchUiDraw_Polygon(&dl, right, 3, dark);
    touchUiDraw_Line(&dl, x + 1, y - 13, x - 2, y + 9.5f, 1.0f, touchUi_Black(0.55f));
    /* the iron's seams across the faces */
    touchUiDraw_Line(&dl, x - 7, y - 2, x - 1, y + 0.5f, 0.8f, touchUi_Black(0.35f));
    touchUiDraw_Line(&dl, x - 0.5f, y + 0.5f, x + 8, y - 2.5f, 0.8f, touchUi_Black(0.35f));
}

static void build(Uint32 now) {
    touchUiDraw_Begin(&dl);
    float alpha = opacity_steps[T.opacity];
    int cursor = atomic_load(&T.cursor);
    char label[64];
    for (int b = 0; b < N_BTN; b++) {
        if (!shown(b)) continue;
        Circle c = button_at(b);
        float size = c.r >= 40 ? 15.0f : c.r >= 28 ? (strlen(btn_label[b]) > 4 ? 10.0f : 11.5f) : 8.5f;
        if (b == B_LIGHT) draw_flashlight(c, T.held[b], atomic_load(&T.light), alpha);
        else if (b == B_CHEAT) draw_pyramid(c, T.held[b], cheat_get(), alpha);
        else draw_button(c, btn_label[b], T.held[b], 0, alpha, size);
    }
    /* MENU's and CHEAT's hold rings */
    for (int i = 0; i < MAX_FINGERS; i++)
        if ((T.f[i].kind == F_MENU || (T.f[i].kind == F_BUTTON && T.f[i].idx == B_CHEAT)) && !T.f[i].off) {
            Circle c = button_at(T.f[i].kind == F_MENU ? B_MENU : B_CHEAT);
            float p = (float)(now - T.f[i].t0) / (T.f[i].kind == F_MENU ? MENU_HOLD : CHEAT_HOLD);
            dl.alpha = 1;
            touchUiDraw_Arc(&dl, c.x, c.y, c.r + 4, 3.0f, 0, 6.2831853f * (p > 1 ? 1 : p), 1, blue);
        }
    /* the stick and its run marker, where the thumb is */
    for (int i = 0; i < MAX_FINGERS && !cursor; i++) {
        if (T.f[i].kind != F_STICK) continue;
        float ox = T.f[i].ox, oy = T.f[i].oy, dx = T.f[i].x - ox, dy = T.f[i].y - oy, len = hypotf(dx, dy);
        if (len > STICK_R) dx *= STICK_R / len, dy *= STICK_R / len;
        /* as the Jedi Knight port draws it (tu_DrawStick): the ring, a line to the marker, the knob on the marker
         * while running, the marker's double arrow */
        int lit = T.runs;
        dl.alpha = alpha;
        touchUiDraw_Circle(&dl, ox, oy, STICK_R, touchUi_White(0.08f), 1.5f, touchUi_White(0.45f));
        if (T.run_marker) {
            float mx = T.run_x - ox, my = T.run_y - oy, d = hypotf(mx, my);
            if (d - RUN_R - STICK_R > 1) {
                dl.alpha = (lit ? 1 : IDLE_ALPHA) * alpha;
                touchUiDraw_Line(&dl, ox + mx / d * STICK_R, oy + my / d * STICK_R, ox + mx / d * (d - RUN_R),
                                 oy + my / d * (d - RUN_R), 1.5f, lit ? blue : touchUi_White(0.45f));
            }
        }
        dl.alpha = alpha;
        touchUiDraw_Circle(&dl, lit ? T.run_x : ox + dx, lit ? T.run_y : oy + dy, KNOB_R, touchUi_White(0.30f), 1.5f,
                           touchUi_White(0.45f));
        if (T.run_marker) {
            dl.alpha = (lit ? 1 : IDLE_ALPHA) * alpha;
            touchUiDraw_Circle(&dl, T.run_x, T.run_y, RUN_R, lit ? touchUi_Rgba(0.47f, 0.78f, 1.0f, 0.55f) : touchUi_Black(0.35f),
                               1.5f, touchUi_White(lit ? 0.85f : 0.45f));
            touchUiColor cc = touchUi_White(lit ? 1.0f : 0.85f);
            for (int j = 0; j < 2; j++) {
                float tip = -5.5f + 5.5f * j;
                touchUiDraw_Line(&dl, T.run_x - 6, T.run_y + tip + 5, T.run_x, T.run_y + tip, 2.0f, cc);
                touchUiDraw_Line(&dl, T.run_x, T.run_y + tip, T.run_x + 6, T.run_y + tip + 5, 2.0f, cc);
            }
        }
    }
    if (T.show_fps) {
        Circle m = button_at(B_MENU);
        snprintf(label, sizeof label, "%d FPS", (int)(T.fps + 0.5f));
        dl.alpha = 1;
        touchUiDraw_Text(&dl, label, m.x - m.r - 34.0f, m.y, 11.0f, 1.0f, touchUi_White(0.85f));
    }
    if (T.tray) {
        static const char *const gyro_names[] = {"OFF", "TOUCH", "ALWAYS"};
        dl.alpha = 1;
        Circle first = tray_at(0), last = tray_at(N_TRAY - 1);
        touchUiDraw_RRect(&dl, first.x - TRAY_R - 8, first.y - TRAY_R - 8, last.x - first.x + 2 * TRAY_R + 16, 2 * TRAY_R + 16,
                          TRAY_R + 8, touchUi_Black(0.55f), 0, touchUi_Clear());
        for (int t = 0; t < N_TRAY; t++) {
            int lit = 0, dim = 0;
            switch (t) {
            case T_SENS: snprintf(label, sizeof label, "SENS\n%.1fx", sens_steps[T.sens]); dim = T.gyro == GYRO_OFF; break;
            case T_GYRO:
                if (!T.gyro_sensor) snprintf(label, sizeof label, "NO\nGYRO"), dim = 1;
                else snprintf(label, sizeof label, "GYRO\n%s", gyro_names[T.gyro]), lit = T.gyro != GYRO_OFF;
                break;
            case T_OPACITY: snprintf(label, sizeof label, "OPAC\n%d%%", (int)(opacity_steps[T.opacity] * 100)); break;
            case T_FPS: snprintf(label, sizeof label, "FPS"); lit = T.show_fps; break;
            case T_CAMERA: snprintf(label, sizeof label, "CAM"); lit = T.camera; break;
            case T_CHEAT: snprintf(label, sizeof label, "CHEAT"); lit = cheat_get(); break;
            case T_CONTROLS: snprintf(label, sizeof label, "CTRL\n%s", T.classic ? "CLASSIC" : "MODERN"); break;
            }
            draw_button(tray_at(t), label, 0, lit, dim ? 0.5f : 1.0f, 6.5f);
        }
        dl.alpha = 1;
        touchUiDraw_Text(&dl, "v" SH2_VERSION_LABEL, (first.x + last.x) / 2, first.y + TRAY_R + 18, 8.0f, 1.0f, touchUi_White(0.45f));
    }
}

#ifndef SH2_IOS
/* Elsewhere (the tests), CAMERA's photos are PPM files in the game folder; iOS saves them to Photos. */
void touch_save_photo(const unsigned char *rgba, int w, int h) {
    static int n;
    char path[64];
    snprintf(path, sizeof path, "photo-%d.ppm", n++);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6 %d %d 255\n", w, h);
    for (int i = 0; i < w * h; i++) fwrite(rgba + 4 * i, 1, 3, f);
    fclose(f);
}
#endif

/* Game thread, over each presented frame: the overlay, and CAMERA's photos. */
void touch_draw(void) {
    static Uint32 fps_at;
    static int frames;
    Uint32 now = SDL_GetTicks();
    frames++;
    if (now - fps_at >= FPS_PERIOD) {
        T.fps = frames * 1000.0f / (now - fps_at ? now - fps_at : 1);
        frames = 0, fps_at = now;
    }
    atomic_store(&T.cursor, game_cursor_shown());
    atomic_store(&T.light, game_flashlight_on());
    pthread_mutex_lock(&T.mx);
    if (!T.visible || !T.pw) { pthread_mutex_unlock(&T.mx); return; }
    int shot = T.shot, pw = T.pw, ph = T.ph;
    float k = T.k;
    T.shot = 0;
    build(now);
    pthread_mutex_unlock(&T.mx);
    GLuint fbo = gl_screen_fbo();
    if (shot) {  /* CAMERA: the frame as the game drew it, then with the buttons */
        unsigned char *px = touchUiRender_ReadRGBA(fbo, pw, ph);
        if (px) touch_save_photo(px, pw, ph), free(px);
    }
    touchUiRender_Draw(&dl, fbo, pw, ph, k);
    if (shot) {
        unsigned char *px = touchUiRender_ReadRGBA(fbo, pw, ph);
        if (px) touch_save_photo(px, pw, ph), free(px);
    }
}
#endif
