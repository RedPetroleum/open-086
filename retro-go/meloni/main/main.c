// Meloni on retro-go: runs a Lua game (.mlg from roms/meloni/) with the engine in components/meloni.
#include <rg_system.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "meloni.h"

static rg_app_t *app;
static rg_surface_t *surfaces[2];
static int current;
static char save_path[RG_PATH_MAX];

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

static void present(void)
{
    // The display task reads the surface asynchronously, so alternate between two
    rg_surface_t *s = surfaces[current];
    memcpy(s->data, mel_framebuffer(), MEL_WIDTH * MEL_HEIGHT * 2);
    rg_display_submit(s, 0);
    current ^= 1;
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(surfaces[current ^ 1], 0);
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(surfaces[current ^ 1], filename, width, height);
}

static void start_game(void)
{
    if (!mel_init(app->romPath, save_path))
        RG_LOGE("Game failed to start: %s", mel_error());
}

static void game_menu(void)
{
    const rg_gui_option_t choices[] = {
        {1, _("Continue"), NULL, RG_DIALOG_FLAG_NORMAL, NULL},
        {2, _("Restart"),  NULL, RG_DIALOG_FLAG_NORMAL, NULL},
        {3, _("Options"),  NULL, RG_DIALOG_FLAG_NORMAL, NULL},
        {4, _("About"),    NULL, RG_DIALOG_FLAG_NORMAL, NULL},
        {5, _("Quit"),     NULL, RG_DIALOG_FLAG_NORMAL, NULL},
        RG_DIALOG_END,
    };
    rg_audio_set_mute(true);
    int sel = rg_gui_dialog(mel_game_name(), choices, 0);
    rg_audio_set_mute(false);
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

    // The engine draws into its own framebuffer in internal RAM; these copies for the display
    // task go to PSRAM, the ~170 KB of internal heap left are needed by SD card, audio and display
    surfaces[0] = rg_surface_create(MEL_WIDTH, MEL_HEIGHT, RG_PIXEL_565_LE, MEM_SLOW);
    surfaces[1] = rg_surface_create(MEL_WIDTH, MEL_HEIGHT, RG_PIXEL_565_LE, MEM_SLOW);
    if (!surfaces[0] || !surfaces[1])
        RG_PANIC("Out of memory");

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
    int sample_acc = 0;
    int64_t next_frame = rg_system_timer();

    while (true)
    {
        uint32_t joystick = rg_input_read_gamepad();
        if (joystick & RG_KEY_MENU)
        {
            game_menu();
            next_frame = rg_system_timer();
            continue;
        }
        if (joystick & RG_KEY_OPTION)
        {
            rg_gui_options_menu();
            next_frame = rg_system_timer();
            continue;
        }

        int64_t start = rg_system_timer();
        mel_frame(read_buttons(joystick));

        // Skip showing a frame when the display is still busy with the previous one
        if (rg_display_sync(false))
            present();

        sample_acc += MEL_SAMPLE_RATE;
        int count = sample_acc / MEL_FPS;
        sample_acc -= count * MEL_FPS;
        mel_audio_mix((int16_t *)audio, count);

        rg_system_tick(rg_system_timer() - start);
        rg_audio_submit(audio, count);

        // Pace to 60 fps in case the audio driver doesn't block
        next_frame += 1000000 / MEL_FPS;
        int64_t wait = next_frame - rg_system_timer();
        if (wait > 1000)
            rg_usleep(wait - 500);
        else if (wait < -100000)
            next_frame = rg_system_timer(); // far behind: don't try to catch up
    }
}
