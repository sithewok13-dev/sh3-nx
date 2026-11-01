/* The touch controls (src/runtime/touch.c) without a phone: synthetic finger events go in, and the virtual
 * gamepad the game reads must show the buttons, sticks and triggers they press; a second pad standing in for a
 * real controller must hide them. Writes OUT/touch.ppm, the overlay drawn over a grey frame with the left
 * stick and A held. Built with SH2_GLES and SH2_TOUCH (tests/run-touch-test.sh). */
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

static int W = 1280, H = 590, failures;
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

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : ".";
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
    check("the virtual pad is a game controller", SDL_NumJoysticks() == 1 && SDL_IsGameController(0) && touch_is_virtual_pad(0));
    SDL_GameController *c = SDL_GameControllerOpen(0);
    SDL_JoystickUpdate();
    check("the triggers rest released before any touch", SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 0 &&
                                                              SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 0);
    float inset = W * 0.06f, fx = W - inset - 0.22f * H, fy = H - 0.30f * H, d = 0.115f * H;

    finger(SDL_FINGERDOWN, 1, fx, fy + d);
    check("A pressed", SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A));
    check("nothing else pressed", !SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B) &&
                                      !SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_X));
    finger(SDL_FINGERUP, 1, fx, fy + d);
    check("A released", !SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A));

    finger(SDL_FINGERDOWN, 2, fx + d, fy);
    check("B pressed", SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B));
    finger(SDL_FINGERUP, 2, fx + d, fy);

    finger(SDL_FINGERDOWN, 3, W * 0.5f + 0.11f * H, 0.08f * H);
    check("Start pressed", SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START));
    finger(SDL_FINGERUP, 3, 0, 0);

    finger(SDL_FINGERDOWN, 4, inset + 0.14f * H, 0.21f * H);
    check("L2 pulls the left trigger", SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 30000);
    finger(SDL_FINGERUP, 4, 0, 0);
    check("L2 lets go", SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 0);

    /* left stick: lands anywhere on the left half and pushes from there; pushed past its reach it stays at full */
    float sx = W * 0.25f, sy = H * 0.55f;
    finger(SDL_FINGERDOWN, 5, sx, sy);
    check("a fresh left stick is centered", abs(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX)) < 100);
    finger(SDL_FINGERMOTION, 5, sx + 0.06f * H, sy);
    int half = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
    check("half a push right is half right", half > 14000 && half < 19000);
    finger(SDL_FINGERMOTION, 5, sx + 0.5f * H, sy);
    check("a long push right is full right", SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) > 32000 &&
                                                 abs(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY)) < 100);

    /* right stick, with the left one still held (two thumbs), plus A on a third finger */
    finger(SDL_FINGERDOWN, 6, W * 0.62f, H * 0.45f);
    finger(SDL_FINGERMOTION, 6, W * 0.62f, H * 0.45f - 0.2f * H);
    check("the right stick goes up while the left is held", SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY) < -32000 &&
                                                                SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) > 32000);
    finger(SDL_FINGERUP, 6, 0, 0);
    finger(SDL_FINGERDOWN, 7, fx, fy + d);
    finger(SDL_FINGERMOTION, 5, sx + 0.1f * H, sy - 0.03f * H);  /* the overlay's picture: stick up-right, A held */

    glClearColor(0.25f, 0.27f, 0.3f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLint prog_before = 1234;
    glUseProgram(0);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog_before);
    glViewport(10, 20, 30, 40);
    touch_draw();
    GLint vp[4], prog_after;
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog_after);
    check("the overlay puts GL's state back", vp[0] == 10 && vp[1] == 20 && vp[2] == 30 && vp[3] == 40 &&
                                                prog_after == prog_before && !glIsEnabled(GL_BLEND) && glGetError() == 0);
    uint8_t *px = malloc(4u * W * H);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    char path[512];
    snprintf(path, sizeof path, "%s/touch.ppm", out);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6 %d %d 255\n", W, H);
    for (int y = H - 1; y >= 0; y--)
        for (int x = 0; x < W; x++) fwrite(px + 4 * (y * W + x), 1, 3, f);
    fclose(f);
    /* the A button's center is lit green over the grey */
    const uint8_t *a = px + 4 * ((H - 1 - (int)(fy + d)) * W + (int)fx);
    check("the A button is drawn, green while held", a[1] > a[0] + 30 && a[1] > a[2] + 30);

    /* a quick tap off the controls is the game's to take (a click where a menu shows its cursor: dinput.c) */
    finger(SDL_FINGERUP, 7, 0, 0);
    finger(SDL_FINGERUP, 5, 0, 0);
    finger(SDL_FINGERDOWN, 10, W * 0.62f, H * 0.4f);
    finger(SDL_FINGERUP, 10, W * 0.62f, H * 0.4f);
    float tx, ty;
    int tw, th;
    check("a quick tap waits for the game", touch_take_tap(&tx, &ty, &tw, &th) && fabsf(tx - W * 0.62f) < 1 &&
                                             fabsf(ty - H * 0.4f) < 1 && tw == W && th == H);
    check("once", !touch_take_tap(&tx, &ty, &tw, &th));
    touch_tick();
    SDL_JoystickUpdate();
    check("and a taken tap presses nothing", !SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A));
    /* one the game does not take confirms: A, let go a moment later (touch_tick); a drag does not */
    finger(SDL_FINGERDOWN, 10, W * 0.62f, H * 0.4f);
    finger(SDL_FINGERUP, 10, W * 0.62f, H * 0.4f);
    SDL_Delay(170);
    touch_tick();
    SDL_JoystickUpdate();
    check("a tap nothing takes presses A", SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A));
    touch_tick();
    SDL_JoystickUpdate();
    check("and A is still held for a moment", SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A));
    SDL_Delay(120);
    touch_tick();
    SDL_JoystickUpdate();
    check("then let go", !SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A));
    finger(SDL_FINGERDOWN, 11, W * 0.25f, H * 0.5f);
    finger(SDL_FINGERMOTION, 11, W * 0.25f + 0.2f * H, H * 0.5f);
    finger(SDL_FINGERUP, 11, 0, 0);
    check("a drag is not a tap", !SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A));
    touch_set_tap_confirm(0);
    finger(SDL_FINGERDOWN, 12, W * 0.62f, H * 0.4f);
    finger(SDL_FINGERUP, 12, W * 0.62f, H * 0.4f);
    check("with tap-to-confirm off (the launcher), a tap presses nothing", !SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A));
    touch_set_tap_confirm(1);

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
    finger(SDL_FINGERDOWN, 8, fx + d, fy);
    check("with a controller attached, touches press nothing", !SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B));
    check("and the other pad is not ours", !touch_is_virtual_pad(real));
    printf(failures ? "%d FAILED\n" : "PASS\n", failures);
    return failures != 0;
}
