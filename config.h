#pragma once

// ============================================================
//  DISPLAY
// ============================================================
#define DISPLAY_WIDTH       792
#define DISPLAY_HEIGHT      272

// SPI pins — confirmed from Elecrow CrowPanel 5.79" schematic
#define PIN_EPD_CS          45
#define PIN_EPD_DC          46
#define PIN_EPD_RST         47
#define PIN_EPD_BUSY        48
#define PIN_EPD_MOSI        11
#define PIN_EPD_SCK         12
#define PIN_EPD_PWR         7   // Display power enable — must be HIGH before init

// Onboard buttons (active LOW, internal pull-up)
#define PIN_BTN_MENU        2
#define PIN_BTN_EXIT        1
#define PIN_ROT_UP          6
#define PIN_ROT_DOWN        4
#define PIN_ROT_PRESS       5

// ============================================================
//  TIMING  (all values in milliseconds unless noted)
// ============================================================
#define REFRESH_INTERVAL_MS         (7UL * 60UL * 1000UL)   // 7 minutes
#define DEEP_SLEEP_US(ms)           ((ms) * 1000ULL)         // helper

// View rotation order: 0=Standard, 1=FiveDay, 2=Standard, 3=MultiCity
// Bible verse (view 4) interrupts at top of hour, then resumes rotation
#define NUM_VIEWS                   4
static const uint8_t VIEW_ROTATION[NUM_VIEWS] = { 0, 1, 0, 3 };

#define VERSE_DISPLAY_MINUTES       7        // how long verse shows at top of hour
#define WEATHER_STALE_MINUTES       30       // show stale indicator if older than this

// ============================================================
//  LOCATIONS
// ============================================================
#define NUM_CITIES                  3

// Primary location — used on Standard Day view
#define PRIMARY_CITY_NAME           "Pentwater, MI"
#define PRIMARY_CITY_LAT            43.7814f
#define PRIMARY_CITY_LON            -86.4327f
#define PRIMARY_CITY_TZ             "America/Detroit"

// Multi-city locations
struct CityConfig {
    const char* name;
    const char* label;
    float       lat;
    float       lon;
    const char* tz;
};

static const CityConfig CITIES[NUM_CITIES] = {
    { "Pentwater, MI",     "Pentwater",    43.7814f,  -86.4327f, "America/Detroit" },
    { "Grand Valley, MI",  "Grand Valley", 43.0667f,  -85.5833f, "America/Detroit" },
    { "Alma, MI",          "Alma",         43.3789f,  -84.6602f, "America/Detroit" },
};

// ============================================================
//  WEATHER  (OpenWeatherMap)
// ============================================================
#define OWM_BASE_URL        "https://api.openweathermap.org/data/3.0/onecall"
#define OWM_UNITS           "imperial"      // imperial = °F, metric = °C
#define OWM_LANG            "en"

// ============================================================
//  BIBLE  (api.bible)
// ============================================================
#define BIBLE_BASE_URL      "https://api.scripture.api.bible/v1"
#define BIBLE_TRANSLATION   "AMP"

// AMP Bible ID on api.bible  (id: a81b73293d3080c9-01  dblId: a81b73293d3080c9)
#define BIBLE_ID            "a81b73293d3080c9-01"

// Passage fetch endpoint — verseId substituted at runtime
// e.g. https://api.scripture.api.bible/v1/bibles/{id}/passages/JER.29.11
#define BIBLE_PASSAGE_URL   BIBLE_BASE_URL "/bibles/" BIBLE_ID "/passages/"

// ============================================================
//  UNITS & DISPLAY PREFS
// ============================================================
#define TEMP_UNIT           "F"             // "F" or "C"
#define WIND_UNIT           "mph"           // "mph" or "kph"
#define PRESSURE_UNIT       "inHg"          // "inHg" or "hPa"
#define VISIBILITY_UNIT     "mi"            // "mi" or "km"
#define TIME_FORMAT_24H     false           // false = 12h, true = 24h

// ============================================================
//  WIFI
// ============================================================
#define WIFI_CONNECT_TIMEOUT_MS     15000
#define WIFI_RETRY_COUNT            3

// ============================================================
//  HARDWARE
// ============================================================
#define BOARD_LED_PIN       48              // S3-WROOM onboard LED — confirm
#define SERIAL_BAUD         115200
