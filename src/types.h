#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================
//  VIEW IDs
// ============================================================
#define VIEW_STANDARD       0
#define VIEW_FIVEDAY        1
#define VIEW_MULTICITY      3
#define VIEW_VERSE          4

// ============================================================
//  WEATHER — CURRENT CONDITIONS
// ============================================================
struct CurrentWeather {
    float    temp;                  // °F
    float    feelsLike;             // °F
    float    rainAmt;               // inches in last hour (0 if none)
    float    windSpeed;             // mph
    uint16_t windDeg;               // 0–359
    uint8_t  humidity;              // 0–100 %
    float    pressure;              // inHg
    uint8_t  uvIndex;               // 0–11+
    float    visibility;            // miles
    uint8_t  cloudPct;              // 0–100 %
    uint8_t  precipChance;          // 0–100 % (from hourly[0])
    char     condition[24];         // e.g. "Partly Cloudy"
    uint8_t  conditionIcon;         // mapped icon index (see icons.h)
    uint32_t sunrise;               // unix epoch UTC
    uint32_t sunset;                // unix epoch UTC
    uint32_t nextSunrise;           // tomorrow's sunrise epoch for moon arc
};

// ============================================================
//  WEATHER — DAILY FORECAST (one entry per day)
// ============================================================
struct ForecastDay {
    char     day[4];                // "MON", "TUE", etc. null-terminated
    float    high;                  // °F
    float    low;                   // °F
    float    rainAmt;               // inches expected
    uint8_t  precipChance;          // 0–100 %
    uint8_t  windSpeed;             // mph
    uint8_t  cloudPct;              // 0–100 %
    char     condition[16];         // e.g. "Rainy"
    uint8_t  conditionIcon;         // mapped icon index
};

// ============================================================
//  WEATHER — CITY SNAPSHOT  (for multi-city view)
// ============================================================
struct CityWeather {
    float    temp;                  // °F
    float    rainAmt;               // inches
    uint8_t  precipChance;          // 0–100 %
    float    windSpeed;             // mph
    uint16_t windDeg;               // 0–359
    char     condition[16];
    uint8_t  conditionIcon;
    uint32_t localEpoch;            // current unix epoch at that city (same for MI cities)
};

// ============================================================
//  BIBLE VERSE
// ============================================================
struct VerseOfDay {
    char text[384];                 // verse body — AMP tends to be verbose
    char reference[40];             // e.g. "Jeremiah 29:11"
    char translation[8];            // "AMP"
};

// ============================================================
//  RTC MEMORY  — persists across deep sleep
//  Total size: ~1.1 KB  (well within 8 KB RTC SRAM limit)
// ============================================================
struct RtcData {
    // --- rotation state ---
    uint8_t  viewRotationIndex;     // index into VIEW_ROTATION[] array in config.h
    uint32_t lastVerseHour;         // hour (0–23) of last verse display (day-scoped)
    uint32_t lastVerseDay;          // day-of-year of last verse fetch

    // --- current weather (primary city) ---
    CurrentWeather current;

    // --- 5-day forecast ---
    ForecastDay forecast[5];

    // --- multi-city snapshots ---
    CityWeather cities[NUM_CITIES];

    // --- bible verse ---
    VerseOfDay verse;

    // --- fetch metadata ---
    uint32_t lastWeatherFetch;      // unix epoch of last successful weather fetch
    uint32_t lastVerseFetch;        // unix epoch of last successful verse fetch

    // --- validity flags ---
    bool     weatherValid;          // false until first successful fetch
    bool     verseValid;            // false until first successful fetch

    // --- boot counter (useful for debugging) ---
    uint32_t bootCount;
};

// ============================================================
//  WIND DIRECTION  — degrees to cardinal string
// ============================================================
inline const char* windDegToCardinal(uint16_t deg) {
    static const char* dirs[] = {
        "N","NNE","NE","ENE","E","ESE","SE","SSE",
        "S","SSW","SW","WSW","W","WNW","NW","NNW"
    };
    return dirs[((deg + 11) % 360) / 22];
}

// ============================================================
//  CONDITION ICON INDEX  — OWM icon code -> local index
//  Matches the bitmap array order in icons.h
// ============================================================
#define ICON_CLEAR_DAY      0
#define ICON_CLEAR_NIGHT    1
#define ICON_FEW_CLOUDS     2
#define ICON_SCATTERED      3
#define ICON_BROKEN         4
#define ICON_SHOWER_RAIN    5
#define ICON_RAIN           6
#define ICON_THUNDERSTORM   7
#define ICON_SNOW           8
#define ICON_MIST           9
#define ICON_UNKNOWN        10

inline uint8_t owmIconToIndex(const char* icon) {
    if (!icon) return ICON_UNKNOWN;
    // OWM icon codes: "01d","01n","02d","03d","04d","09d","10d","11d","13d","50d"
    if (icon[0] == '0' && icon[1] == '1') return (icon[2] == 'n') ? ICON_CLEAR_NIGHT : ICON_CLEAR_DAY;
    if (icon[0] == '0' && icon[1] == '2') return ICON_FEW_CLOUDS;
    if (icon[0] == '0' && icon[1] == '3') return ICON_SCATTERED;
    if (icon[0] == '0' && icon[1] == '4') return ICON_BROKEN;
    if (icon[0] == '0' && icon[1] == '9') return ICON_SHOWER_RAIN;
    if (icon[0] == '1' && icon[1] == '0') return ICON_RAIN;
    if (icon[0] == '1' && icon[1] == '1') return ICON_THUNDERSTORM;
    if (icon[0] == '1' && icon[1] == '3') return ICON_SNOW;
    if (icon[0] == '5' && icon[1] == '0') return ICON_MIST;
    return ICON_UNKNOWN;
}

// ============================================================
//  PRESSURE CONVERSION  (hPa -> inHg)
// ============================================================
inline float hpaToInHg(float hpa) {
    return hpa * 0.02953f;
}

// ============================================================
//  VISIBILITY CONVERSION  (meters -> miles)
// ============================================================
inline float metersToMiles(float meters) {
    return meters * 0.000621371f;
}
