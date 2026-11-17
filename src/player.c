/* libVLC wrapper: own playlist + vmem (RV32) frame callbacks rendered with GDI
 * so the host window can be layered (opacity) and click-through. */
#include "player.h"

#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlc/vlc.h>

#define MAX_DECODE_W 3840
#define MAX_DECODE_H 2160

static libvlc_instance_t     *g_vlc;
static libvlc_media_player_t *g_mp;
static HWND                   g_notify;

/* ---- playlist ------------------------------------------------------------ */
static wchar_t **g_items;   /* file paths, insertion order        */
static int      *g_order;   /* play order -> index into g_items   */
static int       g_n, g_cap;
static int       g_pos = -1;          /* position in g_order, -1 = idle */
static int       g_err_streak;
static BOOL      g_loop = TRUE, g_shuffle;
static int       g_volume = 80;
static BOOL      g_muted;

/* ---- frame buffer (shared with vlc decoder thread) ---------------------- */
static CRITICAL_SECTION g_cs;
static unsigned char   *g_buf;
static unsigned         g_w, g_h, g_pitch;
static BOOL             g_have_frame;
static volatile LONG    g_frame_posted;

/* ---- logging (GHOSTREEL_LOG=1 -> ghostreel.log next to exe) ------------------------ */
static FILE *g_log;

static void log_cb(void *data, int level, const libvlc_log_t *ctx,
                   const char *fmt, va_list args)
{
    static const char *lv[] = { "dbg", "?", "info", "warn", "err" };
    const char *module = NULL;
    libvlc_log_get_context(ctx, &module, NULL, NULL);
    fprintf(g_log, "[%s] %s: ", (level >= 0 && level <= 4) ? lv[level] : "?",
            module ? module : "-");
    vfprintf(g_log, fmt, args);
    fputc('\n', g_log);
    fflush(g_log);
}

/* ---- helpers -------------------------------------------------------------- */
static char *to_utf8(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = malloc(n);
    if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

static BOOL is_video_ext(const wchar_t *path)
{
    static const wchar_t *exts[] = {
        L".mp4", L".m4v", L".mkv", L".webm", L".avi", L".mov", L".wmv",
        L".flv", L".ts", L".m2ts", L".mts", L".mpg", L".mpeg", L".ogv",
        L".3gp", L".gif", NULL };
    const wchar_t *e = PathFindExtensionW(path);
    for (int i = 0; exts[i]; i++)
        if (!_wcsicmp(e, exts[i])) return TRUE;
    return FALSE;
}

static void post_frame(void)
{
    if (InterlockedExchange(&g_frame_posted, 1) == 0)
        PostMessageW(g_notify, WM_APP_FRAME, 0, 0);
}

/* ---- vmem callbacks (vlc threads) ---------------------------------------- */
static unsigned fmt_setup(void **opaque, char *chroma, unsigned *width,
                          unsigned *height, unsigned *pitches, unsigned *lines)
{
    unsigned w = *width, h = *height;
    if (w > MAX_DECODE_W || h > MAX_DECODE_H) { /* let swscale shrink it */
        double s = min((double)MAX_DECODE_W / w, (double)MAX_DECODE_H / h);
        w = (unsigned)(w * s) & ~1u;
        h = (unsigned)(h * s) & ~1u;
    }
    memcpy(chroma, "RV32", 4);
    unsigned pitch = (w * 4 + 31) & ~31u;

    EnterCriticalSection(&g_cs);
    free(g_buf);
    g_buf = calloc((size_t)pitch * (h + 1), 1);
    g_w = w; g_h = h; g_pitch = pitch;
    g_have_frame = FALSE;
    LeaveCriticalSection(&g_cs);
    if (!g_buf) return 0;

    *width = w; *height = h;
    pitches[0] = pitch;
    lines[0] = h;
    PostMessageW(g_notify, WM_APP_VIDEOSIZE, w, h);
    return 1;
}

static void fmt_cleanup(void *opaque)
{
    EnterCriticalSection(&g_cs);
    free(g_buf);
    g_buf = NULL;
    g_have_frame = FALSE;
    LeaveCriticalSection(&g_cs);
    post_frame();
}

static void *vid_lock(void *opaque, void **planes)
{
    EnterCriticalSection(&g_cs);
    planes[0] = g_buf;
    return NULL;
}

static void vid_unlock(void *opaque, void *picture, void *const *planes)
{
    g_have_frame = TRUE;
    LeaveCriticalSection(&g_cs);
}

static void vid_display(void *opaque, void *picture)
{
    post_frame();
}

/* ---- events (vlc event thread: only post messages) ----------------------- */
static void on_event(const struct libvlc_event_t *ev, void *data)
{
    switch (ev->type) {
    case libvlc_MediaPlayerEndReached:
        PostMessageW(g_notify, WM_APP_END, 0, 0); break;
    case libvlc_MediaPlayerEncounteredError:
        PostMessageW(g_notify, WM_APP_END, 1, 0); break;
    case libvlc_MediaPlayerPlaying:
        PostMessageW(g_notify, WM_APP_PLAYING, 0, 0); break;
    default: break;
    }
}

/* ---- init ------------------------------------------------------------------ */
BOOL player_init(HWND notify)
{
    g_notify = notify;
    InitializeCriticalSection(&g_cs);

    const char *args[] = {
        "--ignore-config",
        "--no-video-title-show",
        "--no-stats",
        "--no-snapshot-preview",
        "--no-sub-autodetect-file",
        "--avcodec-hw=any",
    };
    g_vlc = libvlc_new(sizeof args / sizeof *args, args);
    if (!g_vlc) /* e.g. avcodec plugin missing -> retry with core opts only */
        g_vlc = libvlc_new(4, args);
    if (!g_vlc) return FALSE;

    if (GetEnvironmentVariableW(L"GHOSTREEL_LOG", NULL, 0)) {
        wchar_t p[MAX_PATH];
        GetModuleFileNameW(NULL, p, MAX_PATH);
        PathRemoveFileSpecW(p);
        PathAppendW(p, L"ghostreel.log");
        g_log = _wfopen(p, L"w");
        if (g_log) libvlc_log_set(g_vlc, log_cb, NULL);
    }
    libvlc_set_user_agent(g_vlc, "GhostReel", "GhostReel/1.0");

    g_mp = libvlc_media_player_new(g_vlc);
    if (!g_mp) return FALSE;
    libvlc_video_set_callbacks(g_mp, vid_lock, vid_unlock, vid_display, NULL);
    libvlc_video_set_format_callbacks(g_mp, fmt_setup, fmt_cleanup);
    libvlc_video_set_key_input(g_mp, 0);
    libvlc_video_set_mouse_input(g_mp, 0);

    libvlc_event_manager_t *em = libvlc_media_player_event_manager(g_mp);
    libvlc_event_attach(em, libvlc_MediaPlayerEndReached, on_event, NULL);
    libvlc_event_attach(em, libvlc_MediaPlayerEncounteredError, on_event, NULL);
    libvlc_event_attach(em, libvlc_MediaPlayerPlaying, on_event, NULL);
    return TRUE;
}

void player_shutdown(void)
{
    if (g_mp) {
        libvlc_media_player_stop(g_mp);
        libvlc_media_player_release(g_mp);
        g_mp = NULL;
    }
    if (g_vlc) {
        if (g_log) libvlc_log_unset(g_vlc);
        libvlc_release(g_vlc);
        g_vlc = NULL;
    }
    if (g_log) { fclose(g_log); g_log = NULL; }
    for (int i = 0; i < g_n; i++) free(g_items[i]);
    free(g_items); free(g_order);
    g_items = NULL; g_order = NULL; g_n = g_cap = 0;
    free(g_buf); g_buf = NULL;
    DeleteCriticalSection(&g_cs);
}

/* ---- playlist -------------------------------------------------------------- */
static void shuffle_from(int start)
{
    for (int i = g_n - 1; i > start; i--) {
        int j = start + rand() % (i - start + 1);
        int t = g_order[i]; g_order[i] = g_order[j]; g_order[j] = t;
    }
}

static void push_item(const wchar_t *path)
{
    if (g_n == g_cap) {
        int cap = g_cap ? g_cap * 2 : 64;
        wchar_t **it = realloc(g_items, cap * sizeof *it);
        if (!it) return;
        g_items = it;
        int *o = realloc(g_order, cap * sizeof *o);
        if (!o) return;
        g_order = o;
        g_cap = cap;
    }
    g_items[g_n] = _wcsdup(path);
    g_order[g_n] = g_n;
    g_n++;
}

static int cmp_logical(const void *a, const void *b)
{
    return StrCmpLogicalW(*(const wchar_t *const *)a, *(const wchar_t *const *)b);
}

typedef struct { wchar_t **v; int n, cap; } pathvec;

static void vec_push(pathvec *pv, const wchar_t *p)
{
    if (pv->n == pv->cap) {
        int cap = pv->cap ? pv->cap * 2 : 64;
        wchar_t **v = realloc(pv->v, cap * sizeof *v);
        if (!v) return;
        pv->v = v; pv->cap = cap;
    }
    pv->v[pv->n++] = _wcsdup(p);
}

static void scan_dir(const wchar_t *dir, pathvec *pv, int depth)
{
    if (depth > 16) return;
    wchar_t pat[MAX_PATH * 2];
    _snwprintf(pat, MAX_PATH * 2, L"%s\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch,
                                NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    pathvec files = {0}, dirs = {0};
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
        wchar_t full[MAX_PATH * 2];
        _snwprintf(full, MAX_PATH * 2, L"%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) vec_push(&dirs, full);
        else if (is_video_ext(full)) vec_push(&files, full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    qsort(files.v, files.n, sizeof *files.v, cmp_logical);
    qsort(dirs.v, dirs.n, sizeof *dirs.v, cmp_logical);
    for (int i = 0; i < files.n; i++) { vec_push(pv, files.v[i]); free(files.v[i]); }
    for (int i = 0; i < dirs.n; i++) { scan_dir(dirs.v[i], pv, depth + 1); free(dirs.v[i]); }
    free(files.v); free(dirs.v);
}

static void play_pos(int pos)
{
    if (pos < 0 || pos >= g_n) return;
    g_pos = pos;
    char *u = to_utf8(g_items[g_order[pos]]);
    if (!u) return;
    libvlc_media_t *md = libvlc_media_new_path(g_vlc, u);
    free(u);
    if (!md) return;
    libvlc_media_player_set_media(g_mp, md);
    libvlc_media_release(md);
    libvlc_media_player_play(g_mp);
}

void player_add_paths(const wchar_t *const *paths, int count, BOOL replace)
{
    if (replace) {
        libvlc_media_player_stop(g_mp);
        for (int i = 0; i < g_n; i++) free(g_items[i]);
        g_n = 0;
        g_pos = -1;
    }
    int first_new = g_n;
    for (int i = 0; i < count; i++) {
        DWORD a = GetFileAttributesW(paths[i]);
        if (a == INVALID_FILE_ATTRIBUTES) continue;
        if (a & FILE_ATTRIBUTE_DIRECTORY) {
            pathvec pv = {0};
            scan_dir(paths[i], &pv, 0);
            for (int k = 0; k < pv.n; k++) { push_item(pv.v[k]); free(pv.v[k]); }
            free(pv.v);
        } else {
            push_item(paths[i]); /* explicit files: accept any extension */
        }
    }
    if (g_n == first_new) return;
    if (g_shuffle) shuffle_from(g_pos >= 0 ? g_pos + 1 : first_new);
    if (!player_is_active()) {
        g_err_streak = 0;
        play_pos(g_pos >= 0 ? g_pos + 1 < g_n ? g_pos + 1 : 0 : first_new);
    }
}

int player_count(void) { return g_n; }

BOOL player_is_active(void)
{
    libvlc_state_t s = libvlc_media_player_get_state(g_mp);
    return s == libvlc_Opening || s == libvlc_Buffering ||
           s == libvlc_Playing || s == libvlc_Paused;
}

BOOL player_is_playing(void) { return libvlc_media_player_is_playing(g_mp) != 0; }

void player_on_end(BOOL error)
{
    if (g_n == 0) return;
    if (error) {
        if (++g_err_streak >= g_n) { g_pos = -1; return; } /* everything broken */
    }
    if (g_pos + 1 < g_n) play_pos(g_pos + 1);
    else if (g_loop) {
        if (g_shuffle) shuffle_from(0);
        play_pos(0);
    } else {
        g_pos = -1; /* stop at end of list */
    }
}

void player_on_playing(void)
{
    g_err_streak = 0;
    libvlc_audio_set_volume(g_mp, g_volume);
    libvlc_audio_set_mute(g_mp, g_muted);
}

void player_on_frame_painted(void) { InterlockedExchange(&g_frame_posted, 0); }

/* ---- transport --------------------------------------------------------------- */
void player_toggle_pause(void)
{
    if (player_is_active()) libvlc_media_player_pause(g_mp);
    else if (g_n) play_pos(g_pos >= 0 ? g_pos : 0);
}

void player_next(void)
{
    if (!g_n) return;
    play_pos(g_pos + 1 < g_n ? g_pos + 1 : 0);
}

void player_prev(void)
{
    if (!g_n) return;
    /* >3s in: restart current, like most players */
    if (player_is_active() && libvlc_media_player_get_time(g_mp) > 3000) {
        libvlc_media_player_set_time(g_mp, 0);
        return;
    }
    play_pos(g_pos > 0 ? g_pos - 1 : g_n - 1);
}

void player_seek_ms(long long delta)
{
    if (!player_is_active() || !libvlc_media_player_is_seekable(g_mp)) return;
    libvlc_time_t t = libvlc_media_player_get_time(g_mp) + delta;
    libvlc_time_t len = libvlc_media_player_get_length(g_mp);
    if (t < 0) t = 0;
    if (len > 0 && t > len - 500) t = len - 500;
    libvlc_media_player_set_time(g_mp, t);
}

/* ---- audio ------------------------------------------------------------------ */
void player_set_volume(int vol)
{
    g_volume = vol < 0 ? 0 : vol > 150 ? 150 : vol;
    libvlc_audio_set_volume(g_mp, g_volume);
}
int  player_get_volume(void) { return g_volume; }
void player_toggle_mute(void) { g_muted = !g_muted; libvlc_audio_set_mute(g_mp, g_muted); }
BOOL player_is_muted(void) { return g_muted; }

/* ---- modes --------------------------------------------------------------------- */
void player_set_loop(BOOL loop) { g_loop = loop; }
BOOL player_get_loop(void) { return g_loop; }

void player_set_shuffle(BOOL shuffle)
{
    if (shuffle == g_shuffle) return;
    g_shuffle = shuffle;
    if (!g_n) return;
    int cur = g_pos >= 0 ? g_order[g_pos] : -1;
    for (int i = 0; i < g_n; i++) g_order[i] = i;
    if (shuffle) {
        if (cur >= 0) { g_order[0] = cur; g_order[cur] = 0; g_pos = 0; }
        shuffle_from(cur >= 0 ? 1 : 0);
    } else if (cur >= 0) {
        g_pos = cur;
    }
}
BOOL player_get_shuffle(void) { return g_shuffle; }

const wchar_t *player_current_name(void)
{
    if (g_pos < 0 || g_pos >= g_n) return NULL;
    return PathFindFileNameW(g_items[g_order[g_pos]]);
}

/* ---- painting -------------------------------------------------------------------- */
BOOL player_paint(HDC dc, const RECT *rc)
{
    BOOL ok = FALSE;
    int cw = rc->right - rc->left, ch = rc->bottom - rc->top;
    EnterCriticalSection(&g_cs);
    if (g_buf && g_have_frame && g_w && g_h && cw > 0 && ch > 0) {
        /* letterbox */
        int dw = cw, dh = (int)((long long)cw * g_h / g_w);
        if (dh > ch) { dh = ch; dw = (int)((long long)ch * g_w / g_h); }
        int dx = rc->left + (cw - dw) / 2, dy = rc->top + (ch - dh) / 2;

        BITMAPINFO bi = {0};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = g_pitch / 4;
        bi.bmiHeader.biHeight = -(LONG)g_h; /* top-down */
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        if ((unsigned)dw < g_w) {
            SetStretchBltMode(dc, HALFTONE);
            SetBrushOrgEx(dc, 0, 0, NULL);
        } else {
            SetStretchBltMode(dc, COLORONCOLOR);
        }
        StretchDIBits(dc, dx, dy, dw, dh, 0, 0, g_w, g_h, g_buf, &bi,
                      DIB_RGB_COLORS, SRCCOPY);

        /* black bars */
        HBRUSH black = GetStockObject(BLACK_BRUSH);
        RECT r;
        SetRect(&r, rc->left, rc->top, rc->right, dy);           FillRect(dc, &r, black);
        SetRect(&r, rc->left, dy + dh, rc->right, rc->bottom);   FillRect(dc, &r, black);
        SetRect(&r, rc->left, dy, dx, dy + dh);                  FillRect(dc, &r, black);
        SetRect(&r, dx + dw, dy, rc->right, dy + dh);            FillRect(dc, &r, black);
        ok = TRUE;
    }
    LeaveCriticalSection(&g_cs);
    return ok;
}
