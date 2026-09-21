#include "home_pokemon.h"
#include "cJSON.h"
#include "miniz.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

unsigned home_pokemon_id(int64_t now)
{
    return now < 1704067200 ? 0 : 1 + (unsigned)((now / 86400) % 151);
}
static const char *string(const cJSON *j, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
/* Copy complete UTF-8 sequences, collapsing Pokédex form feeds and newlines. */
static void clean(char *out, size_t cap, const char *s)
{
    size_t n = 0;
    bool space = false;
    if (!s)
        s = "";
    while (*s && n + 1 < cap) {
        unsigned char c = (unsigned char)*s;
        if (c <= 32 || c == 127) {
            space = n > 0;
            s++;
            continue;
        }
        size_t len = c < 128                  ? 1
                     : c >= 0xc2 && c <= 0xdf ? 2
                     : c >= 0xe0 && c <= 0xef ? 3
                     : c >= 0xf0 && c <= 0xf4 ? 4
                                              : 0;
        if (!len) {
            s++;
            continue;
        }
        bool valid = true;
        for (size_t k = 1; k < len; k++)
            if (!s[k] || ((unsigned char)s[k] & 0xc0) != 0x80) {
                valid = false;
                break;
            }
        if (!valid) {
            s++;
            continue;
        }
        if (n + len + (space ? 1 : 0) >= cap)
            break;
        if (space)
            out[n++] = ' ';
        memcpy(out + n, s, len);
        n += len;
        s += len;
        space = false;
    }
    out[n] = 0;
}
static const char *english(const cJSON *j, const char *array, const char *field)
{
    const cJSON *item;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(j, array))
    {
        const char *lang = string(cJSON_GetObjectItemCaseSensitive(item, "language"), "name");
        const char *value = string(item, field);
        if (lang && !strcmp(lang, "en") && value && *value)
            return value;
    }
    return NULL;
}
bool home_pokemon_parse(const char *json, size_t size, unsigned id, home_pokemon_t *out)
{
    if (!json || !out || size == 0 || size > 128 * 1024 || id < 1 || id > 151 ||
        memchr(json, 0, size))
        return false;
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(json, size, &end, false);
    if (!j)
        return false;
    while (end < json + size && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t'))
        end++;
    if (end != json + size || !cJSON_IsObject(j)) {
        cJSON_Delete(j);
        return false;
    }
    const cJSON *number = cJSON_GetObjectItemCaseSensitive(j, "id");
    const char *name = english(j, "names", "name");
    const char *intro = english(j, "flavor_text_entries", "flavor_text");
    bool ok = cJSON_IsNumber(number) && number->valuedouble == id && name && intro;
    if (ok) {
        out->id = id;
        clean(out->name, sizeof out->name, name);
        clean(out->genus, sizeof out->genus, english(j, "genera", "genus"));
        clean(out->introduction, sizeof out->introduction, intro);
        ok = out->name[0] && out->introduction[0];
    }
    cJSON_Delete(j);
    return ok;
}
static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint32_t png_crc32(const uint8_t *p, size_t n)
{
    uint32_t c = UINT32_MAX;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++)
            c = (c >> 1) ^ (0xedb88320U & (0U - (c & 1U)));
    }
    return ~c;
}
static int paeth(int a, int b, int c)
{
    int p = a + b - c, x = abs(p - a), y = abs(p - b), z = abs(p - c);
    return x <= y && x <= z ? a : y <= z ? b : c;
}
/* Flat poster colours: preserve dark ink, flatten warm shading to yellow/red,
 * and use paper for cool colours the panel cannot reproduce. No dithering. */
static unsigned pigment(unsigned r, unsigned g, unsigned b, unsigned a)
{
    if (a < 128)
        return 1;
    unsigned high = r > g ? r : g;
    if (b > high)
        high = b;
    unsigned low = r < g ? r : g;
    if (b < low)
        low = b;
    if (high < 85 || (high - low < 35 && high < 155))
        return 0;
    /* Hue rather than RGB distance keeps shaded yellow from turning red or
     * black. Saturated blues/greens remain white inside a strong silhouette. */
    if (r > b + 25 && r > g && g * 100 < r * 48)
        return 3;
    if (r > b + 25 && g > b + 20 && g * 100 >= r * 48)
        return 2;
    return 1;
}
bool home_pokemon_png(const uint8_t *png, size_t size, uint8_t out[HOME_POKEMON_BYTES])
{
    static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!png || !out || size < 33 || size > 128 * 1024 || memcmp(png, signature, 8))
        return false;
    unsigned w = 0, h = 0, depth = 0, type = 0, colors = 0, channels = 0;
    uint8_t palette[256][4];
    memset(palette, 255, sizeof palette);
    uint8_t *compressed = malloc(size), *raw = NULL;
    uint8_t *mask = calloc(96 * 96, 1);
    bool transparent = false;
    if (!compressed || !mask) {
        free(compressed);
        free(mask);
        return false;
    }
    size_t used = 0;
    bool ended = false, ok = false, header = false, data = false;
    for (size_t pos = 8; pos + 12 <= size;) {
        uint32_t n = be32(png + pos);
        const uint8_t *tag = png + pos + 4, *p = tag + 4;
        if (n > size - pos - 12 || png_crc32(tag, n + 4) != be32(p + n))
            goto done;
        if (!header && memcmp(tag, "IHDR", 4))
            goto done;
        if (!memcmp(tag, "IHDR", 4)) {
            if (header || n != 13)
                goto done;
            header = true;
            w = be32(p);
            h = be32(p + 4);
            depth = p[8];
            type = p[9];
            if (!w || !h || w > 96 || h > 96 || p[10] || p[11] || p[12])
                goto done;
            if (type == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8))
                channels = 1;
            else if (depth == 8 && (type == 0 || type == 2 || type == 4 || type == 6))
                channels = type == 0 ? 1 : type == 2 ? 3 : type == 4 ? 2 : 4;
            else
                goto done;
        } else if (!memcmp(tag, "PLTE", 4)) {
            if (data || colors || !n || n % 3 || n > 768)
                goto done;
            colors = n / 3;
            for (unsigned i = 0; i < colors; i++)
                memcpy(palette[i], p + 3 * i, 3);
        } else if (!memcmp(tag, "tRNS", 4)) {
            /* Reject unsupported colour-key transparency rather than drawing it wrong. */
            if (data || type != 3 || !colors || n > colors)
                goto done;
            for (unsigned i = 0; i < n; i++)
                palette[i][3] = p[i];
        } else if (!memcmp(tag, "IDAT", 4)) {
            if (type == 3 && !colors)
                goto done;
            memcpy(compressed + used, p, n);
            used += n;
            data = true;
        } else if (!memcmp(tag, "IEND", 4)) {
            if (n || !data || pos + 12 != size)
                goto done;
            ended = true;
            break;
        } else if (!(tag[0] & 32))
            goto done; /* Unknown critical chunk. */
        pos += n + 12;
    }
    if (!ended || !used)
        goto done;
    size_t stride = (w * channels * depth + 7) / 8, length = (stride + 1) * h;
    unsigned bpp = (channels * depth + 7) / 8;
    raw = malloc(length);
    if (!raw || tinfl_decompress_mem_to_mem(raw, length, compressed, used,
                                            TINFL_FLAG_PARSE_ZLIB_HEADER) != length)
        goto done;
    for (unsigned y = 0; y < h; y++) {
        uint8_t *row = raw + y * (stride + 1) + 1;
        unsigned filter = row[-1];
        if (filter > 4)
            goto done;
        for (size_t x = 0; x < stride; x++) {
            int a = x >= bpp ? row[x - bpp] : 0, b = y ? (row - stride - 1)[x] : 0;
            int c = y && x >= bpp ? (row - stride - 1)[x - bpp] : 0;
            row[x] += (uint8_t)(filter == 1   ? a
                                : filter == 2 ? b
                                : filter == 3 ? (a + b) / 2
                                : filter == 4 ? paeth(a, b, c)
                                              : 0);
        }
    }
    memset(out, 0x55, HOME_POKEMON_BYTES);
    for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x++) {
            const uint8_t *row = raw + y * (stride + 1) + 1,
                          *p = type == 3 ? row : row + x * channels;
            unsigned r, g, b, a = 255;
            if (type == 3) {
                unsigned index =
                    (row[x * depth / 8] >> (8 - depth - (x * depth % 8))) & ((1U << depth) - 1);
                if (index >= colors)
                    goto done;
                r = palette[index][0];
                g = palette[index][1];
                b = palette[index][2];
                a = palette[index][3];
            } else if (type == 0 || type == 4) {
                r = g = b = p[0];
                if (type == 4)
                    a = p[1];
            } else {
                r = p[0];
                g = p[1];
                b = p[2];
                if (type == 6)
                    a = p[3];
            }
            unsigned at = (y + (96 - h) / 2) * 96 + x + (96 - w) / 2, shift = 6 - 2 * (at % 4);
            mask[at] = a >= 128;
            transparent |= a < 128;
            out[at / 4] = (out[at / 4] & ~(3U << shift)) | (pigment(r, g, b, a) << shift);
        }
    /* Draw a one-pixel outer contour from alpha, never from colour: white
     * body parts stay solid and cannot disappear into the paper background.
     * Read only the original mask so the outline cannot grow recursively. */
    if (transparent) {
        for (unsigned y = 0; y < 96; y++)
            for (unsigned x = 0; x < 96; x++) {
                unsigned at = y * 96 + x;
                if (!mask[at] && ((x && mask[at - 1]) || (x < 95 && mask[at + 1]) ||
                                  (y && mask[at - 96]) || (y < 95 && mask[at + 96])))
                    out[at / 4] &= ~(3U << (6 - 2 * (at % 4)));
            }
    }
    ok = true;
done:
    free(compressed);
    free(raw);
    free(mask);
    return ok;
}
