#include "common/stdafx.h"
#include "common/scummsys.h"
#include "common/system.h"
#include "common/util.h"
#include "backends/intern.h"

extern "C" {
    #include "gui.h"
    #include "archaos.h"
    #include "syscall.h"
    void sleep(uint32_t ms);
    uint32_t time_ticks(void);
}

class OSystem_ArchaOS : public OSystem {
private:
    int _win;
    int _w, _h;
    uint32_t _palette[256];
    uint32_t *_framebuffer32;
    uint8_t *_framebuffer8;
    int _shake_pos;
    bool _palette_changed;
    uint32_t _msec_start;
    TimerProc _timer_callback;
    int _timer_interval;
    uint32_t _timer_next_tick;
    bool _mouse_visible;
    char _caption[128];

public:
    OSystem_ArchaOS();
    virtual ~OSystem_ArchaOS();

    // Graphics
    virtual void init_size(uint w, uint h);
    virtual int16 get_height() { return (int16)_h; }
    virtual int16 get_width() { return (int16)_w; }
    virtual void set_palette(const byte *colors, uint start, uint num);
    virtual void copy_rect(const byte *buf, int pitch, int x, int y, int w, int h);
    virtual void move_screen(int dx, int dy, int height);
    virtual void update_screen();
    virtual void set_shake_pos(int shakeOffset);

    // Mouse
    virtual bool show_mouse(bool visible);
    virtual void warp_mouse(int x, int y);
    virtual void set_mouse_cursor(const byte *buf, uint w, uint h, int hotspot_x, int hotspot_y);

    // Events and Time
    virtual uint32 get_msecs();
    virtual void delay_msecs(uint msecs);
    virtual void set_timer(TimerProc callback, int interval);
    virtual bool poll_event(Event *event);

    // Sound
    SoundProc _sound_proc;
    void *_sound_param;
    uint32_t _sound_pump_next;
    static const int SOUND_BUF_SAMPLES = 512; // stereo 16-bit: 512 frames = 2048 bytes
    int16_t _sound_buf[SOUND_BUF_SAMPLES * 2]; // stereo

    virtual bool set_sound_proc(SoundProc proc, void *param, SoundFormat format);
    virtual void clear_sound_proc();

    // Audio CD
    virtual bool poll_cdrom() { return false; }
    virtual void play_cdrom(int track, int num_loops, int start_frame, int duration) { (void)track; (void)num_loops; (void)start_frame; (void)duration; }
    virtual void stop_cdrom() {}
    virtual void update_cdrom() {}

    // Mutex
    virtual MutexRef create_mutex(void) { return (MutexRef)1; }
    virtual void lock_mutex(MutexRef mutex) { (void)mutex; }
    virtual void unlock_mutex(MutexRef mutex) { (void)mutex; }
    virtual void delete_mutex(MutexRef mutex) { (void)mutex; }

    // Overlay
    virtual void show_overlay() {}
    virtual void hide_overlay() {}
    virtual void clear_overlay() {}
    virtual void grab_overlay(NewGuiColor *buf, int pitch) { (void)buf; (void)pitch; }
    virtual void copy_rect_overlay(const NewGuiColor *buf, int pitch, int x, int y, int w, int h) { (void)buf; (void)pitch; (void)x; (void)y; (void)w; (void)h; }

    // Misc
    virtual uint32 property(int param, Property *value) {
        switch (param) {
        case PROP_GET_SAMPLE_RATE:
            return 22050;
        case PROP_SET_WINDOW_CAPTION:
            if (value && value->caption) {
                strncpy(_caption, value->caption, sizeof(_caption) - 1);
                _caption[sizeof(_caption) - 1] = '\0';
            }
            return 1;
        case PROP_GET_FULLSCREEN:
            return 0;
        case PROP_HAS_SCALER:
            return 0;
        default:
            return 0;
        }
    }
    virtual void quit() { exit(0); }
};

OSystem_ArchaOS::OSystem_ArchaOS()
    : _win(-1), _w(320), _h(200), _framebuffer32(NULL), _framebuffer8(NULL),
      _shake_pos(0), _palette_changed(false), _timer_callback(NULL),
      _timer_interval(0), _timer_next_tick(0), _mouse_visible(true),
      _sound_proc(NULL), _sound_param(NULL), _sound_pump_next(0)
{
    _caption[0] = '\0';
    _msec_start = time_ticks() * 10;
    for (int i = 0; i < 256; i++) {
        _palette[i] = 0xFF000000 | (i << 16) | (i << 8) | i;
    }
    memset(_sound_buf, 0, sizeof(_sound_buf));
}

OSystem_ArchaOS::~OSystem_ArchaOS() {
    if (_win >= 0) {
        gui_close_window(_win);
        _win = -1;
    }
    if (_framebuffer32) {
        free(_framebuffer32);
        _framebuffer32 = NULL;
    }
    if (_framebuffer8) {
        free(_framebuffer8);
        _framebuffer8 = NULL;
    }
}

void OSystem_ArchaOS::init_size(uint w, uint h) {
    _w = (int)w;
    _h = (int)h;

    if (_framebuffer8) free(_framebuffer8);
    if (_framebuffer32) free(_framebuffer32);

    _framebuffer8 = (uint8_t *)malloc(_w * _h);
    _framebuffer32 = (uint32_t *)malloc(_w * _h * sizeof(uint32_t));
    if (_framebuffer8) memset(_framebuffer8, 0, _w * _h);
    if (_framebuffer32) memset(_framebuffer32, 0, _w * _h * sizeof(uint32_t));

    if (_win < 0) {
        const char *title = _caption[0] ? _caption : "SCUMM Adventure";
        printf("[ScummVM-ArchaOS] Creating window '%s' %dx%d\n", title, _w, _h);
        _win = gui_create_window(title, _w, _h);
        printf("[ScummVM-ArchaOS] Created window ID %d\n", _win);
        gui_set_cursor_mode(_win, GUI_CURSOR_NORMAL);
    }
}

void OSystem_ArchaOS::set_palette(const byte *colors, uint start, uint num) {
    for (uint i = 0; i < num && (start + i) < 256; i++) {
        uint8_t r = colors[i * 4 + 0];
        uint8_t g = colors[i * 4 + 1];
        uint8_t b = colors[i * 4 + 2];
        _palette[start + i] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
    _palette_changed = true;
}

void OSystem_ArchaOS::copy_rect(const byte *buf, int pitch, int x, int y, int w, int h) {
    if (!_framebuffer8) return;

    for (int row = 0; row < h; row++) {
        int dst_y = y + row;
        if (dst_y >= 0 && dst_y < _h) {
            int copy_w = (x + w > _w) ? (_w - x) : w;
            if (copy_w > 0 && x >= 0) {
                memcpy(&_framebuffer8[dst_y * _w + x], &buf[row * pitch], copy_w);
            }
        }
    }
}

void OSystem_ArchaOS::move_screen(int dx, int dy, int height) {
    (void)dx;
    if (!_framebuffer8 || height <= 0 || height > _h) return;

    if (dy > 0 && dy < height) {
        memmove(&_framebuffer8[dy * _w], &_framebuffer8[0], (height - dy) * _w);
        memset(&_framebuffer8[0], 0, dy * _w);
    } else if (dy < 0 && -dy < height) {
        memmove(&_framebuffer8[0], &_framebuffer8[(-dy) * _w], (height + dy) * _w);
        memset(&_framebuffer8[(height + dy) * _w], 0, (-dy) * _w);
    }
}

void OSystem_ArchaOS::update_screen() {
    if (_win < 0 || !_framebuffer32 || !_framebuffer8) return;

    int total = _w * _h;
    if (_shake_pos > 0 && _shake_pos < _h) {
        int offset = _shake_pos * _w;
        for (int i = 0; i < total - offset; i++) {
            _framebuffer32[i] = _palette[_framebuffer8[i + offset]];
        }
        for (int i = total - offset; i < total; i++) {
            _framebuffer32[i] = 0xFF000000;
        }
    } else {
        for (int i = 0; i < total; i++) {
            _framebuffer32[i] = _palette[_framebuffer8[i]];
        }
    }

    gui_draw_buffer32(_win, _framebuffer32, _w, _h);
    gui_update(_win);
    static int frame = 0;
    if ((++frame % 10) == 1) printf("[ScummVM] Frame %d rendered\n", frame);
}

void OSystem_ArchaOS::set_shake_pos(int shakeOffset) {
    _shake_pos = shakeOffset;
}

bool OSystem_ArchaOS::show_mouse(bool visible) {
    _mouse_visible = visible;
    if (_win >= 0) {
        gui_set_cursor_mode(_win, visible ? GUI_CURSOR_NORMAL : GUI_CURSOR_HIDDEN);
    }
    return true;
}

void OSystem_ArchaOS::warp_mouse(int x, int y) {
    (void)x; (void)y;
}

void OSystem_ArchaOS::set_mouse_cursor(const byte *buf, uint w, uint h, int hotspot_x, int hotspot_y) {
    (void)buf; (void)w; (void)h; (void)hotspot_x; (void)hotspot_y;
}

uint32 OSystem_ArchaOS::get_msecs() {
    return (uint32)time_ticks();
}

void OSystem_ArchaOS::delay_msecs(uint msecs) {
    if (msecs <= 10) {
        yield();
    } else {
        sleep(msecs);
    }
}

void OSystem_ArchaOS::set_timer(TimerProc callback, int interval) {
    _timer_callback = callback;
    _timer_interval = interval;
    _timer_next_tick = get_msecs() + interval;
}

bool OSystem_ArchaOS::poll_event(Event *event) {
    uint32 now = get_msecs();
    if (_timer_callback && now >= _timer_next_tick) {
        int next_int = _timer_callback(_timer_interval);
        _timer_interval = next_int ? next_int : _timer_interval;
        _timer_next_tick = now + _timer_interval;
    }

    // Pump the sound/MIDI engine every ~10ms.
    // generate_samples() fires _timer_proc -> IMuseInternal::midiTimerCallback
    // which advances MIDI players and unblocks IMuse::startSound.
    if (_sound_proc && now >= _sound_pump_next) {
        _sound_proc(_sound_param, (byte *)_sound_buf, SOUND_BUF_SAMPLES * 2 * sizeof(int16_t));
        sys_call5(SYS_AUDIO_WRITE, (uint32_t)_sound_buf, SOUND_BUF_SAMPLES * 2 * sizeof(int16_t), 2, 22050, 16);
        _sound_pump_next = now + 10;
    }

    if (_win < 0) return false;

    gui_event_t ev;
    if (gui_get_raw_event(_win, &ev)) {
        if (ev.type == GUI_EVENT_MOUSE_DOWN) {
            event->event_code = (ev.button == 2) ? EVENT_RBUTTONDOWN : EVENT_LBUTTONDOWN;
            event->mouse.x = ev.x;
            event->mouse.y = ev.y;
            return true;
        } else if (ev.type == GUI_EVENT_MOUSE_UP) {
            event->event_code = (ev.button == 2) ? EVENT_RBUTTONUP : EVENT_LBUTTONUP;
            event->mouse.x = ev.x;
            event->mouse.y = ev.y;
            return true;
        } else if (ev.type == GUI_EVENT_MOUSE_MOVE) {
            event->event_code = EVENT_MOUSEMOVE;
            event->mouse.x = ev.x;
            event->mouse.y = ev.y;
            return true;
        } else if (ev.type == GUI_EVENT_KEY_DOWN) {
            event->event_code = EVENT_KEYDOWN;
            int keycode = 0;
            if (ev.scancode == GUI_KEY_ESCAPE) keycode = 27;
            else if (ev.scancode == GUI_KEY_ENTER) keycode = 13;
            else if (ev.scancode == GUI_KEY_SPACE) keycode = 32;
            else if (ev.scancode == GUI_KEY_F5) keycode = 319;
            else keycode = ev.key ? (int)ev.key : (int)ev.scancode;
            event->kbd.keycode = keycode;
            event->kbd.ascii = ev.key ? (uint16)ev.key : (uint16)keycode;
            event->kbd.flags = 0;
            return true;
        } else if (ev.type == GUI_EVENT_KEY_UP) {
            event->event_code = EVENT_KEYUP;
            event->kbd.keycode = (int)ev.scancode;
            event->kbd.ascii = (uint16)ev.key;
            event->kbd.flags = 0;
            return true;
        } else if (ev.type == GUI_EVENT_CLOSE) {
            event->event_code = EVENT_QUIT;
            return true;
        }
    }
    return false;
}

bool OSystem_ArchaOS::set_sound_proc(SoundProc proc, void *param, SoundFormat format) {
    (void)format;
    _sound_proc = proc;
    _sound_param = param;
    _sound_pump_next = get_msecs(); // pump immediately on next poll_event
    return true;
}

void OSystem_ArchaOS::clear_sound_proc() {
    _sound_proc = NULL;
    _sound_param = NULL;
}

OSystem *OSystem_ArchaOS_create() {
    return new OSystem_ArchaOS();
}
