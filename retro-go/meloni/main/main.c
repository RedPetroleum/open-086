// Meloni on retro-go: runs a Lua game (.mlg from roms/meloni/) with the engine in components/meloni,
// which build_retro_go.sh copies in from meloni-games (engine/, pinned by MELONI_COMMIT).
#include <rg_system.h>
#include <esp_heap_caps.h>
#include <multi_heap.h>
#include <stdio.h>
#include <string.h>

#include "meloni.h"

#define FRAME_US (1000000 / MEL_FPS)
#define FRAME_BYTES (MEL_WIDTH * MEL_HEIGHT * 2)
// With frameskip at least every (MAX_SKIP + 1)th update is drawn when a frame can be handed over.
// If even that is too slow, the game slows down rather than showing hardly anything.
#define MAX_SKIP 4
#define LUA_ARENA (3 * 1024 * 1024)

static rg_app_t *app;
static char save_path[RG_PATH_MAX];

// Finished frames are copied from the engine framebuffer (internal RAM) into one of two surfaces in
// PSRAM, which the display task sends out. The display takes one frame at a time and needs ~30 to
// 60 ms for a full one, much longer than drawing.
// Frameskip (default): _update runs 60 times per second. A frame is drawn only when the presenter
// task is idle, i.e. the display has started on the previous frame; the presenter hands the new one
// over the moment the display is done, so drawing and sending overlap and every drawn frame is shown.
// Whether there is time to draw is judged by the main loop's own work (debt below), not by the clock:
// the audio driver blocks and paces the loop, so time lost in a long frame can't be made up anyway.
// Without frameskip (to compare, the behaviour before): every update is drawn, a frame is handed over
// only if the display happens to be free, otherwise it is thrown away.
static bool frameskip = true;
static rg_surface_t screen; // the engine framebuffer as a surface, for screenshots
static rg_surface_t *copies[2];
static int current; // the copy to fill next
static rg_task_t *presenter;
// Work beyond one frame's time still to be made up (us): an update with drawing (~20 ms) usually
// doesn't fit into one frame, then the next update goes without drawing
static int64_t debt;

// The Lua heap is a heap of its own in one block of PSRAM. Lua makes tens of thousands of small
// allocations; in the system heap, retro-go's statistics walk all of them once a second with the
// heap locked and interrupts off, which stalled the game for ~45 ms. Only the main task allocates
// from it, so it needs no lock. When it is full, the system heap takes over.
static multi_heap_handle_t lua_heap;
static uint8_t *lua_arena;

// Frame statistics over one second, drawn over the game with "Show stats"
static bool show_stats;
static char stats_text[512] = "measuring ...";
static struct
{
    int64_t start, update_us, draw_us, other_us;
    int updates, draws, update_max, draw_max;
    rg_display_counters_t display;
} st;

void mel_plat_log(const char *msg)
{
    RG_LOGI("%s", msg);
}

int64_t mel_plat_time_us(void)
{
    return rg_system_timer();
}

static bool in_arena(void *p)
{
    return lua_arena && (uint8_t *)p >= lua_arena && (uint8_t *)p < lua_arena + LUA_ARENA;
}

// The Lua heap goes to PSRAM (8 MB), the internal RAM is kept for the display and audio buffers
void *mel_plat_realloc(void *ptr, size_t size)
{
    if (size == 0)
    {
        if (in_arena(ptr))
            multi_heap_free(lua_heap, ptr);
        else
            heap_caps_free(ptr);
        return NULL;
    }
    if (lua_heap && (!ptr || in_arena(ptr)))
    {
        void *p = multi_heap_realloc(lua_heap, ptr, size);
        if (p)
            return p;
    }
    if (ptr && !in_arena(ptr))
    {
        void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p ? p : heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
    }
    // A new block or one that has to leave the full arena
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p)
        p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (p && ptr)
    {
        size_t old = multi_heap_get_allocated_size(lua_heap, ptr);
        memcpy(p, ptr, old < size ? old : size);
        multi_heap_free(lua_heap, ptr);
    }
    return p;
}

// Hands frames to the display. rg_display_submit() blocks until the display has taken the previous
// frame; the message stays queued until then, so rg_task_messages_waiting() tells whether a frame is
// still waiting. Once submitted, the copy before it is free again (the display is done reading it).
static void presenter_task(void *arg)
{
    rg_task_msg_t msg;
    while (rg_task_peek(&msg))
    {
        if (msg.type == RG_TASK_MSG_STOP)
            break;
        rg_display_submit(msg.dataPtr, 0);
        rg_task_receive(&msg);
    }
}

static bool presenter_idle(void)
{
    return rg_task_messages_waiting(presenter) == 0;
}

// Copies the finished frame and hands it over; only call when presenter_idle()
static void present(bool wait_for_display)
{
    rg_surface_t *s = copies[current];
    memcpy(s->data, mel_framebuffer(), FRAME_BYTES);
    if (wait_for_display)
        rg_task_send(presenter, &(rg_task_msg_t){.dataPtr = s});
    else
        rg_display_submit(s, 0); // the display is free, this doesn't block
    current ^= 1;
}

static void stats_reset(void)
{
    memset(&st, 0, sizeof(st));
    st.start = rg_system_timer();
    st.display = rg_display_get_counters();
}

// "12.3" from microseconds (newlib nano has no float printf)
#define MS_LEN 24
static const char *ms(char *buf, int64_t us)
{
    snprintf(buf, MS_LEN, "%d.%d", (int)(us / 1000), (int)(us / 100 % 10));
    return buf;
}

static void stats_tick(void)
{
    int64_t elapsed = rg_system_timer() - st.start;
    if (elapsed < 1000000)
        return;
    rg_display_counters_t dc = rg_display_get_counters();
    int shown = dc.totalFrames - st.display.totalFrames;
    int64_t display_us = dc.busyTime - st.display.busyTime;
    char a[MS_LEN], b[MS_LEN], c[MS_LEN], d[MS_LEN], e[MS_LEN], f[MS_LEN];
    snprintf(stats_text, sizeof(stats_text),
             "speed %d%%  cpu %d%%  skip %s\n"
             "update %s ms  max %s\n"
             "draw   %s ms  max %s  rest %s\n"
             "drawn %d  shown %d  display %s ms\n"
             "lua %d KB  free %d KB  psram %d KB",
             (int)(st.updates * 100LL * 1000000 / MEL_FPS / elapsed),
             (int)((st.update_us + st.draw_us + st.other_us) * 100 / elapsed), frameskip ? "on" : "off",
             ms(a, st.updates ? st.update_us / st.updates : 0), ms(b, st.update_max),
             ms(c, st.draws ? st.draw_us / st.draws : 0), ms(d, st.draw_max),
             ms(e, st.updates ? st.other_us / st.updates : 0),
             (int)(st.draws * 1000000LL / elapsed), (int)(shown * 1000000LL / elapsed),
             ms(f, shown ? display_us / shown : 0),
             (int)(mel_mem_used() / 1024), (int)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (int)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    stats_reset();
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(copies[current ^ 1], 0); // the newest frame
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(&screen, filename, width, height);
}

static void start_game(void)
{
    if (!mel_init(app->romPath, save_path))
        RG_LOGE("Game failed to start: %s", mel_error());
}

static bool toggled(rg_gui_event_t event)
{
    return event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT || event == RG_DIALOG_ENTER;
}

static rg_gui_event_t frameskip_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (toggled(event))
    {
        frameskip = !frameskip;
        rg_settings_set_number(NS_APP, "Frameskip", frameskip);
    }
    strcpy(option->value, frameskip ? _("On") : _("Off"));
    return RG_DIALOG_VOID;
}

static rg_gui_event_t stats_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (toggled(event))
    {
        show_stats = !show_stats;
        rg_settings_set_number(NS_APP, "ShowStats", show_stats);
    }
    strcpy(option->value, show_stats ? _("On") : _("Off"));
    return RG_DIALOG_VOID;
}

static void game_menu(void)
{
    const rg_gui_option_t choices[] = {
        {1, _("Continue"),   NULL, RG_DIALOG_FLAG_NORMAL, NULL},
        {2, _("Restart"),    NULL, RG_DIALOG_FLAG_NORMAL, NULL},
        {3, _("Options"),    NULL, RG_DIALOG_FLAG_NORMAL, NULL},
        {0, _("Frameskip"),  "-",  RG_DIALOG_FLAG_NORMAL, &frameskip_cb},
        {0, _("Show stats"), "-",  RG_DIALOG_FLAG_NORMAL, &stats_cb},
        {4, _("About"),      NULL, RG_DIALOG_FLAG_NORMAL, NULL},
        {5, _("Quit"),       NULL, RG_DIALOG_FLAG_NORMAL, NULL},
        RG_DIALOG_END,
    };
    rg_audio_set_mute(true);
    int sel = rg_gui_dialog(mel_game_name(), choices, 0);
    rg_audio_set_mute(false);
    rg_settings_commit();
    switch (sel)
    {
    case 2:
        mel_quit();
        start_game();
        break;
    case 3:
        rg_gui_options_menu();
        break;
    case 4:
        rg_gui_about_menu();
        break;
    case 5:
        mel_quit();
        mel_shutdown();
        rg_system_exit();
        break;
    }
    rg_input_wait_for_key(RG_KEY_ANY, false, 500); // don't pass the closing button to the game
}

static uint32_t read_buttons(uint32_t joystick)
{
    uint32_t b = 0;
    if (joystick & RG_KEY_LEFT)   b |= MEL_BTN_LEFT;
    if (joystick & RG_KEY_RIGHT)  b |= MEL_BTN_RIGHT;
    if (joystick & RG_KEY_UP)     b |= MEL_BTN_UP;
    if (joystick & RG_KEY_DOWN)   b |= MEL_BTN_DOWN;
    if (joystick & RG_KEY_A)      b |= MEL_BTN_A;
    if (joystick & RG_KEY_B)      b |= MEL_BTN_B;
    if (joystick & RG_KEY_START)  b |= MEL_BTN_START;
    if (joystick & RG_KEY_SELECT) b |= MEL_BTN_SELECT;
    return b;
}

void app_main(void)
{
    const rg_handlers_t handlers = {
        .screenshot = &screenshot_handler,
        .event = &event_handler,
    };
    app = rg_system_init(MEL_SAMPLE_RATE, &handlers, NULL);
    rg_system_set_tick_rate(MEL_FPS);
    app->frameskip = 0;
    frameskip = rg_settings_get_number(NS_APP, "Frameskip", 1) != 0;
    show_stats = rg_settings_get_number(NS_APP, "ShowStats", 0) != 0;

    screen = (rg_surface_t){
        .width = MEL_WIDTH,
        .height = MEL_HEIGHT,
        .stride = MEL_WIDTH * 2,
        .format = RG_PIXEL_565_LE,
        .data = (void *)mel_framebuffer(),
    };
    // The engine draws into its own framebuffer in internal RAM; the copies for the display go to
    // PSRAM, the ~170 KB of internal heap left are needed by SD card, audio and display
    copies[0] = rg_surface_create(MEL_WIDTH, MEL_HEIGHT, RG_PIXEL_565_LE, MEM_SLOW);
    copies[1] = rg_surface_create(MEL_WIDTH, MEL_HEIGHT, RG_PIXEL_565_LE, MEM_SLOW);
    if (!copies[0] || !copies[1])
        RG_PANIC("Out of memory");
    if ((lua_arena = heap_caps_malloc(LUA_ARENA, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)))
        lua_heap = multi_heap_register(lua_arena, LUA_ARENA);
    if (!lua_heap)
    {
        RG_LOGE("No Lua arena, the Lua heap uses the system heap");
        heap_caps_free(lua_arena);
        lua_arena = NULL;
    }
    // Higher priority than the main task, so a waiting frame goes out as soon as the display is free
    presenter = rg_task_create("meloni_present", &presenter_task, NULL, 3 * 1024, RG_TASK_PRIORITY_5, 0);

    if (!app->romPath || !app->romPath[0])
        RG_PANIC("No game selected");

    // savedata() goes where retro-go keeps SRAM (/retro-go/saves/meloni/<game>.mlg.sram),
    // so the launcher's "Delete save" works for it
    char *sram = rg_emu_get_path(RG_PATH_SAVE_SRAM, app->romPath);
    snprintf(save_path, sizeof(save_path), "%s", sram);
    free(sram);
    rg_storage_mkdir(rg_dirname(save_path));

    start_game();

    static rg_audio_frame_t audio[MEL_SAMPLE_RATE / MEL_FPS + 1];
    int sample_acc = 0, skipped = 0;
    int64_t next_frame = rg_system_timer();
    stats_reset();

    while (true)
    {
        uint32_t joystick = rg_input_read_gamepad();
        if (joystick & (RG_KEY_MENU | RG_KEY_OPTION))
        {
            // The menu draws over the screen: the last frame must be out first
            while (!presenter_idle())
                rg_task_delay(10);
            rg_display_sync(true);
            if (joystick & RG_KEY_MENU)
                game_menu();
            else
                rg_gui_options_menu();
            next_frame = rg_system_timer();
            debt = 0;
            stats_reset();
            continue;
        }

        int64_t t0 = rg_system_timer();
        mel_update(read_buttons(joystick));
        int64_t t1 = rg_system_timer();
        next_frame += FRAME_US; // when this update should be over

        bool draw = !frameskip || (presenter_idle() && (debt == 0 || skipped >= MAX_SKIP));
        int64_t t2 = t1;
        if (draw)
        {
            mel_draw();
            t2 = rg_system_timer();
            if (show_stats)
                mel_draw_overlay(stats_text);
            if (frameskip)
                present(true);
            else if (presenter_idle() && rg_display_sync(false))
                present(false);
            skipped = 0;
        }
        else
            skipped++;

        sample_acc += MEL_SAMPLE_RATE;
        int count = sample_acc / MEL_FPS;
        sample_acc -= count * MEL_FPS;
        mel_audio_mix((int16_t *)audio, count);
        int64_t t3 = rg_system_timer();

        st.updates++;
        st.update_us += t1 - t0;
        st.update_max = RG_MAX(st.update_max, (int)(t1 - t0));
        if (draw)
        {
            st.draws++;
            st.draw_us += t2 - t1;
            st.draw_max = RG_MAX(st.draw_max, (int)(t2 - t1));
        }
        st.other_us += t3 - t2;
        stats_tick();

        // A long stall is lost time anyway, don't let it suppress drawing for long
        debt = RG_MIN(RG_MAX(debt + (t3 - t0) - FRAME_US, 0), 4 * FRAME_US);

        rg_system_tick(t3 - t0);
        rg_audio_submit(audio, count);

        // Pace to 60 fps in case the audio driver doesn't block
        int64_t wait = next_frame - rg_system_timer();
        if (wait > 1000)
            rg_usleep(wait - 500);
        else if (wait < -100000)
            next_frame = rg_system_timer(); // far behind: don't try to catch up
    }
}
