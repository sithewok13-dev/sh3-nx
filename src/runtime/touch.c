/* On-screen touch controls (iOS, or SH2_TOUCH to test them elsewhere).
 *
 * The controls are a virtual SDL gamepad: the game (src/win32/dinput.c) and the launcher read it like any
 * other controller, and a real one, once connected, takes over (dinput prefers it) while the overlay hides.
 * Touches arrive on the main thread (ios.c's event watch) and set the virtual pad's buttons and axes; the
 * game thread draws the overlay over each presented frame (touch_draw), in the window's pixels.
 *
 * Layout, landscape, sized from the screen's height H and kept clear of the rounded corners:
 *   left half   a floating stick that starts where the thumb lands (movement)
 *   right side  A B X Y in a diamond; dragging anywhere else on the right half is the right stick (camera)
 *   top corners L1 / L2 and R1 / R2
 *   top middle  Back and Start */
#if defined(SH2_IOS) || defined(SH2_TOUCH)
#include "rt.h"
#include "../d3d8/gl.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <pthread.h>

enum { C_A, C_B, C_X, C_Y, C_BACK, C_START, C_L1, C_R1, C_L2, C_R2, N_BUTTONS };
/* the SDL button (or, for the triggers, axis) each control drives */
static const int ctl_button[N_BUTTONS] = {
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_CONTROLLER_AXIS_TRIGGERRIGHT};
static const float ctl_color[N_BUTTONS][3] = {  /* Xbox's colors for the face buttons, grey for the rest */
    {0.35f, 0.80f, 0.30f}, {0.90f, 0.25f, 0.25f}, {0.25f, 0.50f, 0.95f}, {0.95f, 0.80f, 0.20f},
    {0.8f, 0.8f, 0.8f}, {0.8f, 0.8f, 0.8f}, {0.8f, 0.8f, 0.8f}, {0.8f, 0.8f, 0.8f}, {0.8f, 0.8f, 0.8f}, {0.8f, 0.8f, 0.8f}};

typedef struct { float x, y, r; int rect; } Shape;  /* center and radius (half-height for the shoulder bars) in pixels */

#define MAX_FINGERS 10
enum { F_NONE, F_BUTTON, F_LSTICK, F_RSTICK };
static struct {
    pthread_mutex_t mx;
    SDL_Joystick *joy;              /* the virtual pad, opened so its state can be set */
    SDL_JoystickID joy_id;
    int w, h;                       /* the window in pixels */
    int visible;                    /* no real controller attached */
    struct { SDL_FingerID id; int kind, button; float ox, oy, x, y; Uint32 t0; float travel; } f[MAX_FINGERS];
    int held[N_BUTTONS];
    int tap_a;                      /* a quick tap off the controls presses A (touch_set_tap_confirm) */
    Uint32 tap_release;             /* when the A a tap pressed lets go (0: none held) */
} T = {.mx = PTHREAD_MUTEX_INITIALIZER, .tap_a = 1};

static void layout(int i, Shape *s) {
    float W = T.w, H = T.h, inset = W * 0.06f;  /* the panel's rounded corners and the camera housing */
    float fx = W - inset - 0.22f * H, fy = H - 0.30f * H, d = 0.115f * H, br = 0.068f * H;
    switch (i) {
    case C_A: *s = (Shape){fx, fy + d, br, 0}; break;
    case C_B: *s = (Shape){fx + d, fy, br, 0}; break;
    case C_X: *s = (Shape){fx - d, fy, br, 0}; break;
    case C_Y: *s = (Shape){fx, fy - d, br, 0}; break;
    case C_BACK: *s = (Shape){W * 0.5f - 0.11f * H, 0.08f * H, 0.045f * H, 0}; break;
    case C_START: *s = (Shape){W * 0.5f + 0.11f * H, 0.08f * H, 0.045f * H, 0}; break;
    case C_L1: *s = (Shape){inset + 0.14f * H, 0.08f * H, 0.05f * H, 1}; break;
    case C_L2: *s = (Shape){inset + 0.14f * H, 0.21f * H, 0.05f * H, 1}; break;
    case C_R1: *s = (Shape){W - inset - 0.14f * H, 0.08f * H, 0.05f * H, 1}; break;
    case C_R2: *s = (Shape){W - inset - 0.14f * H, 0.21f * H, 0.05f * H, 1}; break;
    }
}
static float stick_reach(void) { return 0.12f * T.h; }

/* Which control a touch at x, y (pixels) lands on: a button, else a stick by side of the screen. */
static int hit(float x, float y, int *button) {
    for (int i = 0; i < N_BUTTONS; i++) {
        Shape s;
        layout(i, &s);
        float dx = fabsf(x - s.x), dy = fabsf(y - s.y), slack = 1.35f;  /* fingers are wider than the art */
        if (s.rect ? (dx < s.r * 2.2f * slack && dy < s.r * slack) : (dx * dx + dy * dy < s.r * s.r * slack * slack)) {
            *button = i;
            return F_BUTTON;
        }
    }
    return x < T.w * 0.5f ? F_LSTICK : F_RSTICK;
}

/* Pushes the fingers' state into the virtual pad. Caller holds T.mx. */
static void apply(void) {
    if (!T.joy) return;
    int held[N_BUTTONS] = {0};
    float stick[2][2] = {{0, 0}, {0, 0}};
    for (int i = 0; i < MAX_FINGERS; i++) {
        if (T.f[i].kind == F_BUTTON) held[T.f[i].button] = 1;
        else if (T.f[i].kind == F_LSTICK || T.f[i].kind == F_RSTICK) {
            float dx = (T.f[i].x - T.f[i].ox) / stick_reach(), dy = (T.f[i].y - T.f[i].oy) / stick_reach();
            float len = sqrtf(dx * dx + dy * dy);
            if (len > 1) dx /= len, dy /= len;
            stick[T.f[i].kind == F_RSTICK][0] = dx;
            stick[T.f[i].kind == F_RSTICK][1] = dy;
        }
    }
    if (T.tap_release) held[C_A] = 1;  /* a quick tap's A, until touch_tick lets go */
    for (int i = 0; i < N_BUTTONS; i++) {
        T.held[i] = held[i];
        /* a trigger rests at the axis's bottom: SDL maps -32768..32767 to 0..32767, so 0 would read as half pulled */
        if (i == C_L2 || i == C_R2) SDL_JoystickSetVirtualAxis(T.joy, ctl_button[i], held[i] ? 32767 : -32768);
        else SDL_JoystickSetVirtualButton(T.joy, ctl_button[i], held[i]);
    }
    static const int axes[2][2] = {{SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY},
                                   {SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY}};
    for (int s = 0; s < 2; s++)
        for (int a = 0; a < 2; a++) SDL_JoystickSetVirtualAxis(T.joy, axes[s][a], (Sint16)(stick[s][a] * 32767));
}

/* Real controllers attached (anything but our own pad): the overlay shows only without one. */
static void count_pads(void) {
    int real = 0;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i) && SDL_JoystickGetDeviceInstanceID(i) != T.joy_id) real++;
    if (T.visible != !real) rt_log("touch: %s", real ? "a controller is attached, touch controls hidden" : "touch controls shown");
    T.visible = !real;
}

/* Main thread, once the window is open: the virtual pad. Every SDL_GameControllerButton and axis, in SDL's
 * order, so SDL maps it as a standard gamepad and button i of the pad is SDL button i. */
void touch_init(void) {
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
    SDL_GL_GetDrawableSize(rt_window, &T.w, &T.h);
    count_pads();
    apply();  /* the triggers' resting value: a new virtual pad starts every axis at 0, half pulled for a trigger */
}

/* Is this SDL joystick index our own pad (dinput.c uses it to prefer a real one)? */
int touch_is_virtual_pad(int device_index) {
    return T.joy && SDL_JoystickGetDeviceInstanceID(device_index) == T.joy_id;
}

/* Main thread, for every SDL event (ios.c's event watch): touches and controllers coming and going. */
void touch_event(const SDL_Event *e) {
    pthread_mutex_lock(&T.mx);
    switch (e->type) {
    case SDL_CONTROLLERDEVICEADDED: case SDL_CONTROLLERDEVICEREMOVED: case SDL_JOYDEVICEADDED: case SDL_JOYDEVICEREMOVED:
        count_pads();
        break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) SDL_GL_GetDrawableSize(rt_window, &T.w, &T.h);
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
            int button = 0, kind = hit(x, y, &button);
            for (int i = 0; i < MAX_FINGERS; i++)  /* one finger per stick: a second one on that side is ignored */
                if (kind != F_BUTTON && T.f[i].kind == kind) kind = F_NONE;
            if (kind != F_NONE) T.f[free_slot] = (typeof(T.f[0])){e->tfinger.fingerId, kind, button, x, y, x, y, e->tfinger.timestamp, 0};
        } else if (slot >= 0 && e->type == SDL_FINGERMOTION) {
            float d = hypotf(x - T.f[slot].ox, y - T.f[slot].oy);
            if (d > T.f[slot].travel) T.f[slot].travel = d;
            T.f[slot].x = x;
            T.f[slot].y = y;
        } else if (slot >= 0 && e->type == SDL_FINGERUP) {
            /* a quick tap off the buttons (a stick that never moved) confirms: A, held for a moment */
            int quick = e->tfinger.timestamp - T.f[slot].t0 < 300 && T.f[slot].travel < 0.04f * T.h;
            if (T.tap_a && quick && T.f[slot].kind != F_BUTTON) T.tap_release = SDL_GetTicks() + 90;
            T.f[slot].kind = F_NONE;
        }
        apply();
        break;
    }
    }
    pthread_mutex_unlock(&T.mx);
}

/* Main thread, every pump (ios.c): lets go of a quick tap's A once it has been held long enough to be seen. */
void touch_tick(void) {
    pthread_mutex_lock(&T.mx);
    if (T.tap_release && SDL_TICKS_PASSED(SDL_GetTicks(), T.tap_release)) {
        T.tap_release = 0;
        apply();
    }
    pthread_mutex_unlock(&T.mx);
}

/* The launcher handles its own taps (it knows where its items are): it turns tap-to-confirm off meanwhile. */
void touch_set_tap_confirm(int on) {
    pthread_mutex_lock(&T.mx);
    T.tap_a = on;
    pthread_mutex_unlock(&T.mx);
}

/* ---- the overlay (game thread) ---- */

static GLuint prog, vao;
static GLint u_box, u_screen, u_shape, u_color;

static GLuint shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        rt_log("touch: overlay shader: %s", log);
    }
    return s;
}

/* One quad per shape, drawn from gl_VertexID with no buffers. Pixel coordinates have y down, as touches do. */
static void overlay_init(void) {
    static const char *vs =
        "#version 300 es\n"
        "uniform vec4 uBox; uniform vec2 uScreen; out vec2 vPx;\n"
        "void main() {\n"
        "  vec2 c = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
        "  vPx = uBox.xy + (c * 2.0 - 1.0) * uBox.zw;\n"
        "  gl_Position = vec4(vPx / uScreen * 2.0 - 1.0, 0.0, 1.0);\n"
        "  gl_Position.y = -gl_Position.y;\n"
        "}\n";
    static const char *fs =
        "#version 300 es\nprecision highp float;\n"
        "uniform vec4 uBox; uniform vec4 uShape; uniform vec4 uColor; in vec2 vPx; out vec4 o;\n"
        "void main() {\n"
        "  vec2 p = vPx - uBox.xy;\n"
        /* uShape: x radius, y half-width of a bar's straight part, z ring thickness (0: filled) */
        "  vec2 q = vec2(max(abs(p.x) - uShape.y, 0.0), p.y);\n"
        "  float d = length(q) - uShape.x;\n"
        "  float a = 1.0 - smoothstep(-1.0, 1.0, d);\n"
        "  if (uShape.z > 0.0) a *= smoothstep(-1.0, 1.0, d + uShape.z);\n"
        "  o = vec4(uColor.rgb, uColor.a * a);\n"
        "}\n";
    prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
    glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(prog);
    u_box = glGetUniformLocation(prog, "uBox");
    u_screen = glGetUniformLocation(prog, "uScreen");
    u_shape = glGetUniformLocation(prog, "uShape");
    u_color = glGetUniformLocation(prog, "uColor");
    glGenVertexArrays(1, &vao);
}

static void draw_shape(float x, float y, float r, float bar, float ring, const float *rgb, float alpha) {
    glUniform4f(u_box, x, y, r + bar + 2, r + 2);
    glUniform4f(u_shape, r, bar, ring, 0);
    glUniform4f(u_color, rgb[0], rgb[1], rgb[2], alpha);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* Game thread, after the frame is on the window's framebuffer and before the swap: the controls over it.
 * Whatever GL state it changes is put back, so the D3D layer's state cache stays right. */
void touch_draw(void) {
    pthread_mutex_lock(&T.mx);
    int visible = T.visible && T.joy, w = T.w, h = T.h, held[N_BUTTONS];
    memcpy(held, T.held, sizeof held);
    typeof(T.f[0]) f[MAX_FINGERS];
    memcpy(f, T.f, sizeof f);
    pthread_mutex_unlock(&T.mx);
    if (!visible || !w) return;
    if (!prog) overlay_init();

    GLint old_prog, old_vao, old_fb, vp[4], bsrc, bdst, bsrca, bdsta, beq, beqa;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_fb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl_screen_fbo());  /* the window, whatever the frame left bound */
    GLboolean blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST), depth = glIsEnabled(GL_DEPTH_TEST),
              cull = glIsEnabled(GL_CULL_FACE), stencil = glIsEnabled(GL_STENCIL_TEST), cmask[4];
    glGetIntegerv(GL_CURRENT_PROGRAM, &old_prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_BLEND_SRC_RGB, &bsrc); glGetIntegerv(GL_BLEND_DST_RGB, &bdst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &bsrca); glGetIntegerv(GL_BLEND_DST_ALPHA, &bdsta);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &beq); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &beqa);
    glGetBooleanv(GL_COLOR_WRITEMASK, cmask);

    glUseProgram(prog);
    glBindVertexArray(vao);
    glViewport(0, 0, w, h);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_STENCIL_TEST);
    glColorMask(1, 1, 1, 1);
    glUniform2f(u_screen, (float)w, (float)h);

    static const float white[3] = {1, 1, 1};
    for (int i = 0; i < N_BUTTONS; i++) {
        Shape s;
        layout(i, &s);
        float bar = s.rect ? s.r * 1.2f : 0;
        draw_shape(s.x, s.y, s.r, bar, 0, ctl_color[i], held[i] ? 0.55f : 0.22f);
        draw_shape(s.x, s.y, s.r, bar, 0.08f * s.r + 1.5f, white, held[i] ? 0.8f : 0.45f);
    }
    /* the sticks: where they are held, else a faint hint of where to put the left thumb */
    int lheld = 0;
    for (int i = 0; i < MAX_FINGERS; i++) {
        if (f[i].kind != F_LSTICK && f[i].kind != F_RSTICK) continue;
        lheld |= f[i].kind == F_LSTICK;
        float dx = f[i].x - f[i].ox, dy = f[i].y - f[i].oy, len = sqrtf(dx * dx + dy * dy), reach = stick_reach();
        if (len > reach) dx *= reach / len, dy *= reach / len;
        draw_shape(f[i].ox, f[i].oy, reach, 0, 2.5f, white, 0.35f);
        draw_shape(f[i].ox + dx, f[i].oy + dy, 0.45f * reach, 0, 0, white, 0.4f);
    }
    if (!lheld) {
        float hx = w * 0.06f + 0.24f * h, hy = h - 0.30f * h;
        draw_shape(hx, hy, stick_reach(), 0, 2.5f, white, 0.2f);
        draw_shape(hx, hy, 0.45f * stick_reach(), 0, 0, white, 0.15f);
    }

    glUseProgram(old_prog);
    glBindVertexArray(old_vao);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_fb);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glBlendFuncSeparate(bsrc, bdst, bsrca, bdsta);
    glBlendEquationSeparate(beq, beqa);
    if (!blend) glDisable(GL_BLEND);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    if (stencil) glEnable(GL_STENCIL_TEST);
    glColorMask(cmask[0], cmask[1], cmask[2], cmask[3]);
}
#endif
