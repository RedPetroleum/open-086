// Meloni on retro-go: runs a Lua game (.mlg from roms/meloni/) with the engine in components/meloni,
// which build_retro_go.sh copies in from meloni-games (engine/, pinned by MELONI_COMMIT).
#include <rg_system.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

#include "meloni.h"

#define FRAME_US (1000000 / MEL_FPS)
// With frameskip at least every (MAX_SKIP + 1)th update is drawn when the display is free (12 fps).
// If even that is too slow, the game slows down rather than showing hardly anything.
#define MAX_SKIP 4

static rg_app_t *app;
static char save_path[RG_PATH_MAX];

// Frameskip (default): _update runs 60 times per second, _draw only when the display has taken the
// previous frame and the game is not behind. The display task reads the engine framebuffer directly:
// it converts it line by line into its own DMA buffers and is done with it once rg_display_sync()
// reports it free, so the engine must not draw while the display is busy.
// Without frameskip (to compare): every update is drawn, and a frame is copied into one of two
// PSRAM surfaces when the display is free, otherwise it is thrown away.
static bool frameskip = true;
static rg_surface_t screen; // the engine framebuffer as a surface
static rg_surface_t *copies[2];
static int current;

// Frame statistics over one second, drawn over the game with "Show stats" and logged
static bool show_stats;
static char stats_text[320] = "measuring ...";
static struct
{
    int64_t start, update_us, draw_us, other_us;
    int updates, draws, update_max, draw_max;
} st;

void mel_plat_log(const char *msg)
{
    RG_LOGI("%s", msg);
}

int64_t mel_plat_time_us(void)
{
    return rg_system_timer();
}

// The Lua heap goes to PSRAM (8 MB), the internal RAM is kept for the display and audio buffers
void *mel_plat_realloc(void *ptr, size_t size)
{
    if (size == 0)
    {
        heap_caps_free(ptr);
        return NULL;
    }
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
}

static void present_copy(void)
{
    if (!copies[0] || !copies[1])
    {
        // Only needed without frameskip; PSRAM, the ~170 KB of internal heap left are needed by
        // SD card, audio and display
        copies[0] = rg_surface_create(MEL_WIDTH, MEL_HEIGHT, RG_PIXEL_565_LE, MEM_SLOW);
        copies[1] = rg_surface_create(MEL_WIDTH, MEL_HEIGHT, RG_PIXEL_565_LE, MEM_SLOW);
        if (!copies[0] || !copies[1])
        {
            RG_LOGE("Out of memory for the frame copies, back to frameskip");
            rg_surface_free(copies[0]), copies[0] = NULL;
            rg_surface_free(copies[1]), copies[1] = NULL;
            frameskip = true;
            return;
        }
    }
    rg_surface_t *s = copies[current];
    memcpy(s->data, mel_framebuffer(), MEL_WIDTH * MEL_HEIGHT * 2);
    rg_display_submit(s, 0);
    current ^= 1;
}

static void stats_reset(void)
{
    memset(&st, 0, sizeof(st));
    st.start = rg_system_timer();
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
    char a[MS_LEN], b[MS_LEN], c[MS_LEN], d[MS_LEN], e[MS_LEN];
    int busy = (int)((st.update_us + st.draw_us + st.other_us) * 100 / elapsed);
    snprintf(stats_text, sizeof(stats_text),
             "speed %d%%  fps %d  cpu %d%%  skip %s\n"
             "update %s ms  max %s\n"
             "draw   %s ms  max %s  rest %s\n"
             "lua %d KB  internal free %d KB",
             (int)(st.updates * 100LL * 1000000 / MEL_FPS / elapsed), (int)(st.draws * 1000000LL / elapsed), busy,
             frameskip ? "on" : "off",
             ms(a, st.updates ? st.update_us / st.updates : 0), ms(b, st.update_max),
             ms(c, st.draws ? st.draw_us / st.draws : 0), ms(d, st.draw_max),
             ms(e, st.updates ? st.other_us / st.updates : 0),
             (int)(mel_mem_used() / 1024), (int)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    if (show_stats)
    {
        char line[sizeof(stats_text)];
        snprintf(line, sizeof(line), "%s", stats_text);
        for (char *p = line; *p; p++)
            if (*p == '\n')
                *p = '|';
        RG_LOGI("stats: %s", line);
    }
    stats_reset();
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(&screen, 0);
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

    // The engine draws into its own framebuffer in internal RAM, the display task reads it from there
    screen = (rg_surface_t){
        .width = MEL_WIDTH,
        .height = MEL_HEIGHT,
        .stride = MEL_WIDTH * 2,
        .format = RG_PIXEL_565_LE,
        .data = (void *)mel_framebuffer(),
    };

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
            rg_display_sync(true); // the menu draws over the screen, the display must be done with the game
            if (joystick & RG_KEY_MENU)
                game_menu();
            else
                rg_gui_options_menu();
            next_frame = rg_system_timer();
            stats_reset();
            continue;
        }

        int64_t t0 = rg_system_timer();
        mel_update(read_buttons(joystick));
        int64_t t1 = rg_system_timer();
        next_frame += FRAME_US; // when this update should be over

        // Draw only when the display is free (it can't show more anyway: a full frame takes ~31 ms
        // at 40 MHz) and the update came in time, else catch up first
        bool draw = !frameskip || (rg_display_sync(false) && (t1 <= next_frame || skipped >= MAX_SKIP));
        int64_t t2 = t1;
        if (draw)
        {
            mel_draw();
            t2 = rg_system_timer();
            if (show_stats)
                mel_draw_overlay(stats_text);
            if (frameskip)
                rg_display_submit(&screen, 0);
            else if (rg_display_sync(false))
                present_copy();
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
