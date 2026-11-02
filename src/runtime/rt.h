/* Host runtime for the recompiled sh3.exe: guest memory, threads and the Win32 bridges. */
#pragma once
#include "recomp_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Guest <-> host pointers. Guest address 0 stays NULL. */
#define GPTR(va) ((va) ? (void *)XBOX_PTR(va) : NULL)
static inline uint32_t GVA(const void *p) {
    return p ? (uint32_t)((uintptr_t)p - (uintptr_t)g_xbox_mem_offset) : 0;
}
#define GSTR(va) ((const char *)GPTR(va))

/* Arguments of a bridged call: the guest pushed them, then the return address. */
#define ARG(n) MEM32(g_esp + 4 + 4 * (n))
#define ARGF(n) MEMF(g_esp + 4 + 4 * (n))

/* A bridge is a void(void) guest-callable function, found by its export name.
 * WINAPI(cname, "export", n): stdcall, the bridge pops n arguments.
 * CDECL(cname, "export"): the caller pops the arguments.
 * The body reads ARG(i) and returns EAX. */
typedef struct Bridge { const char *name; void (*fn)(void); } Bridge;
/* The records sit together in one section, which the linker brackets with start and end symbols: ELF's
 * __start_/__stop_, Mach-O's section$start$/section$end$ (Apple platforms). */
#ifdef __APPLE__
#define BRIDGE_SECTION "__DATA,game_bridges"
extern const Bridge rt_bridges_begin[] __asm("section$start$__DATA$game_bridges");
extern const Bridge rt_bridges_end[] __asm("section$end$__DATA$game_bridges");
#else
#define BRIDGE_SECTION "game_bridges"
extern const Bridge rt_bridges_begin[] __asm("__start_game_bridges");
extern const Bridge rt_bridges_end[] __asm("__stop_game_bridges");
#endif
#define BRIDGE_REG(cname, ename) \
    static const Bridge cname##_reg __attribute__((used, section(BRIDGE_SECTION), aligned(8))) = { ename, cname }
#define WINAPI(cname, ename, n) \
    static uint32_t cname##_impl(void); \
    static void cname(void) { uint32_t _r = cname##_impl(); g_eax = _r; g_esp += 4 + 4 * (n); } \
    BRIDGE_REG(cname, ename); \
    static uint32_t cname##_impl(void)
#define CDECL(cname, ename) WINAPI(cname, ename, 0)

/* x87 stack, for bridges that return a double in ST(0) or take one there. */
static inline void rt_fpush(double v) { g_fp_top = (g_fp_top + 7) & 7; g_fp_stack[g_fp_top] = v; }
static inline double rt_fpop(void) { double v = g_fp_stack[g_fp_top]; g_fp_top = (g_fp_top + 1) & 7; return v; }

/* rt_core.c */
void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
_Noreturn void rt_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rt_dump_thread(void);
extern int rt_frame;                 /* frames presented so far */
extern int rt_trace;                 /* SH3_TRACE=1: log every bridged call */
extern char rt_game_dir[512];        /* folder with sh3.exe and data/ */
uint32_t galloc(uint32_t size);      /* guest heap, 16-byte aligned, zeroed */
void gfree(uint32_t va);
uint32_t grealloc(uint32_t va, uint32_t size);
uint32_t gsize(uint32_t va);
uint32_t gstrdup(const char *s);
uint32_t rt_heap_used(void);   /* bytes of the guest heap handed out so far */
recomp_func_t rt_resolve(uint32_t va);
/* Calls guest code at va with args (pushed right to left). cdecl: we pop them; stdcall: the callee does. */
uint32_t guest_call(uint32_t va, int nargs, const uint32_t *args, int stdcall);
/* A host function the guest can call through a pointer (vtables, callbacks). */
uint32_t rt_host_fn(const char *name, void (*fn)(void));

/* COM objects live in guest memory: vtable pointer at +0, then the object's own fields.
 * Methods are stdcall with `this` as ARG(0); METHOD(cname, n) pops n arguments including this. */
#define METHOD(cname, n) \
    static uint32_t cname##_impl(void); \
    static void cname(void) { uint32_t _r = cname##_impl(); g_eax = _r; g_esp += 4 + 4 * (n); } \
    static uint32_t cname##_impl(void)
#define THIS ARG(0)
typedef struct { const char *name; void (*fn)(void); } ComEntry;  /* fn NULL: stops the port when called */
uint32_t com_vtable(const ComEntry *e, int n);
uint32_t com_new(uint32_t vtable, uint32_t size);  /* zeroed object of size bytes, vtable set */

/* user32.c: vsnprintf with the arguments read from the guest va_list at args. */
int rt_format(char *out, size_t n, const char *fmt, uint32_t args);

/* user32.c */
extern struct SDL_Window *rt_window;
void rt_pump_events(void);
/* SDL_PollEvent, except on iOS, where the main thread pumps and the game thread only takes what it queued. */
union SDL_Event;
int rt_poll_event(union SDL_Event *e);
void rt_window_open(void);

/* ios.c (SH2_IOS): the main thread opens the window and pumps events, the game runs on its own thread. */
#ifdef SH2_IOS
const char *ios_game_dir(void);
void ios_require_file(const char *name);
void ios_main_start(void);
_Noreturn void ios_main_loop(void);
void ios_frame_gate(void);
void ios_screen_size(int *w, int *h);
#endif
/* touch.c (iOS, or SH2_TOUCH with SH2_GLES to test elsewhere): on-screen controls as a virtual gamepad. */
#if defined(SH2_IOS) || defined(SH2_TOUCH)
void touch_init(void);                        /* main thread, once the window is open */
void touch_event(const union SDL_Event *e);   /* main thread, every event */
void touch_draw(void);                        /* game thread, over the presented frame */
int touch_is_virtual_pad(int device_index);   /* dinput.c: a real controller wins over it */
void touch_tick(void);                        /* main thread, every pump: ends a quick tap's A press */
void touch_set_tap_confirm(int on);           /* a quick tap off the controls presses A (on by default) */
int touch_take_tap(float *x, float *y, int *w, int *h);  /* dinput.c: a pending tap, in drawable pixels */
void touch_press_a(void);
void touch_sound_cheat(int on);              /* touch_sound.c: CHEAT's sound */
float *touch_sound_make(int on, int *n);     /* its samples (mono, 48 kHz floats; the caller frees them) */
int touch_running(float *x, float *y);       /* tests: the stick is on the run marker (and where it is) */
void touch_save_photo(const unsigned char *rgba, int w, int h);  /* CAMERA: top row first; ios_photos.m */
void touch_control_center(int b, float *x, float *y);           /* tests: a control's center in pixels */
#endif

/* the game's side of the touch controls, on every platform (d3d8.c calls the ticks each frame) */
void cheat_set(int on);                       /* game/cheat.c: everything (health, guns, ammo, items) */
int cheat_get(void);
void cheat_tick(void);                        /* game thread, every presented frame */
void controls_set_classic(int on);            /* game/controls.c: the original controls instead of modern */
void controls_tick(void);                     /* game thread, every presented frame */
enum { SCREEN_PLAY, SCREEN_CUTSCENE, SCREEN_PAUSE, SCREEN_MENU, SCREEN_GAME_MENU, SCREEN_LOADING };
extern volatile int rt_game_loaded;          /* rt_core.c: sh3.exe is in guest memory (the launcher runs before) */
int game_screen(void);                        /* game/controls.c: which screen the game is on (SCREEN_*) */
int game_pause_tap(float y480);               /* a tap on the pause menu: its row picked (1) or nothing there (0) */
int game_flashlight_on(void);                 /* game/controls.c: the flashlight is on */
int game_frame_size(int *w, int *h);          /* game/controls.c: the frame the cursor is measured in (0: none yet) */

/* kernel32.c */
void rt_thread_init_main(uint32_t stack_size);
/* Runs fn(arg) on a new host thread that has its own guest registers, stack and TIB, so fn can guest_call. */
void rt_spawn(void (*fn)(void *), void *arg, uint32_t stack_size);
/* Maps a guest path (backslashes, drive letters, any case) to a host path under rt_game_dir. */
const char *rt_path(const char *guest, char *out, size_t n);
void rt_set_last_error(uint32_t e);
