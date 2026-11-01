/* iOS (SH2_IOS): what the main thread does there, and the game thread's side of it.
 *
 * UIKit wants windows, views and its event loop on the main thread, and that thread's 1 MB stack is far
 * too small for the lifted code, which recurses as deep as the guest. So the main thread opens the window
 * and the GL context, starts the game on a 16 MB thread like every other platform, and then only pumps
 * events. The game thread draws (an EAGL context can be current on any one thread) and reads the events the
 * main thread queued (rt_poll_event).
 *
 * iOS kills an app that renders while in the background: when the app is about to leave the screen the
 * main thread asks the game thread to park at its next Present and waits until it has finished its GL
 * work; the game resumes when the app is back. */
#ifdef SH2_IOS
#include "rt.h"
#include "host.h"
#include "../d3d8/gl.h"
#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#include "unzip.h"

void gl_context_open(void);  /* d3d8.c */

#ifndef IOS_APP_FOLDER
#define IOS_APP_FOLDER "Silent Hill 3"  /* the app's name, as the Files app lists its folder */
#endif
#ifndef IOS_ZIP_PREFIX
#define IOS_ZIP_PREFIX "SH3-game"       /* the build's game-file zips: SH3-game-1.zip, SH3-game-2.zip... */
#endif

static pthread_mutex_t gate_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cv = PTHREAD_COND_INITIALIZER;
static atomic_int background, parked;
static atomic_int game_running;

/* The game folder: the app's Documents folder, which the Files app shows under "On My iPhone" once
 * Info.plist sets UIFileSharingEnabled and LSSupportsOpeningDocumentsInPlace. */
const char *ios_game_dir(void) {
    static char dir[512];
    const char *home = getenv("HOME");
    snprintf(dir, sizeof dir, "%s/Documents", home ? home : ".");
    return dir;
}

/* Called by main() before anything reads the game: without the executable there is nothing to run, so
 * say where the files go instead of failing on the first read. */
void ios_require_file(const char *name) {
    struct stat st;
    if (!stat(name, &st)) return;
    char msg[1024];
    snprintf(msg, sizeof msg,
             "%s is not in the app's folder.\n\nSave the " IOS_ZIP_PREFIX "-1.zip, -2.zip... files from your build into "
             "Files > On My iPhone > %s and start the app again: it unpacks them itself. (Or copy your game folder's "
             "contents there, so that %s sits at the top.)",
             name, IOS_APP_FOLDER, name);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Game files needed", msg, NULL);
    exit(0);
}

/* Runs on the main thread, inside SDL_PumpEvents, before SDL queues the event. */
static int SDLCALL app_events(void *unused, SDL_Event *e) {
    (void)unused;
    touch_event(e);
    switch (e->type) {
    case SDL_APP_WILLENTERBACKGROUND:
        atomic_store(&background, 1);
        if (atomic_load(&game_running)) {
            /* Wait (briefly: iOS gives the app a few seconds) for the game thread to finish its GL work. */
            struct timespec until;
            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_sec += 2;
            pthread_mutex_lock(&gate_mx);
            while (!atomic_load(&parked))
                if (pthread_cond_timedwait(&gate_cv, &gate_mx, &until)) break;
            pthread_mutex_unlock(&gate_mx);
            rt_log("ios: to the background (game %s)", atomic_load(&parked) ? "parked" : "did not park in time");
        }
        break;
    case SDL_APP_DIDENTERFOREGROUND:
        pthread_mutex_lock(&gate_mx);
        atomic_store(&background, 0);
        pthread_cond_broadcast(&gate_cv);
        pthread_mutex_unlock(&gate_mx);
        rt_log("ios: back in the foreground");
        break;
    case SDL_APP_LOWMEMORY:
        rt_log("ios: low memory warning (guest heap %u MB)", rt_heap_used() >> 20);
        break;
    case SDL_APP_TERMINATING:
        rt_log("ios: terminating");
        break;
    }
    return 1;
}

/* ---- the game files, delivered as zips ----
 * A download is limited to 2 GB, so the build packs the game folder into SH3-game-1.zip, SH3-game-2.zip...
 * Saved into the app's folder (Files > On My iPhone > the app), they are unpacked here on the next launch, with a
 * progress bar, and deleted once unpacked. */
typedef struct { uint64_t total; Uint64 last_draw; } ImportProgress;

static void draw_progress(uint64_t done, void *ctx) {
    ImportProgress *p = ctx;
    Uint64 now = SDL_GetTicks64();
    if (now - p->last_draw < 100 && done < p->total) return;
    p->last_draw = now;
    int w, h;
    SDL_GL_GetDrawableSize(rt_window, &w, &h);
    glBindFramebuffer(GL_FRAMEBUFFER, gl_screen_fbo());
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    /* a bar across the middle: its frame, then the part done */
    int bw = w * 6 / 10, bh = h / 30 + 2, bx = (w - bw) / 2, by = (h - bh) / 2, edge = bh / 8 + 1;
    glEnable(GL_SCISSOR_TEST);
    glScissor(bx - edge, by - edge, bw + 2 * edge, bh + 2 * edge);
    glClearColor(0.45f, 0.45f, 0.45f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(bx, by, bw, bh);
    glClearColor(0.05f, 0.05f, 0.05f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    int fill = p->total ? (int)((double)done / p->total * bw) : 0;
    if (fill > 0) {
        glScissor(bx, by, fill, bh);
        glClearColor(0.75f, 0.12f, 0.10f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_SCISSOR_TEST);
    SDL_GL_SwapWindow(rt_window);
    SDL_PumpEvents();  /* keeps iOS from taking the app for hung */
}

static int is_game_zip(const char *name) {
    size_t n = strlen(name);
    return !strncmp(name, IOS_ZIP_PREFIX, strlen(IOS_ZIP_PREFIX)) && n > 4 && !strcasecmp(name + n - 4, ".zip");
}
static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* Main thread, with the GL context current: unpacks any game zips in the game folder (the current directory). */
static void import_game_zips(void) {
    DIR *d = opendir(".");
    if (!d) return;
    char *names[64];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < 64)
        if (is_game_zip(e->d_name)) names[n++] = strdup(e->d_name);
    closedir(d);
    if (!n) return;
    qsort(names, n, sizeof *names, by_name);
    ImportProgress p = {0, 0};
    for (int i = 0; i < n; i++) p.total += unzip_total_size(names[i]);
    rt_log("ios: unpacking %d game zip(s), %llu MB", n, (unsigned long long)(p.total >> 20));
    uint64_t done = 0;
    for (int i = 0; i < n; i++) {
        char err[512] = "";
        uint64_t size = unzip_total_size(names[i]);
        if (unzip_to(names[i], ".", draw_progress, &p, done, err, sizeof err)) {
            char msg[1024];
            snprintf(msg, sizeof msg, "%s could not be unpacked: %s.\n\nThe files unpacked so far are kept; fix this and start "
                     "the app again to carry on.", names[i], err);
            rt_log("ios: %s", msg);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Game files", msg, rt_window);
            exit(1);
        }
        done += size;
        remove(names[i]);  /* unpacked: the space goes back to the phone */
        rt_log("ios: unpacked %s", names[i]);
        free(names[i]);
    }
    draw_progress(p.total, &p);
}

/* Main thread, before the game starts: window, GL context (left current on no thread), app events. */
void ios_main_start(void) {
    rt_window_open();
    gl_context_open();
    import_game_zips();
    SDL_GL_MakeCurrent(rt_window, NULL);  /* the game thread makes it current (gl_context_open) */
    touch_init();
    SDL_AddEventWatch(app_events, NULL);
    int w, h;
    SDL_GL_GetDrawableSize(rt_window, &w, &h);
    rt_log("ios: window %dx%d pixels", w, h);
}

/* Main thread, after the game thread started: never returns. SDL_PumpEvents runs UIKit's run loop
 * (touches, controllers, app events) and queues what it gets for rt_poll_event. */
_Noreturn void ios_main_loop(void) {
    atomic_store(&game_running, 1);
    for (;;) {
        SDL_PumpEvents();
        SDL_Delay(4);
    }
}

/* Game thread, once per presented frame (and per launcher frame): parks while the app is in the background. */
void ios_frame_gate(void) {
    if (!atomic_load(&background)) return;
    glFinish();  /* nothing of ours may still be queued for the GPU when the app leaves the screen */
    pthread_mutex_lock(&gate_mx);
    atomic_store(&parked, 1);
    pthread_cond_broadcast(&gate_cv);
    while (atomic_load(&background)) pthread_cond_wait(&gate_cv, &gate_mx);
    atomic_store(&parked, 0);
    pthread_mutex_unlock(&gate_mx);
}

/* The screen in pixels, for the "Full screen" resolution (rt_core.c read_ini): the window is open before the game reads
 * sh2e.ini. 0 when it is not known. */
void ios_screen_size(int *w, int *h) {
    *w = *h = 0;
    if (rt_window) SDL_GL_GetDrawableSize(rt_window, w, h);
    if (*w < *h) { int t = *w; *w = *h; *h = t; }  /* landscape, whichever way the view reports it */
}
#endif
