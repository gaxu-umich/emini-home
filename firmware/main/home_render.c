/* Home native scene renderer. Original Home implementation, 2026.
 * Exactly one pigment per pixel; generated Atkinson masks retain OFL notice.
 * Pure, reentrant C: no heap, I/O, global mutable state or device dependency. */
#include "home_types.h"
#include "home_places.h"
#include "home_air.h"
#include "home_parse.h"
#include "home_qr.h"
#include "home_sky.h"
#include "generated/home_font.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { BLACK = 0, PAPER = 1, YELLOW = 2, RED = 3, W = 400, H = 300 };
#ifndef HOME_VERSION_TEXT
#define HOME_VERSION_TEXT "0.5.5-weather-news"
#endif
/* Brushes (D-HOME-CC-23, narrowed by D-HOME-CC-25): the user picks the tone structure in the
 * panel. The line-based screens (engraving, cross-hatch) were dropped after the device test:
 * on narrow strips and thin bars they read as broken stripes, not as texture. */
enum { RASTER_NOISE = 0, RASTER_DOTS = 1, RASTER_GRID = 2 };
typedef struct {
    uint8_t *frame;
    int cell, intensity;
    int lang; /* LANG_*: chooses the words, and the Polish one-letter line-break rule. */
    int raster; /* tone structure in force: grain, or a screen of dots/lines/cross/grid */
    int brush;  /* the user's brush (RASTER_*), painted on the large fields only */
} canvas_t;
#include "generated/home_noise.h"
/* Tones are ordered dither: through a 64x64 blue-noise mask by default (Renderer 2, 0.5.0: no
 * grid, no banding on ramps), or through one of the brushes below; the 4x4 Bayer matrix of
 * 0.3.1-0.4.4 stays as the "grid" brush. */
static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static int imin(int a, int b)
{
    return a < b ? a : b;
}
static int imax(int a, int b)
{
    return a > b ? a : b;
}
static double clamp(double v, double a, double b)
{
    return !isfinite(v) ? a : v < a ? a : v > b ? b : v;
}
static float clampf(float v, float a, float b)
{
    return !isfinite(v) ? a : v < a ? a : v > b ? b : v;
}
/* Three display languages. English is the fallback: a missing translation shows English
 * words rather than boxes or an empty line. */
enum { LANG_EN = 0, LANG_PL = 1, LANG_ZH = 2 };
int home_language(const char *locale)
{
    if (!locale)
        return LANG_EN;
    if (locale[0] == 'p' && locale[1] == 'l')
        return LANG_PL;
    if (locale[0] == 'z' && locale[1] == 'h')
        return LANG_ZH;
    return LANG_EN;
}
static int lang_of(const home_config_t *c)
{
    return home_language(c->locale);
}
/* Chinese is kept as a dictionary from the English wording instead of a third argument at
 * every call: the screens stay readable, the whole translation can be reviewed in one place,
 * and the next language is a second table. Sorted by the English text, binary search. */
typedef struct {
    const char *en, *zh;
} phrase_t;
static const phrase_t chinese[] = {
    {" · sunscreen from %s", " · %s 起涂防晒"},
    {"%lu d %lu h", "%lu 天 %lu 小时"},
    {"%s for %d h", "%s 持续 %d 小时"},
    {"%s from %s", "%s 从 %s 起"},
    {"%s until %s", "%s 到 %s"},
    {"1  JOIN THIS WI-FI NETWORK", "1  用手机连接此 WI-FI"},
    {"1  JOIN WI-FI", "1  连接 WI-FI"},
    {"2  OPEN HOME", "2  打开 HOME"},
    {"2  OPEN THIS ADDRESS IN YOUR BROWSER", "2  在浏览器中打开此地址"},
    {"24h", "24h"},
    {"3  Pairing code", "3  配对码"},
    {"A forecast for your place.", "你所在地的天气预报。"},
    {"A little room for the world.", "留一点空间给世界。"},
    {"A sky for your place.", "你所在地的天空。"},
    {"AIR", "空气"},
    {"AIR QUALITY", "空气质量"},
    {"AWAKE", "运行"},
    {"About %d days", "约 %d 天"},
    {"About %d h", "约 %d 小时"},
    {"Age unknown · check time", "时间未知 · 请检查时钟"},
    {"Air", "空气"},
    {"BATTERY", "电池"},
    {"BATTERY · LAST SEVEN DAYS", "电池 · 最近七天"},
    {"Cannot load data · check the phone panel", "无法获取数据 · 请查看手机面板"},
    {"Charged", "已充满"},
    {"Charging", "充电中"},
    {"Checked %lld days ago", "%lld 天前查询"},
    {"Checked %lld h ago", "%lld 小时前查询"},
    {"Checked just now", "刚刚查询"},
    {"Checked within the hour", "一小时内查询过"},
    {"Choose a screen.", "选择一个画面。"},
    {"Choose an RSS or Atom source in your phone panel. One story, without a stream to chase.",
     "在手机面板里选择 RSS 或 Atom 源。只看一条消息，不必追着信息流跑。"},
    {"Clear skies", "晴"},
    {"Cloud cover", "多云"},
    {"Computed on the device · nothing downloaded", "在设备上算出 · 不下载数据"},
    {"Connect your phone.", "连接你的手机。"},
    {"Connection failed · saved data", "连接失败 · 使用已存数据"},
    {"DOWNLOADS", "下载次数"},
    {"DRAWN IN", "绘制用时"},
    {"Date unknown", "日期未知"},
    {"Dry until %s", "%s 前无降水"},
    {"EU index", "欧盟指数"},
    {"FORECAST", "预报"},
    {"First quarter", "上弦月"},
    {"Fog", "雾"},
    {"Full in %d days", "%d 天后满月"},
    {"Full moon", "满月"},
    {"Full story in phone panel", "全文见手机面板"},
    {"Full today", "今天满月"},
    {"Help · emini.ink/home", "帮助 · emini.ink/home"},
    {"Home reads the clock from the internet, and the sun and the moon appear here as soon as it has one.",
     "Home 从网络获取时间，一旦有了时间，日月就会出现在这里。"},
    {"Home, meet your phone.", "Home，认识一下你的手机。"},
    {"Hourly detail unavailable", "无逐小时预报"},
    {"Last quarter", "下弦月"},
    {"Learning how long a charge lasts", "正在学习一次充电能用多久"},
    {"Lit %s%%", "照亮 %s%%"},
    {"Make this space yours.", "这块地方留给你。"},
    {"Midnight sun", "极昼"},
    {"Mostly clear", "大致晴朗"},
    {"NETWORK PASSWORD", "网络密码"},
    {"NEXT 24 H · PM2.5, UV IN YELLOW", "未来 24 小时 · PM2.5，紫外线为黄色"},
    {"NEXT HOURS · °%s / mm", "未来几小时 · °%s / 毫米"},
    {"NOW", "现在"},
    {"New in %d days", "%d 天后新月"},
    {"New moon", "新月"},
    {"New today", "今天新月"},
    {"No index", "无指数"},
    {"No pollen forecast for this place", "此地无花粉预报"},
    {"No range", "无高低温"},
    {"ONE STORY", "一条消息"},
    {"Older data · waiting for update", "数据较旧 · 等待更新"},
    {"Open the panel on your phone and choose what Home shows.", "打开手机面板，选择 Home 显示的内容。"},
    {"Open the phone panel and use your location. Air quality, UV and pollen from Open-Meteo will appear here within the hour.",
     "打开手机面板并使用你的位置。来自 Open-Meteo 的空气质量、紫外线和花粉会在一小时内出现。"},
    {"Open the phone panel and use your location. The first forecast will appear here.",
     "打开手机面板并使用你的位置。第一份预报会出现在这里。"},
    {"Open the phone panel and use your location. The sun and the moon are then worked out here, with nothing downloaded.",
     "打开手机面板并使用你的位置。日月将在设备上算出，不下载任何数据。"},
    {"PICTURES DRAWN", "已绘制画面"},
    {"PM2.5 in µg per m3", "PM2.5 微克每立方米"},
    {"Partly cloudy", "局部多云"},
    {"Password", "密码"},
    {"Polar night", "极夜"},
    {"Rain ahead", "有雨"},
    {"SKY", "天空"},
    {"Sky needs the time.", "天空需要时间。"},
    {"Sleet", "雨夹雪"},
    {"Snow", "雪"},
    {"Sunrise", "日出"},
    {"Sunrise %s · Sunset %s", "日出 %s · 日落 %s"},
    {"Sunrise and sunset unknown", "日出日落未知"},
    {"Sunset", "日落"},
    {"TODAY", "今天"},
    {"The air, at a glance.", "一眼看懂空气。"},
    {"The sun does not rise today", "今天太阳不升"},
    {"The sun does not set today", "今天太阳不落"},
    {"This screen arrives with the next update.", "这个画面会在下次更新时出现。"},
    {"Thunderstorms", "雷雨"},
    {"Waning crescent", "残月"},
    {"Waning gibbous", "亏凸月"},
    {"Waxing crescent", "蛾眉月"},
    {"Waxing gibbous", "盈凸月"},
    {"Weather", "天气"},
    {"Weather forecast", "天气预报"},
    {"Wind %s m/s", "风 %s 米/秒"},
    {"Wind —", "风 —"},
    {"With you for %lld days", "陪伴你 %lld 天"},
    {"Write a message in your phone panel. A reminder, a thought, something worth keeping in view.",
     "在手机面板里写一段话。提醒、想法，或者值得留在眼前的东西。"},
    {"YOUR NOTE", "你的便笺"},
    {"Your source", "你的信息源"},
    {"Yours to keep in view.", "留在眼前的话。"},
    {"day %d h %02d min", "昼长 %d 小时 %02d 分"},
    {"day %d h %02d min (%s%s min)", "昼长 %d 小时 %02d 分（%s%s 分）"},
    {"daylight all day", "全天有光"},
    {"no daylight today", "今天没有日光"},
    {"sunscreen now", "现在涂防晒"},
};
/* Checked by the harness: every English phrase above is one the screens really pass to
 * tr(), and every Chinese character is in the font the device carries (GB 2312). */
static const char *chinese_for(const char *en)
{
    size_t low = 0, high = sizeof chinese / sizeof chinese[0];
    while (low < high) {
        size_t mid = (low + high) / 2;
        int d = strcmp(en, chinese[mid].en);
        if (!d)
            return chinese[mid].zh[0] ? chinese[mid].zh : NULL;
        if (d < 0)
            high = mid;
        else
            low = mid + 1;
    }
    return NULL;
}
static const char *tr(int lang, const char *en, const char *pol)
{
    if (lang == LANG_ZH) {
        const char *zh = chinese_for(en);
        return zh ? zh : en;
    }
    return lang == LANG_PL ? pol : en;
}
/* One of three word lists, for the arrays that name months, levels and phases. */
static const char *const *words(int lang, const char *const *en, const char *const *pol,
                                const char *const *zh)
{
    return lang == LANG_ZH ? zh : lang == LANG_PL ? pol : en;
}
static void pixel(canvas_t *c, int x, int y, int p)
{
    if ((unsigned)x >= W || (unsigned)y >= H)
        return;
    if (c->intensity == 0) {
        if (p == YELLOW)
            p = PAPER;
        else if (p == RED)
            p = BLACK;
    }
    unsigned i = (unsigned)y * 100u + (unsigned)x / 4u, shift = 6u - ((unsigned)x % 4u) * 2u;
    c->frame[i] = (uint8_t)((c->frame[i] & ~(3u << shift)) | ((unsigned)p << shift));
}
static void rect(canvas_t *c, int x, int y, int w, int h, int p)
{
    for (int yy = imax(y, 0); yy < imin(y + h, H); ++yy)
        for (int xx = imax(x, 0); xx < imin(x + w, W); ++xx)
            pixel(c, xx, yy, p);
}
/* Tone threshold in 0..1 at (x, y) for ink pigment `ink`, in cell space (x, y >> shift), so a
 * screen for colour is never finer than 2 px. Blue noise by default; the structured screens
 * (Renderer 2, 0.4) give each pigment its own angle (yellow 15, red 75, black 45 degrees) so
 * that pigments meeting on one field do not moire. Tone = line thickness or dot size. */
static float threshold(const canvas_t *c, int x, int y, int ink, int shift)
{
    int xs = x >> shift, ys = y >> shift;
    if (c->raster == RASTER_GRID)
        return (bayer[ys & 3][xs & 3] + 0.5f) * 0.0625f;
    if (c->raster == RASTER_NOISE)
        return (home_noise[(ys & 63) * 64 + (xs & 63)] + 0.5f) * (1.0f / 256.0f);
    /* Halftone: dots on a 6-cell grid (12 px for colour), each pigment at its own angle so
     * two of them meeting on one field do not moire. Tone = the area of the dot. */
    static const float angles[3][2] = {{0.258819f, 0.965926f}, {0.965926f, 0.258819f},
                                       {0.707107f, 0.707107f}};
    const float *ang = angles[ink == YELLOW ? 0 : ink == RED ? 1 : 2];
    const float inv = 1.0f / 6.0f;
    float u = (xs * ang[1] + ys * ang[0]) * inv, v = (-xs * ang[0] + ys * ang[1]) * inv;
    float du = (u - floorf(u)) - 0.5f, dv = (v - floorf(v)) - 0.5f;
    return (du * du + dv * dv) * 3.14159265f;
}
static int mix(canvas_t *c, int x, int y, int a, int b, float coverage)
{
    /* cell is 1/2/4; shifting avoids two runtime divisions per pigment pixel. */
    int shift = c->cell >> 1;
    if (c->intensity == 0) {
        if (a == YELLOW)
            a = PAPER;
        if (a == RED)
            a = BLACK;
        if (b == YELLOW)
            b = PAPER;
        if (b == RED)
            b = BLACK;
    }
    /* Colour is never finer than 2 px; black-and-paper patterns keep 1 px. */
    if (shift == 0 && (a == YELLOW || a == RED || b == YELLOW || b == RED))
        shift = 1;
    if (c->intensity == 1)
        coverage *= 0.55f;
    return coverage > threshold(c, x, y, b, shift) ? b : a;
}
/* Three pigments in one point (Renderer 2): shares wa, wb, wd of pigments a, b, d (any sum > 0;
 * they are normalised). One mask threshold picks the pigment by interval, so every share is
 * monotonic in its weight. Same cell and intensity rules as mix(): colour never finer than 2 px;
 * intensity 0 folds colour into black and paper; intensity 1 keeps 55 % of the colour shares. */
static int mix3(canvas_t *c, int x, int y, int a, int b, int d, float wa, float wb, float wd)
{
    int shift = c->cell >> 1;
    if (c->intensity == 0) {
        a = a == YELLOW ? PAPER : a == RED ? BLACK : a;
        b = b == YELLOW ? PAPER : b == RED ? BLACK : b;
        d = d == YELLOW ? PAPER : d == RED ? BLACK : d;
    }
    if (shift == 0 && (a == YELLOW || a == RED || b == YELLOW || b == RED || d == YELLOW || d == RED))
        shift = 1;
    if (c->intensity == 1) { /* less colour: every colour share, whichever slot holds it */
        if (a == YELLOW || a == RED)
            wa *= 0.55f;
        if (b == YELLOW || b == RED)
            wb *= 0.55f;
        if (d == YELLOW || d == RED)
            wd *= 0.55f;
    }
    wa = wa < 0 ? 0 : wa;
    wb = wb < 0 ? 0 : wb;
    wd = wd < 0 ? 0 : wd;
    float sum = wa + wb + wd;
    if (sum <= 0)
        return a;
    float t = threshold(c, x, y, wd >= wb ? d : b, shift) * sum;
    return t < wa ? a : t < wa + wb ? b : d;
}
static void line(canvas_t *c, int x0, int y0, int x1, int y1, int p)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1,
        err = dx + dy;
    for (;;) {
        pixel(c, x0, y0, p);
        if (x0 == x1 && y0 == y1)
            break;
        int e = 2 * err;
        if (e >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}
static size_t bounded(const char *s, size_t cap)
{
    size_t n = 0;
    if (s)
        while (n < cap && s[n])
            ++n;
    return n;
}
static bool contains(const char *s, size_t cap, const char *needle)
{
    size_t n = bounded(s, cap), m = strlen(needle);
    if (m > n)
        return false;
    for (size_t i = 0; i <= n - m; ++i)
        if (!memcmp(s + i, needle, m))
            return true;
    return false;
}
static bool time_valid(int64_t t)
{
    return t >= 1577836800LL && t <= 4102444800LL;
}
/* Consume malformed UTF-8 one byte at a time; never read beyond the span. */
static uint32_t next_cp(const char *s, size_t n, size_t *at)
{
    if (*at >= n)
        return 0;
    uint8_t a = (uint8_t)s[(*at)++];
    if (a < 0x80)
        return a;
    unsigned count = a >= 0xc2 && a <= 0xdf   ? 1
                     : a >= 0xe0 && a <= 0xef ? 2
                     : a >= 0xf0 && a <= 0xf4 ? 3
                                              : 0;
    if (!count || n - *at < count)
        return '?';
    uint32_t cp = a & ((1u << (6 - count)) - 1u);
    size_t start = *at;
    for (unsigned k = 0; k < count; ++k) {
        uint8_t b = (uint8_t)s[start + k];
        if ((b & 0xc0) != 0x80)
            return '?';
        cp = (cp << 6) | (b & 63);
    }
    if ((count == 1 && cp < 0x80) || (count == 2 && cp < 0x800) || (count == 3 && cp < 0x10000) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        return '?';
    *at += count;
    return cp;
}
static const home_glyph_t *find_glyph(const home_font_t *f, uint32_t cp)
{
    unsigned lo = f->first, hi = lo + f->count;
    while (lo < hi) {
        unsigned m = lo + (hi - lo) / 2;
        if (home_glyphs[m].codepoint < cp)
            lo = m + 1;
        else
            hi = m;
    }
    if (lo < (unsigned)f->first + f->count && home_glyphs[lo].codepoint == cp)
        return &home_glyphs[lo];
    return NULL;
}
/* Han, kana, CJK punctuation and full-width forms: no spaces, so each character is a
 * line-break opportunity (poster text), and every glyph comes from the CJK tables. */
static bool cjk(uint32_t cp)
{
    return (cp >= 0x2e80 && cp <= 0x9fff) || (cp >= 0xf900 && cp <= 0xfaff) ||
           (cp >= 0xff00 && cp <= 0xffef);
}
/* Kinsoku: closing punctuation never starts a line, opening never ends one. */
static bool cjk_no_start(uint32_t cp)
{
    return cp == 0x3001 || cp == 0x3002 || cp == 0xff0c || cp == 0xff0e || cp == 0xff01 ||
           cp == 0xff1f || cp == 0xff1a || cp == 0xff1b || cp == 0x300d || cp == 0x300f ||
           cp == 0x3011 || cp == 0x3015 || cp == 0xff09 || cp == 0x300b || cp == 0x3009 ||
           cp == 0x2026 || cp == 0x2014;
}
static bool cjk_no_end(uint32_t cp)
{
    return cp == 0x300c || cp == 0x300e || cp == 0x3010 || cp == 0x3014 || cp == 0xff08 ||
           cp == 0x300a || cp == 0x3008;
}
/* The CJK table of the same pixel size as Atkinson font fi, if there is one. */
static const home_glyph_t *cjk_glyph(int fi, uint32_t cp)
{
    if (!cjk(cp))
        return NULL;
    for (unsigned i = 0; i < sizeof home_cjk_fonts / sizeof *home_cjk_fonts; ++i)
        if (home_cjk_fonts[i].size == home_fonts[fi].size)
            return find_glyph(&home_cjk_fonts[i], cp);
    return NULL;
}
static bool has_glyph(int fi, uint32_t cp)
{
    return cjk(cp) ? cjk_glyph(fi, cp) != NULL : find_glyph(&home_fonts[fi], cp) != NULL;
}
static const home_glyph_t *glyph(int fi, uint32_t cp)
{
    const home_glyph_t *g = cjk(cp) ? cjk_glyph(fi, cp) : find_glyph(&home_fonts[fi], cp);
    return g ? g : &home_glyphs[home_fonts[fi].first + ('?' - 32)];
}
static int width(int fi, const char *s, size_t cap)
{
    size_t at = 0, n = bounded(s, cap);
    int w = 0;
    while (at < n)
        w += glyph(fi, next_cp(s, n, &at))->advance;
    return w;
}
static void draw_glyph(canvas_t *c, int x, int baseline, int fi, uint32_t cp, int p, int bx, int by,
                       int bw, int bh)
{
    const home_glyph_t *g = glyph(fi, cp);
    int stride = (g->width + 7) / 8;
    for (int y = 0; y < g->height; ++y)
        for (int xx = 0; xx < g->width; ++xx) {
            int px = x + g->left + xx, py = baseline + g->top + y;
            if (px < bx || px >= bx + bw || py < by || py >= by + bh)
                continue;
            if (home_font_bits[g->offset + y * stride + xx / 8] & (0x80 >> (xx & 7)))
                pixel(c, px, py, p);
        }
}
/* True when cp[end] is the space after a one-letter Polish word (a i o u w z, any
 * case). Such a space is not a line break unless the line has no other space. */
static bool one_letter_word(const uint32_t *cp, int end)
{
    if (end < 1 || (end >= 2 && cp[end - 2] != ' '))
        return false;
    uint32_t ch = cp[end - 1] | 0x20u;
    return ch == 'a' || ch == 'i' || ch == 'o' || ch == 'u' || ch == 'w' || ch == 'z';
}
/* Word wrapping also breaks unspaced identifiers. Last line has a real glyph
 * ellipsis; all ink is bounded to its text region, including negative bearings. */
static void text(canvas_t *c, int x, int y, int w, int h, int fi, int p, const char *s, size_t cap)
{
    if (!s || w <= 0 || h <= 0)
        return;
    size_t n = bounded(s, cap), at = 0;
    int step = home_fonts[fi].size + 4, rows = imax(1, h / step);
    for (int row = 0; row < rows && at < n; ++row) {
        uint32_t cp[128];
        int count = 0, used = 0, space = -1, kept = -1;
        size_t space_at = 0, kept_at = 0;
        while (at < n && s[at] == ' ')
            ++at;
        size_t line_start = at;
        while (at < n && count < 128) {
            size_t before = at;
            uint32_t ch = next_cp(s, n, &at);
            if (ch == '\r')
                continue;
            if (ch == '\n')
                break;
            if (ch == '\t')
                ch = ' ';
            int advance = glyph(fi, ch)->advance;
            if (cjk(ch) && count && !cjk_no_start(ch) && !cjk_no_end(cp[count - 1])) {
                space = count; /* break before this character, keeping the previous one */
                space_at = before;
            }
            if (used + advance > w) {
                at = before;
                if (space < 0) {
                    space = kept;
                    space_at = kept_at;
                }
                if (space >= 0) {
                    count = space;
                    at = space_at;
                }
                break;
            }
            cp[count++] = ch;
            used += advance;
            if (cjk(ch) && !cjk_no_end(ch)) {
                space = count;
                space_at = at;
            } else if (ch == ' ' && c->lang == LANG_PL && one_letter_word(cp, count - 1)) {
                kept = count - 1;
                kept_at = at;
            } else if (ch == ' ') {
                space = count - 1;
                space_at = at;
            }
        }
        if (!count && at < n &&
            at == line_start) { /* A narrower box than one glyph must still progress. */
            size_t skip = at;
            next_cp(s, n, &skip);
            at = skip;
        }
        while (count && cp[count - 1] == ' ')
            --count;
        if (row == rows - 1 && at < n) {
            int avail = w - glyph(fi, 0x2026)->advance;
            used = 0;
            int keep = 0;
            while (keep < count && used + glyph(fi, cp[keep])->advance <= avail)
                used += glyph(fi, cp[keep++])->advance;
            count = keep;
            if (count < 128)
                cp[count++] = 0x2026;
        }
        int cursor = x;
        for (int k = 0; k < count; ++k) {
            draw_glyph(c, cursor, y + row * step + home_fonts[fi].size, fi, cp[k], p, x, y, w, h);
            cursor += glyph(fi, cp[k])->advance;
        }
    }
}
static void txt(canvas_t *c, int x, int y, int w, int h, int fi, const char *s)
{
    text(c, x, y, w, h, fi, BLACK, s, 512);
}
static void top(canvas_t *c, const home_config_t *cfg, const char *section)
{
    text(c, 14, 7, 192, 20, 1, BLACK, cfg->name[0] ? cfg->name : "emini HOME", sizeof cfg->name);
    int tw = width(0, section, 96);
    txt(c, imax(212, 386 - tw), 9, 174, 17, 0, section);
    rect(c, 14, 31, 372, 1, BLACK);
}
static void stamp(char *out, size_t len, int64_t epoch, int lang, bool clock24, const char *zone)
{
    struct tm tm;
    if (epoch <= 0 || !home_tz_localtime(zone, epoch, &tm)) {
        snprintf(out, len, "%s", tr(lang, "Date unknown", "Data nieznana"));
        return;
    }
    static const char *en[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                               "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    static const char *po[] = {"STY", "LUT", "MAR", "KWI", "MAJ", "CZE",
                               "LIP", "SIE", "WRZ", "PAŹ", "LIS", "GRU"};
    /* Chinese writes the date as 9 month 15 day and the half of the day before the hour. */
    if (lang == LANG_ZH) {
        if (clock24)
            snprintf(out, len, "%d月%d日 · %02d:%02d", tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour, tm.tm_min);
        else
            snprintf(out, len, "%d月%d日 · %s%d:%02d", tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour < 12 ? "上午" : "下午",
                     tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min);
        return;
    }
    if (clock24)
        snprintf(out, len, "%02d %s · %02d:%02d", tm.tm_mday, (lang == LANG_PL ? po : en)[tm.tm_mon],
                 tm.tm_hour, tm.tm_min);
    else
        snprintf(out, len, "%02d %s · %d:%02d %s", tm.tm_mday,
                 (lang == LANG_PL ? po : en)[tm.tm_mon],
                 tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
}
static home_source_state_t state_at(const home_source_meta_t *m, int64_t now)
{
    if (!m->valid)
        return m->state == HOME_ERROR ? HOME_ERROR : HOME_EMPTY;
    if (m->state == HOME_ERROR)
        return HOME_ERROR;
    if (!time_valid(now) || !time_valid(m->fetched_at) || m->fetched_at > now + 300)
        return HOME_STALE;
    if (m->state == HOME_STALE || (m->expires_at > 0 && now >= m->expires_at))
        return HOME_STALE;
    return HOME_READY;
}
static void source_footer(canvas_t *c, const home_config_t *cfg, const home_source_meta_t *m,
                          int64_t now, const char *source, int64_t issue)
{
    int lang = lang_of(cfg);
    char date[64], status[96];
    stamp(date, sizeof date, issue, lang, cfg->clock24, cfg->timezone);
    home_source_state_t st = state_at(m, now);
    if (st == HOME_ERROR)
        snprintf(status, sizeof status, "%s",
                 tr(lang, "Connection failed · saved data", "Brak połączenia · zapisane dane"));
    else if (!time_valid(now) || !time_valid(m->fetched_at) || m->fetched_at > now + 300)
        snprintf(status, sizeof status, "%s",
                 tr(lang, "Age unknown · check time", "Wiek nieznany · sprawdź czas"));
    else if (st == HOME_STALE)
        snprintf(
            status, sizeof status, "%s",
            tr(lang, "Older data · waiting for update", "Starsze dane · czekają na aktualizację"));
    else {
        /* "Checked" is when we last asked the provider, not when the content last changed.
         * A conditional request usually comes back 304 Not Modified, which keeps fetched_at
         * where it was: counting from it made the line say "within the hour" right after a
         * fresh check and left the Refresh gesture with nothing to show (16.09). */
        int64_t checked = time_valid(m->checked_at) ? m->checked_at : m->fetched_at;
        int64_t hours = (now - checked) / 3600;
        if (now - checked < 120)
            snprintf(status, sizeof status, "%s", tr(lang, "Checked just now", "Sprawdzono teraz"));
        else if (hours < 1)
            snprintf(status, sizeof status, "%s",
                     tr(lang, "Checked within the hour", "Sprawdzono w ostatniej godzinie"));
        else if (hours < 48)
            snprintf(status, sizeof status,
                     tr(lang, "Checked %lld h ago", "Sprawdzono %lld godz. temu"), (long long)hours);
        else
            snprintf(status, sizeof status,
                     tr(lang, "Checked %lld days ago", "Sprawdzono %lld dni temu"),
                     (long long)(hours / 24));
    }
    if (!source[0]) {
        char checked[64];
        stamp(checked, sizeof checked, time_valid(m->checked_at) ? m->checked_at : m->fetched_at,
              lang, cfg->clock24, cfg->timezone);
        char *clock = strstr(checked, " · ");
        if (st == HOME_READY)
            snprintf(status, sizeof status, "%s %s", tr(lang, "Checked", "Sprawdzono"),
                     clock ? clock + strlen(" · ") : "—");
        else
            snprintf(status, sizeof status, "%s",
                     st == HOME_ERROR ? tr(lang, "Update failed", "Błąd aktualizacji")
                                      : tr(lang, "Waiting for update", "Czeka na aktualizację"));
        char *separator = strstr(date, " · ");
        if (separator)
            *separator = 0;
        int date_width = width(0, date, sizeof date);
        rect(c, 14, 268, 372, 1, BLACK);
        txt(c, 14, 277, 360 - date_width, 18, 0, status);
        txt(c, 386 - date_width, 277, date_width, 18, 0, date);
        return;
    }
    rect(c, 14, 261, 372, 1, BLACK);
    txt(c, 14, 265, 215, 17, 0, source);
    txt(c, 231, 265, 155, 17, 0, date);
    txt(c, 14, 281, 372, 17, 0, status);
}
static void empty(canvas_t *c, const home_config_t *cfg, home_screen_t screen,
                  home_source_state_t st)
{
    int lang = lang_of(cfg);
    const char *title =
        screen == HOME_WEATHER
            ? tr(lang, "A forecast for your place.", "Prognoza dla Twojego miejsca.")
        : screen == HOME_FEED ? tr(lang, "A little room for the world.", "Trochę miejsca na świat.")
        : screen == HOME_AIR  ? tr(lang, "The air, at a glance.", "Powietrze na jeden rzut oka.")
        : screen == HOME_SKY  ? tr(lang, "A sky for your place.", "Niebo dla Twojego miejsca.")
                              : tr(lang, "Make this space yours.", "To miejsce jest dla Ciebie.");
    /* Sunset ramp paper -> yellow -> red (three pigments per point), in the user's brush. */
    c->raster = c->brush;
    for (int x = 280; x < 400; ++x) {
        float t = (x - 280) / 120.0f;
        for (int y = 43; y < 243; ++y) {
            pixel(c, x, y,
                  mix3(c, x, y, PAPER, YELLOW, RED, 1.0f - 0.8f * t, 0.8f * t * (1.0f - 0.5f * t),
                       0.4f * t * t));
            if ((x + y / 2) % 31 == 0)
                pixel(c, x, y, mix(c, x, y, YELLOW, RED, 0.4f));
        }
    }
    c->raster = RASTER_NOISE;
    txt(c, 14, 54, 268, 112, 3, title);
    const char *body =
        screen == HOME_WEATHER ? tr(lang,
                                    "Open the phone panel and use your location. The first "
                                    "forecast will appear here.",
                                    "Otwórz panel w telefonie i użyj swojej lokalizacji. Pierwsza "
                                    "prognoza pojawi się tutaj.")
        : screen == HOME_FEED  ? tr(lang,
                                    "Choose an RSS or Atom source in your phone panel. One story, "
                                    "without a stream to chase.",
                                    "Wybierz źródło RSS lub Atom w panelu telefonu. Jedna "
                                    "wiadomość, bez gonienia za strumieniem.")
        : screen == HOME_SKY   ? tr(lang,
                                    "Open the phone panel and use your location. The sun and the "
                                    "moon are then worked out here, with nothing downloaded.",
                                    "Otwórz panel w telefonie i użyj swojej lokalizacji. Słońce i "
                                    "księżyc policzą się tutaj, bez pobierania.")
        : screen == HOME_AIR   ? tr(lang,
                                    "Open the phone panel and use your location. Air quality, UV "
                                    "and pollen from Open-Meteo will appear here within the hour.",
                                    "Otwórz panel w telefonie i użyj swojej lokalizacji. Jakość "
                                    "powietrza, UV i pyłki z Open-Meteo pojawią się tu w ciągu godziny.")
                               : tr(lang,
                                    "Write a message in your phone panel. A reminder, a thought, "
                                    "something worth keeping in view.",
                                    "Wpisz wiadomość w panelu telefonu. Przypomnienie, myśl, coś, "
                                    "co warto mieć na widoku.");
    txt(c, 14, 172, 260, 80, 1, body);
    rect(c, 14, 261, 372, 1, BLACK);
    txt(c, 14, 269, 372, 22, 1,
        st == HOME_ERROR ? tr(lang, "Cannot load data · check the phone panel",
                              "Nie można pobrać danych · sprawdź panel")
                         : "emini.ink/home");
}
static const char *condition(const home_weather_t *w, int lang)
{
    if (contains(w->symbol, sizeof w->symbol, "thunder"))
        return tr(lang, "Thunderstorms", "Burze");
    if (contains(w->symbol, sizeof w->symbol, "sleet"))
        return tr(lang, "Sleet", "Śnieg z deszczem");
    if (contains(w->symbol, sizeof w->symbol, "snow"))
        return tr(lang, "Snow", "Śnieg");
    if (contains(w->symbol, sizeof w->symbol, "rain"))
        return tr(lang, "Rain ahead", "Deszcz");
    if (contains(w->symbol, sizeof w->symbol, "fog"))
        return tr(lang, "Fog", "Mgła");
    if (contains(w->symbol, sizeof w->symbol, "clearsky"))
        return tr(lang, "Clear skies", "Pogodne niebo");
    if (contains(w->symbol, sizeof w->symbol, "fair"))
        return tr(lang, "Mostly clear", "Przeważnie pogodnie");
    if (contains(w->symbol, sizeof w->symbol, "partlycloudy"))
        return tr(lang, "Partly cloudy", "Częściowe zachmurzenie");
    if (contains(w->symbol, sizeof w->symbol, "cloudy"))
        return tr(lang, "Cloud cover", "Zachmurzenie");
    return tr(lang, "Weather forecast", "Prognoza pogody");
}
static double temp(double c, bool f)
{
    return f ? c * 1.8 + 32 : c;
}
/* A finite, clamped value: decimal comma in Polish, U+2212 minus, never "-0". */
static void number(char *out, size_t len, double v, int decimals, int lang)
{
    char digits[24];
    snprintf(digits, sizeof digits, "%.*f", decimals, fabs(v));
    bool zero = strspn(digits, "0.") == strlen(digits);
    char *dot = lang == LANG_PL ? strchr(digits, '.') : NULL;
    if (dot)
        *dot = ',';
    snprintf(out, len, "%s%s", v < 0 && !zero ? "−" : "", digits);
}
/* 24-hour "18:00" (an end at midnight reads "24:00") or 12-hour "6 PM"/"6:30 PM";
 * the AM/PM mark may be left to the other end of a range. */
static void clock_text(char *out, size_t len, const struct tm *tm, bool clock24, bool end,
                       bool mark)
{
    if (clock24) {
        snprintf(out, len, "%02d:%02d", end && !tm->tm_hour && !tm->tm_min ? 24 : tm->tm_hour,
                 tm->tm_min);
        return;
    }
    int hour = tm->tm_hour % 12 ? tm->tm_hour % 12 : 12;
    const char *meridiem = !mark ? "" : tm->tm_hour < 12 ? " AM" : " PM";
    if (tm->tm_min)
        snprintf(out, len, "%d:%02d%s", hour, tm->tm_min, meridiem);
    else
        snprintf(out, len, "%d%s", hour, meridiem);
}
/* Reference layout: today's conditions above seven local-date forecast columns.
 * Drawn with this project's fonts and primitives; no external image assets. */
static void weather_circle(canvas_t *c, int cx, int cy, int r, int color)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r)
                pixel(c, cx + x, cy + y, color);
}
static void weather_icon(canvas_t *c, const char *symbol, int cx, int cy, int r, bool hero)
{
    if (!symbol[0]) {
        txt(c, cx - r, cy - 9, r * 2, 20, 1, "—");
        return;
    }
    bool clear = strstr(symbol, "clearsky"), fair = strstr(symbol, "fair"),
         part = strstr(symbol, "partlycloudy");
    if (clear || fair || part) {
        int sx = clear ? cx : cx + r / 3, sy = clear ? cy : cy - r / 3;
        weather_circle(c, sx, sy, r, BLACK);
        weather_circle(c, sx, sy, r - 1, c->intensity ? (hero && clear ? RED : YELLOW) : PAPER);
        if (clear)
            return;
    }
    if (strstr(symbol, "fog")) {
        for (int i = -1; i <= 1; i++)
            rect(c, cx - r, cy + i * (r / 2 + 1), 2 * r, 2, BLACK);
        return;
    }
    weather_circle(c, cx - r / 2, cy, r * 2 / 3, BLACK);
    weather_circle(c, cx, cy - r / 3, r * 3 / 4, BLACK);
    weather_circle(c, cx + r / 2, cy, r * 2 / 3, BLACK);
    rect(c, cx - r, cy, 2 * r, r / 2 + 1, BLACK);
    if (strstr(symbol, "rain") || strstr(symbol, "sleet"))
        for (int i = -1; i <= 1; i++) {
            int x = cx + i * r / 2;
            line(c, x, cy + r, x - r / 4, cy + r + r / 3, BLACK);
            line(c, x + 1, cy + r, x - r / 4 + 1, cy + r + r / 3, BLACK);
        }
    if (strstr(symbol, "snow"))
        for (int i = -1; i <= 1; i++) {
            int x = cx + i * r / 2, y = cy + r;
            line(c, x - 2, y, x + 2, y, BLACK);
            line(c, x, y - 2, x, y + 2, BLACK);
        }
    if (strstr(symbol, "thunder")) {
        int ink = c->intensity ? RED : BLACK;
        line(c, cx + 2, cy + r / 2, cx - 3, cy + r, ink);
        line(c, cx - 3, cy + r, cx + 3, cy + r, ink);
        line(c, cx + 3, cy + r, cx - 2, cy + r + r / 2, ink);
    }
}
static void weather_center(canvas_t *c, int cx, int y, int w, int fi, const char *s)
{
    int used = imin(w, width(fi, s, 128));
    txt(c, cx - used / 2, y, used + 2, 32, fi, s);
}
static const home_weather_day_t *weather_day(const home_weather_t *w, int32_t date)
{
    for (int i = 0; i < HOME_WEATHER_DAYS; i++)
        if (w->days[i].date == date && w->days[i].valid)
            return &w->days[i];
    return NULL;
}
/* Enlarge the existing 64 px mask to 96 px, shrinking only to fit long values. */
static void weather_temperature(canvas_t *c, const char *s)
{
    int natural = width(4, s, 40);
    int scale = imin(150, 23000 / imax(1, natural));
    int cursor = 0;
    size_t at = 0, len = strlen(s);
    while (at < len) {
        const home_glyph_t *g = glyph(4, next_cp(s, len, &at));
        int stride = (g->width + 7) / 8;
        int gw = (g->width * scale + 99) / 100, gh = (g->height * scale + 99) / 100;
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++) {
                int sx = x * 100 / scale, sy = y * 100 / scale;
                if (home_font_bits[g->offset + sy * stride + sx / 8] & (0x80 >> (sx & 7))) {
                    int px = 12 + (cursor + g->left) * scale / 100 + x;
                    int py = 158 + g->top * scale / 100 + y;
                    if (px >= 12 && px < 244 && py >= 73 && py < 162)
                        pixel(c, px, py, BLACK);
                }
            }
        cursor += g->advance;
    }
}
static void weather(canvas_t *c, const home_config_t *cfg, const home_weather_t *w,
                    const home_air_t *air, int64_t now)
{
    top(c, cfg, cfg->location[0] ? cfg->location : tr(c->lang, "Weather", "Pogoda"));
    if (!w->meta.valid || !isfinite(w->temperature)) {
        empty(c, cfg, HOME_WEATHER, w->meta.state);
        return;
    }
    bool fahrenheit = cfg->units[0] == 'F';
    struct tm local;
    bool dated = home_tz_localtime(cfg->timezone, now, &local);
    int32_t today =
        dated ? (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday : 0;
    const home_weather_day_t *day = weather_day(w, today);
    /* Connected squares at the top shrink into separated marks below. */
    if (c->intensity)
        for (int row = 0, y = 34; y < 122; row++, y += 8) {
            int side = imax(1, 8 - imax(0, row - 1));
            int inset = (8 - side) / 2;
            for (int x = 14 + (row % 2) * 8; x < 386; x += 16)
                rect(c, x + inset, y + inset, imin(side, 386 - x - inset), side, YELLOW);
        }
    const char *label = condition(w, c->lang);
    text(c, 14, 38, 258, 32, width(3, label, 128) <= 258 ? 3 : 2, BLACK, label, 128);
    char value[40], a[24], b[24], range[100];
    number(a, sizeof a, temp(w->temperature, fahrenheit), 0, c->lang);
    snprintf(value, sizeof value, "%s°", a);
    weather_temperature(c, value);
    weather_icon(c, w->symbol, 320, 76, 32, true);

    if (day && isfinite(day->low) && isfinite(day->high)) {
        number(a, sizeof a, temp(day->high, fahrenheit), 0, c->lang);
        number(b, sizeof b, temp(day->low, fahrenheit), 0, c->lang);
    } else {
        snprintf(a, sizeof a, "—");
        snprintf(b, sizeof b, "—");
    }
    snprintf(range, sizeof range, "H %s°%s | L %s°%s", a, cfg->units, b, cfg->units);
    weather_center(c, 308, 136, 156, width(1, range, sizeof range) <= 156 ? 1 : 0, range);
    int aqi = air->meta.valid ? air->us_aqi : -1;
    if (aqi >= 0)
        snprintf(value, sizeof value, "US AQI %d", aqi);
    else
        snprintf(value, sizeof value, "US AQI —");
    int ink = aqi > 100 ? RED : aqi > 50 ? YELLOW : BLACK;
    int used = width(0, value, sizeof value);
    text(c, 308 - used / 2, 153, used + 2, 16, 0, ink, value, sizeof value);
    rect(c, 14, 169, 372, 1, BLACK);
    char date_text[40];
    int64_t civil = 0;
    if (dated) {
        snprintf(date_text, sizeof date_text, "%04d-%02d-%02dT00:00:00Z", local.tm_year + 1900,
                 local.tm_mon + 1, local.tm_mday);
        civil = home_parse_time(date_text);
    }
    static const char *en[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *pl[] = {"ND", "PN", "WT", "ŚR", "CZ", "PT", "SB"};
    static const char *zh[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    for (int i = 0; i < 7; i++) {
        int x = 14 + i * 372 / 7, end = 14 + (i + 1) * 372 / 7, cx = (x + end) / 2;
        if (i)
            rect(c, x, 172, 1, 92, BLACK);
        struct tm future = {0};
        time_t at = civil + (i + 1) * 86400;
        if (dated)
            gmtime_r(&at, &future);
        int32_t key = (future.tm_year + 1900) * 10000 + (future.tm_mon + 1) * 100 + future.tm_mday;
        day = dated ? weather_day(w, key) : NULL;
        const char *weekday = dated ? (c->lang == LANG_ZH   ? zh[future.tm_wday]
                                       : c->lang == LANG_PL ? pl[future.tm_wday]
                                                            : en[future.tm_wday])
                                    : "—";
        int label_width = imin(end - x - 4, width(0, weekday, 40));
        text(c, cx - label_width / 2, 174, label_width + 2, 20, 0, c->intensity ? RED : BLACK,
             weekday, 40);
        weather_icon(c, day ? day->symbol : "", cx, 204, 9, false);
        if (day && isfinite(day->high)) {
            number(a, sizeof a, temp(day->high, fahrenheit), 0, c->lang);
            snprintf(value, sizeof value, "%s°", a);
        } else
            snprintf(value, sizeof value, "—");
        weather_center(c, cx, 220, end - x - 4, width(2, value, sizeof value) < end - x - 4 ? 2 : 1,
                       value);
        if (day && isfinite(day->low)) {
            number(a, sizeof a, temp(day->low, fahrenheit), 0, c->lang);
            snprintf(value, sizeof value, "%s°", a);
        } else
            snprintf(value, sizeof value, "—");
        weather_center(c, cx, 244, end - x - 4, 0, value);
    }
    source_footer(c, cfg, &w->meta, now, "", w->forecast_at);
}
static uint32_t fingerprint(const char *s, size_t cap)
{
    uint32_t hash = 2166136261u;
    size_t n = bounded(s, cap);
    for (size_t i = 0; i < n; ++i)
        hash = (hash ^ (uint8_t)s[i]) * 16777619u;
    return hash;
}
/* A reproducible printed signature of this particular text, not a score or a
 * data chart. It changes when the story/message changes and exports exactly. */
typedef struct {
    int x, y, width, height;
} paper_window_t;
static void signature(canvas_t *c, const char *s, size_t cap, int style, int topy, int bottom,
                      const paper_window_t *paper)
{
    uint32_t hash = fingerprint(s, cap);
    int extent = bottom - topy;
    float phase = (hash % 97) / 15.0f, inv_height = 1.0f / imax(1, extent - 1);
    for (int y = topy; y < bottom; ++y) {
        float a = (y - topy) * inv_height, dy = y - topy + 38.0f;
        float row_phase = phase * (13.0f / 23.0f) + a * 5.0f;
        for (int x = 0; x < W; ++x) {
            // The caller immediately paints this rectangle opaque paper.
            // Skipping its texture is bit-exact and avoids hidden trig work.
            if (paper && x >= paper->x && x < paper->x + paper->width && y >= paper->y &&
                y < paper->y + paper->height)
                continue;
            float dx = x - 200.0f;
            float pattern = style == HOME_RHYTHM ? (0.5f + 0.5f * sinf(x / 23.0f + row_phase))
                            : style == HOME_ATLAS
                                ? (0.5f + 0.5f * cosf(sqrtf(dx * dx + dy * dy) / 13.0f + phase))
                                : (x / 399.0f);
            pixel(c, x, y, mix(c, x, y, YELLOW, RED, pattern * a * 0.88f));
            if (style == HOME_PRINT && ((x + (hash % 11)) % 21 == 0))
                pixel(c, x, y, mix(c, x, y, PAPER, YELLOW, 0.6f));
        }
    }
}
/* Poster-only layout. Weather, setup, source labels and the existing text()
 * path are unchanged. Measure and paint share exactly the same line breaker. */
static bool poster_layout(canvas_t *c, int x, int y, int w, int h, int fi, const char *s,
                          size_t cap, bool compact, int lang, int *height)
{
    size_t n = bounded(s, cap), at = 0;
    int row = 0, step = home_fonts[fi].size + 4, needed = 0;
    while (at < n) {
        while (at < n &&
               (s[at] == ' ' || (compact && (s[at] == '\n' || s[at] == '\r' || s[at] == '\t'))))
            ++at;
        if (at == n)
            break;
        if (row >= 24)
            return false;
        uint32_t cp[128];
        int count = 0, used = 0, space = -1, kept = -1;
        size_t space_at = 0, kept_at = 0;
        while (at < n && count < 128) {
            size_t before = at;
            uint32_t ch = next_cp(s, n, &at);
            if (ch == '\r')
                continue;
            if (ch == '\n' && !compact)
                break;
            if (ch == '\n' || ch == '\t')
                ch = ' ';
            if (compact && ch == ' ' && (!count || cp[count - 1] == ' '))
                continue;
            if (!has_glyph(fi, ch) && ch != ' ')
                return false; /* try the next size; CJK tables cover fewer sizes */
            const home_glyph_t *g = glyph(fi, ch);
            if (cjk(ch) && count && !cjk_no_start(ch) && !cjk_no_end(cp[count - 1])) {
                space = count;
                space_at = before;
            }
            if (used + g->advance > w - 8 || used + g->left + g->width > w - 6) {
                at = before;
                if (!count)
                    return false;
                if (space < 0) {
                    space = kept;
                    space_at = kept_at;
                }
                if (!compact && space >= 0) {
                    count = space;
                    at = space_at;
                }
                break;
            }
            cp[count++] = ch;
            used += g->advance;
            if (cjk(ch) && !cjk_no_end(ch)) {
                space = count;
                space_at = at;
            } else if (ch == ' ' && lang == LANG_PL && one_letter_word(cp, count - 1)) {
                kept = count - 1;
                kept_at = at;
            } else if (ch == ' ') {
                space = count - 1;
                space_at = at;
            }
        }
        while (count && cp[count - 1] == ' ')
            --count;
        int cursor = x + 6; /* includes the64px lowercase-j negative bearing */
        for (int i = 0; i < count; ++i) {
            const home_glyph_t *g = glyph(fi, cp[i]);
            int top = row * step + home_fonts[fi].size + g->top;
            int bottom = top + g->height;
            if (cursor + g->left < x || top < 0 || bottom > h)
                return false;
            needed = imax(needed, bottom);
            if (c)
                draw_glyph(c, cursor, y + row * step + home_fonts[fi].size, fi, cp[i], BLACK, x, y,
                           w, h);
            cursor += g->advance;
        }
        ++row;
        needed = imax(needed, row * step);
        if (needed > h)
            return false;
    }
    *height = needed;
    return true;
}
static void poster_text(canvas_t *c, int x, int y, int w, int h, const char *s, size_t cap,
                        bool large)
{
    const int candidates[] = {4, 5, 7, 3, 2, 1, 0, 6}; /* 64,48,44,30,22,16,12,10 */
    /* Each size is tried with the Polish one-letter rule first, then without it,
     * before a smaller size: a poster keeps its size rather than a perfect break. */
    for (int compact = 0; compact < 2; ++compact)
        for (unsigned i = large ? 0 : 1; i < sizeof(candidates) / sizeof(*candidates); ++i) {
            int height, fi = candidates[i];
            for (int rule = c->lang == LANG_PL; rule >= 0; --rule) {
                if (!poster_layout(NULL, x, y, w, h, fi, s, cap, compact != 0, rule != 0, &height))
                    continue;
                int offset = (h - height) / 2;
                (void)poster_layout(c, x, y + offset, w, h - offset, fi, s, cap, compact != 0,
                                    rule != 0, &height);
                return;
            }
        }
    /* Validated EN/PL title/note byte limits fit at10px with compact wrapping.
     * Keep an explicit fallback for out-of-contract calls, never an overrun. */
    text(c, x, y, w, h, 6, BLACK, s, cap);
}
static void feed(canvas_t *c, const home_config_t *cfg, const home_feed_t *f, int64_t now)
{
    int lang = lang_of(cfg);
    top(c, cfg, tr(lang, "ONE STORY", "JEDNA WIADOMOŚĆ"));
    if (!f->meta.valid || !f->title[0]) {
        empty(c, cfg, HOME_FEED, f->meta.state);
        return;
    }
    int style = cfg->style[HOME_FEED] <= HOME_ATLAS ? cfg->style[HOME_FEED] : HOME_PRINT;
    text(c, 14, 44, 372, 22, 1, BLACK,
         f->source[0] ? f->source : tr(lang, "Your source", "Twoje źródło"), sizeof f->source);
    if (!f->summary[0]) {
        poster_text(c, 14, 76, 372, 177, f->title, sizeof f->title, cfg->large_text);
    } else if (style == HOME_ATLAS) {
        poster_text(c, 14, 76, 172, 177, f->title, sizeof f->title, cfg->large_text);
        rect(c, 194, 78, 1, 172, BLACK);
        text(c, 206, 78, 180, 174, cfg->large_text ? 1 : 0, BLACK, f->summary, sizeof f->summary);
    } else {
        int title_height = style == HOME_RHYTHM ? 65 : 78;
        poster_text(c, 14, 72, 372, title_height, f->title, sizeof f->title, cfg->large_text);
        rect(c, 14, 77 + title_height, 372, 1, BLACK);
        text(c, 14, 85 + title_height, 372, 172 - title_height, cfg->large_text ? 1 : 0, BLACK,
             f->summary, sizeof f->summary);
    }
    source_footer(c, cfg, &f->meta, now,
                  tr(lang, "Full story in phone panel", "Całość w panelu telefonu"), f->published_at);
}
static void note(canvas_t *c, const home_config_t *cfg, int64_t now)
{
    int lang = lang_of(cfg);
    top(c, cfg, tr(lang, "YOUR NOTE", "TWOJA KARTKA"));
    if (!cfg->note[0]) {
        empty(c, cfg, HOME_NOTE, HOME_EMPTY);
        return;
    }
    int style = cfg->style[HOME_NOTE] <= HOME_ATLAS ? cfg->style[HOME_NOTE] : HOME_PRINT;
    if (style == HOME_PRINT) {
        signature(c, cfg->note, sizeof cfg->note, style, 36, 55, NULL);
        poster_text(c, 14, 68, 372, 178, cfg->note, sizeof cfg->note, cfg->large_text);
    } else if (style == HOME_RHYTHM) {
        signature(c, cfg->note, sizeof cfg->note, style, 208, 261, NULL);
        poster_text(c, 14, 52, 372, 150, cfg->note, sizeof cfg->note, cfg->large_text);
    } else {
        signature(c, cfg->note, sizeof cfg->note, style, 37, 260,
                  &(paper_window_t){14, 52, 372, 193});
        rect(c, 14, 52, 372, 193, PAPER);
        poster_text(c, 26, 61, 348, 176, cfg->note, sizeof cfg->note, cfg->large_text);
    }
    rect(c, 14, 267, 372, 1, BLACK);
    txt(c, 14, 277, 220, 19, 0, tr(lang, "Yours to keep in view.", "Warto mieć to na widoku."));
    txt(c, 270, 277, 116, 19, 0, "emini.ink/home");
    (void)now;
}
/* Card for a screen index outside weather/feed/note: the device name like every
 * other screen ("emini HOME" without one), a title, one line of help. */

/* ---- Air: air quality, UV and pollen (0.5.0). Every screen uses all four pigments
 * (D-HOME-CC-24): the PM2.5 scale is one warm ramp paper -> yellow -> red, so good air is a
 * light yellow tone and bad air a deep red; beyond the European scale the field is solid red
 * with a black outline. The red "now" marks and the UV sun are the accents. */
static const char *level_name(int level, int lang)
{
    static const char *const en[6] = {"Very good", "Good", "Moderate", "Poor", "Very poor", "Extremely poor"};
    static const char *const po[6] = {"Bardzo dobre", "Dobre", "Umiarkowane", "Złe", "Bardzo złe", "Skrajnie złe"};
    static const char *const zh[6] = {"很好", "良好", "中等",
                                      "较差", "很差", "极差"};
    if (level < 0 || level > 5)
        return tr(lang, "No index", "Brak indeksu");
    return words(lang, en, po, zh)[level];
}
/* European index bands for PM2.5 in ug/m3 (EEA): 10, 20, 25, 50, 75. */
static int pm25_level(double v)
{
    if (!isfinite(v) || v < 0)
        return -1;
    static const double bands[5] = {10, 20, 25, 50, 75};
    for (int i = 0; i < 5; ++i)
        if (v <= bands[i])
            return i;
    return 5;
}
/* One pixel of the PM2.5 scale: t = 0 paper, 0.5 orange, 1 red (75 ug/m3 and beyond). */
static int pm_tone(canvas_t *c, int x, int y, double v)
{
    float t = (float)clamp(v / 75.0, 0, 1);
    return mix3(c, x, y, PAPER, YELLOW, RED, (1 - t) * (1 - t), 2 * t * (1 - t) + 0.08f, t * t);
}
static void tone_fill(canvas_t *c, int x, int y, int w, int h, double v)
{
    if (w <= 0 || h <= 0)
        return;
    if (!isfinite(v)) {
        for (int yy = y; yy < y + h; ++yy)
            for (int xx = x; xx < x + w; ++xx)
                if (((xx + yy) & 3) == 0)
                    pixel(c, xx, yy, BLACK);
        return;
    }
    if (v > 75) {
        rect(c, x, y, w, h, BLACK);
        rect(c, x + 1, y + 1, w - 2, h - 2, RED);
        return;
    }
    for (int yy = imax(y, 0); yy < imin(y + h, H); ++yy)
        for (int xx = imax(x, 0); xx < imin(x + w, W); ++xx)
            pixel(c, xx, yy, pm_tone(c, xx, yy, v));
}
/* The scale as a legend bar with a black marker at today's value. */
static void scale_bar(canvas_t *c, int x, int y, int w, int h, double v)
{
    for (int xx = x; xx < x + w; ++xx) {
        /* one value per 2 px cell, so the dither decision never differs inside a cell (R0) */
        double value = ((xx & ~1) - x) / (double)(w - 1) * 75.0;
        for (int yy = y; yy < y + h; ++yy)
            pixel(c, xx, yy, pm_tone(c, xx, yy, value));
    }
    rect(c, x, y + h, w, 1, BLACK);
    if (isfinite(v) && v >= 0) {
        int mx = x + (int)(clamp(v, 0, 75) / 75.0 * (w - 3));
        rect(c, mx, y - 3, 3, h + 6, BLACK);
    }
}
static int uv_level(double uv)
{
    if (!isfinite(uv) || uv < 0)
        return -1;
    return uv < 3 ? 0 : uv < 6 ? 1 : uv < 8 ? 2 : uv < 11 ? 3 : 4;
}
static const char *uv_name(int level, int lang)
{
    static const char *const en[5] = {"low", "moderate", "high", "very high", "extreme"};
    static const char *const po[5] = {"niskie", "umiarkowane", "wysokie", "bardzo wysokie", "ekstremalne"};
    static const char *const zh[5] = {"低", "中等", "高", "很高",
                                      "极高"};
    return level < 0 ? "" : words(lang, en, po, zh)[level];
}
/* "UV 6 · high · sunscreen from 11:00"; the hour is the first with UV >= 3 today. */
static void uv_line(char *out, size_t len, const home_config_t *cfg, const home_air_t *a, int lang)
{
    int level = uv_level(a->uv_index);
    if (level < 0) {
        snprintf(out, len, "UV —");
        return;
    }
    char v[16], when[64] = "";
    number(v, sizeof v, clamp(a->uv_index, 0, 20), 0, lang);
    int first = -1;
    for (int k = 0; k < imin(a->hourly_count, HOME_AIR_HOURS); ++k)
        if (isfinite(a->hourly_uv[k]) && a->hourly_uv[k] >= 3) {
            first = k;
            break;
        }
    if (first == 0)
        snprintf(when, sizeof when, " · %s", tr(lang, "sunscreen now", "krem teraz"));
    else if (first > 0 && time_valid(a->forecast_at)) {
        struct tm at;
        if (home_tz_localtime(cfg->timezone, a->forecast_at + (int64_t)first * 3600, &at)) {
            char t[24];
            clock_text(t, sizeof t, &at, cfg->clock24, false, false);
            snprintf(when, sizeof when, tr(lang, " · sunscreen from %s", " · krem od %s"), t);
        }
    }
    snprintf(out, len, "UV %s · %s%s", v, uv_name(level, lang), when);
}
/* The UV sun: a disc that grows and reddens with the index, with eight rays. */
static void uv_sun(canvas_t *c, int cx, int cy, double uv, double size)
{
    if (!isfinite(uv) || uv < 0)
        uv = 0;
    int r = 7 + (int)(clamp(uv, 0, 11) * size);
    float heat = (float)clamp(uv / 11.0, 0, 1) * 0.85f;
    for (int y = cy - r; y <= cy + r; ++y)
        for (int x = cx - r; x <= cx + r; ++x) {
            /* the heat of a pixel is that of its 2 px cell, so yellow and red never split a cell (R0) */
            int dx = (x & ~1) + 1 - cx, dy = (y & ~1) + 1 - cy;
            if (dx * dx + dy * dy <= r * r)
                pixel(c, x, y, mix(c, x, y, YELLOW, RED, heat * (1.0f - (dx * dx + dy * dy) / (float)(r * r))));
        }
    for (int k = 0; k < 8; ++k) {
        double ang = k * 3.14159265358979323846 / 4;
        int x0 = cx + (int)((r + 3) * cos(ang)), y0 = cy + (int)((r + 3) * sin(ang));
        int x1 = cx + (int)((r + 7 + (k & 1) * 3) * cos(ang)), y1 = cy + (int)((r + 7 + (k & 1) * 3) * sin(ang));
        int p = uv >= 8 ? RED : YELLOW; /* 2 px thick whatever the direction (R0) */
        line(c, x0, y0, x1, y1, p);
        line(c, x0 + 1, y0, x1 + 1, y1, p);
        line(c, x0, y0 + 1, x1, y1 + 1, p);
        line(c, x0 + 1, y0 + 1, x1 + 1, y1 + 1, p);
    }
}
/* 0 none, 1 low, 2 moderate, 3 high, from grains per m3; grass counts lower. */
static int pollen_level(int kind, double v)
{
    if (!isfinite(v) || v < 0)
        return -1;
    double mid = kind == HOME_POLLEN_GRASS ? 5 : 20, hi = kind == HOME_POLLEN_GRASS ? 20 : 80;
    return v < 1 ? 0 : v < mid ? 1 : v < hi ? 2 : 3;
}
static const char *pollen_name(int kind, int lang)
{
    static const char *const en[4] = {"Alder", "Birch", "Grass", "Mugwort"};
    static const char *const po[4] = {"Olcha", "Brzoza", "Trawy", "Bylica"};
    static const char *const zh[4] = {"桤木", "桦树", "禾草", "艾蒿"};
    return words(lang, en, po, zh)[kind & 3];
}
/* Four tiles "Birch ●●●○": 6 px dots, yellow for low, orange for moderate, red for high;
 * hidden when the data has no pollen (outside Europe). */
static int pollen_row(canvas_t *c, const home_air_t *a, int x, int y, int w, int lang, bool list)
{
    int shown = 0;
    for (int k = 0; k < HOME_POLLEN_COUNT; ++k)
        if (isfinite(a->pollen[k]))
            ++shown;
    if (!shown)
        return 0;
    int i = 0;
    for (int k = 0; k < HOME_POLLEN_COUNT; ++k) {
        if (!isfinite(a->pollen[k]))
            continue;
        int level = pollen_level(k, a->pollen[k]);
        int tx = list ? x : x + i * (w / shown), ty = list ? y + i * 20 : y;
        int tw = list ? w - 30 : w / shown - 30;
        const char *name = pollen_name(k, lang);
        if (width(1, name, 16) <= tw)
            txt(c, tx, ty, tw, 20, 1, name);
        else
            txt(c, tx, ty + 2, tw, 17, 0, name);
        int dx = (list ? x + w - 28 : tx + (w / shown) - 30) & ~1; /* even origin, even pitch: R0 */
        for (int d = 0; d < 3; ++d) {
            int ox = dx + d * 10, oy = (ty + 5) & ~1;
            if (d < level) {
                for (int yy = 0; yy < 6; ++yy)
                    for (int xx = 0; xx < 6; ++xx)
                        pixel(c, ox + xx, oy + yy,
                              level >= 3 ? RED : level == 2 ? mix(c, ox + xx, oy + yy, YELLOW, RED, 0.5f) : YELLOW);
            } else {
                rect(c, ox, oy, 6, 6, PAPER);
                for (int e = 0; e < 6; ++e) {
                    pixel(c, ox + e, oy, BLACK);
                    pixel(c, ox + e, oy + 5, BLACK);
                    pixel(c, ox, oy + e, BLACK);
                    pixel(c, ox + 5, oy + e, BLACK);
                }
            }
        }
        ++i;
    }
    return shown;
}
static void air_headline(char *value, size_t vlen, char *word, size_t wlen, const home_config_t *cfg,
                         const home_air_t *a, int lang, int *level)
{
    *level = home_air_level(a->european_aqi);
    if (*level < 0)
        *level = pm25_level(a->pm2_5);
    if (cfg->air_main == 1 && a->us_aqi >= 0)
        snprintf(value, vlen, "%d", a->us_aqi);
    else if (cfg->air_main == 2 && isfinite(a->pm2_5))
        number(value, vlen, clamp(a->pm2_5, 0, 999), 0, lang);
    else if (a->european_aqi >= 0)
        snprintf(value, vlen, "%d", a->european_aqi);
    else if (isfinite(a->pm2_5))
        number(value, vlen, clamp(a->pm2_5, 0, 999), 0, lang);
    else
        snprintf(value, vlen, "—");
    snprintf(word, wlen, "%s", level_name(*level, lang));
}
static void air_metrics(char *out, size_t len, const home_config_t *cfg, const home_air_t *a, int lang)
{
    char pm[24], pm10[24], us[24];
    if (isfinite(a->pm2_5))
        number(pm, sizeof pm, clamp(a->pm2_5, 0, 999), 0, lang);
    else
        snprintf(pm, sizeof pm, "—");
    if (isfinite(a->pm10))
        number(pm10, sizeof pm10, clamp(a->pm10, 0, 999), 0, lang);
    else
        snprintf(pm10, sizeof pm10, "—");
    if (a->us_aqi >= 0)
        snprintf(us, sizeof us, " · US AQI %d", a->us_aqi);
    else
        us[0] = 0;
    if (cfg->air_main == 2)
        snprintf(out, len, "PM10 %s%s", pm10, us);
    else
        snprintf(out, len, "PM2.5 %s · PM10 %s%s", pm, pm10, us);
}
/* 24 hourly PM2.5 bars, each in the tone of its own value. */
static void air_bars(canvas_t *c, const home_air_t *a, int x, int y, int w, int h)
{
    int n = imin(a->hourly_count, HOME_AIR_HOURS);
    if (n < 2)
        return;
    /* The scale follows the day's maximum (at least 20 ug/m3): clean air fills the chart with
     * pale yellow bars instead of leaving it empty, and the tone still tells the band. */
    double top = 20;
    for (int k = 0; k < n; ++k)
        if (isfinite(a->hourly_pm2_5[k]) && a->hourly_pm2_5[k] * 1.15 > top)
            top = clamp(a->hourly_pm2_5[k] * 1.15, 20, 999);
    int pitch = w / HOME_AIR_HOURS, bw = imax((pitch - 1) & ~1, 2);
    rect(c, x, y + h, w, 1, BLACK);
    for (int k = 0; k < n; ++k) {
        double v = a->hourly_pm2_5[k];
        if (!isfinite(v))
            continue;
        int bh = ((int)(clamp(v, 0, top) / top * (h - 2)) + 2) & ~1;
        /* even origin and height: a bar never splits a 2 px colour cell (R0) */
        tone_fill(c, (x + k * pitch) & ~1, y + h - bh, bw, bh, v);
    }
    rect(c, x, y - 2, 1, h + 3, BLACK); /* axis; index 0 is now */
}
static void air(canvas_t *c, const home_config_t *cfg, const home_air_t *a, int64_t now)
{
    int lang = lang_of(cfg);
    top(c, cfg, tr(lang, "AIR", "POWIETRZE"));
    if (!a->meta.valid) {
        empty(c, cfg, HOME_AIR, a->meta.state);
        return;
    }
    int style = cfg->style[HOME_AIR] <= HOME_ATLAS ? cfg->style[HOME_AIR] : HOME_PRINT;
    int level;
    char value[24], word[40], uv[96], metrics[96];
    air_headline(value, sizeof value, word, sizeof word, cfg, a, lang, &level);
    uv_line(uv, sizeof uv, cfg, a, lang);
    air_metrics(metrics, sizeof metrics, cfg, a, lang);
    const char *unit = cfg->air_main == 2 ? tr(lang, "PM2.5 in µg per m3", "PM2.5 w µg na m3")
                       : cfg->air_main == 1 ? "US AQI"
                                            : tr(lang, "EU index", "Indeks EU");
    int raster = c->brush, n = imin(a->hourly_count, HOME_AIR_HOURS);
    if (style == HOME_RHYTHM) {
        txt(c, 14, 40, 178, 19, 0, tr(lang, "AIR QUALITY", "JAKOŚĆ POWIETRZA"));
        txt(c, 14, 56, 214, 40, width(3, word, sizeof word) > 214 ? 2 : 3, word);
        txt(c, 14, 90, 200, 18, 0, unit);
        txt(c, 240, 42, 146, 60, width(5, value, sizeof value) > 146 ? 7 : 5, value);
        scale_bar(c, 240, 104, 146, 5, a->pm2_5);
        /* PM2.5 over 24 h, the fill in the tone of each hour; UV as a yellow curve with the sun
         * at its peak. */
        int gx = 14, gy = 116, gw = 372, gh = 96;
        double top = 20; /* the day's maximum sets the scale, so clean air is not an empty field */
        for (int k = 0; k < n; ++k)
            if (isfinite(a->hourly_pm2_5[k]) && a->hourly_pm2_5[k] * 1.15 > top)
                top = clamp(a->hourly_pm2_5[k] * 1.15, 20, 999);
        c->raster = raster;
        int lastx = -1, lasty = 0;
        for (int xx = 0; xx < gw && n >= 2; ++xx) {
            double index = (double)xx * (n - 1) / (gw - 1);
            int k = imin((int)index, n - 2);
            double f = index - k, v0 = a->hourly_pm2_5[k], v1 = a->hourly_pm2_5[k + 1];
            if (!isfinite(v0) || !isfinite(v1)) {
                lastx = -1;
                continue;
            }
            double v = v0 * (1 - f) + v1 * f;
            int py = gy + gh - 1 - (int)(clamp(v, 0, top) / top * (gh - 1));
            /* the tone comes from the even column of the cell (R0); the height stays per column */
            double cell_index = (double)(xx & ~1) * (n - 1) / (gw - 1);
            int ck = imin((int)cell_index, n - 2);
            double cf = cell_index - ck;
            double cv = isfinite(a->hourly_pm2_5[ck]) && isfinite(a->hourly_pm2_5[ck + 1])
                            ? a->hourly_pm2_5[ck] * (1 - cf) + a->hourly_pm2_5[ck + 1] * cf
                            : v;
            for (int yy = py; yy < gy + gh; ++yy)
                pixel(c, gx + xx, yy, cv > 75 ? RED : pm_tone(c, gx + xx, yy, cv));
            if (lastx >= 0)
                line(c, lastx, lasty, gx + xx, py, BLACK);
            lastx = gx + xx;
            lasty = py;
        }
        c->raster = RASTER_NOISE;
        lastx = -1;
        int peak_x = -1, peak_y = gy + gh;
        for (int xx = 0; xx < gw && n >= 2; ++xx) {
            double index = (double)xx * (n - 1) / (gw - 1);
            int k = imin((int)index, n - 2);
            double f = index - k, u0 = a->hourly_uv[k], u1 = a->hourly_uv[k + 1];
            if (!isfinite(u0) || !isfinite(u1)) {
                lastx = -1;
                continue;
            }
            int py = gy + gh - 1 - (int)(clamp(u0 * (1 - f) + u1 * f, 0, 11) / 11.0 * (gh - 1));
            if (py < peak_y) {
                peak_y = py;
                peak_x = gx + xx;
            }
            if (lastx >= 0) {
                line(c, lastx, lasty, gx + xx, py, YELLOW);
                line(c, lastx, lasty + 1, gx + xx, py + 1, YELLOW);
            }
            lastx = gx + xx;
            lasty = py;
        }
        if (peak_x >= 0 && peak_y < gy + gh - 4)
            uv_sun(c, imin(imax(peak_x, gx + 44), gx + gw - 24), imax(peak_y, gy + 14), a->uv_index, 0.4);
        rect(c, gx, gy + gh, gw, 1, BLACK);
        rect(c, gx, gy - 2, 1, gh + 3, BLACK); /* axis; the left edge is now */
        txt(c, 14, 215, 372, 17, 0,
            tr(lang, "NEXT 24 H · PM2.5, UV IN YELLOW", "KOLEJNE 24 H · PM2.5, UV NA ŻÓŁTO"));
        txt(c, 14, 236, 372, 21, 1, uv);
    } else if (style == HOME_ATLAS) {
        /* The dial: 24 hour segments clockwise from now at the top, each in the tone of its
         * value; a soft yellow glow inside; the red hand marks now. */
        int cx = 100, cy = 146, ro = 96, ri = 66;
        c->raster = raster;
        for (int y = cy - ro; y <= cy + ro; ++y)
            for (int x = cx - ro; x <= cx + ro; ++x) {
                /* every decision (inside, ring, hour wedge, glow) from the 2 px cell, so a cell
                 * never holds two pigments by geometry alone (R0) */
                double dx = (x & ~1) + 1 - cx, dy = (y & ~1) + 1 - cy, r = sqrt(dx * dx + dy * dy);
                if (r > ro)
                    continue;
                if (r < ri) {
                    pixel(c, x, y, mix(c, x, y, PAPER, YELLOW, 0.55f * (float)((r / ri) * (r / ri))));
                    continue;
                }
                double ang = atan2(dx, -dy);
                if (ang < 0)
                    ang += 2 * 3.14159265358979323846;
                int k = (int)(ang / (2 * 3.14159265358979323846) * HOME_AIR_HOURS);
                if (k >= n || !isfinite(a->hourly_pm2_5[k])) {
                    if (((x + y) & 3) == 0)
                        pixel(c, x, y, BLACK);
                    continue;
                }
                double v = a->hourly_pm2_5[k];
                pixel(c, x, y, v > 75 ? RED : pm_tone(c, x, y, v));
            }
        c->raster = RASTER_NOISE;
        for (int y = cy - ro; y <= cy + ro; ++y)
            for (int x = cx - ro; x <= cx + ro; ++x) {
                /* the outlines follow the same 2 px cells as the fill, so they never leave a lone
                 * colour pixel beside them (R0) */
                double dx = (x & ~1) + 1 - cx, dy = (y & ~1) + 1 - cy, r = sqrt(dx * dx + dy * dy);
                if ((r > ro - 2 && r <= ro) || (r >= ri && r < ri + 2))
                    pixel(c, x, y, BLACK);
            }
        rect(c, cx - 1, cy - ro - 5, 3, ro - ri + 10, RED);
        int vf = width(7, value, sizeof value) > 120 ? 3 : 7;
        int vw = imin(width(vf, value, sizeof value), 120), uw = imin(width(0, unit, 64), 124);
        txt(c, cx - vw / 2, cy - (vf == 7 ? 30 : 22), vw + 2, vf == 7 ? 50 : 36, vf, value);
        txt(c, cx - uw / 2, cy + 14, uw + 2, 18, 0, unit);
        txt(c, 208, 62, 178, width(3, word, sizeof word) > 178 ? 60 : 36, width(3, word, sizeof word) > 178 ? 2 : 3, word);
        scale_bar(c, 208, 104, 178, 5, a->pm2_5);
        uv_sun(c, 368, 50, a->uv_index, 0.6);
        txt(c, 208, 44, 140, 18, 0, tr(lang, "AIR QUALITY", "JAKOŚĆ POWIETRZA"));
        txt(c, 208, 124, 178, 40, 1, uv);
        pollen_row(c, a, 208, 170, 178, lang, true);
        txt(c, 14, 245, 372, 14, 0, metrics);
    } else {
        txt(c, 14, 40, 172, 18, 0, tr(lang, "AIR QUALITY", "JAKOŚĆ POWIETRZA"));
        txt(c, 12, 56, 174, 66, width(4, value, sizeof value) > 174 ? 3 : 4, value);
        txt(c, 14, 124, 170, 18, 0, unit);
        txt(c, 14, 142, 176, width(3, word, sizeof word) > 176 ? 60 : 36, width(3, word, sizeof word) > 176 ? 2 : 3, word);
        scale_bar(c, 14, 182, 172, 5, a->pm2_5);
        c->raster = raster;
        air_bars(c, a, 200, 66, 186, 66);
        c->raster = RASTER_NOISE;
        uv_sun(c, 368, 48, a->uv_index, 0.6);
        txt(c, 200, 136, 90, 17, 0, tr(lang, "NOW", "TERAZ"));
        txt(c, 300, 136, 86, 17, 0, "+24 h");
        txt(c, 14, 194, 372, 20, 1, uv);
        if (!pollen_row(c, a, 14, 216, 372, lang, false))
            txt(c, 14, 216, 372, 20, 1,
                tr(lang, "No pollen forecast for this place", "Brak prognozy pyłków dla tego miejsca"));
        txt(c, 14, 241, 372, 17, 0, metrics);
    }
    source_footer(c, cfg, &a->meta, now, "Open-Meteo · CC BY 4.0", a->forecast_at);
}
static void status(canvas_t *c, const char *name, size_t cap, const char *title, const char *body)
{
    if (name && bounded(name, cap))
        text(c, 14, 8, 372, 22, 1, BLACK, name, cap);
    else
        txt(c, 14, 8, 372, 22, 1, "emini HOME");
    rect(c, 14, 35, 372, 1, BLACK);
    text(c, 14, 54, 372, 100, 3, BLACK, title ? title : "Home", 256);
    text(c, 14, 163, 372, 80, 1, BLACK, body ? body : "", 512);
    signature(c, title ? title : "Home", 256, HOME_PRINT, 251, 270, NULL);
    /* The panel is local; the web address is where help lives. */
    txt(c, 14, 279, 372, 18, 0, tr(c->lang, "Help · emini.ink/home", "Pomoc · emini.ink/home"));
}
/* ---- Sky: the sun and the moon, computed on the device ------------------
 * One local day fills the screen: the horizontal axis runs from local midnight
 * to the next, the tone of every field comes from the altitude of the sun at
 * that moment, and the only red on the screen is the two-pixel "now" marker. */
enum { SKY_DAY_S = 86400 };
typedef struct {
    home_sky_t s;
    int64_t midnight, now;
    double lat, lon;
} sky_t;

/* UTC instant at which the local calendar day holding `now` begins. Found by
 * bisection on the local date rather than by subtracting an offset: a day that
 * starts with a spring-forward jump has no 00:00 local at all, and the search
 * still returns its first second. */
static long day_key(const char *zone, int64_t utc, bool *ok)
{
    struct tm tm;
    *ok = home_tz_localtime(zone, utc, &tm);
    return *ok ? (((long)tm.tm_year * 12 + tm.tm_mon) * 32L + tm.tm_mday) : 0;
}
static bool sky_midnight(const char *zone, int64_t now, int64_t *out)
{
    bool ok;
    long today = day_key(zone, now, &ok);
    if (!ok)
        return false;
    int64_t lo = now - 2 * SKY_DAY_S, hi = now;
    if (day_key(zone, lo, &ok) >= today || !ok)
        return false;
    while (hi - lo > 1) {
        int64_t mid = lo + (hi - lo) / 2;
        long key = day_key(zone, mid, &ok);
        if (!ok)
            return false;
        if (key >= today)
            hi = mid;
        else
            lo = mid;
    }
    *out = hi;
    return true;
}
/* An event time to the nearest minute, the resolution every almanac prints. */
static bool event_clock(char *out, size_t len, const home_config_t *cfg, int64_t t, bool mark)
{
    struct tm tm;
    if (t <= 0 || !home_tz_localtime(cfg->timezone, t + 30, &tm))
        return false;
    tm.tm_sec = 0;
    clock_text(out, len, &tm, cfg->clock24, false, mark);
    return true;
}
/* The one sentence every composition carries: when the sun rises and sets, or
 * why it does neither. */
static void sun_hours_text(char *out, size_t len, const home_config_t *cfg, const home_sky_t *s)
{
    int lang = lang_of(cfg);
    char a[24], b[24];
    if (s->polar_day)
        snprintf(out, len, "%s", tr(lang, "The sun does not set today", "Słońce dziś nie zachodzi"));
    else if (s->polar_night)
        snprintf(out, len, "%s", tr(lang, "The sun does not rise today", "Słońce dziś nie wschodzi"));
    else if (!event_clock(a, sizeof a, cfg, s->sunrise, !cfg->clock24) ||
             !event_clock(b, sizeof b, cfg, s->sunset, !cfg->clock24))
        snprintf(out, len, "%s", tr(lang, "Sunrise and sunset unknown", "Wschód i zachód nieznane"));
    else
        snprintf(out, len, tr(lang, "Sunrise %s · Sunset %s", "Wschód %s · Zachód %s"), a, b);
}
/* "day 12 h 08 min (-4 min)": the length of this day and its change on yesterday. */
static void day_length_text(char *out, size_t len, const home_config_t *cfg, const home_sky_t *s)
{
    int lang = lang_of(cfg);
    char delta[24];
    int minutes = (int)((s->day_length_s + 30) / 60), change = (int)(s->day_length_delta_s / 60);
    if (s->polar_day || s->polar_night) {
        snprintf(out, len, "%s",
                 s->polar_day ? tr(lang, "daylight all day", "światło przez całą dobę")
                              : tr(lang, "no daylight today", "dziś bez światła dnia"));
        return;
    }
    if (!change) {
        snprintf(out, len, tr(lang, "day %d h %02d min", "dzień %d h %02d min"), minutes / 60,
                 minutes % 60);
        return;
    }
    number(delta, sizeof delta, change, 0, lang);
    snprintf(out, len, tr(lang, "day %d h %02d min (%s%s min)", "dzień %d h %02d min (%s%s min)"),
             minutes / 60, minutes % 60, change > 0 ? "+" : "", delta);
}
static const char *moon_name(int phase, int lang)
{
    switch (phase) {
    case HOME_MOON_WAXING_CRESCENT:
        return tr(lang, "Waxing crescent", "Przybywający sierp");
    case HOME_MOON_FIRST_QUARTER:
        return tr(lang, "First quarter", "Pierwsza kwadra");
    case HOME_MOON_WAXING_GIBBOUS:
        return tr(lang, "Waxing gibbous", "Przybywający garb");
    case HOME_MOON_FULL:
        return tr(lang, "Full moon", "Pełnia");
    case HOME_MOON_WANING_GIBBOUS:
        return tr(lang, "Waning gibbous", "Ubywający garb");
    case HOME_MOON_LAST_QUARTER:
        return tr(lang, "Last quarter", "Ostatnia kwadra");
    case HOME_MOON_WANING_CRESCENT:
        return tr(lang, "Waning crescent", "Ubywający sierp");
    default:
        return tr(lang, "New moon", "Nów");
    }
}
/* Whichever of the next full and the next new moon comes first. */
static void moon_note(char *out, size_t len, const home_sky_t *s, int lang)
{
    bool full = s->days_to_full <= s->days_to_new;
    int days = full ? s->days_to_full : s->days_to_new;
    if (!days)
        snprintf(out, len, "%s",
                 full ? tr(lang, "Full today", "Pełnia dziś") : tr(lang, "New today", "Nów dziś"));
    else if (full)
        snprintf(out, len, tr(lang, "Full in %d days", "Pełnia za %d dni"), days);
    else
        snprintf(out, len, tr(lang, "New in %d days", "Nów za %d dni"), days);
}
static void moon_lit_text(char *out, size_t len, const home_sky_t *s, int lang)
{
    char value[24];
    number(value, sizeof value, clamp(s->moon_fraction * 100.0, 0, 100), 0, lang);
    snprintf(out, len, tr(lang, "Lit %s%%", "Oświetlony %s%%"), value);
}
/* The largest of the offered fonts whose single line fits the width. */
/* A size the font cannot draw is not a candidate: the CJK tables exist at 30, 22, 16 and 12 px
 * only, so a Chinese word offered a 64 px slot would come out as rows of '?'. Whoever picks a
 * size has to ask whether that size can draw this text (0.5.1, the third language). */
static bool drawable(int fi, const char *s, size_t cap)
{
    size_t at = 0, n = bounded(s, cap);
    while (at < n) {
        uint32_t cp = next_cp(s, n, &at);
        if (cp != ' ' && !has_glyph(fi, cp))
            return false;
    }
    return true;
}
/* The last size of the list is the floor; if even that cannot draw the text, the smallest
 * size that can wins, so a fallback is always a readable one. */
static int fit_floor(const char *s, const int *order, int n)
{
    if (drawable(order[n - 1], s, 256))
        return order[n - 1];
    for (int fi = 0; fi < 8; ++fi)
        if (drawable(fi, s, 256))
            return fi;
    return order[n - 1];
}
static int fit_font(const char *s, int w, const int *order, int n)
{
    for (int i = 0; i < n; ++i)
        if (drawable(order[i], s, 256) && width(order[i], s, 256) <= w)
            return order[i];
    return fit_floor(s, order, n);
}
/* The largest of the offered fonts with no word wider than the box, so text()
 * wraps between words instead of cutting one with an ellipsis. */
static int fit_wrapped(const char *s, int w, const int *order, int n)
{
    size_t len = strlen(s);
    for (int i = 0; i < n; ++i) {
        size_t at = 0;
        int word = 0, worst = 0;
        while (at < len) {
            uint32_t ch = next_cp(s, len, &at);
            word = ch == ' ' ? 0 : word + glyph(order[i], ch)->advance;
            if (word > worst)
                worst = word;
        }
        if (worst <= w && drawable(order[i], s, len + 1))
            return order[i];
    }
    return fit_floor(s, order, n);
}
/* The colour of the sky at a solar altitude, as three pigments for mix3()
 * (D-HOME-CC-24: every screen uses all four). The ramp runs paper and yellow by
 * day, through gold and the red of sunset, into the black of night, where the
 * grains of paper left over read as stars. */
typedef struct {
    int a, b, d;
    float wa, wb, wd;
} tone_t;
static tone_t sky_tone(double alt)
{
    static const tone_t steps[7] = {
        {PAPER, YELLOW, BLACK, 0.06f, 0.94f, 0.00f}, /* above 8 deg: the full day */
        {PAPER, YELLOW, RED, 0.14f, 0.80f, 0.06f},   /* 3 to 8: the first warmth */
        {PAPER, YELLOW, RED, 0.06f, 0.62f, 0.32f},   /* 0 to 3: gold turning orange */
        {YELLOW, RED, BLACK, 0.42f, 0.43f, 0.15f},   /* -3 to 0: the sunset itself */
        {YELLOW, BLACK, PAPER, 0.22f, 0.75f, 0.03f}, /* -6 to -3: civil twilight, no red alone on black (R2-3) */
        {YELLOW, BLACK, PAPER, 0.05f, 0.92f, 0.03f}, /* -12 to -6: nautical twilight */
        {PAPER, BLACK, RED, 0.03f, 0.97f, 0.00f},    /* night, with stars */
    };
    int i = alt >= 8 ? 0 : alt >= 3 ? 1 : alt >= 0 ? 2 : alt >= -3 ? 3 : alt >= -6 ? 4
            : alt >= -12                                                           ? 5
                                                                                   : 6;
    return steps[i];
}
/* How warm the ground under the sun is: yellow while the sun is high, red as it
 * reaches the horizon. Fills the dome of Print and the curve of Rhythm. */
static tone_t warm_fill(double alt, float lift)
{
    tone_t t = {PAPER, YELLOW, RED, 0.05f, 0.95f, 0.00f};
    double a = alt < 0 ? 0 : alt > 24 ? 24 : alt;
    float heat = (float)(1.0 - a / 24.0);
    t.wa = 0.06f * lift;
    t.wb = (0.42f + 0.53f * (1.0f - heat)) * lift;
    t.wd = 0.58f * heat * heat * lift;
    return t;
}
/* How dark the sky behind the altitude curve is: paper by day, black by night,
 * where the remaining grains of paper read as stars. */
static float night_cover(double alt)
{
    return alt >= 0     ? 0.0f
           : alt >= -6  ? (float)(0.10 + 0.04 * -alt)
           : alt >= -12 ? 0.64f
           : alt >= -18 ? 0.85f
                        : 0.95f;
}
static int64_t sky_time_at(const sky_t *k, int step, int steps)
{
    return k->midnight + (int64_t)step * SKY_DAY_S / steps;
}
/* The 24 h band. Tone follows the sun; with `arc` the daylight is a low arc
 * over the black night instead of a full-height field. */
static void sky_strip(canvas_t *c, const sky_t *k, int x, int y, int w, int h, bool arc)
{
    tone_t t = sky_tone(0);
    double alt = 0;
    for (int i = 0; i < w; ++i) {
        if ((i & 3) == 0) { /* one sample per four columns keeps every colour cell uniform */
            alt = home_sky_altitude(k->lat, k->lon, sky_time_at(k, i, w));
            t = sky_tone(alt);
        }
        int top = arc && alt > 0 ? y + h - 2 - (int)(clamp(alt / 55.0, 0, 1) * (h - 4)) : y;
        for (int yy = y; yy < y + h; ++yy)
            pixel(c, x + i, yy,
                  yy < top ? PAPER : mix3(c, x + i, yy, t.a, t.b, t.d, t.wa, t.wb, t.wd));
    }
}
/* Atlas: the colour of the horizon through the three hours either side of now,
 * a warm band under the moon on a screen that is otherwise deliberately nocturnal. */
static void sky_horizon_band(canvas_t *c, const sky_t *k, int x, int y, int w, int h)
{
    tone_t t = sky_tone(0);
    for (int i = 0; i < w; ++i) {
        if ((i & 3) == 0)
            t = sky_tone(home_sky_altitude(k->lat, k->lon,
                                           k->now + (int64_t)(i - w / 2) * 21600 / w));
        for (int yy = y; yy < y + h; ++yy)
            pixel(c, x + i, yy, mix3(c, x + i, yy, t.a, t.b, t.d, t.wa, t.wb, t.wd));
    }
}
/* Hour labels under a 24 h band: midnight, both sixes and noon. */
static void sky_hours_axis(canvas_t *c, const home_config_t *cfg, int x, int y, int w)
{
    static const int hours[5] = {0, 6, 12, 18, 24};
    for (int i = 0; i < 5; ++i) {
        char label[16];
        if (cfg->clock24)
            snprintf(label, sizeof label, "%02d", hours[i]);
        else
            snprintf(label, sizeof label, "%d %s", hours[i] % 12 ? hours[i] % 12 : 12,
                     hours[i] % 24 < 12 ? "AM" : "PM");
        int tw = width(6, label, sizeof label), lx = x + i * (w - 1) / 4 - tw / 2;
        lx = imin(imax(lx, x), x + w - tw);
        rect(c, x + i * (w - 1) / 4, y, 1, 3, BLACK);
        txt(c, lx, y + 4, tw, 14, 6, label);
    }
}
/* The instant marker: two pixels of red, the only red on the screen. */
static void sky_now(canvas_t *c, const sky_t *k, int x, int y, int w, int h)
{
    double f = clamp((double)(k->now - k->midnight) / SKY_DAY_S, 0, 1);
    int mx = imin(imax(x + (int)(f * (w - 2) + 0.5), x), x + w - 2);
    rect(c, mx, y, 2, h, RED);
}
/* A disc with a paper halo, so it reads on a dithered field: the sun filled
 * while it is up, an outline while it is below the horizon. */
static void sky_disc(canvas_t *c, int cx, int cy, int r, bool filled)
{
    for (int yy = cy - r - 3; yy <= cy + r + 3; ++yy)
        for (int xx = cx - r - 3; xx <= cx + r + 3; ++xx) {
            int dx = xx - cx, dy = yy - cy, d2 = dx * dx + dy * dy;
            if (d2 > (r + 3) * (r + 3))
                continue;
            if (d2 > r * r)
                pixel(c, xx, yy, PAPER);
            else
                pixel(c, xx, yy, filled || d2 > (r - 2) * (r - 2) ? BLACK : PAPER);
        }
}
/* The sun where it stands now: a halo that fades outwards through yellow into
 * red, and a disc that turns from black to red as it nears the horizon. The halo
 * is measured from the corner of each colour cell, so no grain of it falls below
 * the two-pixel minimum. */
static void sun_mark(canvas_t *c, int cx, int cy, int r, double alt)
{
    int reach = r + 7;
    for (int yy = cy - reach; yy <= cy + reach; ++yy)
        for (int xx = cx - reach; xx <= cx + reach; ++xx) {
            int dx = xx - cx, dy = yy - cy;
            if (dx * dx + dy * dy <= r * r)
                continue;
            int gx = (xx & ~1) - cx, gy = (yy & ~1) - cy;
            float d = sqrtf((float)(gx * gx + gy * gy));
            if (d > reach)
                continue;
            float v = clampf((reach - d) / (float)(reach - r), 0.0f, 1.0f);
            pixel(c, xx, yy,
                  mix3(c, xx, yy, PAPER, YELLOW, RED, 1.0f - v, v * 0.80f, v * v * 0.50f));
        }
    for (int yy = cy - r; yy <= cy + r; ++yy)
        for (int xx = cx - r; xx <= cx + r; ++xx) {
            int dx = xx - cx, dy = yy - cy;
            if (dx * dx + dy * dy <= r * r)
                pixel(c, xx, yy, alt < 6.0 ? RED : BLACK);
        }
}
/* The moon at its phase: the lit part paper, the shadow a black dither with a
 * soft terminator, the limb a 2 px rim. Waxing moons are lit from the right. */
static void moon_disc(canvas_t *c, int cx, int cy, int r, double lit, bool waxing)
{
    double inv = 1.0 / r, soft = r / 7.0 < 3.0 ? 3.0 : r / 7.0;
    int rim = (int)(2.2 * 2.2 + 2 * 2.2 * r);
    for (int yy = cy - r - 3; yy <= cy + r + 3; ++yy)
        for (int xx = cx - r - 3; xx <= cx + r + 3; ++xx) {
            int dx = xx - cx, dy = yy - cy, d2 = dx * dx + dy * dy;
            if (d2 > r * r) {
                if (d2 <= r * r + rim)
                    pixel(c, xx, yy, BLACK);
                continue;
            }
            double nx = dx * inv, ny = dy * inv;
            double half = sqrt(clamp(1.0 - ny * ny, 0, 1));
            double edge = -(2.0 * lit - 1.0) * half;
            double s = waxing ? nx - edge : edge - nx; /* positive on the lit side */
            float dark = (float)clamp(0.5 - s * r / soft, 0, 1);
            pixel(c, xx, yy, mix(c, xx, yy, PAPER, BLACK, 0.05f + 0.9f * dark));
        }
}
/* Print: the dome of the sun over the horizon on the shared 24 h axis, with the
 * sun itself where it stands now. */
static void sun_dome(canvas_t *c, const sky_t *k, int x, int y, int w, int h)
{
    int horizon = y + h - 18, sky_h = horizon - y;
    double rise = k->s.polar_day ? 0.0 : (double)(k->s.sunrise - k->midnight) / SKY_DAY_S;
    double set = k->s.polar_day ? 1.0 : (double)(k->s.sunset - k->midnight) / SKY_DAY_S;
    bool dome = k->s.polar_day || (!k->s.polar_night && k->s.sunrise && k->s.sunset && set > rise);
    if (dome) {
        int x0 = x + (int)(clamp(rise, 0, 1) * (w - 1)), x1 = x + (int)(clamp(set, 0, 1) * (w - 1));
        for (int xx = x0; xx <= x1; ++xx) {
            double u = x1 > x0 ? (double)(xx - x0) / (x1 - x0) : 0.5;
            double amp = sin(u * 3.14159265358979323846);
            if (k->s.polar_day)
                amp = 0.45 + 0.55 * amp;
            int top = horizon - 2 - (int)(amp * (sky_h - 6));
            /* Yellow at the crown, red where the dome meets the ground; the tone is
             * read from the top of each colour cell, so no grain stands alone. */
            for (int yy = top; yy < horizon; ++yy) {
                float v = clampf((float)(horizon - (yy & ~1)) / (float)(horizon - top + 1), 0, 1);
                float low = (1.0f - v) * (1.0f - v) * (1.0f - v);
                pixel(c, xx, yy,
                      mix3(c, xx, yy, PAPER, YELLOW, RED, 0.10f * v, 0.42f + 0.50f * v,
                           0.60f * low));
            }
            rect(c, xx, top - 1, 1, 2, BLACK);
        }
    }
    if (!dome) {
        /* Polar night: the field is the sky itself, a starry black with the twilight glow of
         * the sun that stays below the horizon, read per 2 px column so no grain is alone. */
        for (int xx = x; xx < x + w; ++xx) {
            double alt = home_sky_altitude(k->lat, k->lon, sky_time_at(k, (xx & ~1) - x, w));
            tone_t t = sky_tone(alt);
            for (int yy = y; yy < horizon; ++yy)
                pixel(c, xx, yy, mix3(c, xx, yy, t.a, t.b, t.d, t.wa, t.wb, t.wd));
        }
    }
    for (int yy = horizon + 1; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            pixel(c, xx, yy, mix(c, xx, yy, PAPER, BLACK, 0.22f));
    rect(c, x, horizon, w, 1, BLACK);
    double f = clamp((double)(k->now - k->midnight) / SKY_DAY_S, 0, 1);
    double alt = home_sky_altitude(k->lat, k->lon, k->now);
    int cx = imin(imax(x + (int)(f * (w - 1)), x + 10), x + w - 11);
    if (alt < 0) {
        sky_disc(c, cx, horizon + 9, 6, false);
        return;
    }
    /* The sun rides the curve it is drawn on, and its halo stops short of the
     * header rule even when the dome reaches the top of the field. */
    double u = k->s.polar_day ? f : dome ? clamp((f - rise) / (set - rise), 0, 1) : 0.5;
    double amp = sin(u * 3.14159265358979323846);
    if (k->s.polar_day)
        amp = 0.45 + 0.55 * amp;
    sun_mark(c, cx, imax(horizon - 3 - (int)(amp * (sky_h - 6)), y + 11), 8, alt);
}
/* Rhythm: the altitude of the sun through the day, the daylight filled in
 * yellow under the curve, the night a starry black above it. */
static void sky_curve(canvas_t *c, const sky_t *k, int x, int y, int w, int h)
{
    double high = home_sky_altitude(k->lat, k->lon, k->s.solar_noon);
    double low = home_sky_altitude(k->lat, k->lon, k->s.solar_noon + SKY_DAY_S / 2);
    if (high < 6)
        high = 6;
    if (low > -6)
        low = -6;
    double inv = (h - 1) / (high - low);
    int horizon = y + (int)(high * inv), last = -1;
    float t = 0.0f;
    for (int i = 0; i < w; ++i) {
        double alt = home_sky_altitude(k->lat, k->lon, sky_time_at(k, i, w));
        if ((i & 3) == 0)
            t = night_cover(alt);
        int cy = imin(imax(y + (int)((high - alt) * inv), y), y + h - 1);
        for (int yy = y; yy < y + h; ++yy)
            pixel(c, x + i, yy, mix(c, x + i, yy, PAPER, BLACK, t));
        if (alt > 0)
            for (int yy = cy; yy < horizon; ++yy) {
                /* Denser just under the curve, and read from the top of each colour
                 * cell so the ramp never leaves a single grain of colour alone. */
                float up = clampf((float)(horizon - (yy & ~1)) / (float)(horizon - cy + 1), 0, 1);
                float scale = 0.55f + 0.45f * up;
                tone_t g = warm_fill(alt, scale);
                pixel(c, x + i, yy,
                      mix3(c, x + i, yy, g.a, g.b, g.d, g.wa + 1.0f - scale, g.wb, g.wd));
            }
        int ink = t > 0.5f ? PAPER : BLACK;
        if (last >= 0)
            line(c, x + i - 1, last, x + i, cy, ink);
        rect(c, x + i, cy, 1, 2, ink);
        last = cy;
        if ((i & 3) < 2)
            pixel(c, x + i, horizon, ink);
    }
}
/* Rhythm: the sun on its own curve, drawn before the marker so that the halo,
 * which is dithered, cannot break the solid red column of "now". */
static void sky_sun_on_curve(canvas_t *c, const sky_t *k, int x, int y, int w, int h)
{
    double alt = home_sky_altitude(k->lat, k->lon, k->now);
    double high = home_sky_altitude(k->lat, k->lon, k->s.solar_noon);
    double low = home_sky_altitude(k->lat, k->lon, k->s.solar_noon + SKY_DAY_S / 2);
    if (high < 6)
        high = 6;
    if (low > -6)
        low = -6;
    double f = clamp((double)(k->now - k->midnight) / SKY_DAY_S, 0, 1);
    int cx = imin(imax(x + (int)(f * (w - 1)), x + 12), x + w - 13);
    int cy = imin(imax(y + (int)((high - alt) * (h - 1) / (high - low)), y + 12), y + h - 13);
    sun_mark(c, cx, cy, 5, alt);
}
static void sky_footer(canvas_t *c, const home_config_t *cfg, int64_t now)
{
    int lang = lang_of(cfg);
    char date[64];
    stamp(date, sizeof date, now, lang, cfg->clock24, cfg->timezone);
    rect(c, 14, 261, 372, 1, BLACK);
    txt(c, 14, 265, 372, 17, 0,
        tr(lang, "Computed on the device · nothing downloaded",
           "Liczone na urządzeniu · nic nie pobiera"));
    txt(c, 14, 281, 200, 17, 0, date);
    int tw = width(0, cfg->location, sizeof cfg->location);
    if (cfg->location[0] && tw <= 150)
        text(c, 386 - tw, 281, tw, 17, 0, BLACK, cfg->location, sizeof cfg->location);
}
static void sky(canvas_t *c, const home_config_t *cfg, int64_t now)
{
    int lang = lang_of(cfg);
    int style = cfg->style[HOME_SKY] <= HOME_ATLAS ? cfg->style[HOME_SKY] : HOME_PRINT;
    static const int head[] = {4, 5, 7, 3, 2}, small[] = {1, 0}, larger[] = {2, 1, 0};
    static const int title[] = {3, 2, 1}, wide[] = {3, 2, 1, 0};
    sky_t k;
    char hours[96], length[96], label[64], value[64], note[168], lit[48];

    if (!cfg->location_ready) {
        top(c, cfg, tr(lang, "SKY", "NIEBO"));
        empty(c, cfg, HOME_SKY, HOME_EMPTY);
        return;
    }
    c->raster = c->brush; /* the user's brush on the sky's tones (D-HOME-CC-23) */
    if (!time_valid(now) || !sky_midnight(cfg->timezone, now, &k.midnight) ||
        !home_sky_day(cfg->latitude, cfg->longitude, k.midnight, &k.s)) {
        status(c, cfg->name, sizeof cfg->name, tr(lang, "Sky needs the time.", "Niebo czeka na czas."),
               tr(lang,
                  "Home reads the clock from the internet, and the sun and the moon appear here as "
                  "soon as it has one.",
                  "Home bierze godzinę z internetu — słońce i księżyc pojawią się tutaj, gdy tylko "
                  "ją pozna."));
        return;
    }
    k.now = now;
    k.lat = cfg->latitude;
    k.lon = cfg->longitude;
    top(c, cfg, tr(lang, "SKY", "NIEBO"));
    sun_hours_text(hours, sizeof hours, cfg, &k.s);
    day_length_text(length, sizeof length, cfg, &k.s);
    moon_note(note, sizeof note, &k.s, lang);
    moon_lit_text(lit, sizeof lit, &k.s, lang);
    bool waxing =
        k.s.moon_phase >= HOME_MOON_WAXING_CRESCENT && k.s.moon_phase <= HOME_MOON_WAXING_GIBBOUS;
    int hours_font =
        cfg->large_text ? fit_font(hours, 372, larger, 3) : fit_font(hours, 372, small, 2);

    if (style == HOME_RHYTHM) {
        txt(c, 14, 38, 310, 34, cfg->large_text ? fit_font(hours, 310, wide, 4)
                                                : fit_font(hours, 310, larger, 3),
            hours);
        moon_disc(c, 356, 56, 20, k.s.moon_fraction, waxing);
        sky_curve(c, &k, 14, 80, 372, 130);
        sky_sun_on_curve(c, &k, 14, 80, 372, 130);
        sky_now(c, &k, 14, 80, 372, 130);
        sky_hours_axis(c, cfg, 14, 209, 372);
        snprintf(value, sizeof value, "%s · %s", moon_name(k.s.moon_phase, lang), lit);
        txt(c, 14, 230, 372, 30,
            cfg->large_text ? fit_font(value, 372, larger, 3) : fit_font(value, 372, small, 2),
            value);
    } else if (style == HOME_ATLAS) {
        const char *name = moon_name(k.s.moon_phase, lang);
        txt(c, 14, 42, 184, 72, fit_wrapped(name, 184, title, 3), name);
        txt(c, 14, 120, 184, 21, 1, lit);
        txt(c, 14, 144, 184, 21, 1, note);
        txt(c, 14, 170, 184, 18, 0, length);
        moon_disc(c, 292, 116, 80, k.s.moon_fraction, waxing);
        txt(c, 14, 200, 372, 30, hours_font, hours);
        sky_horizon_band(c, &k, 14, 232, 372, 8);
        sky_strip(c, &k, 14, 242, 372, 18, true);
        sky_now(c, &k, 14, 242, 372, 18);
    } else {
        bool up = home_sky_altitude(k.lat, k.lon, now) >= 0;
        if (k.s.polar_day || k.s.polar_night) {
            snprintf(label, sizeof label, "%s",
                     k.s.polar_day ? tr(lang, "Midnight sun", "Dzień polarny")
                                   : tr(lang, "Polar night", "Noc polarna"));
            snprintf(value, sizeof value, "%d h", k.s.polar_day ? 24 : 0);
        } else {
            snprintf(label, sizeof label, "%s",
                     up ? tr(lang, "Sunset", "Zachód") : tr(lang, "Sunrise", "Wschód"));
            if (!event_clock(value, sizeof value, cfg, up ? k.s.sunset : k.s.sunrise, !cfg->clock24))
                snprintf(value, sizeof value, "—");
        }
        txt(c, 12, 40, 184, 78, fit_font(value, 182, head, 5), value);
        snprintf(note, sizeof note, "%s · %s", label, length);
        txt(c, 14, 128, 372, 21, fit_font(note, 372, small, 2), note);
        txt(c, 14, 152, 372, 26, hours_font, hours);
        sun_dome(c, &k, 200, 36, 186, 92);
        sky_strip(c, &k, 14, 186, 372, 52, false);
        sky_now(c, &k, 14, 186, 372, 52);
        sky_hours_axis(c, cfg, 14, 240, 372);
    }
    c->raster = RASTER_NOISE;
    sky_footer(c, cfg, now);
}
static void pokemon(canvas_t *c, const home_config_t *cfg, const home_pokemon_t *p, int64_t now)
{
    top(c, cfg, "POKEMON OF THE DAY");
    if (!p->meta.valid) {
        txt(c, 14, 60, 372, 65, 3, "Meet a Pokemon each day.");
        txt(c, 14, 145, 372, 100, 1,
            p->meta.state == HOME_ERROR
                ? "Could not download today's Pokemon. Home will retry in 30 minutes."
                : "Enable Pokemon in your phone panel and connect Home to Wi-Fi. A sprite and introduction will appear here.");
        return;
    }
    char title[96];
    snprintf(title, sizeof title, "#%03u  %s", p->id, p->name);
    int style = cfg->style[HOME_POKEMON];
    int sx = style == HOME_ATLAS ? 194 : 8, sy = 60;
    int tx = style == HOME_ATLAS ? 14 : 208;
    if (style == HOME_RHYTHM) {
        sx = 8;
        sy = 64;
    }
    txt(c, 14, 36, 372, 30, cfg->large_text ? 2 : 1, title);
    int left = 96, right = -1, above = 96, below = -1;
    for (int y = 0; y < 96; y++)
        for (int x = 0; x < 96; x++) {
            unsigned at = y * 96 + x, color = (p->sprite[at / 4] >> (6 - 2 * (at % 4))) & 3;
            if (color != PAPER) {
                left = imin(left, x);
                right = imax(right, x);
                above = imin(above, y);
                below = imax(below, y);
            }
        }
    if (right >= left && below >= above) {
        int zoom = imin(4, imin(192 / (right - left + 1), 192 / (below - above + 1)));
        int ox = sx + (192 - zoom * (right - left + 1)) / 2;
        int oy = sy + (192 - zoom * (below - above + 1)) / 2;
        for (int y = above; y <= below; y++)
            for (int x = left; x <= right; x++) {
                unsigned at = y * 96 + x, color = (p->sprite[at / 4] >> (6 - 2 * (at % 4))) & 3;
                if (!c->intensity && color >= 2)
                    color = BLACK;
                rect(c, ox + zoom * (x - left), oy + zoom * (y - above), zoom, zoom, color);
            }
    }
    txt(c, tx, 76, 178, 42, 1, p->genus);
    txt(c, tx, 122, 178, 130, cfg->large_text ? 1 : 0, p->introduction);
    if (style == HOME_RHYTHM)
        rect(c, 202, 74, 2, 170, RED);
    source_footer(c, cfg, &p->meta, now, "PokeAPI / Pokemon", p->meta.fetched_at);
}
void home_render(const home_config_t *cfg, const home_data_t *data, home_screen_t screen,
                 int64_t now, uint8_t frame[HOME_FRAME_BYTES])
{
    if (!frame)
        return;
    memset(frame, 0x55, HOME_FRAME_BYTES);
    if (!cfg || !data)
        return;
    canvas_t c = {frame, (cfg->texture == 2 || cfg->texture == 4) ? cfg->texture : 1,
                  imin(cfg->intensity, 2), lang_of(cfg), RASTER_NOISE,
                  cfg->brush <= RASTER_GRID ? cfg->brush : RASTER_NOISE};
    if (screen == HOME_WEATHER && !cfg->location_ready) {
        top(&c, cfg, tr(c.lang, "Weather", "Pogoda"));
        empty(&c, cfg, HOME_WEATHER, HOME_EMPTY);
    } else if (screen == HOME_WEATHER)
        weather(&c, cfg, &data->weather, &data->air, now);
    else if (screen == HOME_SKY)
        sky(&c, cfg, now);
    else if (screen == HOME_FEED)
        feed(&c, cfg, &data->feed, now);
    else if (screen == HOME_AIR)
        air(&c, cfg, &data->air, now);
    else if (screen == HOME_POKEMON)
        pokemon(&c, cfg, &data->pokemon, now);
    else if (screen == HOME_NOTE)
        note(&c, cfg, now);
    else {
        /* Status keeps its fixed texture and intensity, as home_render_status() does. */
        canvas_t card = {frame, 1, 2, c.lang, RASTER_NOISE, RASTER_NOISE};
        /* Air exists in the settings since 0.5.0; its card comes next. */
        const char *title = screen == HOME_AIR ? tr(c.lang, "Air", "Powietrze")
                                               : tr(c.lang, "Choose a screen.", "Wybierz ekran.");
        const char *body =
            screen == HOME_AIR
                ? tr(c.lang, "This screen arrives with the next update.",
                     "Ten ekran pojawi się w następnej aktualizacji.")
                : tr(c.lang, "Open the panel on your phone and choose what Home shows.",
                     "Otwórz panel w telefonie i wybierz, co ma pokazywać Home.");
        status(&card, cfg->name, sizeof cfg->name, title, body);
    }
}
/* ---- The "emini" card (0.6): what the device knows about itself. One screen you reach with
 * the button: battery with an estimate the device measured on itself, a few counters, a week of
 * battery, and a code that leads to the site. Colour carries meaning here too: the battery ramp
 * runs paper -> yellow -> red as it empties. */
static int battery_pigment(canvas_t *c, int x, int y, int percent)
{
    float t = 1.0f - (float)clamp(percent, 0, 100) / 100.0f; /* 0 full, 1 empty */
    return mix3(c, x, y, PAPER, YELLOW, RED, (1 - t) * (1 - t) * 1.2f, 2 * t * (1 - t) + 0.25f,
                t * t * 1.4f);
}
static void battery_bar(canvas_t *c, int x, int y, int w, int h, int percent, bool charging)
{
    rect(c, x, y, w, h, BLACK);
    rect(c, x + 2, y + 2, w - 4, h - 4, PAPER);
    rect(c, x + w, y + h / 3, 4, h / 3, BLACK); /* the cap of a battery */
    int fill = percent < 0 ? 0 : (w - 6) * clamp(percent, 0, 100) / 100;
    for (int yy = y + 3; yy < y + h - 3; ++yy)
        for (int xx = x + 3; xx < x + 3 + fill; ++xx)
            pixel(c, xx, yy, battery_pigment(c, xx, yy, percent));
    if (charging) /* a bolt in the empty part, drawn in the ink of the fill */
        for (int k = 0; k < 14; ++k) {
            int bx = x + w / 2 - 4 + (k < 7 ? k : 13 - k) / 2, by = y + 5 + k;
            rect(c, bx, by, 3, 1, BLACK);
        }
}
static void info_number(canvas_t *c, int x, int y, int w, const char *label, const char *value)
{
    txt(c, x, y, w, 15, 0, label);
    txt(c, x, y + 15, w, 26, 2, value);
}
static void info_week(canvas_t *c, const home_stats_t *s, int x, int y, int w, int h, int lang)
{
    txt(c, x, y, w, 15, 0, tr(lang, "BATTERY · LAST SEVEN DAYS", "BATERIA · OSTATNIE SIEDEM DNI"));
    int top = y + 18, hh = h - 22, pitch = w / HOME_BATTERY_DAYS;
    rect(c, x, top + hh, w, 1, BLACK);
    for (int k = 0; k < HOME_BATTERY_DAYS; ++k) {
        int day = s->battery_day[HOME_BATTERY_DAYS - 1 - k];
        int bx = x + k * pitch + 2, bw = (pitch - 6) & ~1;
        if (day < 0) {
            for (int yy = top + hh - 4; yy < top + hh; yy += 2)
                for (int xx = bx; xx < bx + bw; xx += 2)
                    pixel(c, xx, yy, BLACK);
            continue;
        }
        int bh = imax(4, (hh - 2) * clamp(day, 0, 100) / 100) & ~1;
        for (int yy = top + hh - bh; yy < top + hh; ++yy)
            for (int xx = bx; xx < bx + bw; ++xx)
                pixel(c, xx, yy, battery_pigment(c, xx, yy, day));
    }
    txt(c, x + w - 46, y, 46, 15, 0, tr(lang, "TODAY", "DZIŚ"));
}
void home_render_info(const home_config_t *cfg, const home_stats_t *s, int64_t now,
                      uint8_t frame[HOME_FRAME_BYTES])
{
    if (!frame || !cfg || !s)
        return;
    memset(frame, 0x55, HOME_FRAME_BYTES);
    canvas_t c = {frame, (cfg->texture == 2 || cfg->texture == 4) ? cfg->texture : 1,
                  imin(cfg->intensity, 2), lang_of(cfg), RASTER_NOISE,
                  cfg->brush <= RASTER_GRID ? cfg->brush : RASTER_NOISE};
    int lang = c.lang;
    top(&c, cfg, "EMINI");
    char value[32], line[96], a[24];
    /* A faint warm panel behind the counters: the right half of this card is otherwise all
     * black text, and the screen has four pigments (D-HOME-CC-24). */
    for (int y = 36; y < 170; ++y) {
        float fade = 0.17f * (1.0f - (float)((y & ~1) - 36) / 134.0f);
        for (int x = 198; x < 390; ++x)
            pixel(&c, x, y, mix(&c, x, y, PAPER, YELLOW, fade));
    }
    /* Battery, the loudest thing on this card. */
    txt(&c, 14, 40, 176, 15, 0, tr(lang, "BATTERY", "BATERIA"));
    if (s->percent >= 0)
        snprintf(value, sizeof value, "%d%%", s->percent);
    else
        snprintf(value, sizeof value, "—");
    txt(&c, 12, 54, 178, 62, 4, value);
    if (s->charging)
        snprintf(line, sizeof line, "%s", s->full ? tr(lang, "Charged", "Naładowana")
                                                  : tr(lang, "Charging", "Ładuje się"));
    else if (s->estimate_hours >= 48)
        snprintf(line, sizeof line, tr(lang, "About %d days", "Około %d dni"), s->estimate_hours / 24);
    else if (s->estimate_hours >= 0)
        snprintf(line, sizeof line, tr(lang, "About %d h", "Około %d h"), s->estimate_hours);
    else
        snprintf(line, sizeof line, "%s",
                 tr(lang, "Learning how long a charge lasts", "Uczy się, na jak długo starcza"));
    txt(&c, 14, 118, 176, 24, 1, line);
    battery_bar(&c, 14, 146, 168, 26, s->percent, s->charging);
    /* Counters: the numbers people photograph. */
    snprintf(value, sizeof value, "%lu", (unsigned long)s->pictures);
    info_number(&c, 208, 40, 178, tr(lang, "PICTURES DRAWN", "NARYSOWANYCH OBRAZÓW"), value);
    if (s->awake_hours >= 48)
        snprintf(value, sizeof value, tr(lang, "%lu d %lu h", "%lu d %lu h"),
                 (unsigned long)(s->awake_hours / 24), (unsigned long)(s->awake_hours % 24));
    else
        snprintf(value, sizeof value, "%lu h", (unsigned long)s->awake_hours);
    info_number(&c, 208, 82, 178, tr(lang, "AWAKE", "CZUWA"), value);
    snprintf(value, sizeof value, "%lu", (unsigned long)s->fetches);
    info_number(&c, 208, 124, 88, tr(lang, "DOWNLOADS", "POBRAŃ"), value);
    snprintf(value, sizeof value, "%lu ms", (unsigned long)s->render_ms);
    info_number(&c, 298, 124, 88, tr(lang, "DRAWN IN", "RYSOWANIE"), value);
    /* A week of battery, then where this thing lives and where it comes from. */
    info_week(&c, s, 14, 180, 286, 76, lang);
    home_qr_paint(frame, "https://emini.ink/home", 312, 172, 74, 74, NULL);
    rect(&c, 14, 261, 372, 1, BLACK);
    if (s->first_start > 0 && time_valid(now)) {
        long long days = (now - s->first_start) / 86400;
        snprintf(line, sizeof line, tr(lang, "With you for %lld days", "Z Tobą od %lld dni"), days);
    } else
        snprintf(line, sizeof line, "emini Home");
    txt(&c, 14, 265, 232, 17, 0, line);
    stamp(a, sizeof a, now, lang, cfg->clock24, cfg->timezone);
    txt(&c, 250, 265, 136, 17, 0, a);
    snprintf(line, sizeof line, "%s · %s · emini.ink/home", HOME_VERSION_TEXT,
             s->address[0] ? s->address : "home.local");
    txt(&c, 14, 281, 372, 17, 0, line);
}
void home_render_setup(const char *ssid, const char *password, const char *code,
                       const char *address, int lang, uint8_t frame[HOME_FRAME_BYTES])
{
    if (!frame)
        return;
    memset(frame, 0x55, HOME_FRAME_BYTES);
    canvas_t c = {frame, 1, 2, lang, RASTER_NOISE, RASTER_NOISE};
    char wifi_payload[256], password_line[96];
    bool bounded_inputs = ssid && password && code && address && bounded(ssid, 33) <= 32 &&
                          bounded(password, 64) <= 63 && bounded(code, 7) == 6 &&
                          bounded(address, 128) < 128;
    if (bounded_inputs) {
        snprintf(password_line, sizeof(password_line), "%s: %s", tr(lang, "Password", "Hasło"),
                 password);
        bool fit = width(0, ssid, 33) <= 372 &&
                   width(0, password_line, sizeof(password_line)) <= 372 &&
                   width(1, address, 128) <= 372;
        bool panel_url = !strncmp(address, "http://", 7) && !strpbrk(address, "?#@\r\n");
        if (fit && panel_url &&
            home_qr_wifi_text(ssid, password, wifi_payload, sizeof(wifi_payload)) &&
            home_qr_paint(frame, wifi_payload, 14, 48, 172, 136, NULL) &&
            home_qr_paint(frame, address, 214, 48, 172, 136, NULL)) {
            /* 26 px box: the 22 px font descends 25 rows below the box top. */
            txt(&c, 14, 3, 372, 26, 2, tr(lang, "Connect your phone.", "Połącz telefon."));
            txt(&c, 14, 29, 180, 17, 0, tr(lang, "1  JOIN WI-FI", "1  POŁĄCZ WI-FI"));
            txt(&c, 214, 29, 172, 17, 0, tr(lang, "2  OPEN HOME", "2  OTWÓRZ HOME"));
            // These values remain legible as a complete manual fallback.
            text(&c, 14, 184, 372, 17, 0, BLACK, ssid, 33);
            txt(&c, 14, 201, 372, 18, 0, password_line);
            text(&c, 14, 220, 372, 24, 1, BLACK, address, 128);
            txt(&c, 14, 253, 176, 21, 1, tr(lang, "3  Pairing code", "3  Kod parowania"));
            text(&c, 206, 246, 180, 37, 3, BLACK, code, 7);
            rect(&c, 14, 278, 372, 1, BLACK);
            txt(&c, 14, 282, 372, 17, 0, "emini.ink/home");
            // QR payload contains the private setup AP password; no token is
            // ever added to the panel URL. Frame access stays parent-private.
            memset(wifi_payload, 0, sizeof(wifi_payload));
            memset(password_line, 0, sizeof(password_line));
            return;
        }
    }
    // Unusual long fields or an encoding failure keep the complete established
    // manual setup instead of drawing a tiny/partial QR or hiding credentials.
    memset(frame, 0x55, HOME_FRAME_BYTES);
    memset(wifi_payload, 0, sizeof(wifi_payload));
    memset(password_line, 0, sizeof(password_line));
    txt(&c, 14, 7, 372, 26, 2, tr(lang, "Home, meet your phone.", "Home, poznaj swój telefon."));
    rect(&c, 14, 37, 372, 1, BLACK);
    txt(&c, 14, 42, 372, 18, 0,
        tr(lang, "1  JOIN THIS WI-FI NETWORK", "1  POŁĄCZ TELEFON Z TĄ SIECIĄ WI-FI"));
    text(&c, 14, 61, 372, 44, 1, BLACK, ssid ? ssid : "", 64);
    txt(&c, 14, 104, 372, 17, 0, tr(lang, "NETWORK PASSWORD", "HASŁO SIECI"));
    text(&c, 14, 122, 372, 60, 1, BLACK, password ? password : "", 128);
    txt(&c, 14, 185, 372, 18, 0,
        tr(lang, "2  OPEN THIS ADDRESS IN YOUR BROWSER", "2  OTWÓRZ TEN ADRES W PRZEGLĄDARCE"));
    text(&c, 14, 204, 372, 26, 2, BLACK, address ? address : "", 128);
    txt(&c, 14, 237, 160, 20, 1, tr(lang, "3  Pairing code", "3  Kod parowania"));
    text(&c, 202, 230, 184, 37, 3, BLACK, code ? code : "", 32);
    rect(&c, 14, 273, 372, 1, BLACK);
    txt(&c, 14, 279, 372, 18, 0, "emini.ink/home");
}
void home_render_status(const char *title, const char *body, int lang,
                        uint8_t frame[HOME_FRAME_BYTES])
{
    if (!frame)
        return;
    memset(frame, 0x55, HOME_FRAME_BYTES);
    canvas_t c = {frame, 1, 2, lang, RASTER_NOISE, RASTER_NOISE};
    status(&c, NULL, 0, title, body);
}
#ifdef HOME_TESTCARD
/* Panel measurement cards (plan 0.5.0, step 0.1). Compiled only with
 * -DHOME_TESTCARD=1, never part of a release image. Cell 1 px colour, cell 3
 * and blue noise are deliberate here, outside the R0 rule, to measure what the
 * pigments do before Renderer 2 picks its minimum colour cluster. */
static int card_threshold(int x, int y, int cell, bool noise)
{
    int cx = x / cell, cy = y / cell;
    return noise ? home_noise[(cy & 63) * 64 + (cx & 63)] : bayer[cy & 3][cx & 3] * 16 + 8;
}
static void card_patch(canvas_t *c, int x, int y, int w, int h, int a, int b, int cell, bool noise,
                       int percent)
{
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            pixel(c, xx, yy, percent * 256 / 100 > card_threshold(xx, yy, cell, noise) ? b : a);
}
static bool card_hatch(int x, int y, int angle, int period, int width)
{
    double rad = angle * 3.14159265358979323846 / 180.0;
    double m = fmod(-x * sin(rad) + y * cos(rad), (double)period);
    if (m < 0)
        m += period;
    return m < width;
}
typedef struct {
    const char *label;
    int a, b, cell;
    bool noise;
} card_row_t;
/* Card A: 0..100 % in steps of 10 %, one pigment pair per row, cell 2 px. */
static void card_ramps(canvas_t *c)
{
    static const card_row_t rows[10] = {
        {"P/Y B", PAPER, YELLOW, 2, false}, {"P/Y N", PAPER, YELLOW, 2, true},
        {"P/R B", PAPER, RED, 2, false},    {"P/R N", PAPER, RED, 2, true},
        {"Y/R B", YELLOW, RED, 2, false},   {"Y/R N", YELLOW, RED, 2, true},
        {"K/Y B", BLACK, YELLOW, 2, false}, {"K/Y N", BLACK, YELLOW, 2, true},
        {"K/R B", BLACK, RED, 2, false},    {"K/R N", BLACK, RED, 2, true},
    };
    txt(c, 2, 0, 396, 16, 0, "A · RAMPY 0–100 % · B BAYER 4×4 · N SZUM · KOMÓRKA 2 PX");
    for (int r = 0; r < 10; ++r) {
        int y = 16 + r * 26;
        txt(c, 2, y + 5, 44, 16, 0, rows[r].label);
        for (int i = 0; i <= 10; ++i)
            card_patch(c, 46 + i * 32, y, 32, 24, rows[r].a, rows[r].b, rows[r].cell,
                       rows[r].noise, i * 10);
    }
    for (int i = 0; i <= 10; ++i) {
        char s[8];
        snprintf(s, sizeof s, "%d", i * 10);
        txt(c, 46 + i * 32 + 2, 280, 30, 16, 0, s);
    }
}
/* Card B: cells 1..4 px, each at 25/50/75 %, Bayer and blue noise. */
static void card_cells(canvas_t *c)
{
    static const card_row_t rows[9] = {
        {"P/Y B", PAPER, YELLOW, 0, false}, {"P/Y N", PAPER, YELLOW, 0, true},
        {"P/R B", PAPER, RED, 0, false},    {"P/R N", PAPER, RED, 0, true},
        {"K/Y B", BLACK, YELLOW, 0, false}, {"K/R B", BLACK, RED, 0, false},
        {"Y/R B", YELLOW, RED, 0, false},   {"P/K B", PAPER, BLACK, 0, false},
        {"P/K N", PAPER, BLACK, 0, true},
    };
    txt(c, 2, 0, 396, 16, 0, "B · KOMÓRKA 1·2·3·4 PX, W GRUPIE 25 · 50 · 75 %");
    for (int g = 0; g < 4; ++g) {
        char s[8];
        snprintf(s, sizeof s, "%d PX", g + 1);
        txt(c, 46 + g * 87 + 2, 14, 80, 16, 0, s);
    }
    for (int r = 0; r < 9; ++r) {
        int y = 28 + r * 26;
        txt(c, 2, y + 5, 44, 16, 0, rows[r].label);
        for (int g = 0; g < 4; ++g)
            for (int k = 0; k < 3; ++k)
                card_patch(c, 46 + g * 87 + k * 29, y, 29, 24, rows[r].a, rows[r].b, g + 1,
                           rows[r].noise, 25 * (k + 1));
    }
    txt(c, 2, 280, 396, 16, 0, "B BAYER · N SZUM NIEBIESKI 64×64 · P PAPIER, K CZERŃ");
}
/* Card C: hatching, isolated dots, text on colour, solid edges. */
static void card_structure(canvas_t *c)
{
    static const int angles[4] = {0, 15, 45, 75}, widths[3] = {1, 2, 3};
    static const struct {
        const char *label;
        int a, b;
    } bands[3] = {{"Y/P", PAPER, YELLOW}, {"R/P", PAPER, RED}, {"K/P", PAPER, BLACK}};
    txt(c, 2, 0, 396, 16, 0, "C · KRESKI OKRES 8 PX: KĄT 0·15·45·75, GRUBOŚĆ 1·2·3 PX");
    for (int b = 0; b < 3; ++b) {
        int y = 16 + b * 32;
        txt(c, 2, y + 8, 44, 16, 0, bands[b].label);
        for (int a = 0; a < 4; ++a)
            for (int w = 0; w < 3; ++w) {
                int x0 = 46 + (a * 3 + w) * 29;
                for (int yy = y; yy < y + 30; ++yy)
                    for (int xx = x0; xx < x0 + 28; ++xx)
                        pixel(c, xx, yy,
                              card_hatch(xx, yy, angles[a], 8, widths[w]) ? bands[b].b : bands[b].a);
            }
    }
    txt(c, 2, 112, 396, 16, 0, "KROPKI 1·2·3·4 PX: Y NA P · R NA P · Y NA K · R NA K · K NA P");
    static const struct {
        int a, b;
    } dots[5] = {{PAPER, YELLOW}, {PAPER, RED}, {BLACK, YELLOW}, {BLACK, RED}, {PAPER, BLACK}};
    for (int g = 0; g < 5; ++g)
        for (int s = 1; s <= 4; ++s) {
            int x0 = 46 + g * 68 + (s - 1) * 17;
            rect(c, x0, 128, 16, 30, dots[g].a);
            for (int j = 0; j < 3; ++j)
                for (int i = 0; i < 2; ++i)
                    rect(c, x0 + 2 + i * 8, 130 + j * 9, s, s, dots[g].b);
        }
    rect(c, 14, 164, 186, 66, YELLOW);
    text(c, 18, 166, 178, 16, 0, BLACK, "Deszcz od 18:00 · 14–17 °C", 64);
    text(c, 18, 182, 178, 20, 1, BLACK, "Deszcz od 18:00 · 14°", 64);
    text(c, 18, 202, 178, 26, 2, BLACK, "Deszcz od 18:00", 64);
    rect(c, 206, 164, 180, 66, RED);
    text(c, 210, 166, 172, 16, 0, PAPER, "Deszcz od 18:00 · 14–17 °C", 64);
    text(c, 210, 182, 172, 20, 1, PAPER, "Deszcz od 18:00 · 14°", 64);
    text(c, 210, 202, 172, 26, 2, PAPER, "Deszcz od 18:00", 64);
    static const int fields[5] = {RED, YELLOW, BLACK, YELLOW, RED};
    for (int i = 0; i < 5; ++i)
        rect(c, 14 + i * 62, 236, 62, 40, fields[i]);
    rect(c, 324, 236, 62, 40, BLACK);
    rect(c, 325, 237, 60, 38, PAPER);
    txt(c, 2, 282, 396, 16, 0, "TEKST 12/16/22 PX NA KOLORZE · PEŁNE POLA I KRAWĘDZIE");
}
void home_render_testcard(int card, uint8_t frame[HOME_FRAME_BYTES])
{
    if (!frame)
        return;
    memset(frame, 0x55, HOME_FRAME_BYTES);
    canvas_t c = {frame, 1, 2, LANG_EN, RASTER_NOISE, RASTER_NOISE};
    if (card == 0)
        card_ramps(&c);
    else if (card == 1)
        card_cells(&c);
    else
        card_structure(&c);
}
#endif
