/* The touch controls (src/runtime/touch.c) without a phone: synthetic finger events go in, and the virtual
 * gamepad the game reads must show the buttons, sticks and triggers they press; MENU's tap and hold, the tray,
 * the game's menus (its mouse cursor) and a second pad standing in for a real controller must do what
 * HOUSE_STYLE.md's touch standard says. Writes OUT/touch.ppm, the overlay drawn over a grey frame with the
 * stick and FIRE held, and OUT/tray.ppm with the tray open. Built with SH2_GLES and SH2_TOUCH
 * (tests/run-touch-test.sh). */
#include <math.h>
#include "../src/runtime/rt.h"
#include "../src/d3d8/gl.h"
#include <SDL2/SDL.h>
#include <stdarg.h>

SDL_Window *rt_window;
GLuint gl_screen_fbo(void) { return 0; }  /* the window: framebuffer 0 here, as on every platform but iOS */
void rt_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
/* the game's side, faked */
static int cheat, classic = -1, cursor;
void cheat_set(int on) { cheat = on; }
int cheat_get(void) { return cheat; }
void controls_set_classic(int on) { classic = on; }
int game_cursor_shown(void) { return cursor; }

enum { B_FIRE, B_AIM, B_ACTION, B_RUN, B_LIGHT, B_MAP, B_ITEMS, B_MENU, B_CAMERA };
enum { T_SENS, T_GYRO, T_OPACITY, T_FPS, T_CAMERA, T_CHEAT, T_CONTROLS };

static int W = 1280, H = 590, failures;
static SDL_GameController *c;
static void check(const char *what, int ok) {
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    failures += !ok;
}
static void finger(Uint32 type, SDL_FingerID id, float x, float y) {
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    e.tfinger.fingerId = id;
    e.tfinger.x = x / W;
    e.tfinger.y = y / H;
    touch_event(&e);
    SDL_JoystickUpdate();  /* what the main thread's SDL_PumpEvents does on the phone */
    SDL_GameControllerUpdate();
}
static void at(int b, float *x, float *y) { touch_control_center(b, x, y); }
static void press(SDL_FingerID id, int b) { float x, y; at(b, &x, &y); finger(SDL_FINGERDOWN, id, x, y); }
static void tick(void) { touch_tick(); SDL_JoystickUpdate(); SDL_GameControllerUpdate(); }
static int btn(SDL_GameControllerButton b) { return SDL_GameControllerGetButton(c, b); }
static int axis(SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(c, a); }
static void shot(const char *out, const char *name) {
    uint8_t *px = malloc(4u * W * H);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    char path[512];
    snprintf(path, sizeof path, "%s/%s", out, name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6 %d %d 255\n", W, H);
    for (int y = H - 1; y >= 0; y--)
        for (int x = 0; x < W; x++) fwrite(px + 4 * (y * W + x), 1, 3, f);
    fclose(f);
    free(px);
}
static int pixel_differs(float x, float y, const uint8_t bg[3]) {
    uint8_t p[4];
    glReadPixels((int)x, H - 1 - (int)y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    return abs(p[0] - bg[0]) + abs(p[1] - bg[1]) + abs(p[2] - bg[2]) > 20;
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : ".";
    remove("touch.cfg");
    /* SDL drops joystick input while none of its windows has focus, and this one is hidden off screen */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    rt_window = SDL_CreateWindow("touch", 0, 0, W, H, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!rt_window || !SDL_GL_CreateContext(rt_window)) { printf("no GL ES window: %s\n", SDL_GetError()); return 1; }
    SDL_GL_GetDrawableSize(rt_window, &W, &H);

    touch_init();
    check("modern controls by default", classic == 0);
    check("the virtual pad is a game controller", SDL_NumJoysticks() == 1 && SDL_IsGameController(0) && touch_is_virtual_pad(0));
    c = SDL_GameControllerOpen(0);
    SDL_JoystickUpdate();
    check("the triggers rest released before any touch", axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 0 &&
                                                              axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 0);

    press(1, B_ACTION);
    check("ACTION presses A", btn(SDL_CONTROLLER_BUTTON_A) && !btn(SDL_CONTROLLER_BUTTON_B) && !btn(SDL_CONTROLLER_BUTTON_X));
    finger(SDL_FINGERUP, 1, 0, 0);
    check("and lets go", !btn(SDL_CONTROLLER_BUTTON_A));
    press(1, B_FIRE);
    check("FIRE aims and attacks (RT and A)", btn(SDL_CONTROLLER_BUTTON_A) && axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 30000);
    finger(SDL_FINGERUP, 1, 0, 0);
    check("and lets go of both", !btn(SDL_CONTROLLER_BUTTON_A) && axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 0);
    press(1, B_AIM);
    check("AIM pulls RT alone", axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 30000 && !btn(SDL_CONTROLLER_BUTTON_A));
    finger(SDL_FINGERUP, 1, 0, 0);
    press(1, B_RUN);
    check("RUN is X", btn(SDL_CONTROLLER_BUTTON_X));
    finger(SDL_FINGERUP, 1, 0, 0);
    press(1, B_LIGHT);
    check("LIGHT is B", btn(SDL_CONTROLLER_BUTTON_B));
    finger(SDL_FINGERUP, 1, 0, 0);
    press(1, B_MAP);
    check("MAP is Y", btn(SDL_CONTROLLER_BUTTON_Y));
    finger(SDL_FINGERUP, 1, 0, 0);
    press(1, B_ITEMS);
    check("ITEMS is Start", btn(SDL_CONTROLLER_BUTTON_START));
    finger(SDL_FINGERUP, 1, 0, 0);

    /* the stick: where the thumb lands on the left, a 30% dead zone, full past its 60 pt */
    float sx = W * 0.2f, sy = H * 0.6f;
    finger(SDL_FINGERDOWN, 5, sx, sy);
    check("a fresh stick is centered", abs(axis(SDL_CONTROLLER_AXIS_LEFTX)) < 100);
    finger(SDL_FINGERMOTION, 5, sx + 15, sy);
    check("inside the dead zone it stays centered", abs(axis(SDL_CONTROLLER_AXIS_LEFTX)) < 100);
    finger(SDL_FINGERMOTION, 5, sx + 39, sy);
    int part = axis(SDL_CONTROLLER_AXIS_LEFTX);
    check("past it, part way right", part > 13000 && part < 19000);
    finger(SDL_FINGERMOTION, 5, sx + 200, sy);
    check("a long push is full right", axis(SDL_CONTROLLER_AXIS_LEFTX) > 32000 && abs(axis(SDL_CONTROLLER_AXIS_LEFTY)) < 100);
    finger(SDL_FINGERMOTION, 5, sx, sy - 100);
    check("on the run marker: straight ahead, running", axis(SDL_CONTROLLER_AXIS_LEFTY) < -32000 && btn(SDL_CONTROLLER_BUTTON_X));
    finger(SDL_FINGERMOTION, 5, sx + 200, sy);
    check("off it, walking again", !btn(SDL_CONTROLLER_BUTTON_X));

    /* look: a drag on the right, with the stick still held (two thumbs) */
    finger(SDL_FINGERDOWN, 6, W * 0.6f, H * 0.45f);
    finger(SDL_FINGERMOTION, 6, W * 0.6f, H * 0.45f - 100);
    check("a drag up on the right looks up while the stick is held", axis(SDL_CONTROLLER_AXIS_RIGHTY) < -32000 &&
                                                                          axis(SDL_CONTROLLER_AXIS_LEFTX) > 32000);
    finger(SDL_FINGERUP, 6, 0, 0);
    check("lifted, the look stops", abs(axis(SDL_CONTROLLER_AXIS_RIGHTY)) < 100);
    float fx, fy;
    at(B_FIRE, &fx, &fy);
    finger(SDL_FINGERDOWN, 7, fx, fy);
    finger(SDL_FINGERMOTION, 7, fx - 80, fy);
    check("a drag on FIRE looks too, and keeps firing", axis(SDL_CONTROLLER_AXIS_RIGHTX) < -20000 && btn(SDL_CONTROLLER_BUTTON_A));
    finger(SDL_FINGERMOTION, 7, fx, fy);

    /* the picture: stick and FIRE held */
    glClearColor(0.25f, 0.27f, 0.3f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLint prog_before = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog_before);
    glViewport(10, 20, 30, 40);
    touch_draw();
    GLint vp[4], prog_after;
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog_after);
    check("the overlay puts GL's state back", vp[0] == 10 && vp[1] == 20 && vp[2] == 30 && vp[3] == 40 &&
                                                prog_after == prog_before && !glIsEnabled(GL_BLEND) && glGetError() == 0);
    static const uint8_t bg[3] = {64, 69, 77};
    float mx, my;
    at(B_MENU, &mx, &my);
    check("FIRE, MENU and the stick are drawn", pixel_differs(fx + 30, fy, bg) && pixel_differs(mx + 18, my, bg) &&
                                                   pixel_differs(sx + 60, sy, bg));
    shot(out, "touch.ppm");
    finger(SDL_FINGERUP, 7, 0, 0);
    finger(SDL_FINGERUP, 5, 0, 0);

    /* a quick tap off the controls is the game's to take (a click where a menu shows its cursor: dinput.c) */
    finger(SDL_FINGERDOWN, 10, W * 0.6f, H * 0.4f);
    finger(SDL_FINGERUP, 10, W * 0.6f, H * 0.4f);
    float tx, ty;
    int tw, th;
    check("a quick tap waits for the game", touch_take_tap(&tx, &ty, &tw, &th) && fabsf(tx - W * 0.6f) < 1 &&
                                             fabsf(ty - H * 0.4f) < 1 && tw == W && th == H);
    check("once", !touch_take_tap(&tx, &ty, &tw, &th));
    tick();
    check("and a taken tap presses nothing", !btn(SDL_CONTROLLER_BUTTON_A));
    /* one the game does not take confirms: A, let go a moment later (touch_tick); a drag does not */
    finger(SDL_FINGERDOWN, 10, W * 0.6f, H * 0.4f);
    finger(SDL_FINGERUP, 10, W * 0.6f, H * 0.4f);
    SDL_Delay(170);
    tick();
    check("a tap nothing takes presses A", btn(SDL_CONTROLLER_BUTTON_A));
    SDL_Delay(120);
    tick();
    check("then lets go", !btn(SDL_CONTROLLER_BUTTON_A));
    finger(SDL_FINGERDOWN, 11, W * 0.25f, H * 0.5f);
    finger(SDL_FINGERMOTION, 11, W * 0.25f + 120, H * 0.5f);
    finger(SDL_FINGERUP, 11, 0, 0);
    check("a drag is not a tap", !touch_take_tap(&tx, &ty, &tw, &th));
    touch_set_tap_confirm(0);
    finger(SDL_FINGERDOWN, 12, W * 0.6f, H * 0.4f);
    finger(SDL_FINGERUP, 12, W * 0.6f, H * 0.4f);
    check("with tap-to-confirm off (the launcher), a tap is nothing", !touch_take_tap(&tx, &ty, &tw, &th));
    touch_set_tap_confirm(1);

    /* MENU: a tap pauses (Back); a 0.45 s hold opens the tray instead */
    press(13, B_MENU);
    finger(SDL_FINGERUP, 13, mx, my);
    check("a tap on MENU presses Back", btn(SDL_CONTROLLER_BUTTON_BACK));
    SDL_Delay(140);
    tick();
    check("for a moment", !btn(SDL_CONTROLLER_BUTTON_BACK));
    press(14, B_MENU);
    finger(SDL_FINGERMOTION, 14, mx - 80, my + 40);
    SDL_Delay(480);
    tick();
    finger(SDL_FINGERUP, 14, 0, 0);
    check("slid off MENU, a hold gives neither", !btn(SDL_CONTROLLER_BUTTON_BACK));
    float cx, cy;
    touch_control_center(-1 - T_CHEAT, &cx, &cy);
    finger(SDL_FINGERDOWN, 15, cx, cy);
    finger(SDL_FINGERUP, 15, cx, cy);
    check("and no tray opened (CHEAT stays off)", !cheat);
    press(14, B_MENU);
    SDL_Delay(480);
    tick();
    finger(SDL_FINGERUP, 14, mx, my);
    tick();
    check("held 0.45 s, MENU does not pause", !btn(SDL_CONTROLLER_BUTTON_BACK));
    glClear(GL_COLOR_BUFFER_BIT);
    touch_draw();
    check("the tray is drawn", pixel_differs(cx, cy - 12, bg));
    shot(out, "tray.ppm");
    float kx, ky;
    touch_control_center(-1 - T_CONTROLS, &kx, &ky);
    finger(SDL_FINGERDOWN, 16, kx, ky);
    finger(SDL_FINGERUP, 16, kx, ky);
    check("CONTROLS switches to classic and keeps the tray open", classic == 1);
    finger(SDL_FINGERDOWN, 16, kx, ky);
    finger(SDL_FINGERUP, 16, kx, ky);
    check("and back to modern", classic == 0);
    finger(SDL_FINGERDOWN, 15, cx, cy);
    finger(SDL_FINGERUP, 15, cx, cy);
    check("CHEAT turns the cheat on", cheat);
    finger(SDL_FINGERDOWN, 15, cx, cy);
    finger(SDL_FINGERUP, 15, cx, cy);
    check("and closes the tray (the next touch there is no CHEAT)", cheat);
    finger(SDL_FINGERUP, 15, 0, 0);
    FILE *cfg = fopen("touch.cfg", "r");
    check("the settings are kept", cfg != NULL);
    if (cfg) fclose(cfg);

    /* in the game's menus (its cursor shown) only the top row is a control; a touch elsewhere is a click */
    SDL_Delay(200);  /* the earlier taps' A, pressed and let go (the phone ticks all the time) */
    tick();
    SDL_Delay(120);
    tick();
    cursor = 1;
    touch_draw();
    press(17, B_FIRE);
    check("in a menu FIRE is not there", axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 0 && !btn(SDL_CONTROLLER_BUTTON_A));
    finger(SDL_FINGERUP, 17, fx, fy);
    check("and a touch there is a click", touch_take_tap(&tx, &ty, &tw, &th));
    press(17, B_ITEMS);
    check("ITEMS is still there", btn(SDL_CONTROLLER_BUTTON_START));
    finger(SDL_FINGERUP, 17, 0, 0);
    cursor = 0;
    touch_draw();

    /* a real controller (another pad) hides the controls and stops their input */
    SDL_VirtualJoystickDesc d2;
    SDL_zero(d2);
    d2.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d2.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d2.naxes = 6;
    d2.nbuttons = 15;
    d2.name = "Some controller";
    int real = SDL_JoystickAttachVirtualEx(&d2);
    SDL_Event added;
    SDL_zero(added);
    added.type = SDL_CONTROLLERDEVICEADDED;
    added.cdevice.which = real;
    touch_event(&added);
    press(8, B_LIGHT);
    check("with a controller attached, touches press nothing", !btn(SDL_CONTROLLER_BUTTON_B));
    check("and the other pad is not ours", !touch_is_virtual_pad(real));
    remove("touch.cfg");
    printf(failures ? "%d FAILED\n" : "PASS\n", failures);
    return failures != 0;
}
