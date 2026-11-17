#ifndef GHOSTREEL_PLAYER_H
#define GHOSTREEL_PLAYER_H

#include <windows.h>

/* Messages posted to the notify window from libVLC threads. */
#define WM_APP_VIDEOSIZE (WM_APP + 1) /* wParam = width, lParam = height   */
#define WM_APP_FRAME     (WM_APP + 2) /* new frame ready -> repaint         */
#define WM_APP_END       (WM_APP + 3) /* wParam = 1 on error, 0 on EOF      */
#define WM_APP_PLAYING   (WM_APP + 4) /* playback actually started          */

BOOL player_init(HWND notify);
void player_shutdown(void);

/* Playlist. Paths may be files or folders (scanned recursively, natural
 * sort). replace=TRUE clears the playlist first. Starts playback if idle. */
void player_add_paths(const wchar_t *const *paths, int count, BOOL replace);
int  player_count(void);

/* Handlers for the WM_APP_* messages above (call from the UI thread). */
void player_on_end(BOOL error);
void player_on_playing(void);
void player_on_frame_painted(void);

void player_toggle_pause(void);
void player_next(void);
void player_prev(void);
void player_seek_ms(long long delta);
BOOL player_is_active(void);   /* opening/playing/paused */
BOOL player_is_playing(void);

void player_set_volume(int vol);      /* 0..150 */
int  player_get_volume(void);
void player_toggle_mute(void);
BOOL player_is_muted(void);

void player_set_loop(BOOL loop);
BOOL player_get_loop(void);
void player_set_shuffle(BOOL shuffle);
BOOL player_get_shuffle(void);

/* Current item file name (no dir) or NULL. */
const wchar_t *player_current_name(void);

/* Paint the latest frame letterboxed into rc. Returns FALSE if no frame. */
BOOL player_paint(HDC dc, const RECT *rc);

#endif
