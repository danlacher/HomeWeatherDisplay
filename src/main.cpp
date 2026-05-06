#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <time.h>

#include "config.h"
#include "secrets.h"
#include "types.h"
#include "weather.h"
#include "bible.h"
#include "display.h"

#include <HardwareSerial.h>
#define DEBUG_SERIAL Serial0

HardwareSerial Serial0(0);

// ============================================================
//  RTC MEMORY  — survives deep sleep
// ============================================================
RTC_DATA_ATTR RtcData rtc;

// ============================================================
//  HELPERS
// ============================================================

static bool wifiConnect() {
    Serial.printf("[wifi] Connecting to %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
            Serial.printf("[wifi] Timeout — last status: %d\n", WiFi.status());
            return false;
        }
        delay(250);
        Serial.printf("  status: %d\n", WiFi.status());
    }
    Serial.printf("[wifi] Connected: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

static void wifiDisconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

static bool ntpSync() {
    Serial.println("[ntp] Syncing...");
    configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.cloudflare.com");

    // Wait up to 10s for sync
    uint32_t start = millis();
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
        if (millis() - start > 10000) {
            Serial.println("[ntp] Sync timeout");
            return false;
        }
        delay(100);
    }
    time_t now = time(nullptr);
    Serial.printf("[ntp] Synced: %s", ctime(&now));
    return true;
}

// Returns true if current time is within VERSE_DISPLAY_MINUTES of the top of any hour
static bool isTopOfHour() {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    return (t->tm_min == 0);
}

// Returns true if we need to show verse this wake cycle
// Logic: show at top of hour, but only once per hour
static bool shouldShowVerse() {
    if (!isTopOfHour()) return false;

    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    uint32_t thisHour = t->tm_hour;
    uint32_t thisDay  = t->tm_yday;

    // Already showed verse this hour today
    if (rtc.lastVerseHour == thisHour && rtc.lastVerseDay == (uint32_t)thisDay)
        return false;

    return true;
}

// Returns true if the verse needs to be fetched (new calendar day)
static bool shouldFetchVerse() {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    return (rtc.lastVerseDay != (uint32_t)t->tm_yday || !rtc.verseValid);
}

// Returns true if weather data is missing or stale
static bool shouldFetchWeather() {
    if (!rtc.weatherValid) return true;
    time_t now = time(nullptr);
    uint32_t ageMinutes = ((uint32_t)now - rtc.lastWeatherFetch) / 60;
    return (ageMinutes >= WEATHER_STALE_MINUTES);
}

// Returns true if weather data is stale enough to show indicator
static bool weatherIsStale() {
    if (!rtc.weatherValid) return false;
    time_t now = time(nullptr);
    uint32_t ageMinutes = ((uint32_t)now - rtc.lastWeatherFetch) / 60;
    return (ageMinutes >= WEATHER_STALE_MINUTES);
}

// Advance view rotation index, skipping VIEW_VERSE (handled separately)
static uint8_t nextViewIndex(uint8_t current) {
    return (current + 1) % NUM_VIEWS;
}

// Enter deep sleep for REFRESH_INTERVAL_MS
static void goToSleep() {
    Serial.printf("[sleep] Going to sleep for %lu minutes\n",
                  REFRESH_INTERVAL_MS / 60000UL);

    epd.hibernate();
    wifiDisconnect();
    delay(100);

    // Wake on timer OR MENU button (GPIO2, active LOW)
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_US(REFRESH_INTERVAL_MS));
    esp_sleep_enable_ext1_wakeup(1ULL << PIN_BTN_MENU, ESP_EXT1_WAKEUP_ANY_LOW);

    Serial.println("[sleep] Entering deep sleep...");
    Serial.flush();
    esp_deep_sleep_start();
}

static bool wokenByButton() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    return (cause == ESP_SLEEP_WAKEUP_EXT1);
}

// ============================================================
//  FIRST BOOT INIT
// ============================================================
static void firstBootInit() {
    Serial.println("[boot] First boot — initialising RTC data");
    memset(&rtc, 0, sizeof(rtc));
    rtc.viewRotationIndex = 0;
    rtc.lastVerseHour     = 255;    // impossible hour — forces first verse check
    rtc.lastVerseDay      = 999;    // impossible day — forces first verse fetch
    rtc.weatherValid      = false;
    rtc.verseValid        = false;
    rtc.bootCount         = 1;
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);  // LED on = we reached setup()

    delay(3000);
    Serial.begin(SERIAL_BAUD);
    delay(500);
    Serial.println("=== CHECKPOINT 1: Serial OK ===");
    Serial.flush();

    if (rtc.bootCount == 0) {
        firstBootInit();
    } else {
        rtc.bootCount++;
    }
    Serial.printf("=== CHECKPOINT 2: Boot count %lu ===\n", rtc.bootCount);
    Serial.flush();

    pinMode(PIN_EPD_PWR, OUTPUT);
    digitalWrite(PIN_EPD_PWR, HIGH);
    Serial.println("=== CHECKPOINT 3: PWR pin HIGH ===");
    Serial.flush();
    delay(100);

    displayInit();
    Serial.println("=== CHECKPOINT 4: Display init done ===");
    Serial.flush();

    bool wifiOk = wifiConnect();
    Serial.printf("=== CHECKPOINT 5: WiFi %s ===\n", wifiOk ? "OK" : "FAILED");
    Serial.flush();


    // First boot detection — bootCount will be 0 on power-on reset
    if (rtc.bootCount == 0) {
        firstBootInit();
    } else {
        rtc.bootCount++;
    }

    // Init display first so we can show something quickly
    //displayInit();
    Serial.println("=== CHECKPOINT 4: Display init SKIPPED ===");
    Serial.flush();


    // ---- Connect WiFi ----
    //bool wifiOk = wifiConnect();
    wifiOk = wifiConnect();
    bool ntpOk  = false;

    if (wifiOk) {
        ntpOk = ntpSync();
    }

    // ---- Determine what to show this cycle ----
    bool showVerse   = false;
    bool fetchVerse  = false;
    bool fetchWeather = false;

    if (ntpOk) {
        showVerse    = shouldShowVerse();
        fetchVerse   = shouldFetchVerse();
        fetchWeather = shouldFetchWeather();
    } else {
        // No NTP — still render if we have cached data
        fetchWeather = false;
        fetchVerse   = false;
        showVerse    = false;
    }

    // ---- Fetch weather if needed ----
    if (wifiOk && fetchWeather) {
        Serial.println("[main] Fetching weather...");
        bool ok = fetchAllWeather(rtc.current, rtc.forecast, rtc.cities);
        if (ok) {
            rtc.weatherValid    = true;
            rtc.lastWeatherFetch = (uint32_t)time(nullptr);
            Serial.println("[main] Weather fetch OK");
        } else {
            Serial.println("[main] Weather fetch FAILED — using cached data");
        }
    }

    // ---- Fetch verse if new day ----
    if (wifiOk && fetchVerse) {
        Serial.println("[main] Fetching verse...");
        bool ok = fetchVerseOfDay(rtc.verse);
        if (ok) {
            rtc.verseValid      = true;
            rtc.lastVerseFetch  = (uint32_t)time(nullptr);
            time_t now = time(nullptr);
            struct tm* t = localtime(&now);
            rtc.lastVerseDay = t->tm_yday;
            Serial.println("[main] Verse fetch OK");
        } else {
            Serial.println("[main] Verse fetch FAILED — using cached verse");
        }
    }

    // ---- Disconnect WiFi before rendering (save power) ----
    if (wifiOk) wifiDisconnect();

    // ---- Decide which view to render ----
    uint8_t viewToRender;

    static const uint8_t ALL_VIEWS[] = { VIEW_STANDARD, VIEW_FIVEDAY, VIEW_MULTICITY, VIEW_VERSE };
    static const int ALL_VIEWS_COUNT = 4;

    if (showVerse && rtc.verseValid) {
        viewToRender = VIEW_VERSE;
        time_t now = time(nullptr);
        struct tm* t = localtime(&now);
        rtc.lastVerseHour = t->tm_hour;
        rtc.lastVerseDay  = t->tm_yday;
        Serial.println("[main] Rendering: Bible verse");
    } else if (wokenByButton()) {
       // Button press — force advance through all 4 views for testing
        //rtc.viewRotationIndex = (rtc.viewRotationIndex + 1) % 4;
        //viewToRender = rtc.viewRotationIndex;

        rtc.viewRotationIndex = (rtc.viewRotationIndex + 1) % ALL_VIEWS_COUNT;
        viewToRender = ALL_VIEWS[rtc.viewRotationIndex];

        Serial.printf("[main] Button press — rendering view: %d\n", viewToRender);
    } else {
        viewToRender = VIEW_ROTATION[rtc.viewRotationIndex];
        rtc.viewRotationIndex = nextViewIndex(rtc.viewRotationIndex);
        Serial.printf("[main] Rendering view: %d\n", viewToRender);
    }

    // ---- Render ----
    if (!rtc.weatherValid && viewToRender != VIEW_VERSE) {
        // No data at all — show a simple "waiting for data" message
        // using a minimal direct draw rather than a full view
        epd.setFullWindow();
        epd.firstPage();
        do {
            epd.fillScreen(GxEPD_WHITE);
            epd.setFont(nullptr);
            epd.setCursor(300, 130);
            epd.setTextColor(GxEPD_BLACK);
            epd.print("Connecting... waiting for data");
        } while (epd.nextPage());
    } else {
        switch (viewToRender) {
            case VIEW_STANDARD:
                drawStandardView(rtc.current, rtc.forecast);
                break;
            case VIEW_FIVEDAY:
                drawFiveDayView(rtc.current, rtc.forecast);
                break;
            case VIEW_MULTICITY:
                drawMultiCityView(rtc.cities);
                break;
            case VIEW_VERSE:
                if (rtc.verseValid)
                  drawVerseView(rtc.verse);
                else {
                    // Show placeholder verse for layout testing
                    VerseOfDay placeholder;
                    strncpy(placeholder.text, "For I know the plans I have for you, declares the Lord, plans to prosper you and not to harm you, plans to give you hope and a future.", sizeof(placeholder.text));
                    strncpy(placeholder.reference, "Jeremiah 29:11", sizeof(placeholder.reference));
                    strncpy(placeholder.translation, "AMP", sizeof(placeholder.translation));
                    drawVerseView(placeholder);
                }
                break;
        }

        // Stale indicator if weather is old
        if (weatherIsStale()) {
            drawStaleIndicator();
        }
    }

    // ---- Sleep ----
    goToSleep();
}

// ============================================================
//  LOOP  — never reached, device sleeps after setup()
// ============================================================
void loop() {
    // Deep sleep restarts from setup() on wake.
    // This should never execute.
    esp_deep_sleep_start();
}