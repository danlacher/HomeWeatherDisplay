#include "weather.h"
#include "config.h"
#include "secrets.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ============================================================
//  INTERNAL HELPERS
// ============================================================

// Build the OWM One Call 3.0 URL for a given lat/lon.
// exclude=minutely,alerts to keep payload small
static String buildOwmUrl(float lat, float lon) {
    String url = OWM_BASE_URL;
    url += "?lat="     + String(lat, 4);
    url += "&lon="     + String(lon, 4);
    url += "&exclude=minutely,alerts";
    url += "&units="   + String(OWM_UNITS);
    url += "&lang="    + String(OWM_LANG);
    url += "&appid="   + String(OWM_API_KEY);
    return url;
}

// Fetch raw JSON string from a URL.
// Returns empty string on failure.
static String httpGet(const String& url) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(10000);
    http.addHeader("Accept", "application/json");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[weather] HTTP error %d for %s\n", code, url.c_str());
        http.end();
        return "";
    }

    String body = http.getString();
    http.end();
    return body;
}

// Map OWM daily pop (probability of precipitation, 0.0–1.0) to uint8_t percent
static uint8_t popToPercent(float pop) {
    return (uint8_t)(pop * 100.0f + 0.5f);
}

// Format a unix epoch into a short day-of-week string e.g. "MON"
static void epochToDayStr(uint32_t epoch, char* out, size_t len) {
    static const char* days[] = { "SUN","MON","TUE","WED","THU","FRI","SAT" };
    time_t t = (time_t)epoch;
    struct tm* tm_info = gmtime(&t);
    strncpy(out, days[tm_info->tm_wday], len);
    out[len - 1] = '\0';
}

// Parse OWM rain object — returns inches (OWM gives mm)
static float parseRainMm(JsonObjectConst obj, const char* key) {
    if (obj[key].isNull()) return 0.0f;
    // OWM nests rain as { "1h": mm } under current, or just mm under hourly
    if (obj[key].is<JsonObjectConst>()) {
        float mm = obj[key]["1h"] | 0.0f;
        return mm * 0.0393701f;         // mm -> inches
    }
    float mm = obj[key] | 0.0f;
    return mm * 0.0393701f;
}

// ============================================================
//  PARSE CURRENT CONDITIONS
// ============================================================
static void parseCurrent(JsonObjectConst cur,
                         JsonArrayConst  hourly,
                         JsonObjectConst daily0,
                         CurrentWeather& out) {
    out.temp         = cur["temp"]       | 0.0f;
    out.feelsLike    = cur["feels_like"] | 0.0f;
    out.humidity     = cur["humidity"]   | 0;
    out.cloudPct     = cur["clouds"]     | 0;
    out.uvIndex      = (uint8_t)(cur["uvi"].as<float>() + 0.5f);
    out.windSpeed    = cur["wind_speed"] | 0.0f;
    out.windDeg      = cur["wind_deg"]   | 0;
    out.sunrise      = cur["sunrise"]    | 0;
    out.sunset       = cur["sunset"]     | 0;
    out.visibility   = metersToMiles(cur["visibility"] | 0.0f);
    out.pressure     = hpaToInHg(cur["pressure"] | 1013.0f);
    out.rainAmt      = parseRainMm(cur, "rain");

    // Condition text + icon from first weather entry
    JsonArrayConst weather = cur["weather"];
    if (weather.size() > 0) {
        const char* desc = weather[0]["description"] | "Unknown";
        strncpy(out.condition, desc, sizeof(out.condition) - 1);
        out.condition[sizeof(out.condition) - 1] = '\0';
        // Capitalise first letter
        if (out.condition[0] >= 'a' && out.condition[0] <= 'z')
            out.condition[0] -= 32;

        const char* icon = weather[0]["icon"] | "";
        out.conditionIcon = owmIconToIndex(icon);
    }

    // Precip chance comes from hourly[0] (current hour)
    if (hourly.size() > 0) {
        out.precipChance = popToPercent(hourly[0]["pop"] | 0.0f);
    } else {
        out.precipChance = popToPercent(daily0["pop"] | 0.0f);
    }
}

// ============================================================
//  PARSE 5-DAY FORECAST
//  OWM daily[0] = today, daily[1..5] = next 5 days
//  We use daily[0] as "today" and daily[1..5] as forecast days
// ============================================================
static void parseForecast(JsonArrayConst daily, ForecastDay out[5]) {
    // Start from index 1 (tomorrow) and go up to 5 days
    for (int i = 0; i < 5; i++) {
        int di = i + 1;
        if (di >= (int)daily.size()) break;

        JsonObjectConst day = daily[di];
        out[i].high        = day["temp"]["max"]  | 0.0f;
        out[i].low         = day["temp"]["min"]  | 0.0f;
        out[i].precipChance = popToPercent(day["pop"] | 0.0f);
        out[i].windSpeed   = (uint8_t)(day["wind_speed"].as<float>() + 0.5f);
        out[i].cloudPct    = day["clouds"] | 0;
        out[i].rainAmt     = parseRainMm(day, "rain");

        epochToDayStr(day["dt"] | 0, out[i].day, sizeof(out[i].day));

        JsonArrayConst weather = day["weather"];
        if (weather.size() > 0) {
            const char* desc = weather[0]["description"] | "Unknown";
            strncpy(out[i].condition, desc, sizeof(out[i].condition) - 1);
            out[i].condition[sizeof(out[i].condition) - 1] = '\0';
            if (out[i].condition[0] >= 'a' && out[i].condition[0] <= 'z')
                out[i].condition[0] -= 32;

            const char* icon = weather[0]["icon"] | "";
            out[i].conditionIcon = owmIconToIndex(icon);
        }
    }
}

// ============================================================
//  PARSE CITY SNAPSHOT  (current only, no forecast)
// ============================================================
static void parseCitySnapshot(JsonObjectConst cur,
                               JsonArrayConst  hourly,
                               CityWeather&    out) {
    out.temp         = cur["temp"]       | 0.0f;
    out.windSpeed    = cur["wind_speed"] | 0.0f;
    out.windDeg      = cur["wind_deg"]   | 0;
    out.rainAmt      = parseRainMm(cur, "rain");
    out.localEpoch   = cur["dt"]         | 0;

    if (hourly.size() > 0)
        out.precipChance = popToPercent(hourly[0]["pop"] | 0.0f);

    JsonArrayConst weather = cur["weather"];
    if (weather.size() > 0) {
        const char* desc = weather[0]["description"] | "Unknown";
        strncpy(out.condition, desc, sizeof(out.condition) - 1);
        out.condition[sizeof(out.condition) - 1] = '\0';
        if (out.condition[0] >= 'a' && out.condition[0] <= 'z')
            out.condition[0] -= 32;

        const char* icon = weather[0]["icon"] | "";
        out.conditionIcon = owmIconToIndex(icon);
    }
}

// ============================================================
//  CORE FETCH + PARSE  (shared by all public functions)
// ============================================================
static bool fetchAndParse(float lat, float lon,
                          CurrentWeather* current,
                          ForecastDay     forecast[5],
                          CityWeather*    city) {
    String url  = buildOwmUrl(lat, lon);
    String body = httpGet(url);

    if (body.isEmpty()) {
        Serial.println("[weather] Empty response");
        return false;
    }

    // OWM One Call response is large — use PSRAM-backed allocator
    // N8R8 has 8MB PSRAM; JsonDocument uses heap by default.
    // 32KB is comfortable for One Call with hourly truncated.
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[weather] JSON parse error: %s\n", err.c_str());
        return false;
    }

    JsonObjectConst cur     = doc["current"];
    JsonArrayConst  hourly  = doc["hourly"];
    JsonArrayConst  daily   = doc["daily"];

    if (current && forecast) {
        parseCurrent(cur, hourly, daily[0], *current);
        parseForecast(daily, forecast);
    }

    if (city) {
        parseCitySnapshot(cur, hourly, *city);
    }

    return true;
}

// ============================================================
//  PUBLIC API
// ============================================================
bool fetchPrimaryWeather(CurrentWeather& current, ForecastDay forecast[5]) {
    Serial.println("[weather] Fetching primary city...");
    return fetchAndParse(PRIMARY_CITY_LAT, PRIMARY_CITY_LON,
                         &current, forecast, nullptr);
}

bool fetchCityWeather(float lat, float lon, CityWeather& out) {
    return fetchAndParse(lat, lon, nullptr, nullptr, &out);
}

bool fetchAllCities(CityWeather cities[NUM_CITIES]) {
    bool allOk = true;
    for (int i = 0; i < NUM_CITIES; i++) {
        Serial.printf("[weather] Fetching city: %s\n", CITIES[i].name);
        bool ok = fetchCityWeather(CITIES[i].lat, CITIES[i].lon, cities[i]);
        if (!ok) {
            Serial.printf("[weather] Failed for %s — keeping last known\n", CITIES[i].name);
            allOk = false;
        }
    }
    return allOk;
}

bool fetchAllWeather(CurrentWeather& current, ForecastDay forecast[5], CityWeather cities[NUM_CITIES]) {
    bool primaryOk = fetchPrimaryWeather(current, forecast);
    fetchAllCities(cities);      // best-effort — don't fail overall on city errors
    return primaryOk;
}
