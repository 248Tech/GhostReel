/* INI settings in %APPDATA%\GhostReel\settings.ini */
#include "settings.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>

#define SEC L"overlay"

static BOOL ini_path(wchar_t *out, BOOL create_dir)
{
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, out))) return FALSE;
    PathAppendW(out, L"GhostReel");
    if (create_dir) CreateDirectoryW(out, NULL);
    PathAppendW(out, L"settings.ini");
    return TRUE;
}

static int get_int(const wchar_t *ini, const wchar_t *key, int def)
{
    return (int)GetPrivateProfileIntW(SEC, key, def, ini);
}

static void put_int(const wchar_t *ini, const wchar_t *key, int v)
{
    wchar_t buf[32];
    _snwprintf(buf, 32, L"%d", v);
    WritePrivateProfileStringW(SEC, key, buf, ini);
}

void settings_load(settings_t *s)
{
    ZeroMemory(s, sizeof *s);
    s->opacity = 100;
    s->volume = 80;
    s->loop = TRUE;

    wchar_t ini[MAX_PATH];
    if (!ini_path(ini, FALSE) || !PathFileExistsW(ini)) return;

    /* GetPrivateProfileInt is unsigned; negative coords stored as text */
    wchar_t buf[64];
    GetPrivateProfileStringW(SEC, L"rect", L"", buf, 64, ini);
    RECT r;
    if (swscanf(buf, L"%ld,%ld,%ld,%ld", &r.left, &r.top, &r.right, &r.bottom) == 4 &&
        r.right - r.left >= 64 && r.bottom - r.top >= 36) {
        /* only restore if still on some monitor */
        if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) s->rect = r;
    }
    s->opacity = get_int(ini, L"opacity", 100);
    if (s->opacity < 10 || s->opacity > 100) s->opacity = 100;
    s->volume = get_int(ini, L"volume", 80);
    if (s->volume > 150) s->volume = 80;
    s->loop = get_int(ini, L"loop", 1) != 0;
    s->shuffle = get_int(ini, L"shuffle", 0) != 0;
    GetPrivateProfileStringW(SEC, L"last_dir", L"", s->last_dir, MAX_PATH, ini);
}

void settings_save(const settings_t *s)
{
    wchar_t ini[MAX_PATH];
    if (!ini_path(ini, TRUE)) return;
    wchar_t buf[64];
    _snwprintf(buf, 64, L"%ld,%ld,%ld,%ld", s->rect.left, s->rect.top,
               s->rect.right, s->rect.bottom);
    WritePrivateProfileStringW(SEC, L"rect", buf, ini);
    put_int(ini, L"opacity", s->opacity);
    put_int(ini, L"volume", s->volume);
    put_int(ini, L"loop", s->loop);
    put_int(ini, L"shuffle", s->shuffle);
    WritePrivateProfileStringW(SEC, L"last_dir", s->last_dir, ini);
}
