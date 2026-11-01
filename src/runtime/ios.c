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

void gl_context_open(void);  /* d3d8.c */

#ifndef IOS_APP_FOLDER
#define IOS_APP_FOLDER "Silent Hill 3"  /* the app's name, as the Files app lists its folder */
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
             "%s is not in the app's folder.\n\nCopy your PC game folder's contents (with the Enhanced Edition files) "
             "into Files > On My iPhone > %s, so that %s sits at the top, then start the app again.",
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

/* Main thread, before the game starts: window, GL context (left current on no thread), app events. */
void ios_main_start(void) {
    rt_window_open();
    gl_context_open();
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
