/* GhostReel: borderless always-on-top video overlay on stripped libVLC.
 *
 * Global hotkeys (work even in click-through mode):
 *   Ctrl+Alt+T            toggle click-through
 *   Ctrl+Alt+Up/Down      opacity +/-
 *   Ctrl+Alt+Space        play/pause
 *   Ctrl+Alt+Right/Left   next/previous
 *   Ctrl+Alt+Shift+R/L    seek +/-10 s
 *   Ctrl+Alt+H            hide/show (pauses while hidden)
 *   Ctrl+Alt+Q            quit
 * Mouse: drag = move, edges = resize (Shift = free aspect), wheel = volume,
 *        Ctrl+wheel = opacity, double-click = pause, right-click = menu.
 */
#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>

#include "player.h"
#include "settings.h"

#define APP_NAME   L"GhostReel"
#define TIMER_OSD  1
#define OSD_MS     1500

enum {
    HK_CLICKTHRU = 1, HK_OPA_UP, HK_OPA_DOWN, HK_PAUSE, HK_NEXT, HK_PREV,
    HK_SEEK_FWD, HK_SEEK_BACK, HK_HIDE, HK_QUIT,
};

enum {
    CMD_OPEN_FILES = 100, CMD_OPEN_FOLDER, CMD_ADD_FILES, CMD_PAUSE, CMD_NEXT,
    CMD_PREV, CMD_LOOP, CMD_SHUFFLE, CMD_MUTE, CMD_CLICKTHRU, CMD_HIDE,
    CMD_FIT_VIDEO, CMD_EXIT,
    CMD_OPACITY_BASE = 200, /* + percent */
};

static HWND       g_hwnd;
static settings_t g_set;
static BOOL       g_clickthru;
static BOOL       g_hidden_paused;
static double     g_aspect = 16.0 / 9.0;
static wchar_t    g_osd[160];
static HFONT      g_font_osd, g_font_hint;
static HDC        g_memdc;
static HBITMAP    g_membmp, g_oldbmp;
static int        g_memw, g_memh;

/* ---------------------------------------------------------------- helpers -- */
static int dpi_scale(int v)
{
    UINT dpi = g_hwnd ? GetDpiForWindow(g_hwnd) : 96;
    return MulDiv(v, dpi, 96);
}

static void make_fonts(void)
{
    if (g_font_osd) DeleteObject(g_font_osd);
    if (g_font_hint) DeleteObject(g_font_hint);
    g_font_osd = CreateFontW(-dpi_scale(16), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_hint = CreateFontW(-dpi_scale(14), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                              DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

static void osd(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(g_osd, 160, fmt, ap);
    va_end(ap);
    g_osd[159] = 0;
    SetTimer(g_hwnd, TIMER_OSD, OSD_MS, NULL);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void apply_opacity(void)
{
    SetLayeredWindowAttributes(g_hwnd, 0, (BYTE)(g_set.opacity * 255 / 100), LWA_ALPHA);
}

static void set_opacity(int pct)
{
    g_set.opacity = pct < 10 ? 10 : pct > 100 ? 100 : pct;
    apply_opacity();
    osd(L"Opacity %d%%", g_set.opacity);
}

static void set_volume(int vol)
{
    player_set_volume(vol);
    g_set.volume = player_get_volume();
    osd(L"Volume %d%%", g_set.volume);
}

static void set_clickthru(BOOL on)
{
    g_clickthru = on;
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    ex = on ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT);
    SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
    osd(on ? L"Click-through ON  (Ctrl+Alt+T to undo)" : L"Click-through OFF");
}

static void toggle_hidden(void)
{
    if (IsWindowVisible(g_hwnd)) {
        if (player_is_playing()) {
            g_hidden_paused = TRUE;
            player_toggle_pause();
        }
        ShowWindow(g_hwnd, SW_HIDE);
    } else {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (g_hidden_paused) player_toggle_pause();
        g_hidden_paused = FALSE;
    }
}

static void update_title(void)
{
    const wchar_t *n = player_current_name();
    wchar_t t[MAX_PATH + 32];
    if (n) _snwprintf(t, MAX_PATH + 32, L"%s - %s", n, APP_NAME);
    else wcscpy(t, APP_NAME);
    SetWindowTextW(g_hwnd, t);
}

/* Resize window to video aspect, keeping width (or height if too tall). */
static void fit_to_video(void)
{
    RECT r;
    GetWindowRect(g_hwnd, &r);
    int w = r.right - r.left;
    int h = (int)(w / g_aspect + 0.5);
    MONITORINFO mi = { sizeof mi };
    GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    int maxh = mi.rcWork.bottom - mi.rcWork.top;
    if (h > maxh) { h = maxh; w = (int)(h * g_aspect + 0.5); }
    int x = r.left, y = r.top;
    if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
    if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
    SetWindowPos(g_hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void default_rect(RECT *r)
{
    MONITORINFO mi = { sizeof mi };
    POINT origin = {0, 0};
    GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi);
    int m = 24, w = 480, h = 270;
    r->right = mi.rcWork.right - m;
    r->bottom = mi.rcWork.bottom - m;
    r->left = r->right - w;
    r->top = r->bottom - h;
}

/* ------------------------------------------------------------ file dialog -- */
static void remember_dir(const wchar_t *path, BOOL is_dir)
{
    wcsncpy(g_set.last_dir, path, MAX_PATH - 1);
    g_set.last_dir[MAX_PATH - 1] = 0;
    if (!is_dir) PathRemoveFileSpecW(g_set.last_dir);
}

/* Returns number of selected paths; *out is malloc'd array of _wcsdup'd strings. */
static int pick_paths(BOOL folder, wchar_t ***out)
{
    *out = NULL;
    IFileOpenDialog *dlg = NULL;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileOpenDialog, (void **)&dlg)))
        return 0;

    DWORD opts = 0;
    IFileOpenDialog_GetOptions(dlg, &opts);
    opts |= FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT;
    opts |= folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
    IFileOpenDialog_SetOptions(dlg, opts);
    if (!folder) {
        COMDLG_FILTERSPEC spec[] = {
            { L"Video files", L"*.mp4;*.m4v;*.mkv;*.webm;*.avi;*.mov;*.wmv;*.flv;"
                              L"*.ts;*.m2ts;*.mts;*.mpg;*.mpeg;*.ogv;*.3gp;*.gif" },
            { L"All files", L"*.*" },
        };
        IFileOpenDialog_SetFileTypes(dlg, 2, spec);
    }
    if (g_set.last_dir[0]) {
        IShellItem *start = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(g_set.last_dir, NULL, &IID_IShellItem,
                                                  (void **)&start))) {
            IFileOpenDialog_SetFolder(dlg, start);
            IShellItem_Release(start);
        }
    }

    int n = 0;
    IShellItemArray *arr = NULL;
    if (SUCCEEDED(IFileOpenDialog_Show(dlg, g_hwnd)) &&
        SUCCEEDED(IFileOpenDialog_GetResults(dlg, &arr))) {
        DWORD count = 0;
        IShellItemArray_GetCount(arr, &count);
        *out = calloc(count ? count : 1, sizeof **out);
        for (DWORD i = 0; *out && i < count; i++) {
            IShellItem *it = NULL;
            if (FAILED(IShellItemArray_GetItemAt(arr, i, &it))) continue;
            wchar_t *p = NULL;
            if (SUCCEEDED(IShellItem_GetDisplayName(it, SIGDN_FILESYSPATH, &p))) {
                (*out)[n++] = _wcsdup(p);
                CoTaskMemFree(p);
            }
            IShellItem_Release(it);
        }
        IShellItemArray_Release(arr);
    }
    IFileOpenDialog_Release(dlg);
    if (n) remember_dir((*out)[0], folder);
    return n;
}

static void open_dialog(BOOL folder, BOOL replace)
{
    wchar_t **paths;
    int n = pick_paths(folder, &paths);
    if (n) {
        player_add_paths((const wchar_t *const *)paths, n, replace);
        osd(L"%d item(s) in playlist", player_count());
    }
    for (int i = 0; i < n; i++) free(paths[i]);
    free(paths);
}

/* ------------------------------------------------------------------- menu -- */
static void show_menu(int x, int y)
{
    HMENU m = CreatePopupMenu(), op = CreatePopupMenu();
    static const int levels[] = { 100, 90, 75, 60, 50, 35, 25 };
    for (int i = 0; i < 7; i++) {
        wchar_t s[16];
        _snwprintf(s, 16, L"%d%%", levels[i]);
        AppendMenuW(op, MF_STRING | (g_set.opacity == levels[i] ? MF_CHECKED : 0),
                    CMD_OPACITY_BASE + levels[i], s);
    }
    AppendMenuW(m, MF_STRING, CMD_OPEN_FILES,  L"Open files...");
    AppendMenuW(m, MF_STRING, CMD_OPEN_FOLDER, L"Open folder...");
    AppendMenuW(m, MF_STRING, CMD_ADD_FILES,   L"Add files to playlist...");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, CMD_PAUSE, L"Play / Pause\tCtrl+Alt+Space");
    AppendMenuW(m, MF_STRING, CMD_NEXT,  L"Next\tCtrl+Alt+Right");
    AppendMenuW(m, MF_STRING, CMD_PREV,  L"Previous\tCtrl+Alt+Left");
    AppendMenuW(m, MF_STRING | (player_get_loop() ? MF_CHECKED : 0), CMD_LOOP, L"Loop playlist");
    AppendMenuW(m, MF_STRING | (player_get_shuffle() ? MF_CHECKED : 0), CMD_SHUFFLE, L"Shuffle");
    AppendMenuW(m, MF_STRING | (player_is_muted() ? MF_CHECKED : 0), CMD_MUTE, L"Mute\tM");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)op, L"Opacity");
    AppendMenuW(m, MF_STRING | (g_clickthru ? MF_CHECKED : 0), CMD_CLICKTHRU,
                L"Click-through\tCtrl+Alt+T");
    AppendMenuW(m, MF_STRING, CMD_FIT_VIDEO, L"Fit window to video");
    AppendMenuW(m, MF_STRING, CMD_HIDE, L"Hide\tCtrl+Alt+H");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, CMD_EXIT, L"Exit\tCtrl+Alt+Q");

    SetForegroundWindow(g_hwnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, g_hwnd, NULL);
    DestroyMenu(m); /* destroys submenu too */
    if (cmd) SendMessageW(g_hwnd, WM_COMMAND, cmd, 0);
}

static void on_command(int cmd)
{
    if (cmd > CMD_OPACITY_BASE && cmd <= CMD_OPACITY_BASE + 100) {
        set_opacity(cmd - CMD_OPACITY_BASE);
        return;
    }
    switch (cmd) {
    case CMD_OPEN_FILES:  open_dialog(FALSE, TRUE); break;
    case CMD_OPEN_FOLDER: open_dialog(TRUE, TRUE); break;
    case CMD_ADD_FILES:   open_dialog(FALSE, FALSE); break;
    case CMD_PAUSE:       player_toggle_pause(); break;
    case CMD_NEXT:        player_next(); break;
    case CMD_PREV:        player_prev(); break;
    case CMD_LOOP:
        player_set_loop(!player_get_loop());
        g_set.loop = player_get_loop();
        osd(L"Loop %s", g_set.loop ? L"on" : L"off");
        break;
    case CMD_SHUFFLE:
        player_set_shuffle(!player_get_shuffle());
        g_set.shuffle = player_get_shuffle();
        osd(L"Shuffle %s", g_set.shuffle ? L"on" : L"off");
        break;
    case CMD_MUTE:
        player_toggle_mute();
        osd(player_is_muted() ? L"Muted" : L"Unmuted");
        break;
    case CMD_CLICKTHRU:   set_clickthru(!g_clickthru); break;
    case CMD_FIT_VIDEO:   fit_to_video(); break;
    case CMD_HIDE:        toggle_hidden(); break;
    case CMD_EXIT:        DestroyWindow(g_hwnd); break;
    }
}

/* ---------------------------------------------------------------- hotkeys -- */
static void register_hotkeys(HWND h)
{
    const UINT ca = MOD_CONTROL | MOD_ALT;
    struct { int id; UINT mod; UINT vk; } hk[] = {
        { HK_CLICKTHRU, ca | MOD_NOREPEAT, 'T' },
        { HK_OPA_UP,    ca, VK_UP },
        { HK_OPA_DOWN,  ca, VK_DOWN },
        { HK_PAUSE,     ca | MOD_NOREPEAT, VK_SPACE },
        { HK_NEXT,      ca | MOD_NOREPEAT, VK_RIGHT },
        { HK_PREV,      ca | MOD_NOREPEAT, VK_LEFT },
        { HK_SEEK_FWD,  ca | MOD_SHIFT, VK_RIGHT },
        { HK_SEEK_BACK, ca | MOD_SHIFT, VK_LEFT },
        { HK_HIDE,      ca | MOD_NOREPEAT, 'H' },
        { HK_QUIT,      ca | MOD_NOREPEAT, 'Q' },
    };
    int failed = 0;
    for (size_t i = 0; i < sizeof hk / sizeof *hk; i++)
        if (!RegisterHotKey(h, hk[i].id, hk[i].mod, hk[i].vk)) failed++;
    if (failed) osd(L"%d hotkey(s) already taken by another app", failed);
}

static void on_hotkey(int id)
{
    switch (id) {
    case HK_CLICKTHRU: set_clickthru(!g_clickthru); break;
    case HK_OPA_UP:    set_opacity(g_set.opacity + 5); break;
    case HK_OPA_DOWN:  set_opacity(g_set.opacity - 5); break;
    case HK_PAUSE:     player_toggle_pause(); break;
    case HK_NEXT:      player_next(); break;
    case HK_PREV:      player_prev(); break;
    case HK_SEEK_FWD:  player_seek_ms(10000); osd(L"+10 s"); break;
    case HK_SEEK_BACK: player_seek_ms(-10000); osd(L"-10 s"); break;
    case HK_HIDE:      toggle_hidden(); break;
    case HK_QUIT:      DestroyWindow(g_hwnd); break;
    }
}

/* ---------------------------------------------------------------- painting -- */
static void draw_text_shadow(HDC dc, const wchar_t *s, RECT *r, UINT fmt)
{
    RECT sh = *r;
    OffsetRect(&sh, 1, 1);
    SetTextColor(dc, RGB(0, 0, 0));
    DrawTextW(dc, s, -1, &sh, fmt);
    SetTextColor(dc, RGB(240, 240, 240));
    DrawTextW(dc, s, -1, r, fmt);
}

static void on_paint(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    int w = rc.right, hh = rc.bottom;

    if (!g_memdc || w != g_memw || hh != g_memh) {
        if (g_memdc) {
            SelectObject(g_memdc, g_oldbmp);
            DeleteObject(g_membmp);
            DeleteDC(g_memdc);
        }
        g_memdc = CreateCompatibleDC(dc);
        g_membmp = CreateCompatibleBitmap(dc, w > 0 ? w : 1, hh > 0 ? hh : 1);
        g_oldbmp = SelectObject(g_memdc, g_membmp);
        g_memw = w; g_memh = hh;
    }

    if (!player_paint(g_memdc, &rc)) {
        FillRect(g_memdc, &rc, GetStockObject(BLACK_BRUSH));
        if (!player_count()) {
            SelectObject(g_memdc, g_font_hint);
            SetBkMode(g_memdc, TRANSPARENT);
            SetTextColor(g_memdc, RGB(150, 150, 150));
            RECT t = rc;
            DrawTextW(g_memdc, L"Drop videos or folders here\nRight-click for menu",
                      -1, &t, DT_CENTER | DT_CALCRECT | DT_WORDBREAK);
            int th = t.bottom - t.top;
            t = rc;
            t.top = (hh - th) / 2;
            DrawTextW(g_memdc, L"Drop videos or folders here\nRight-click for menu",
                      -1, &t, DT_CENTER | DT_WORDBREAK);
        }
    }
    if (g_osd[0]) {
        SelectObject(g_memdc, g_font_osd);
        SetBkMode(g_memdc, TRANSPARENT);
        int m = dpi_scale(10);
        RECT t = { m, m, w - m, hh - m };
        draw_text_shadow(g_memdc, g_osd, &t, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    BitBlt(dc, 0, 0, w, hh, g_memdc, 0, 0, SRCCOPY);
    EndPaint(h, &ps);
}

/* --------------------------------------------------------------- resizing -- */
static LRESULT hit_test(HWND h, LPARAM lp)
{
    POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    RECT r;
    GetWindowRect(h, &r);
    int b = dpi_scale(8);
    BOOL l = pt.x < r.left + b, rt = pt.x >= r.right - b;
    BOOL t = pt.y < r.top + b,  bt = pt.y >= r.bottom - b;
    if (t && l)  return HTTOPLEFT;
    if (t && rt) return HTTOPRIGHT;
    if (bt && l) return HTBOTTOMLEFT;
    if (bt && rt) return HTBOTTOMRIGHT;
    if (l)  return HTLEFT;
    if (rt) return HTRIGHT;
    if (t)  return HTTOP;
    if (bt) return HTBOTTOM;
    return HTCAPTION;
}

static void on_sizing(WPARAM edge, RECT *r)
{
    if (GetKeyState(VK_SHIFT) < 0) return; /* free resize */
    int w = r->right - r->left, h = r->bottom - r->top;
    switch (edge) {
    case WMSZ_LEFT: case WMSZ_RIGHT:
        r->bottom = r->top + (int)(w / g_aspect + 0.5);
        break;
    case WMSZ_TOP: case WMSZ_BOTTOM:
        r->right = r->left + (int)(h * g_aspect + 0.5);
        break;
    case WMSZ_TOPLEFT: case WMSZ_TOPRIGHT:
        r->top = r->bottom - (int)(w / g_aspect + 0.5);
        break;
    case WMSZ_BOTTOMLEFT: case WMSZ_BOTTOMRIGHT:
        r->bottom = r->top + (int)(w / g_aspect + 0.5);
        break;
    }
}

/* ----------------------------------------------------------------- wndproc -- */
static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NCHITTEST:
        return hit_test(h, lp);

    case WM_NCLBUTTONDBLCLK:
        if (wp == HTCAPTION) { player_toggle_pause(); return 0; }
        break;

    case WM_NCRBUTTONDOWN:
        return 0;
    case WM_NCRBUTTONUP:
        show_menu(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_CONTEXTMENU: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (x == -1 && y == -1) { RECT r; GetWindowRect(h, &r); x = r.left; y = r.top; }
        show_menu(x, y);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int steps = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        if (!steps) steps = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
        if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) set_opacity(g_set.opacity + steps * 5);
        else set_volume(player_get_volume() + steps * 5);
        return 0;
    }

    case WM_KEYDOWN:
        switch (wp) {
        case VK_SPACE: player_toggle_pause(); break;
        case VK_RIGHT: player_seek_ms(5000); osd(L"+5 s"); break;
        case VK_LEFT:  player_seek_ms(-5000); osd(L"-5 s"); break;
        case VK_UP:    set_volume(player_get_volume() + 5); break;
        case VK_DOWN:  set_volume(player_get_volume() - 5); break;
        case 'N':      player_next(); break;
        case 'P':      player_prev(); break;
        case 'M':      on_command(CMD_MUTE); break;
        case 'O':      open_dialog(FALSE, TRUE); break;
        }
        return 0;

    case WM_HOTKEY:
        on_hotkey((int)wp);
        return 0;

    case WM_COMMAND:
        on_command(LOWORD(wp));
        return 0;

    case WM_DROPFILES: {
        HDROP d = (HDROP)wp;
        UINT n = DragQueryFileW(d, 0xFFFFFFFF, NULL, 0);
        wchar_t **paths = calloc(n ? n : 1, sizeof *paths);
        int k = 0;
        for (UINT i = 0; paths && i < n; i++) {
            UINT len = DragQueryFileW(d, i, NULL, 0) + 1;
            paths[k] = malloc(len * sizeof(wchar_t));
            if (paths[k] && DragQueryFileW(d, i, paths[k], len)) k++;
        }
        DragFinish(d);
        if (k) {
            remember_dir(paths[0], PathIsDirectoryW(paths[0]));
            /* Shift+drop replaces, plain drop appends */
            player_add_paths((const wchar_t *const *)paths, k, GetKeyState(VK_SHIFT) < 0);
            osd(L"%d item(s) in playlist", player_count());
        }
        for (int i = 0; i < k; i++) free(paths[i]);
        free(paths);
        return 0;
    }

    case WM_SIZING:
        on_sizing(wp, (RECT *)lp);
        return TRUE;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = dpi_scale(160);
        mm->ptMinTrackSize.y = dpi_scale(90);
        return 0;
    }

    case WM_DPICHANGED: {
        RECT *r = (RECT *)lp;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        make_fonts();
        return 0;
    }

    case WM_TIMER:
        if (wp == TIMER_OSD) {
            KillTimer(h, TIMER_OSD);
            g_osd[0] = 0;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;

    case WM_APP_VIDEOSIZE:
        if (wp && lp) {
            double a = (double)wp / (double)lp;
            if (a < 0.98 * g_aspect || a > 1.02 * g_aspect) {
                g_aspect = a;
                fit_to_video();
            }
        }
        return 0;

    case WM_APP_FRAME:
        player_on_frame_painted();
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_APP_END:
        player_on_end((BOOL)wp);
        update_title();
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_APP_PLAYING: {
        player_on_playing();
        update_title();
        const wchar_t *n = player_current_name();
        if (n) osd(L"%s", n);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        on_paint(h);
        return 0;

    case WM_DESTROY: {
        WINDOWPLACEMENT wpl = { sizeof wpl };
        if (GetWindowPlacement(h, &wpl)) g_set.rect = wpl.rcNormalPosition;
        g_set.loop = player_get_loop();
        g_set.shuffle = player_get_shuffle();
        g_set.volume = player_get_volume();
        settings_save(&g_set);
        for (int i = HK_CLICKTHRU; i <= HK_QUIT; i++) UnregisterHotKey(h, i);
        player_shutdown();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* -------------------------------------------------------------------- main -- */
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    srand(GetTickCount());

    settings_load(&g_set);

    WNDCLASSEXW wc = { sizeof wc };
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = APP_NAME;
    RegisterClassExW(&wc);

    RECT r = g_set.rect;
    if (IsRectEmpty(&r)) default_rect(&r);

    g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_ACCEPTFILES,
                             APP_NAME, APP_NAME, WS_POPUP,
                             r.left, r.top, r.right - r.left, r.bottom - r.top,
                             NULL, NULL, inst, NULL);
    if (!g_hwnd) return 1;
    if (r.bottom - r.top > 0) g_aspect = (double)(r.right - r.left) / (r.bottom - r.top);
    make_fonts();
    apply_opacity();

    if (!player_init(g_hwnd)) {
        MessageBoxW(NULL, L"Failed to initialise libVLC.\n"
                          L"Make sure libvlc.dll, libvlccore.dll and the plugins folder "
                          L"are next to GhostReel.exe.", APP_NAME, MB_ICONERROR);
        return 1;
    }
    player_set_volume(g_set.volume);
    player_set_loop(g_set.loop);
    player_set_shuffle(g_set.shuffle);

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    register_hotkeys(g_hwnd);

    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) {
        player_add_paths((const wchar_t *const *)(argv + 1), argc - 1, TRUE);
        remember_dir(argv[1], PathIsDirectoryW(argv[1]));
    }
    if (argv) LocalFree(argv);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    CoUninitialize();
    return (int)m.wParam;
}
