// SPDX-License-Identifier: GPL-3.0-only
// Infrared remote for Epson projectors on Flipper Zero.
// Tested target: Epson EB-U05 3LCD. Uses the standard Epson NECext command
// set (address 0x5583). Command bytes were cross-checked against the Flipper
// firmware projector.ir library and the official LIRC Epson configs
// (Epson_12807990 and Epson_ELPST12); Power 0x90 was confirmed on real Epson
// hardware. UI structure follows flipper-google-tv-remote (GPL-3.0-only).

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <infrared_transmit.h>
#include <input/input.h>

#define TAG "EpsonProjector"

/* Standard Epson remote, NEC extended. Address bytes 83 55 -> 0x5583.
 * The NECext command field carries the command byte and its inverse:
 * command = cmd | (~cmd << 8), e.g. Power 0x90 -> 0x6F90. */
#define EPSON_ADDR 0x5583
#define CMD(c)     ((uint16_t)((c) | ((uint8_t) ~(c) << 8)))

#define MENU_ROWS    4
#define HIGHLIGHT_MS 160

typedef enum {
    ActPower,
    ActSource,
    ActMenu,
    ActAVMute,
    ActVolUp,
    ActVolDown,
    ActVideo,
    ActColorMode,
    ActAspect,
    ActAuto,
    ActClose,
    ActCount,
} Action;

typedef struct {
    const char* label;
    uint8_t cmd; /* Epson command byte; 0 means no IR (Close). */
} ActionDef;

static const ActionDef actions[ActCount] = {
    [ActPower] = {"Power", 0x90},
    [ActSource] = {"Source search", 0x8C},
    [ActMenu] = {"Menu", 0x9A},
    [ActAVMute] = {"A/V Mute (blank)", 0x93},
    [ActVolUp] = {"Volume +", 0x98},
    [ActVolDown] = {"Volume -", 0x99},
    [ActVideo] = {"Video source", 0x9C},
    [ActColorMode] = {"Color mode", 0x8F},
    [ActAspect] = {"Aspect", 0x8A},
    [ActAuto] = {"Auto adjust", 0x9E},
    [ActClose] = {"Back to remote", 0x00},
};

/* Main-screen direct keys. */
#define CMD_UP    0xB0
#define CMD_DOWN  0xB2
#define CMD_LEFT  0xB3
#define CMD_RIGHT 0xB1
#define CMD_ENTER 0x85
#define CMD_ESC   0x84

typedef struct {
    bool menu;
    uint8_t action;
    bool highlight;
    InputKey last_key;
    const char* toast;
} Screen;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input;
    FuriMutex* lock;
    Screen screen;
    uint32_t highlight_until;
    uint32_t toast_until;
} App;

static void draw_arrow(Canvas* canvas, int cx, int cy, InputKey key) {
    int dx = 0, dy = 0;
    switch(key) {
    case InputKeyUp:
        dy = -1;
        break;
    case InputKeyDown:
        dy = 1;
        break;
    case InputKeyLeft:
        dx = -1;
        break;
    case InputKeyRight:
        dx = 1;
        break;
    default:
        canvas_draw_str_aligned(canvas, cx, cy, AlignCenter, AlignCenter, "OK");
        return;
    }
    const int tx = cx + 3 * dx, ty = cy + 3 * dy;
    canvas_draw_line(canvas, cx - 3 * dx, cy - 3 * dy, tx, ty);
    canvas_draw_line(canvas, tx, ty, tx - 3 * dx - 3 * dy, ty - 3 * dy - 3 * dx);
    canvas_draw_line(canvas, tx, ty, tx - 3 * dx + 3 * dy, ty - 3 * dy + 3 * dx);
}

static void draw_button(Canvas* canvas, int x, int y, InputKey key, bool selected) {
    if(selected) {
        canvas_draw_rbox(canvas, x, y, 20, 12, 2);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, x, y, 20, 12, 2);
    }
    draw_arrow(canvas, x + 10, y + 6, key);
    canvas_set_color(canvas, ColorBlack);
}

static void draw(Canvas* canvas, void* ctx) {
    App* app = ctx;
    furi_mutex_acquire(app->lock, FuriWaitForever);
    Screen s = app->screen;
    furi_mutex_release(app->lock);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Epson Projector");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, "IR");
    canvas_draw_line(canvas, 0, 13, 127, 13);

    if(s.menu) {
        uint8_t first = 0;
        if(s.action >= MENU_ROWS) first = s.action - MENU_ROWS + 1;
        for(uint8_t row = 0; row < MENU_ROWS; row++) {
            const uint8_t i = first + row;
            const int y = 16 + row * 10;
            if(s.action == i) {
                canvas_draw_box(canvas, 1, y - 1, 122, 10);
                canvas_set_color(canvas, ColorWhite);
            }
            canvas_draw_str(canvas, 5, y + 7, actions[i].label);
            canvas_set_color(canvas, ColorBlack);
        }
        const int bar = 40 * MENU_ROWS / ActCount;
        const int top = 15 + (40 - bar) * first / (ActCount - MENU_ROWS);
        canvas_draw_line(canvas, 126, 15, 126, 54);
        canvas_draw_box(canvas, 125, top, 3, bar);
    } else {
        const bool hl = s.highlight;
        draw_button(canvas, 23, 16, InputKeyUp, hl && s.last_key == InputKeyUp);
        draw_button(canvas, 2, 29, InputKeyLeft, hl && s.last_key == InputKeyLeft);
        draw_button(canvas, 23, 29, InputKeyOk, hl && s.last_key == InputKeyOk);
        draw_button(canvas, 44, 29, InputKeyRight, hl && s.last_key == InputKeyRight);
        draw_button(canvas, 23, 42, InputKeyDown, hl && s.last_key == InputKeyDown);
        canvas_draw_str(canvas, 69, 24, "OK: Enter");
        canvas_draw_str(canvas, 69, 34, "Back: Esc");
        canvas_draw_str(canvas, 69, 45, "Hold OK:");
        canvas_draw_str(canvas, 69, 54, "Power...");
    }

    const char* footer = s.toast ? s.toast :
                         s.menu  ? "OK: send   Back: close" :
                                   "Hold OK: menu  Hold Back: exit";
    canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, footer);
}

static void input_cb(InputEvent* event, void* ctx) {
    App* app = ctx;
    if(event->type != InputTypeRelease) furi_message_queue_put(app->input, event, 0);
}

static void toast(App* app, const char* text) {
    furi_mutex_acquire(app->lock, FuriWaitForever);
    app->screen.toast = text;
    app->toast_until = furi_get_tick() + furi_ms_to_ticks(900);
    furi_mutex_release(app->lock);
}

static void send_cmd(App* app, uint8_t cmd, InputKey key) {
    const InfraredMessage msg = {
        .protocol = InfraredProtocolNECext,
        .address = EPSON_ADDR,
        .command = CMD(cmd),
        .repeat = false,
    };
    infrared_send(&msg, 2);
    furi_mutex_acquire(app->lock, FuriWaitForever);
    app->screen.highlight = true;
    app->screen.last_key = key;
    app->highlight_until = furi_get_tick() + furi_ms_to_ticks(HIGHLIGHT_MS);
    furi_mutex_release(app->lock);
}

static void set_menu(App* app, bool open) {
    furi_mutex_acquire(app->lock, FuriWaitForever);
    app->screen.menu = open;
    if(open) app->screen.action = 0;
    furi_mutex_release(app->lock);
}

static bool handle_menu(App* app, const InputEvent* e, uint8_t action) {
    const bool step = e->type == InputTypePress || e->type == InputTypeRepeat;
    if((e->key == InputKeyUp || e->key == InputKeyDown) && step) {
        furi_mutex_acquire(app->lock, FuriWaitForever);
        app->screen.action = (action + (e->key == InputKeyDown ? 1 : ActCount - 1)) % ActCount;
        furi_mutex_release(app->lock);
    } else if(e->key == InputKeyBack && e->type == InputTypeShort) {
        set_menu(app, false);
    } else if(e->key == InputKeyOk && e->type == InputTypeShort) {
        if(action == ActClose) {
            set_menu(app, false);
        } else {
            send_cmd(app, actions[action].cmd, InputKeyMAX);
            toast(app, "Sent");
        }
    }
    return true;
}

static bool handle_input(App* app, const InputEvent* e) {
    if(e->key == InputKeyBack && e->type == InputTypeLong) return false;

    furi_mutex_acquire(app->lock, FuriWaitForever);
    const bool menu = app->screen.menu;
    const uint8_t action = app->screen.action;
    furi_mutex_release(app->lock);
    if(menu) return handle_menu(app, e, action);

    if(e->key == InputKeyOk && e->type == InputTypeLong) {
        set_menu(app, true);
        return true;
    }

    const bool step = e->type == InputTypePress || e->type == InputTypeRepeat;
    uint8_t cmd = 0;
    if(step) {
        switch(e->key) {
        case InputKeyUp:
            cmd = CMD_UP;
            break;
        case InputKeyDown:
            cmd = CMD_DOWN;
            break;
        case InputKeyLeft:
            cmd = CMD_LEFT;
            break;
        case InputKeyRight:
            cmd = CMD_RIGHT;
            break;
        default:
            break;
        }
    } else if(e->type == InputTypeShort) {
        if(e->key == InputKeyOk)
            cmd = CMD_ENTER;
        else if(e->key == InputKeyBack)
            cmd = CMD_ESC;
    }
    if(cmd) send_cmd(app, cmd, e->key);
    return true;
}

static void expire(App* app) {
    const uint32_t now = furi_get_tick();
    furi_mutex_acquire(app->lock, FuriWaitForever);
    if(app->screen.highlight && (int32_t)(now - app->highlight_until) >= 0)
        app->screen.highlight = false;
    if(app->screen.toast && (int32_t)(now - app->toast_until) >= 0) app->screen.toast = NULL;
    furi_mutex_release(app->lock);
}

int32_t epson_projector_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->input = furi_message_queue_alloc(16, sizeof(InputEvent));
    app->lock = furi_mutex_alloc(FuriMutexTypeNormal);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw, app);
    view_port_input_callback_set(app->view_port, input_cb, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(app->input, &event, furi_ms_to_ticks(50)) == FuriStatusOk) {
            running = handle_input(app, &event);
        }
        expire(app);
        view_port_update(app->view_port);
    }

    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->input);
    furi_mutex_free(app->lock);
    free(app);
    return 0;
}
