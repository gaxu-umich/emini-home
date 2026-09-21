#include "home_pokemon.h"
#include "home_config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long n = ftell(f);
    assert(n >= 0);
    rewind(f);
    char *p = malloc((size_t)n + 1);
    assert(p);
    assert(fread(p, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    p[n] = 0;
    *size = n;
    return p;
}
static void settings(void)
{
    home_config_t c, loaded;
    char error[128];
    home_config_defaults(&c);
    assert(!c.enabled[HOME_POKEMON]);
    assert(home_screen_index("pokemon") == HOME_POKEMON);
    for (int count = 3; count <= 6; count++) {
        if (count == 4)
            continue;
        cJSON *j = home_config_json(&c, false);
        assert(j);
        cJSON *enabled = cJSON_GetObjectItemCaseSensitive(j, "enabled");
        cJSON *order = cJSON_GetObjectItemCaseSensitive(j, "order");
        cJSON *styles = cJSON_GetObjectItemCaseSensitive(j, "styles");
        for (int i = HOME_SCREEN_COUNT - 1; i >= count; i--) {
            cJSON_DeleteItemFromArray(enabled, i);
            cJSON_DeleteItemFromArray(order, i);
            cJSON_DeleteItemFromObjectCaseSensitive(styles, home_screen_name(i));
        }
        char *json = cJSON_PrintUnformatted(j);
        assert(json);
        assert(home_config_decode(json, strlen(json), &loaded, NULL, false, error));
        assert(!strcmp(loaded.feed_url, c.feed_url));
        assert(!loaded.enabled[HOME_POKEMON]);
        for (int i = 0; i < HOME_SCREEN_COUNT; i++)
            assert(loaded.order[i] == i);
        free(json);
        cJSON_Delete(j);
    }
    c.enabled[HOME_POKEMON] = true;
    c.fixed_screen = HOME_POKEMON;
    c.style[HOME_POKEMON] = HOME_ATLAS;
    cJSON *j = home_config_json(&c, false);
    char *json = cJSON_PrintUnformatted(j);
    assert(home_config_decode(json, strlen(json), &loaded, NULL, false, error));
    assert(loaded.enabled[HOME_POKEMON] && loaded.fixed_screen == HOME_POKEMON);
    assert(loaded.style[HOME_POKEMON] == HOME_ATLAS);
    home_data_t data = {0};
    c.quiet_enabled = false;
    assert(home_auto_screen(&c, &data, NULL, HOME_NOTE, 0) != HOME_POKEMON);
    data.pokemon.meta.valid = true;
    assert(home_auto_screen(&c, &data, NULL, HOME_NOTE, 0) == HOME_POKEMON);
    FILE *f = fopen("config.json", "w");
    assert(f);
    fputs(json, f);
    fclose(f);
    free(json);
    cJSON_Delete(j);
}
int main(int argc, char **argv)
{
    assert(argc >= 2);
    if (!strcmp(argv[1], "settings")) {
        settings();
        return 0;
    }
    if (!strcmp(argv[1], "png")) {
        assert(argc == 5);
        size_t n;
        uint8_t *p = read_file(argv[2], &n);
        uint8_t out[HOME_POKEMON_BYTES];
        bool ok = home_pokemon_png(p, n, out);
        assert(ok == (atoi(argv[3]) != 0));
        if (ok) {
            FILE *f = fopen(argv[4], "wb");
            assert(f);
            fwrite(out, 1, sizeof out, f);
            fclose(f);
        }
        free(p);
        return 0;
    }
    assert(!strcmp(argv[1], "card") && argc == 5);
    size_t n;
    char *json = read_file(argv[2], &n);
    home_data_t *d = calloc(1, sizeof(*d));
    assert(d);
    assert(!home_pokemon_parse(json, n, 0, &d->pokemon));
    assert(!home_pokemon_parse(json, n, 26, &d->pokemon));
    assert(!home_pokemon_parse(json, n / 2, 25, &d->pokemon));
    assert(home_pokemon_parse(json, n, 25, &d->pokemon));
    assert(!strcmp(d->pokemon.name, "Pikachu"));
    assert(!strchr(d->pokemon.introduction, '\n') && !strchr(d->pokemon.introduction, '\f'));
    assert(home_pokemon_id(0) == 0);
    assert(home_pokemon_id(1704067200) == home_pokemon_id(1704067200 + 86399));
    assert(home_pokemon_id(1704067200) != home_pokemon_id(1704067200 + 86400));
    free(json);
    uint8_t *png = read_file(argv[3], &n);
    assert(home_pokemon_png(png, n, d->pokemon.sprite));
    free(png);
    int64_t now = 1789992000;
    d->pokemon.meta = (home_source_meta_t){.valid = true,
                                           .state = HOME_READY,
                                           .fetched_at = now,
                                           .checked_at = now,
                                           .expires_at = now + 86400};
    home_config_t c;
    home_config_defaults(&c);
    uint8_t frame[HOME_FRAME_BYTES];
    char path[1024];
    for (int style = 0; style < 3; style++)
        for (int large = 0; large < 2; large++) {
            c.style[HOME_POKEMON] = style;
            c.large_text = large;
            home_render(&c, d, HOME_POKEMON, now, frame);
            snprintf(path, sizeof path, "%s/pokemon-%d-%d.frame", argv[4], style, large);
            FILE *f = fopen(path, "wb");
            assert(f);
            fwrite(frame, 1, sizeof frame, f);
            fclose(f);
        }
    d->pokemon.meta.valid = false;
    d->pokemon.meta.state = HOME_ERROR;
    home_render(&c, d, HOME_POKEMON, now, frame);
    free(d);
    return 0;
}
