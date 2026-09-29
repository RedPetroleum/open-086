// Sound API: a small mixer with MEL_CHANNELS voices, synthesized tones and WAV samples.
#define MEL_INTERNAL
#include "meloni.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define AUTO_CHANNELS 6     // tone()/play() without a channel pick from 0..5; 6 and 7 are left for tunes
#define VOICE_AMPLITUDE 12288 // full volume of one voice; mixes of more than ~3 loud voices clip
#define FADE_SAMPLES 64     // short ramps at start and end avoid clicks

enum { WAVE_SQUARE, WAVE_PULSE, WAVE_TRIANGLE, WAVE_SAW, WAVE_SINE, WAVE_NOISE };

typedef struct
{
    int16_t *data;
    int length;
} sound_t;

typedef struct
{
    bool active;
    bool is_sample;
    int wave;
    uint32_t phase, phase_inc;
    uint32_t noise;
    int16_t noise_value;
    int volume;            // 0..256
    int elapsed, remaining; // samples, remaining < 0 = until stopped (looping samples)
    const sound_t *sound;
    int pos;
    bool loop;
    uint32_t age;
} voice_t;

static voice_t voices[MEL_CHANNELS];
static int16_t sine_table[256];
static uint32_t voice_counter;

void mel_audio_reset(void)
{
    memset(voices, 0, sizeof(voices));
    for (int i = 0; i < 256; i++)
        sine_table[i] = (int16_t)(sinf(i * 2.0f * 3.14159265f / 256) * 32767);
}

static int16_t wave_sample(voice_t *v)
{
    uint32_t p = v->phase;
    switch (v->wave)
    {
    case WAVE_PULSE:    return p < 0x40000000u ? 32767 : -32767;
    case WAVE_TRIANGLE: return (int16_t)(p < 0x80000000u ? (int32_t)(p >> 15) - 32768 : 32767 - (int32_t)((p - 0x80000000u) >> 15));
    case WAVE_SAW:      return (int16_t)((int32_t)(p >> 16) - 32768);
    case WAVE_SINE:     return sine_table[p >> 24];
    case WAVE_NOISE:    return v->noise_value;
    default:            return p < 0x80000000u ? 32767 : -32767;
    }
}

void mel_audio_mix(int16_t *out, int frames)
{
    for (int i = 0; i < frames; i++)
    {
        int32_t mix = 0;
        for (int c = 0; c < MEL_CHANNELS; c++)
        {
            voice_t *v = &voices[c];
            if (!v->active)
                continue;

            int32_t s;
            if (v->is_sample)
            {
                if (v->pos >= v->sound->length)
                {
                    if (!v->loop || v->sound->length == 0)
                    {
                        v->active = false;
                        continue;
                    }
                    v->pos = 0;
                }
                s = v->sound->data[v->pos++];
            }
            else
            {
                s = wave_sample(v);
                uint32_t prev = v->phase;
                v->phase += v->phase_inc;
                if (v->wave == WAVE_NOISE && v->phase < prev)
                {
                    // 16-bit Galois LFSR, a new random level on every period
                    v->noise = (v->noise >> 1) ^ (-(v->noise & 1u) & 0xB400u);
                    v->noise_value = (v->noise & 1) ? 32767 : -32767;
                }
                // Envelope: fade in and fade out
                int gain = 256;
                if (v->elapsed < FADE_SAMPLES)
                    gain = v->elapsed * 256 / FADE_SAMPLES;
                if (v->remaining >= 0 && v->remaining < FADE_SAMPLES)
                    gain = gain * v->remaining / FADE_SAMPLES;
                s = s * gain / 256;
                v->elapsed++;
                if (v->remaining >= 0 && --v->remaining <= 0)
                    v->active = false;
            }
            mix += s * v->volume / 256 * VOICE_AMPLITUDE / 32768;
        }
        if (mix > 32767) mix = 32767;
        if (mix < -32768) mix = -32768;
        out[i * 2] = out[i * 2 + 1] = (int16_t)mix;
    }
}

// ---- Lua bindings ----

static int pick_channel(lua_State *L, int idx)
{
    if (!lua_isnoneornil(L, idx))
    {
        int ch = (int)luaL_checkinteger(L, idx);
        luaL_argcheck(L, ch >= 0 && ch < MEL_CHANNELS, idx, "channel must be 0..7");
        return ch;
    }
    int oldest = 0;
    for (int c = 0; c < AUTO_CHANNELS; c++)
    {
        if (!voices[c].active)
            return c;
        if (voices[c].age < voices[oldest].age)
            oldest = c;
    }
    return oldest;
}

static int volume_arg(lua_State *L, int idx)
{
    float vol = (float)luaL_optnumber(L, idx, 0.5);
    vol = vol < 0 ? 0 : vol > 1 ? 1 : vol;
    return (int)(vol * 256);
}

// tone(freq, [dur=0.2], [wave="square"], [vol=0.5], [ch]) -> ch
static int l_tone(lua_State *L)
{
    static const char *const waves[] = {"square", "pulse", "triangle", "saw", "sine", "noise", NULL};
    float freq = (float)luaL_checknumber(L, 1);
    float dur = (float)luaL_optnumber(L, 2, 0.2);
    int wave = luaL_checkoption(L, 3, "square", waves);
    int volume = volume_arg(L, 4);
    int ch = pick_channel(L, 5);

    voice_t *v = &voices[ch];
    memset(v, 0, sizeof(*v));
    v->active = freq > 0 && dur > 0;
    v->wave = wave;
    v->phase_inc = (uint32_t)(freq / MEL_SAMPLE_RATE * 4294967296.0f);
    v->noise = 0xACE1u;
    v->volume = volume;
    v->remaining = (int)(dur * MEL_SAMPLE_RATE);
    v->age = ++voice_counter;
    lua_pushinteger(L, ch);
    return 1;
}

static sound_t *checksound(lua_State *L, int idx)
{
    return (sound_t *)luaL_checkudata(L, idx, "mel.sound");
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

// loadsound(path): PCM WAV, 8 or 16 bit, mono or stereo, any rate (resampled to MEL_SAMPLE_RATE)
static int l_loadsound(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    size_t size;
    uint8_t *data = (uint8_t *)mel_read_file(path, &size);
    if (!data)
        return luaL_error(L, "sound not found: %s", path);

    int channels = 0, rate = 0, bits = 0;
    const uint8_t *pcm = NULL;
    size_t pcm_len = 0;
    if (size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WAVE", 4) == 0)
    {
        size_t pos = 12;
        while (pos + 8 <= size)
        {
            uint32_t len = rd32(data + pos + 4);
            const uint8_t *chunk = data + pos + 8;
            if (len > size - pos - 8)
                len = size - pos - 8;
            if (memcmp(data + pos, "fmt ", 4) == 0 && len >= 16 && rd16(chunk) == 1)
            {
                channels = rd16(chunk + 2);
                rate = rd32(chunk + 4);
                bits = rd16(chunk + 14);
            }
            else if (memcmp(data + pos, "data", 4) == 0)
            {
                pcm = chunk;
                pcm_len = len;
            }
            pos += 8 + len + (len & 1);
        }
    }
    if (!pcm || channels < 1 || channels > 2 || rate <= 0 || (bits != 8 && bits != 16))
    {
        free(data);
        return luaL_error(L, "%s: only uncompressed PCM WAV (8/16 bit, mono/stereo) is supported", path);
    }

    int frame_bytes = channels * bits / 8;
    int in_frames = (int)(pcm_len / frame_bytes);
    int out_frames = (int)((int64_t)in_frames * MEL_SAMPLE_RATE / rate);

    sound_t *snd = (sound_t *)lua_newuserdatauv(L, sizeof(sound_t), 0);
    snd->data = NULL;
    snd->length = 0;
    luaL_setmetatable(L, "mel.sound");
    snd->data = malloc((out_frames + 1) * sizeof(int16_t));
    if (!snd->data)
    {
        free(data);
        return luaL_error(L, "out of memory loading %s", path);
    }
    for (int i = 0; i < out_frames; i++)
    {
        int src = (int)((int64_t)i * rate / MEL_SAMPLE_RATE);
        const uint8_t *f = pcm + src * frame_bytes;
        int32_t s = 0;
        for (int c = 0; c < channels; c++)
            s += bits == 8 ? ((int)f[c] - 128) << 8 : (int16_t)rd16(f + c * 2);
        snd->data[i] = (int16_t)(s / channels);
    }
    snd->length = out_frames;
    free(data);
    return 1;
}

static int l_sound_gc(lua_State *L)
{
    sound_t *snd = checksound(L, 1);
    for (int c = 0; c < MEL_CHANNELS; c++)
        if (voices[c].sound == snd)
            voices[c].active = false, voices[c].sound = NULL;
    free(snd->data);
    snd->data = NULL;
    snd->length = 0;
    return 0;
}

// play(snd, [vol=0.5], [loop=false], [ch]) -> ch
static int l_play(lua_State *L)
{
    sound_t *snd = checksound(L, 1);
    int volume = volume_arg(L, 2);
    bool loop = lua_toboolean(L, 3);
    int ch = pick_channel(L, 4);

    voice_t *v = &voices[ch];
    memset(v, 0, sizeof(*v));
    v->active = snd->length > 0;
    v->is_sample = true;
    v->sound = snd;
    v->loop = loop;
    v->volume = volume;
    v->remaining = -1;
    v->age = ++voice_counter;
    // Keep the sound alive while it plays: registry[voice] = snd
    lua_pushvalue(L, 1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, v);
    lua_pushinteger(L, ch);
    return 1;
}

// stop([ch]): stops one channel or all
static int l_stop(lua_State *L)
{
    int first = 0, last = MEL_CHANNELS - 1;
    if (!lua_isnoneornil(L, 1))
    {
        first = last = (int)luaL_checkinteger(L, 1);
        luaL_argcheck(L, first >= 0 && first < MEL_CHANNELS, 1, "channel must be 0..7");
    }
    for (int c = first; c <= last; c++)
    {
        voices[c].active = false;
        voices[c].sound = NULL;
        lua_pushnil(L);
        lua_rawsetp(L, LUA_REGISTRYINDEX, &voices[c]);
    }
    return 0;
}

// playing([ch]) -> bool
static int l_playing(lua_State *L)
{
    if (lua_isnoneornil(L, 1))
    {
        bool any = false;
        for (int c = 0; c < MEL_CHANNELS; c++)
            any |= voices[c].active;
        lua_pushboolean(L, any);
        return 1;
    }
    int ch = (int)luaL_checkinteger(L, 1);
    luaL_argcheck(L, ch >= 0 && ch < MEL_CHANNELS, 1, "channel must be 0..7");
    lua_pushboolean(L, voices[ch].active);
    return 1;
}

void mel_audio_open(lua_State *L)
{
    static const luaL_Reg funcs[] = {
        {"tone", l_tone}, {"loadsound", l_loadsound}, {"play", l_play},
        {"stop", l_stop}, {"playing", l_playing},     {NULL, NULL},
    };
    lua_pushglobaltable(L);
    luaL_setfuncs(L, funcs, 0);
    lua_pop(L, 1);

    luaL_newmetatable(L, "mel.sound");
    lua_pushcfunction(L, l_sound_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);
}
