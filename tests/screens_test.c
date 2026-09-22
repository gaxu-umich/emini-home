#include "home_parse.h"
#include "home_config.h"
#include "home_air.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void summary_test(const char *xml, const char *expected)
{
    home_feed_t feed = {0};
    char error[97];
    assert(home_parse_feed(xml, strlen(xml), &feed, 1789984800, error));
    assert(!strcmp(feed.summary, expected));
}
static void save_frame(const char *dir, const char *name, home_config_t *c, home_data_t *d,
                       int screen, int64_t now)
{
    uint8_t frame[HOME_FRAME_BYTES];
    char path[1024];
    home_render(c, d, screen, now, frame);
    snprintf(path, sizeof path, "%s/%s.frame", dir, name);
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(frame, 1, sizeof frame, f) == sizeof frame);
    fclose(f);
    if (screen == HOME_FEED)
        for (size_t i = 0; i < sizeof frame; i++)
            for (int shift = 0; shift < 8; shift += 2)
                assert(((frame[i] >> shift) & 3) < 2); /* no decorative coloured bar */
}
int home_screens_test(const char *weather_file, const char *outdir)
{
    const char *aqi_json = "{\"utc_offset_seconds\":0,\"hourly\":{\"time\":[\"2026-09-22T00:00\",\"2026-09-22T01:00\"],\"us_aqi\":[51,null]}}";
    home_air_t air = {0};
    char aqi_error[97];
    int64_t aqi_now = home_parse_time("2026-09-22T00:30:00Z");
    assert(home_parse_air(aqi_json, strlen(aqi_json), &air, aqi_now, aqi_error));
    assert(air.meta.valid && air.us_aqi == 51 && air.forecast_at == aqi_now - 1800);
    assert(home_parse_air(aqi_json, strlen(aqi_json), &air, aqi_now + 3600, aqi_error));
    assert(air.us_aqi == -1);
    assert(!home_parse_air(aqi_json, strlen(aqi_json), &air, aqi_now + 7200, aqi_error));
    assert(!home_parse_air(aqi_json, strlen(aqi_json), &air, aqi_now - 3600, aqi_error));
    const char *invalid_aqi[] = {
        "{\"hourly\":{\"time\":[\"2026-09-22T00:00\"],\"european_aqi\":[20]}}",
        "{\"hourly\":{\"time\":[\"2026-09-22T00:00\"],\"us_aqi\":[\"51\"]}}",
        "{\"hourly\":{\"time\":[\"2026-09-22T00:00\"],\"us_aqi\":[]}}",
        "{\"utc_offset_seconds\":3600,\"hourly\":{\"time\":[\"2026-09-22T00:00\"],\"us_aqi\":[51]}}",
        "{\"hourly\":{\"time\":[\"2026-09-22T00:00\",\"2026-09-22T02:00\"],\"us_aqi\":[51,60]}}"
    };
    for (size_t i = 0; i < sizeof invalid_aqi / sizeof invalid_aqi[0]; i++) {
        air.us_aqi = 42;
        assert(!home_parse_air(invalid_aqi[i], strlen(invalid_aqi[i]), &air, aqi_now, aqi_error));
        assert(air.us_aqi == 42); /* errors preserve the last good cache */
    }
    summary_test(
        "<rss><channel><title>News</title><item><title>Headline</title><description><![CDATA[<p>First <b>paragraph</b> &amp; more.</p><script>hidden</script><p>Next.</p>]]></description></item></channel></rss>",
        "First paragraph & more. Next.");
    summary_test(
        "<rss><channel><item><title>Headline</title><description>Fish &amp; chips.</description></item></channel></rss>",
        "Fish & chips.");
    summary_test(
        "<feed xmlns='http://www.w3.org/2005/Atom'><entry><title>Headline</title><summary type='html'>&lt;p&gt;Short summary.&lt;/p&gt;</summary><content type='text'>Long content.</content></entry></feed>",
        "Short summary.");
    summary_test(
        "<feed xmlns='http://www.w3.org/2005/Atom'><entry><title>Headline</title><summary type='xhtml'><div xmlns='http://www.w3.org/1999/xhtml'><p>One.</p><p>Two.</p><script>hidden</script></div></summary></entry></feed>",
        "One. Two.");
    summary_test(
        "<feed xmlns='http://www.w3.org/2005/Atom'><entry><title>Headline</title><content type='text'>Content fallback.</content></entry></feed>",
        "Content fallback.");
    summary_test("<rss><channel><item><title>No summary</title></item></channel></rss>", "");
    summary_test(
        "<rss><channel><item><title>中文标题</title><description>第一段摘要，包含中文。</description></item></channel></rss>",
        "第一段摘要，包含中文。");
    char longxml[4096], error[97];
    home_feed_t feed;
    strcpy(longxml, "<rss><channel><item><title>Title</title><description>");
    for (int i = 0; i < 400; i++)
        strcat(longxml, "内容");
    strcat(longxml, "</description></item></channel></rss>");
    assert(home_parse_feed(longxml, strlen(longxml), &feed, 1789984800, error));
    assert(strlen(feed.summary) <= 512 && home_utf8(feed.summary, 512, true));
    FILE *f = fopen(weather_file, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *json = malloc(size + 1);
    assert(json);
    assert(fread(json, 1, size, f) == (size_t)size);
    fclose(f);
    json[size] = 0;
    home_data_t *d = calloc(1, sizeof(*d));
    assert(d);
    int64_t now = home_parse_time("2026-09-22T00:00:00Z");
    assert(home_parse_weather_zone(json, size, &d->weather, now, "UTC", error));
    for (int i = 0; i < 8; i++) {
        assert(d->weather.days[i].valid);
        assert(d->weather.days[i].date == 20260922 + i);
        assert(d->weather.days[i].low <= d->weather.days[i].high);
        assert(d->weather.days[i].symbol[0]); /* next_6_hours after hourly horizon */
    }
    home_weather_t shifted;
    assert(home_parse_weather_zone(json, size, &shifted, now, "America/New_York", error));
    assert(shifted.days[0].date == 20260921);
    assert(home_parse_weather_zone(json, size, &shifted, now, "Asia/Tokyo", error));
    assert(shifted.days[0].date == 20260922);
    assert(!home_parse_weather_zone(json, size, &shifted, now, "invalid-zone", error));
    free(json);
    home_config_t c;
    home_config_defaults(&c);
    c.location_ready = true;
    strcpy(c.location, "Ann Arbor");
    d->weather.meta.state = HOME_READY;
    d->weather.meta.checked_at = d->weather.meta.fetched_at = now;
    d->weather.meta.expires_at = now + 3600;
    d->air.meta = d->weather.meta;
    d->air.us_aqi = 42;
    for (int large = 0; large < 2; large++) {
        c.large_text = large;
        char name[40];
        snprintf(name, sizeof name, "weather-%d", large);
        save_frame(outdir, name, &c, d, HOME_WEATHER, now);
    }
    strcpy(c.units, "F");
    save_frame(outdir, "weather-fahrenheit", &c, d, HOME_WEATHER, now);
    strcpy(c.units, "C");
    const int aqi_values[] = {50, 51, 100, 101, -1};
    for (size_t i = 0; i < sizeof aqi_values / sizeof aqi_values[0]; i++) {
        char name[40];
        d->air.us_aqi = aqi_values[i];
        snprintf(name, sizeof name, "weather-aqi-%d", aqi_values[i]);
        save_frame(outdir, name, &c, d, HOME_WEATHER, now);
    }
    d->air.meta.valid = false;
    d->air.us_aqi = 150;
    save_frame(outdir, "weather-aqi-unavailable", &c, d, HOME_WEATHER, now);
    memset(&d->weather.days[4], 0, sizeof d->weather.days[4]);
    save_frame(outdir, "weather-missing-day", &c, d, HOME_WEATHER, now);
    d->feed.meta = d->weather.meta;
    strcpy(d->feed.title, "New riverside park opens with walking trails and room for wildlife");
    strcpy(d->feed.source, "Local News");
    d->feed.published_at = now;
    strcpy(
        d->feed.summary,
        "The new park opens this weekend after two years of work. Visitors can explore riverside paths, shaded picnic areas and a restored wetland designed to give local birds a place to nest.");
    for (int style = 0; style < 3; style++)
        for (int large = 0; large < 2; large++) {
            c.style[HOME_FEED] = style;
            c.large_text = large;
            char name[40];
            snprintf(name, sizeof name, "news-%d-%d", style, large);
            save_frame(outdir, name, &c, d, HOME_FEED, now);
        }
    d->feed.summary[0] = 0;
    save_frame(outdir, "news-headline-only", &c, d, HOME_FEED, now);
    free(d);
    return 0;
}
