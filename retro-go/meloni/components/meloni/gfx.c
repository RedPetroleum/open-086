// Drawing API: framebuffer, primitives, text, images.
#define MEL_INTERNAL
#include "meloni.h"
#include "lodepng.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RGB_FLAG 0x1000000 // rgb() values carry this bit, plain numbers are palette indexes

uint16_t mel_fb[MEL_WIDTH * MEL_HEIGHT];

static uint16_t palette[256];
static int cam_x, cam_y;
static int clip_x0, clip_y0, clip_x1, clip_y1; // clip_x1/y1 exclusive

typedef struct
{
    int w, h;
    uint16_t *px;
    uint8_t *alpha; // 0 = transparent
} image_t;

// PICO-8's 16 colours, then its 16 "secret" colours
static const uint32_t default_palette[32] = {
    0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8,
    0xFF004D, 0xFFA300, 0xFFEC27, 0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA,
    0x291814, 0x111D35, 0x422136, 0x125359, 0x742F29, 0x49333B, 0xA28879, 0xF3EF7D,
    0xBE1250, 0xFF6C24, 0xA8E72E, 0x00B543, 0x065AB5, 0x754665, 0xFF6E59, 0xFF9D81,
};

uint16_t mel_rgb565(int r, int g, int b)
{
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void palette_reset(void)
{
    memset(palette, 0, sizeof(palette));
    for (int i = 0; i < 32; i++)
    {
        uint32_t c = default_palette[i];
        palette[i] = mel_rgb565(c >> 16, (c >> 8) & 0xFF, c & 0xFF);
    }
}

void mel_gfx_reset(void)
{
    palette_reset();
    cam_x = cam_y = 0;
    clip_x0 = clip_y0 = 0;
    clip_x1 = MEL_WIDTH;
    clip_y1 = MEL_HEIGHT;
    memset(mel_fb, 0, sizeof(mel_fb));
}

// ---- argument helpers: coordinates may be floats, they are floored ----

static int argi(lua_State *L, int idx)
{
    return (int)floorf((float)luaL_checknumber(L, idx));
}

static int opti(lua_State *L, int idx, int def)
{
    return lua_isnoneornil(L, idx) ? def : argi(L, idx);
}

static uint16_t to_color(lua_Integer c)
{
    if (c & RGB_FLAG)
        return mel_rgb565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    return palette[c & 0xFF];
}

static uint16_t argcolor(lua_State *L, int idx, int def)
{
    if (lua_isnoneornil(L, idx))
        return palette[def];
    return to_color((lua_Integer)floorf((float)luaL_checknumber(L, idx)));
}

// ---- raw drawing, screen coordinates (camera already applied) ----

static inline void put(int x, int y, uint16_t c)
{
    if (x >= clip_x0 && x < clip_x1 && y >= clip_y0 && y < clip_y1)
        mel_fb[y * MEL_WIDTH + x] = c;
}

static void hline(int x0, int x1, int y, uint16_t c)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y < clip_y0 || y >= clip_y1)
        return;
    if (x0 < clip_x0) x0 = clip_x0;
    if (x1 >= clip_x1) x1 = clip_x1 - 1;
    uint16_t *p = &mel_fb[y * MEL_WIDTH + x0];
    for (int x = x0; x <= x1; x++)
        *p++ = c;
}

static void fillrect(int x0, int y0, int x1, int y1, uint16_t c)
{
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++)
        hline(x0, x1, y, c);
}

static void drawline(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true)
    {
        put(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void drawcircle(int cx, int cy, int r, uint16_t c, bool fill)
{
    if (r < 0)
        return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y)
    {
        if (fill)
        {
            hline(cx - x, cx + x, cy + y, c);
            hline(cx - x, cx + x, cy - y, c);
            hline(cx - y, cx + y, cy + x, c);
            hline(cx - y, cx + y, cy - x, c);
        }
        else
        {
            put(cx + x, cy + y, c); put(cx - x, cy + y, c);
            put(cx + x, cy - y, c); put(cx - x, cy - y, c);
            put(cx + y, cy + x, c); put(cx - y, cy + x, c);
            put(cx + y, cy - x, c); put(cx - y, cy - x, c);
        }
        y++;
        if (err < 0)
            err += 2 * y + 1;
        else
        {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

// Decodes one UTF-8 codepoint, advances *s. Invalid bytes come back as '?'.
static int utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    int cp = *p++, extra = 0;
    if (cp >= 0xF0) { cp &= 0x07; extra = 3; }
    else if (cp >= 0xE0) { cp &= 0x0F; extra = 2; }
    else if (cp >= 0xC0) { cp &= 0x1F; extra = 1; }
    else if (cp >= 0x80) { *s = (const char *)p; return '?'; }
    while (extra-- > 0 && (*p & 0xC0) == 0x80)
        cp = (cp << 6) | (*p++ & 0x3F);
    *s = (const char *)p;
    return cp;
}

// Draws text in screen coordinates, returns the x after the last line's end.
static int drawtext(const char *text, int x, int y, uint16_t color, int scale)
{
    int start_x = x;
    if (scale < 1)
        scale = 1;
    while (*text)
    {
        int cp = utf8_next(&text);
        if (cp == '\n')
        {
            x = start_x;
            y += 8 * scale + scale;
            continue;
        }
        const uint8_t *glyph = mel_font8x8[cp < 256 ? cp : '?'];
        for (int row = 0; row < 8; row++)
        {
            uint8_t bits = glyph[row];
            for (int col = 0; bits && col < 8; col++, bits <<= 1)
            {
                if (!(bits & 0x80))
                    continue;
                if (scale == 1)
                    put(x + col, y + row, color);
                else
                    fillrect(x + col * scale, y + row * scale, x + col * scale + scale - 1, y + row * scale + scale - 1, color);
            }
        }
        x += 8 * scale;
    }
    return x;
}

void mel_gfx_print(const char *text, int x, int y, uint16_t color, int scale)
{
    drawtext(text, x, y, color, scale);
}

static void blit(const image_t *img, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, bool flip_x, bool flip_y)
{
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return;
    int x0 = dx < clip_x0 ? clip_x0 : dx;
    int y0 = dy < clip_y0 ? clip_y0 : dy;
    int x1 = dx + dw > clip_x1 ? clip_x1 : dx + dw;
    int y1 = dy + dh > clip_y1 ? clip_y1 : dy + dh;
    for (int y = y0; y < y1; y++)
    {
        int v = (y - dy) * sh / dh;
        if (flip_y)
            v = sh - 1 - v;
        int src_y = sy + v;
        if (src_y < 0 || src_y >= img->h)
            continue;
        const uint16_t *src = &img->px[src_y * img->w];
        const uint8_t *alpha = &img->alpha[src_y * img->w];
        uint16_t *dst = &mel_fb[y * MEL_WIDTH];
        for (int x = x0; x < x1; x++)
        {
            int u = (x - dx) * sw / dw;
            if (flip_x)
                u = sw - 1 - u;
            int src_x = sx + u;
            if (src_x < 0 || src_x >= img->w || !alpha[src_x])
                continue;
            dst[x] = src[src_x];
        }
    }
}

// ---- Lua bindings ----

static int l_cls(lua_State *L)
{
    uint16_t c = argcolor(L, 1, 0);
    for (int i = 0; i < MEL_WIDTH * MEL_HEIGHT; i++)
        mel_fb[i] = c;
    return 0;
}

static int l_pset(lua_State *L)
{
    put(argi(L, 1) - cam_x, argi(L, 2) - cam_y, argcolor(L, 3, 7));
    return 0;
}

static int l_pget(lua_State *L)
{
    int x = argi(L, 1) - cam_x, y = argi(L, 2) - cam_y;
    if (x < 0 || x >= MEL_WIDTH || y < 0 || y >= MEL_HEIGHT)
    {
        lua_pushinteger(L, RGB_FLAG);
        return 1;
    }
    uint16_t c = mel_fb[y * MEL_WIDTH + x];
    int r = (c >> 11) << 3, g = ((c >> 5) & 0x3F) << 2, b = (c & 0x1F) << 3;
    lua_pushinteger(L, RGB_FLAG | (r << 16) | (g << 8) | b);
    return 1;
}

static int l_line(lua_State *L)
{
    drawline(argi(L, 1) - cam_x, argi(L, 2) - cam_y, argi(L, 3) - cam_x, argi(L, 4) - cam_y, argcolor(L, 5, 7));
    return 0;
}

static int l_rect(lua_State *L)
{
    int x0 = argi(L, 1) - cam_x, y0 = argi(L, 2) - cam_y;
    int x1 = argi(L, 3) - cam_x, y1 = argi(L, 4) - cam_y;
    uint16_t c = argcolor(L, 5, 7);
    hline(x0, x1, y0, c);
    hline(x0, x1, y1, c);
    drawline(x0, y0, x0, y1, c);
    drawline(x1, y0, x1, y1, c);
    return 0;
}

static int l_rectfill(lua_State *L)
{
    fillrect(argi(L, 1) - cam_x, argi(L, 2) - cam_y, argi(L, 3) - cam_x, argi(L, 4) - cam_y, argcolor(L, 5, 7));
    return 0;
}

static int l_circ(lua_State *L)
{
    drawcircle(argi(L, 1) - cam_x, argi(L, 2) - cam_y, argi(L, 3), argcolor(L, 4, 7), false);
    return 0;
}

static int l_circfill(lua_State *L)
{
    drawcircle(argi(L, 1) - cam_x, argi(L, 2) - cam_y, argi(L, 3), argcolor(L, 4, 7), true);
    return 0;
}

static int l_print(lua_State *L)
{
    int x = opti(L, 2, 0), y = opti(L, 3, 0), scale = opti(L, 5, 1);
    uint16_t color = argcolor(L, 4, 7);
    const char *text = luaL_tolstring(L, 1, NULL); // pushes a value, so read the other args first
    int end = drawtext(text, x - cam_x, y - cam_y, color, scale);
    lua_pushinteger(L, end + cam_x);
    return 1;
}

static int l_textw(lua_State *L)
{
    const char *text = luaL_checkstring(L, 1);
    int scale = opti(L, 2, 1), width = 0, line = 0;
    while (*text)
    {
        if (utf8_next(&text) == '\n')
            line = 0;
        else if (++line > width)
            width = line;
    }
    lua_pushinteger(L, width * 8 * (scale < 1 ? 1 : scale));
    return 1;
}

static int l_camera(lua_State *L)
{
    cam_x = opti(L, 1, 0);
    cam_y = opti(L, 2, 0);
    return 0;
}

static int l_clip(lua_State *L)
{
    if (lua_isnoneornil(L, 1))
    {
        clip_x0 = clip_y0 = 0;
        clip_x1 = MEL_WIDTH;
        clip_y1 = MEL_HEIGHT;
        return 0;
    }
    int x = argi(L, 1), y = argi(L, 2), w = argi(L, 3), h = argi(L, 4);
    clip_x0 = x < 0 ? 0 : x;
    clip_y0 = y < 0 ? 0 : y;
    clip_x1 = x + w > MEL_WIDTH ? MEL_WIDTH : x + w;
    clip_y1 = y + h > MEL_HEIGHT ? MEL_HEIGHT : y + h;
    return 0;
}

static int l_pal(lua_State *L)
{
    if (lua_isnoneornil(L, 1))
    {
        palette_reset();
        return 0;
    }
    int i = argi(L, 1);
    luaL_argcheck(L, i >= 0 && i < 256, 1, "palette index must be 0..255");
    palette[i] = mel_rgb565(argi(L, 2), argi(L, 3), argi(L, 4));
    return 0;
}

static int l_rgb(lua_State *L)
{
    int r = argi(L, 1), g = argi(L, 2), b = argi(L, 3);
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    lua_pushinteger(L, RGB_FLAG | (r << 16) | (g << 8) | b);
    return 1;
}

static image_t *checkimage(lua_State *L, int idx)
{
    return (image_t *)luaL_checkudata(L, idx, "mel.image");
}

static int l_loadimg(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    size_t size;
    char *data = mel_read_file(path, &size);
    if (!data)
        return luaL_error(L, "image not found: %s", path);

    unsigned char *rgba = NULL;
    unsigned w, h;
    unsigned err = lodepng_decode32(&rgba, &w, &h, (const unsigned char *)data, size);
    free(data);
    if (err)
        return luaL_error(L, "cannot decode image %s (lodepng error %d)", path, (int)err);

    image_t *img = (image_t *)lua_newuserdatauv(L, sizeof(image_t), 0);
    memset(img, 0, sizeof(*img));
    luaL_setmetatable(L, "mel.image");
    img->w = w;
    img->h = h;
    img->px = malloc(w * h * sizeof(uint16_t));
    img->alpha = malloc(w * h);
    if (!img->px || !img->alpha)
    {
        free(rgba);
        return luaL_error(L, "out of memory loading %s", path);
    }
    for (unsigned i = 0; i < w * h; i++)
    {
        const unsigned char *p = &rgba[i * 4];
        img->px[i] = mel_rgb565(p[0], p[1], p[2]);
        img->alpha[i] = p[3] >= 128;
    }
    free(rgba);
    return 1;
}

static int l_image_gc(lua_State *L)
{
    image_t *img = checkimage(L, 1);
    free(img->px), img->px = NULL;
    free(img->alpha), img->alpha = NULL;
    return 0;
}

static int l_image_index(lua_State *L)
{
    image_t *img = checkimage(L, 1);
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "w") == 0)
        lua_pushinteger(L, img->w);
    else if (strcmp(key, "h") == 0)
        lua_pushinteger(L, img->h);
    else
        lua_pushnil(L);
    return 1;
}

// spr(img, x, y, [flip_x], [flip_y])
static int l_spr(lua_State *L)
{
    image_t *img = checkimage(L, 1);
    blit(img, 0, 0, img->w, img->h, argi(L, 2) - cam_x, argi(L, 3) - cam_y, img->w, img->h,
         lua_toboolean(L, 4), lua_toboolean(L, 5));
    return 0;
}

// sspr(img, sx, sy, sw, sh, dx, dy, [dw], [dh], [flip_x], [flip_y])
static int l_sspr(lua_State *L)
{
    image_t *img = checkimage(L, 1);
    int sw = argi(L, 4), sh = argi(L, 5);
    blit(img, argi(L, 2), argi(L, 3), sw, sh, argi(L, 6) - cam_x, argi(L, 7) - cam_y,
         opti(L, 8, sw), opti(L, 9, sh), lua_toboolean(L, 10), lua_toboolean(L, 11));
    return 0;
}

// tile(img, n, x, y, [size=16], [flip_x], [flip_y]): n-th tile of a sprite sheet, row by row from 0
static int l_tile(lua_State *L)
{
    image_t *img = checkimage(L, 1);
    int n = argi(L, 2), size = opti(L, 5, 16);
    luaL_argcheck(L, size > 0, 5, "tile size must be positive");
    int per_row = img->w / size;
    if (per_row < 1 || n < 0)
        return 0;
    blit(img, (n % per_row) * size, (n / per_row) * size, size, size, argi(L, 3) - cam_x, argi(L, 4) - cam_y,
         size, size, lua_toboolean(L, 6), lua_toboolean(L, 7));
    return 0;
}

void mel_gfx_open(lua_State *L)
{
    static const luaL_Reg funcs[] = {
        {"cls", l_cls},           {"pset", l_pset},     {"pget", l_pget},         {"line", l_line},
        {"rect", l_rect},         {"rectfill", l_rectfill}, {"circ", l_circ},     {"circfill", l_circfill},
        {"print", l_print},       {"textw", l_textw},   {"camera", l_camera},     {"clip", l_clip},
        {"pal", l_pal},           {"rgb", l_rgb},       {"loadimg", l_loadimg},   {"spr", l_spr},
        {"sspr", l_sspr},         {"tile", l_tile},     {NULL, NULL},
    };
    lua_pushglobaltable(L);
    luaL_setfuncs(L, funcs, 0);
    lua_pop(L, 1);

    luaL_newmetatable(L, "mel.image");
    lua_pushcfunction(L, l_image_gc);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, l_image_index);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}
