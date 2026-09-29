// Meloni: Lua game runtime for the HU-086 (retro-go app "meloni") and the desktop runner.
//
// The engine is platform independent. A platform (main/main.c on the device, runner/ on the
// desktop) loads a game, calls mel_frame() 60 times per second with the pressed buttons,
// shows mel_framebuffer() and plays what mel_audio_mix() produces.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Version of the Lua API that games are written against (meta.json "api"). Raise it when
// the API changes in a way that older firmware cannot run newer games.
#define MEL_API_VERSION   1

#define MEL_WIDTH         320
#define MEL_HEIGHT        240
#define MEL_FPS           60
#define MEL_SAMPLE_RATE   32000
#define MEL_CHANNELS      8

// Button bits, same order as the Lua constants BTN_LEFT .. BTN_SELECT
enum
{
    MEL_BTN_LEFT   = 1 << 0,
    MEL_BTN_RIGHT  = 1 << 1,
    MEL_BTN_UP     = 1 << 2,
    MEL_BTN_DOWN   = 1 << 3,
    MEL_BTN_A      = 1 << 4,
    MEL_BTN_B      = 1 << 5,
    MEL_BTN_START  = 1 << 6,
    MEL_BTN_SELECT = 1 << 7,
};

// game_path: a .mlg archive or a directory containing main.lua.
// save_path: file used by savedata()/loaddata(), NULL disables saving.
// Returns false when the game could not even be loaded; mel_error() has the reason and the
// framebuffer shows the error screen. Runtime errors later on are handled the same way.
bool mel_init(const char *game_path, const char *save_path);
void mel_shutdown(void);

// Runs one frame (_update then _draw). Returns false while the game is in the error state.
bool mel_frame(uint32_t buttons);

// Calls the game's optional _quit() (so it can save) before the platform exits.
void mel_quit(void);

// Mixes `frames` stereo frames (interleaved int16 L/R) at MEL_SAMPLE_RATE.
void mel_audio_mix(int16_t *out, int frames);

// RGB565 in host byte order, MEL_WIDTH * MEL_HEIGHT pixels.
const uint16_t *mel_framebuffer(void);
const char *mel_error(void);
const char *mel_game_name(void);

// Provided by the platform
void mel_plat_log(const char *msg);
int64_t mel_plat_time_us(void);
void *mel_plat_realloc(void *ptr, size_t size); // realloc semantics, used for the Lua heap

// ---- internal, shared between the engine's source files ----
#ifdef MEL_INTERNAL
#include "lua.h"
#include "lauxlib.h"

extern uint16_t mel_fb[MEL_WIDTH * MEL_HEIGHT];
extern const uint8_t mel_font8x8[256][8];

void mel_gfx_reset(void);
void mel_gfx_open(lua_State *L);
void mel_gfx_print(const char *text, int x, int y, uint16_t color, int scale);
void mel_audio_reset(void);
void mel_audio_open(lua_State *L);

// Reads a file of the running game (archive or directory). Returns a malloc'd buffer with a
// trailing NUL (not counted in *size), or NULL.
char *mel_read_file(const char *name, size_t *size);
uint16_t mel_rgb565(int r, int g, int b);
#endif
