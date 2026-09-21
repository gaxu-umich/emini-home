# Pokémon of the day

The local `0.5.4-pokemon-outline` build adds a sixth optional screen. It is disabled by
default and reuses the existing phone panel, screen rotation and display driver.
No backend service or API key is needed.

## Use

After installing this build, reload the phone panel, open **Home → Pokémon**,
enable **In your collection**, and **Save settings**. Once the device has Wi-Fi
and a synchronized clock it downloads the card. Use **Show now** to display it;
select Pokémon in the Rhythm tab to keep it on screen or include it in rotation.
The new screen still uses the panel's normal full refresh, roughly 25 seconds.

The original 151 Pokémon cycle deterministically, one per UTC date. The species
name, category and introduction are English in this first version, regardless
of the display language. The API's first English Pokédex description is used.
Print and Rhythm show the sprite on the left; Atlas on the right. Sprites are
converted to black, white, yellow and red, composited onto white and enlarged
with integer pixel scaling. Dark details remain black, warm shades become solid red or yellow, and cool
colours become white. A black silhouette outline keeps white body parts visible.
There is no dithering. This is a stylized treatment, not original-colour reproduction.
Older converted sprites are discarded on upgrade and downloaded again.

## Data and caching

- Species: `https://pokeapi.co/api/v2/pokemon-species/{id}/`
- Sprite: `https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/{id}.png`
- Both use the existing bounded, certificate-verified HTTPS transport. Redirects
  remain on the respective host. Responses are capped at 128 KiB.
- PNG dimensions are limited to 96 × 96, with chunk CRC checks and bounded
  decompression. Non-interlaced indexed (1/2/4/8-bit), grayscale, RGB and RGBA
  (8-bit channels) are supported; unsupported formats fail without replacing
  the previous card. RGB/grayscale colour-key transparency is not supported.
- Text and the packed sprite are committed together, cached until midnight UTC,
  and persisted with the other sources. Refresh gestures do not bypass this cache.
  A `no-store` response remains in RAM only.
- A failed download retains the previous complete card and waits at least
  30 minutes before retrying (longer when the provider requests it).
- Disabling the screen stops future requests. An already-running request may
  complete, but its result is not published while the screen is disabled.

## Upgrade and limits

The partition table and bootloader are unchanged. The application must still fit
in the existing `0x3f0000`-byte slot; the ESP-IDF build verifies this. A compile-time
check also ensures the expanded source cache fits the 8 KiB NVS record limit.

Three- and five-screen settings are accepted and Pokémon is appended disabled.
Wi-Fi, pairing and user settings are retained. The old binary source cache has
a different size, so it is discarded once and weather/news/air are fetched again.
Downgrading to firmware that only accepts five screens may reset display settings;
keep a pre-update backup if you want an exact rollback.

Build with `docs/BUILD.md`. Follow the backup/write/verify procedure in
`docs/INSTALL.md`, using your locally built files. Do not use `idf.py flash`.
This feature has host tests and an ESP-IDF build check; it has not yet been
validated end to end on a physical NOTE4C.

## Tests

```sh
python3 tools/test_pokemon.py
```

The offline test generates PNG fixtures, runs the production parser and renderer
under AddressSanitizer and UndefinedBehaviorSanitizer, and checks config migration,
scheduling, phone-panel validation, all PNG row filters, indexed bit depths,
transparency, truncation, CRC failures, oversized images and invalid compressed data.
Host tests use zlib in place of the ESP32 ROM miniz inflater.

For a real response and sprite downloaded separately:

```sh
python3 tools/test_pokemon.py --species /tmp/pokemon-species-25.json \
  --sprite /tmp/pokemon-25.png --output-dir /tmp/pokemon-preview
```

This also writes 400 × 300 previews for each composition and text size.
