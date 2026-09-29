// Engine core: Lua state, game files, frame loop, input, saving, error screen.
#define MEL_INTERNAL
#include "meloni.h"
#include "lualib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern const char mel_prelude[];

// .mlg archive: "MLG1", u32 count, then per file: u16 name_len, name, u32 offset, u32 size
// (all little endian, offsets from the start of the file), followed by the file data.
typedef struct
{
    char name[96];
    uint32_t offset, size;
} mlg_entry_t;

static struct
{
    lua_State *L;
    char game_path[256];
    char save_path[256];
    char name[64];
    bool is_dir;
    mlg_entry_t *entries;
    int entry_count;
    bool failed;
    char error[1024];
    uint32_t buttons, prev_buttons;
    uint16_t held[8];
    uint32_t frame;
} eng;

static void set_error(const char *msg);

// ---- game files ----

static bool archive_open(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;
    uint8_t hdr[8];
    bool ok = fread(hdr, 1, 8, fp) == 8 && memcmp(hdr, "MLG1", 4) == 0;
    uint32_t count = ok ? (hdr[4] | hdr[5] << 8 | hdr[6] << 16 | (uint32_t)hdr[7] << 24) : 0;
    if (ok && count > 4096)
        ok = false;
    if (ok && count > 0 && !(eng.entries = calloc(count, sizeof(mlg_entry_t))))
        ok = false;
    for (uint32_t i = 0; ok && i < count; i++)
    {
        uint8_t b[8];
        uint16_t len;
        if (fread(b, 1, 2, fp) != 2)
            ok = false;
        len = b[0] | b[1] << 8;
        mlg_entry_t *e = &eng.entries[i];
        if (!ok || len >= sizeof(e->name) || fread(e->name, 1, len, fp) != len || fread(b, 1, 8, fp) != 8)
        {
            ok = false;
            break;
        }
        e->name[len] = 0;
        e->offset = b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24;
        e->size = b[4] | b[5] << 8 | b[6] << 16 | (uint32_t)b[7] << 24;
    }
    fclose(fp);
    if (!ok)
    {
        free(eng.entries);
        eng.entries = NULL;
        return false;
    }
    eng.entry_count = (int)count;
    return true;
}

char *mel_read_file(const char *name, size_t *size)
{
    while (name[0] == '.' && name[1] == '/')
        name += 2;
    if (name[0] == '/' || strstr(name, ".."))
        return NULL;

    char path[512];
    long offset = 0, length = -1;
    if (eng.is_dir)
    {
        snprintf(path, sizeof(path), "%s/%s", eng.game_path, name);
    }
    else
    {
        for (int i = 0; i < eng.entry_count; i++)
        {
            if (strcmp(eng.entries[i].name, name) == 0)
            {
                offset = eng.entries[i].offset;
                length = eng.entries[i].size;
                break;
            }
        }
        if (length < 0)
            return NULL;
        snprintf(path, sizeof(path), "%s", eng.game_path);
    }

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    if (length < 0)
    {
        fseek(fp, 0, SEEK_END);
        length = ftell(fp);
    }
    fseek(fp, offset, SEEK_SET);
    char *buffer = length >= 0 ? malloc(length + 1) : NULL;
    if (buffer && fread(buffer, 1, length, fp) != (size_t)length)
    {
        free(buffer);
        buffer = NULL;
    }
    fclose(fp);
    if (!buffer)
        return NULL;
    buffer[length] = 0;
    if (size)
        *size = length;
    return buffer;
}

// ---- Lua helpers ----

static void *l_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud, (void)osize;
    if (nsize == 0)
    {
        mel_plat_realloc(ptr, 0);
        return NULL;
    }
    return mel_plat_realloc(ptr, nsize);
}

static int l_panic(lua_State *L)
{
    set_error(lua_tostring(L, -1));
    mel_plat_log("Lua panic");
    return 0;
}

static int msghandler(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    if (!msg)
        msg = luaL_tolstring(L, 1, NULL);
    luaL_traceback(L, L, msg, 1);
    return 1;
}

// Calls global `name` if it exists. Returns false on error (already reported).
static bool call_global(const char *name)
{
    lua_State *L = eng.L;
    lua_pushcfunction(L, msghandler);
    if (lua_getglobal(L, name) != LUA_TFUNCTION)
    {
        lua_pop(L, 2);
        return true;
    }
    if (lua_pcall(L, 0, 0, -2) != LUA_OK)
    {
        set_error(lua_tostring(L, -1));
        lua_pop(L, 2);
        return false;
    }
    lua_pop(L, 1);
    return true;
}

static bool run_chunk(const char *code, size_t len, const char *chunkname)
{
    lua_State *L = eng.L;
    lua_pushcfunction(L, msghandler);
    if (luaL_loadbufferx(L, code, len, chunkname, "t") != LUA_OK || lua_pcall(L, 0, 0, -2) != LUA_OK)
    {
        set_error(lua_tostring(L, -1));
        lua_pop(L, 2);
        return false;
    }
    lua_pop(L, 1);
    return true;
}

// ---- engine API for Lua ----

static int button_arg(lua_State *L, int idx)
{
    int b = (int)luaL_checkinteger(L, idx);
    luaL_argcheck(L, b >= 0 && b < 8, idx, "button must be 0..7 (BTN_LEFT .. BTN_SELECT)");
    return b;
}

// btn([b]): is button b held? Without argument: bitmask of all held buttons
static int l_btn(lua_State *L)
{
    if (lua_isnoneornil(L, 1))
        lua_pushinteger(L, eng.buttons);
    else
        lua_pushboolean(L, eng.buttons & (1u << button_arg(L, 1)));
    return 1;
}

// btnp(b): pressed this frame, repeats after 15 frames every 4 frames while held
static int l_btnp(lua_State *L)
{
    int held = eng.held[button_arg(L, 1)];
    lua_pushboolean(L, held == 1 || (held > 15 && (held - 15) % 4 == 0));
    return 1;
}

// time(): seconds since the game started, counted in frames (1/60 s)
static int l_time(lua_State *L)
{
    lua_pushnumber(L, (lua_Number)eng.frame / MEL_FPS);
    return 1;
}

static int l_frame(lua_State *L)
{
    lua_pushinteger(L, eng.frame);
    return 1;
}

static int l_log(lua_State *L)
{
    int n = lua_gettop(L); // before luaL_buffinit, which pushes a placeholder
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= n; i++)
    {
        if (i > 1)
            luaL_addchar(&b, ' ');
        luaL_tolstring(L, i, NULL);
        luaL_addvalue(&b);
    }
    luaL_pushresult(&b);
    mel_plat_log(lua_tostring(L, -1));
    return 0;
}

static int l_readfile(lua_State *L)
{
    size_t size;
    char *data = mel_read_file(luaL_checkstring(L, 1), &size);
    if (!data)
        return 0;
    lua_pushlstring(L, data, size);
    free(data);
    return 1;
}

static int l_savewrite(lua_State *L)
{
    size_t len;
    const char *data = luaL_checklstring(L, 1, &len);
    if (!eng.save_path[0])
    {
        lua_pushboolean(L, false);
        return 1;
    }
    char tmp[270];
    snprintf(tmp, sizeof(tmp), "%s.tmp", eng.save_path);
    FILE *fp = fopen(tmp, "wb");
    bool ok = fp && fwrite(data, 1, len, fp) == len;
    if (fp)
        fclose(fp);
    remove(eng.save_path);
    ok = ok && rename(tmp, eng.save_path) == 0;
    lua_pushboolean(L, ok);
    return 1;
}

static int l_saveread(lua_State *L)
{
    if (!eng.save_path[0])
        return 0;
    FILE *fp = fopen(eng.save_path, "rb");
    if (!fp)
        return 0;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    char chunk[512];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0)
        luaL_addlstring(&b, chunk, n);
    fclose(fp);
    luaL_pushresult(&b);
    return 1;
}

// ---- error screen ----

static void draw_error_screen(void)
{
    uint16_t bg = mel_rgb565(0x1D, 0x2B, 0x53), fg = mel_rgb565(0xFF, 0xF1, 0xE8);
    uint16_t hl = mel_rgb565(0xFF, 0x00, 0x4D), dim = mel_rgb565(0xC2, 0xC3, 0xC7);
    for (int i = 0; i < MEL_WIDTH * MEL_HEIGHT; i++)
        mel_fb[i] = bg;

    char title[96];
    snprintf(title, sizeof(title), "%s: error", eng.name);
    mel_gfx_print(title, 8, 8, hl, 1);

    // Word-wrap to 38 columns; the traceback lines are long file:line entries
    char line[40];
    int col = 0, y = 24;
    for (const char *p = eng.error; *p && y < MEL_HEIGHT - 24; p++)
    {
        if (*p == '\t')
            continue;
        if (*p != '\n')
            line[col++] = *p;
        if (*p == '\n' || col == 38)
        {
            line[col] = 0;
            mel_gfx_print(line, 8, y, fg, 1);
            y += 10;
            col = 0;
        }
    }
    if (col > 0 && y < MEL_HEIGHT - 24)
    {
        line[col] = 0;
        mel_gfx_print(line, 8, y, fg, 1);
    }
    mel_gfx_print("SELECT+START: menu", 8, MEL_HEIGHT - 16, dim, 1);
}

static void set_error(const char *msg)
{
    if (eng.failed)
        return;
    eng.failed = true;
    snprintf(eng.error, sizeof(eng.error), "%s", msg ? msg : "unknown error");
    mel_plat_log(eng.error);
    draw_error_screen();
}

// ---- public API ----

bool mel_init(const char *game_path, const char *save_path)
{
    mel_shutdown();
    memset(&eng, 0, sizeof(eng));
    snprintf(eng.game_path, sizeof(eng.game_path), "%s", game_path);
    snprintf(eng.save_path, sizeof(eng.save_path), "%s", save_path ? save_path : "");

    // Name: file or folder name without extension
    const char *base = strrchr(game_path, '/');
    base = base ? base + 1 : game_path;
    snprintf(eng.name, sizeof(eng.name), "%s", base);
    char *dot = strrchr(eng.name, '.');
    if (dot && dot != eng.name)
        *dot = 0;

    mel_gfx_reset();
    mel_audio_reset();

    struct stat st;
    if (stat(game_path, &st) != 0)
    {
        set_error("Game not found");
        return false;
    }
    eng.is_dir = S_ISDIR(st.st_mode);
    if (!eng.is_dir && !archive_open(game_path))
    {
        set_error("Not a valid .mlg game archive");
        return false;
    }

    lua_State *L = eng.L = lua_newstate(l_alloc, NULL);
    if (!L)
    {
        set_error("Out of memory");
        return false;
    }
    lua_atpanic(L, l_panic);

    static const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base},        {LUA_COLIBNAME, luaopen_coroutine}, {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},    {LUA_UTF8LIBNAME, luaopen_utf8},
    };
    for (size_t i = 0; i < sizeof(libs) / sizeof(libs[0]); i++)
    {
        luaL_requiref(L, libs[i].name, libs[i].func, 1);
        lua_pop(L, 1);
    }
    // No file access outside the game and its save file
    lua_pushnil(L), lua_setglobal(L, "dofile");
    lua_pushnil(L), lua_setglobal(L, "loadfile");

    static const luaL_Reg funcs[] = {
        {"btn", l_btn},   {"btnp", l_btnp}, {"time", l_time},  {"frame", l_frame}, {"log", l_log},
        {"__readfile", l_readfile}, {"__savewrite", l_savewrite}, {"__saveread", l_saveread}, {NULL, NULL},
    };
    lua_pushglobaltable(L);
    luaL_setfuncs(L, funcs, 0);
    lua_pop(L, 1);
    lua_pushinteger(L, MEL_API_VERSION), lua_setglobal(L, "API_VERSION");
    lua_pushinteger(L, MEL_WIDTH), lua_setglobal(L, "SCREEN_W");
    lua_pushinteger(L, MEL_HEIGHT), lua_setglobal(L, "SCREEN_H");
    static const char *const buttons[] = {"BTN_LEFT", "BTN_RIGHT", "BTN_UP", "BTN_DOWN", "BTN_A", "BTN_B", "BTN_START", "BTN_SELECT"};
    for (int i = 0; i < 8; i++)
        lua_pushinteger(L, i), lua_setglobal(L, buttons[i]);

    mel_gfx_open(L);
    mel_audio_open(L);

    if (!run_chunk(mel_prelude, strlen(mel_prelude), "=prelude"))
        return false;

    size_t size;
    char *main_lua = mel_read_file("main.lua", &size);
    if (!main_lua)
    {
        set_error("main.lua not found in game");
        return false;
    }
    bool ok = run_chunk(main_lua, size, "@main.lua");
    free(main_lua);
    return ok && call_global("_init");
}

bool mel_frame(uint32_t buttons)
{
    if (eng.failed || !eng.L)
        return false;

    eng.prev_buttons = eng.buttons;
    eng.buttons = buttons & 0xFF;
    for (int i = 0; i < 8; i++)
        eng.held[i] = (eng.buttons & (1u << i)) ? (eng.held[i] < 60000 ? eng.held[i] + 1 : eng.held[i]) : 0;

    bool ok = call_global("__tick") && call_global("_update") && call_global("_draw");
    eng.frame++;
    if (ok && (eng.frame & 63) == 0)
        lua_gc(eng.L, LUA_GCSTEP, 0);
    return ok;
}

void mel_quit(void)
{
    if (eng.L && !eng.failed)
        call_global("_quit");
}

void mel_shutdown(void)
{
    if (eng.L)
        lua_close(eng.L);
    eng.L = NULL;
    free(eng.entries);
    eng.entries = NULL;
    eng.entry_count = 0;
    mel_audio_reset();
}

const uint16_t *mel_framebuffer(void)
{
    return mel_fb;
}

const char *mel_error(void)
{
    return eng.failed ? eng.error : NULL;
}

const char *mel_game_name(void)
{
    return eng.name;
}
