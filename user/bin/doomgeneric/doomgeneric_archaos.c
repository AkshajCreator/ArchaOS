// doomgeneric port for ArchaOS
#include <archaos.h>
#include <gui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"

ARCHAOS_GUI_APP("DOOM");
ARCHAOS_APP_AUTHOR("id Software");

static int s_doom_win = -1;

#define KEYQUEUE_SIZE 128
static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWrite = 0;
static unsigned int s_KeyQueueRead = 0;

static void queue_key(int pressed, unsigned char doom_key)
{
    if (!doom_key) return;
    unsigned int next = (s_KeyQueueWrite + 1) % KEYQUEUE_SIZE;
    if (next != s_KeyQueueRead) {
        s_KeyQueue[s_KeyQueueWrite] = (pressed ? 0x0100 : 0x0000) | doom_key;
        s_KeyQueueWrite = next;
    }
}

static unsigned char map_scancode_to_doom(uint8_t sc, char c)
{
    switch (sc) {
        /* Directional Arrows */
        case 0x48: return KEY_UPARROW;
        case 0x50: return KEY_DOWNARROW;
        case 0x4B: return KEY_LEFTARROW;
        case 0x4D: return KEY_RIGHTARROW;

        /* WASD Movement (Deterministic Press & Release) */
        case 0x11: return KEY_UPARROW;     /* W */
        case 0x1F: return KEY_DOWNARROW;   /* S */
        case 0x1E: return KEY_LEFTARROW;   /* A */
        case 0x20: return KEY_RIGHTARROW;  /* D */

        /* Core Actions */
        case 0x1D: return KEY_FIRE;        /* Left Ctrl: Fire */
        case 0x39: return KEY_USE;         /* Space: Open / Use */
        case 0x1C: return KEY_ENTER;       /* Enter */
        case 0x01: return KEY_ESCAPE;      /* Esc: Menu */
        case 0x0F: return KEY_TAB;         /* Tab: Map */
        case 0x2A:                         /* Left Shift: Run */
        case 0x36: return KEY_RSHIFT;      /* Right Shift: Run */
        case 0x38: return KEY_RALT;        /* Alt: Strafe */
        case 0x0E: return KEY_BACKSPACE;

        /* Number keys 1-7: Weapons */
        case 0x02: return '1';
        case 0x03: return '2';
        case 0x04: return '3';
        case 0x05: return '4';
        case 0x06: return '5';
        case 0x07: return '6';
        case 0x08: return '7';
        case 0x09: return '8';
        case 0x0A: return '9';
        case 0x0B: return '0';
        case 0x0C: return KEY_MINUS;
        case 0x0D: return KEY_EQUALS;

        /* Strafe keys (, and .) */
        case 0x33: return ',';
        case 0x34: return '.';

        /* Function keys F1-F12 */
        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;
        case 0x3E: return KEY_F4;
        case 0x3F: return KEY_F5;
        case 0x40: return KEY_F6;
        case 0x41: return KEY_F7;
        case 0x42: return KEY_F8;
        case 0x43: return KEY_F9;
        case 0x44: return KEY_F10;
        case 0x57: return KEY_F11;
        case 0x58: return KEY_F12;

        /* Confirmation keys */
        case 0x15: return 'y';             /* Y */
        case 0x31: return 'n';             /* N */

        default: break;
    }

    if (c >= 'A' && c <= 'Z') return (unsigned char)(c + 32);
    if (c >= ' ' && c <= '~') return (unsigned char)c;

    return 0;
}

#define DOOM_WIN_W 240
#define DOOM_WIN_H 150

void DG_Init()
{
    s_doom_win = gui_create_window("DOOM", DOOM_WIN_W, DOOM_WIN_H);
    if (s_doom_win >= 0) {
        static const uint8_t doom_icon[7][7] = {
            {1, 0, 0, 0, 0, 0, 1},
            {1, 1, 0, 0, 0, 1, 1},
            {0, 1, 1, 1, 1, 1, 0},
            {0, 1, 0, 1, 0, 1, 0},
            {0, 1, 1, 1, 1, 1, 0},
            {0, 0, 1, 1, 1, 0, 0},
            {0, 1, 0, 0, 0, 1, 0}
        };
        gui_set_icon(s_doom_win, doom_icon, GUI_COLOR_LIGHT_RED);
        gui_set_author(s_doom_win, "id Software / ArchaOS");
    }
}

void DG_DrawFrame()
{
    if (s_doom_win >= 0 && DG_ScreenBuffer) {
        gui_draw_buffer32(s_doom_win, DG_ScreenBuffer, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
        gui_update(s_doom_win);
    }

    /* Process pending events */
    if (s_doom_win >= 0) {
        gui_event_t ev;
        while (gui_get_raw_event(s_doom_win, &ev)) {
            if (ev.type == GUI_EVENT_CLOSE) {
                exit(0);
            }
            if (ev.type == GUI_EVENT_KEY_DOWN || ev.type == GUI_EVENT_KEY_UP) {
                int pressed = (ev.type == GUI_EVENT_KEY_DOWN);
                unsigned char dk = map_scancode_to_doom(ev.scancode, ev.key);
                queue_key(pressed, dk);
            }
        }
    }
}

void DG_SleepMs(uint32_t ms)
{
    sleep(ms);
}

uint32_t DG_GetTicksMs()
{
    return time_ticks();
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    if (s_KeyQueueRead == s_KeyQueueWrite) {
        return 0;
    }
    unsigned short data = s_KeyQueue[s_KeyQueueRead];
    s_KeyQueueRead = (s_KeyQueueRead + 1) % KEYQUEUE_SIZE;
    *pressed = (data >> 8) & 1;
    *doomKey = (unsigned char)(data & 0xFF);
    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    if (s_doom_win >= 0 && title) {
        gui_set_title(s_doom_win, title);
    }
}

int main(int argc, char **argv)
{
    printf("Starting DOOM for ArchaOS...\n");
    doomgeneric_Create(argc, argv);

    while (1) {
        doomgeneric_Tick();
    }

    return 0;
}
