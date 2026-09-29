// Desktop runner for Meloni games: the same engine as on the HU-086.
//
//   meloni-run game-dir|game.mlg                  window (needs SDL2), F5 reloads, Esc quits
//   meloni-run --headless --frames 600 --input "10:START,30-90:RIGHT" --screenshot out.png --wav out.wav game-dir
//
// Headless mode exits with status 1 when the game raised a Lua error, which makes it usable
// as a smoke test in CI and for checking a change without the device.
#include "meloni.h"
#include "lodepng.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef MEL_SDL
#include <SDL.h>
#endif

void mel_plat_log(const char *msg)
{
    fprintf(stderr, "[meloni] %s\n", msg);
}

int64_t mel_plat_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void *mel_plat_realloc(void *ptr, size_t size)
{
    if (size == 0)
    {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

static bool save_screenshot(const char *path)
{
    const uint16_t *fb = mel_framebuffer();
    unsigned char *rgb = malloc(MEL_WIDTH * MEL_HEIGHT * 3);
    if (!rgb)
        return false;
    for (int i = 0; i < MEL_WIDTH * MEL_HEIGHT; i++)
    {
        uint16_t c = fb[i];
        rgb[i * 3 + 0] = (c >> 11) << 3 | (c >> 13);
        rgb[i * 3 + 1] = ((c >> 5) & 0x3F) << 2 | ((c >> 9) & 3);
        rgb[i * 3 + 2] = (c & 0x1F) << 3 | ((c >> 2) & 7);
    }
    unsigned err = lodepng_encode24_file(path, rgb, MEL_WIDTH, MEL_HEIGHT);
    free(rgb);
    if (err)
        fprintf(stderr, "screenshot %s failed: %s\n", path, lodepng_error_text(err));
    return err == 0;
}

// ---- scripted input: "10:START,30-90:RIGHT+A" ----

typedef struct
{
    int from, to;
    uint32_t buttons;
} input_step_t;

static input_step_t steps[256];
static int steps_count;

static uint32_t parse_buttons(const char *s)
{
    static const char *const names[] = {"LEFT", "RIGHT", "UP", "DOWN", "A", "B", "START", "SELECT"};
    uint32_t mask = 0;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", s);
    for (char *tok = strtok(buf, "+"); tok; tok = strtok(NULL, "+"))
    {
        bool found = false;
        for (int i = 0; i < 8; i++)
            if (strcasecmp(tok, names[i]) == 0)
                mask |= 1u << i, found = true;
        if (!found)
        {
            fprintf(stderr, "unknown button '%s' (LEFT RIGHT UP DOWN A B START SELECT)\n", tok);
            exit(2);
        }
    }
    return mask;
}

static void parse_input(const char *spec)
{
    char *copy = strdup(spec), *save = NULL;
    for (char *item = strtok_r(copy, ",", &save); item && steps_count < 256; item = strtok_r(NULL, ",", &save))
    {
        char *colon = strchr(item, ':');
        if (!colon)
        {
            fprintf(stderr, "bad input step '%s', expected FRAME[-FRAME]:BUTTON[+BUTTON]\n", item);
            exit(2);
        }
        *colon = 0;
        input_step_t *st = &steps[steps_count++];
        st->from = atoi(item);
        char *dash = strchr(item, '-');
        st->to = dash ? atoi(dash + 1) : st->from;
        st->buttons = parse_buttons(colon + 1);
    }
    free(copy);
}

static uint32_t scripted_buttons(int frame)
{
    uint32_t mask = 0;
    for (int i = 0; i < steps_count; i++)
        if (frame >= steps[i].from && frame <= steps[i].to)
            mask |= steps[i].buttons;
    return mask;
}

// ---- headless ----

static void write_le(FILE *fp, uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; i++)
        fputc((value >> (i * 8)) & 0xFF, fp);
}

static int run_headless(const char *game, const char *save, int frames, const char *screenshot, const char *wav)
{
    int16_t audio[1200];
    int sample_acc = 0, peak = 0;
    uint32_t total = 0;
    FILE *wav_fp = wav ? fopen(wav, "wb") : NULL;
    if (wav_fp)
        fseek(wav_fp, 44, SEEK_SET);

    bool ok = mel_init(game, save);
    for (int f = 0; ok && f < frames; f++)
    {
        ok = mel_frame(scripted_buttons(f));
        sample_acc += MEL_SAMPLE_RATE;
        int count = sample_acc / MEL_FPS;
        sample_acc -= count * MEL_FPS;
        mel_audio_mix(audio, count);
        for (int i = 0; i < count; i++)
            peak = abs(audio[i * 2]) > peak ? abs(audio[i * 2]) : peak;
        if (wav_fp)
            fwrite(audio, 4, count, wav_fp);
        total += count;
    }
    if (screenshot)
        save_screenshot(screenshot);
    if (wav_fp)
    {
        // 16-bit stereo PCM header
        fseek(wav_fp, 0, SEEK_SET);
        fwrite("RIFF", 1, 4, wav_fp), write_le(wav_fp, 36 + total * 4, 4), fwrite("WAVEfmt ", 1, 8, wav_fp);
        write_le(wav_fp, 16, 4), write_le(wav_fp, 1, 2), write_le(wav_fp, 2, 2), write_le(wav_fp, MEL_SAMPLE_RATE, 4);
        write_le(wav_fp, MEL_SAMPLE_RATE * 4, 4), write_le(wav_fp, 4, 2), write_le(wav_fp, 16, 2);
        fwrite("data", 1, 4, wav_fp), write_le(wav_fp, total * 4, 4);
        fclose(wav_fp);
        printf("audio: %s (peak %d of 32767)\n", wav, peak);
    }
    if (!ok)
    {
        fprintf(stderr, "%s: error\n%s\n", game, mel_error() ?: "?");
        mel_shutdown();
        return 1;
    }
    mel_quit();
    mel_shutdown();
    printf("%s: ran %d frames without errors\n", game, frames);
    return 0;
}

// ---- window (SDL2) ----

#ifdef MEL_SDL
// Newest modification time of the files in a game directory (two levels deep)
static time_t newest_mtime(const char *dir, int depth)
{
    time_t newest = 0;
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *ent;
    while ((ent = readdir(d)))
    {
        if (ent->d_name[0] == '.')
            continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        time_t t = st.st_mtime;
        if (S_ISDIR(st.st_mode))
            t = depth > 0 ? newest_mtime(path, depth - 1) : 0;
        if (t > newest)
            newest = t;
    }
    closedir(d);
    return newest;
}

static uint32_t keyboard_buttons(void)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint32_t b = 0;
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) b |= MEL_BTN_LEFT;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) b |= MEL_BTN_RIGHT;
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) b |= MEL_BTN_UP;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) b |= MEL_BTN_DOWN;
    if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_K] || k[SDL_SCANCODE_SPACE]) b |= MEL_BTN_A;
    if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_Y] || k[SDL_SCANCODE_J]) b |= MEL_BTN_B;
    if (k[SDL_SCANCODE_RETURN]) b |= MEL_BTN_START;
    if (k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_RSHIFT]) b |= MEL_BTN_SELECT;
    return b;
}

static int run_window(const char *game, const char *save, int scale)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    char title[300];
    SDL_Window *win = SDL_CreateWindow("Meloni", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       MEL_WIDTH * scale, MEL_HEIGHT * scale, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, MEL_WIDTH, MEL_HEIGHT);
    SDL_RenderSetIntegerScale(ren, SDL_TRUE);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, MEL_WIDTH, MEL_HEIGHT);

    SDL_AudioSpec want = {.freq = MEL_SAMPLE_RATE, .format = AUDIO_S16SYS, .channels = 2, .samples = 512};
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (audio)
        SDL_PauseAudioDevice(audio, 0);

    struct stat st;
    bool is_dir = stat(game, &st) == 0 && S_ISDIR(st.st_mode);
    time_t loaded_mtime = is_dir ? newest_mtime(game, 2) : 0;

    mel_init(game, save);
    snprintf(title, sizeof(title), "Meloni - %s   (F5 reload, F12 screenshot, Esc quit)", mel_game_name());
    SDL_SetWindowTitle(win, title);

    int16_t samples[1200];
    int sample_acc = 0, frame = 0, shots = 0;
    uint64_t freq = SDL_GetPerformanceFrequency(), next = SDL_GetPerformanceCounter();
    bool running = true, reload = false;

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT)
                running = false;
            else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = false;
            else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F5)
                reload = true;
            else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F12)
            {
                char name[300];
                snprintf(name, sizeof(name), "%s-%d.png", mel_game_name(), ++shots);
                if (save_screenshot(name))
                    printf("screenshot: %s\n", name);
            }
        }

        // Reload automatically when a file in the game folder changed
        if (is_dir && ++frame % 30 == 0)
        {
            time_t t = newest_mtime(game, 2);
            if (t != loaded_mtime)
                loaded_mtime = t, reload = true;
        }
        if (reload)
        {
            reload = false;
            mel_quit();
            mel_init(game, save);
            if (audio)
                SDL_ClearQueuedAudio(audio);
            printf("reloaded %s\n", game);
        }

        mel_frame(keyboard_buttons());

        SDL_UpdateTexture(tex, NULL, mel_framebuffer(), MEL_WIDTH * 2);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        sample_acc += MEL_SAMPLE_RATE;
        int count = sample_acc / MEL_FPS;
        sample_acc -= count * MEL_FPS;
        mel_audio_mix(samples, count);
        if (audio && SDL_GetQueuedAudioSize(audio) < MEL_SAMPLE_RATE / 5 * 4) // keep latency below 200 ms
            SDL_QueueAudio(audio, samples, count * 4);

        next += freq / MEL_FPS;
        uint64_t now = SDL_GetPerformanceCounter();
        if (now < next)
            SDL_Delay((Uint32)((next - now) * 1000 / freq));
        else if (now - next > freq / 4)
            next = now; // fell far behind (debugger, window drag): don't try to catch up
    }

    mel_quit();
    mel_shutdown();
    if (audio)
        SDL_CloseAudioDevice(audio);
    SDL_Quit();
    return 0;
}
#endif

static void usage(void)
{
    fprintf(stderr,
            "usage: meloni-run [options] <game-dir | game.mlg>\n"
            "  --headless          run without window (always the case without SDL2)\n"
            "  --frames N          headless: frames to run (default 300 = 5 s)\n"
            "  --input SPEC        headless: buttons per frame, e.g. \"10:START,30-90:RIGHT+A\"\n"
            "  --screenshot FILE   headless: write the last frame as PNG\n"
            "  --wav FILE          headless: write the sound output as WAV\n"
            "  --save FILE         file for savedata()/loaddata() (default: temporary headless, ./<game>.sav in a window)\n"
            "  --scale N           window scale (default 3)\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *game = NULL, *screenshot = NULL, *save = NULL, *wav = NULL;
    bool headless = false;
    int frames = 300, scale = 3;

    for (int i = 1; i < argc; i++)
    {
        const char *a = argv[i];
        bool has_value = i + 1 < argc;
        if (strcmp(a, "--headless") == 0)
            headless = true;
        else if (strcmp(a, "--frames") == 0 && has_value)
            frames = atoi(argv[++i]);
        else if (strcmp(a, "--input") == 0 && has_value)
            parse_input(argv[++i]);
        else if (strcmp(a, "--screenshot") == 0 && has_value)
            screenshot = argv[++i];
        else if (strcmp(a, "--wav") == 0 && has_value)
            wav = argv[++i];
        else if (strcmp(a, "--save") == 0 && has_value)
            save = argv[++i];
        else if (strcmp(a, "--scale") == 0 && has_value)
            scale = atoi(argv[++i]);
        else if (a[0] == '-' || game)
            usage();
        else
            game = a;
    }
    if (!game)
        usage();

    // Strip a trailing slash so the game name comes out right
    char path[1024];
    snprintf(path, sizeof(path), "%s", game);
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/')
        path[--len] = 0;

#ifdef MEL_SDL
    if (!headless)
    {
        char default_save[1100];
        if (!save)
        {
            const char *base = strrchr(path, '/');
            snprintf(default_save, sizeof(default_save), "%s.sav", base ? base + 1 : path);
            save = default_save;
        }
        return run_window(path, save, scale < 1 ? 1 : scale);
    }
#else
    (void)scale;
    if (!headless)
        fprintf(stderr, "built without SDL2, running headless\n");
#endif
    // Without --save, savedata() still works but goes to a temporary file (fresh on every run)
    char temp_save[64] = "";
    if (!save)
    {
        snprintf(temp_save, sizeof(temp_save), "/tmp/meloni-run-%d.sav", (int)getpid());
        save = temp_save;
    }
    int status = run_headless(path, save, frames, screenshot, wav);
    if (temp_save[0])
        remove(temp_save);
    return status;
}
