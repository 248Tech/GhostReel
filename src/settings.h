#ifndef GHOSTREEL_SETTINGS_H
#define GHOSTREEL_SETTINGS_H

#include <windows.h>

typedef struct {
    RECT    rect;        /* window rect; empty = use default placement */
    int     opacity;     /* 10..100 percent */
    int     volume;      /* 0..150 */
    BOOL    loop;
    BOOL    shuffle;
    wchar_t last_dir[MAX_PATH];
} settings_t;

void settings_load(settings_t *s);
void settings_save(const settings_t *s);

#endif
