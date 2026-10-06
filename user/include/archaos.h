#ifndef _ARCHAOS_H
#define _ARCHAOS_H

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"
#include "gui.h"

int      spawn(const char *path);
uint32_t time_ticks(void);

/*
 * Desktop Integration Rule
 * ------------------------
 * To make your application automatically show up on the ArchaOS Desktop,
 * declare your application at file scope (outside any function) with:
 *
 *     ARCHAOS_GUI_APP("My App Title");
 *     // or: ARCHAOS_APP_TITLE("My App Title");
 *
 * The ArchaOS desktop compositor scans compiled binaries in /bin/ for this
 * marker and automatically loads your application onto the Desktop with your
 * specified title. Zero kernel changes required!
 *
 * To customize your 7x7 window icon and author attribution at runtime:
 *     gui_set_icon(win, icon_7x7, GUI_COLOR_CYAN);
 *     gui_set_author(win, "Developer Name");
 */

#ifndef ARCHAOS_GUI_APP
#define ARCHAOS_GUI_APP(name) \
    const char __archaos_gui_app_meta[] __attribute__((used, section(".rodata"))) = "ARCHAOS_GUI:" name
#endif

#ifndef ARCHAOS_APP_TITLE
#define ARCHAOS_APP_TITLE(name) ARCHAOS_GUI_APP(name)
#endif

#ifndef ARCHAOS_APP_AUTHOR
#define ARCHAOS_APP_AUTHOR(author) \
    const char __archaos_app_author_meta[] __attribute__((used, section(".rodata"))) = "ARCHAOS_AUTHOR:" author
#endif

#endif /* _ARCHAOS_H */
