#ifndef HOME_AIR_H
#define HOME_AIR_H
#include "home_types.h"

/* Weather's US AQI from Open-Meteo Air Quality (CC BY 4.0).
 * Requests hourly=us_aqi, forecast_days=1, timezone=UTC. Keeps only the
 * current hour; null/out-of-range values become -1. Rejects missing series,
 * malformed JSON, irregular hours and non-UTC data. Assigns *out only on
 * success; the caller owns HTTP freshness metadata. Input is bounded to 128 KiB. */
bool home_parse_air(const char *json, size_t len, home_air_t *out, int64_t now, char error[97]);
#endif
