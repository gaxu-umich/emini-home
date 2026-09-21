#ifndef HOME_POKEMON_H
#define HOME_POKEMON_H
#include "home_types.h"
/* Original 151, one per UTC date. Independent of location and display language. */
unsigned home_pokemon_id(int64_t now);
bool home_pokemon_parse(const char *json, size_t size, unsigned id, home_pokemon_t *out);
/* Bounded, non-interlaced PNG decoder. Produces a 96x96 packed four-colour sprite. */
bool home_pokemon_png(const uint8_t *png, size_t size, uint8_t out[HOME_POKEMON_BYTES]);
#endif
