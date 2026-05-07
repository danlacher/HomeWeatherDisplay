# HomeWeatherDisplay

A personal Wi-Fi connected e-ink weather dashboard built on the Elecrow CrowPanel 5.79" ESP32-S3 display. Shows current weather conditions, a 5-day forecast, multi-city snapshots, and a daily Bible verse — all on a crisp black-and-white e-ink display with a 7-minute refresh cycle.

---

## Features

- **Standard view** — Current temperature, feels like, condition icon, UV index, humidity, pressure, wind speed/direction, precipitation chance, visibility, sunrise/sunset times with animated sun arc
- **Forecast view** — 5-day forecast with high/low temps, condition icons, rain chance, wind, and cloud cover
- **Cities view** — Side-by-side current conditions for three configurable Michigan locations
- **Verse of the Day** — Daily Bible verse fetched from YouVersion (passage ID) and rendered in AMP translation via api.bible, displayed at the top of every hour
- **Deep sleep between refreshes** — Minimal power consumption; display holds its image with zero power
- **Button navigation** — Onboard buttons and scroll wheel to manually cycle views
- **Stale data indicator** — Small "!" badge if weather data is older than 30 minutes

---

## Hardware

| Component | Details |
|-----------|---------|
| Board | Elecrow CrowPanel 5.79" ESP32-S3 |
| MCU | ESP32-S3-WROOM-1-N8R8 (8MB Flash, 8MB PSRAM) |
| Display | GDEY0579T93 — 792×272px, Black & White e-ink |
| Driver chip | Dual SSD1683 |
| Connectivity | Wi-Fi 2.4GHz, BLE |

### Pin Reference

| Signal | GPIO |
|--------|------|
| EPD CS | 45 |
| EPD DC | 46 |
| EPD RST | 47 |
| EPD BUSY | 48 |
| EPD MOSI | 11 |
| EPD SCK | 12 |
| EPD PWR | 7 |
| MENU button | 2 |
| EXIT button | 1 |
| Scroll UP | 6 |
| Scroll DOWN | 4 |
| Scroll PRESS | 5 |

---

## Software

### Dependencies

Install via PlatformIO (automatically resolved from `platformio.ini`):

| Library | Version | Purpose |
|---------|---------|---------|
| GxEPD2 | ^1.6.2 | E-ink display driver |
| Adafruit GFX | ^1.11.9 | Fonts and drawing primitives |
| ArduinoJson | ^7.2.0 | JSON parsing |

### Project Structure

```
HomeWeatherDisplay/
├── platformio.ini        PlatformIO build configuration
├── README.md             This file
├── .gitignore            Excludes secrets.h from version control
└── src/
    ├── main.cpp          Boot, WiFi, NTP, view rotation, deep sleep
    ├── config.h          All non-secret configuration constants
    ├── secrets.h         API keys and WiFi credentials — NOT committed
    ├── types.h           Shared data structs and RTC memory layout
    ├── weather.h/.cpp    OpenWeatherMap fetch and JSON parsing
    ├── bible.h/.cpp      YouVersion VOTD + api.bible AMP text fetch
    └── display.h/.cpp    GxEPD2 rendering for all four views
```

---

## Setup

### 1. Clone the repository

```bash
git clone https://github.com/danlacher/HomeWeatherDisplay.git
cd HomeWeatherDisplay
```

### 2. Create `src/secrets.h`

Copy the template below into `src/secrets.h` and fill in your credentials. This file is listed in `.gitignore` and will never be committed.

```cpp
#pragma once

// ============================================================
//  SECRETS  —  DO NOT COMMIT THIS FILE
// ============================================================

// Wi-Fi credentials
#define WIFI_SSID           "your_wifi_network_name"
#define WIFI_PASSWORD       "your_wifi_password"

// OpenWeatherMap API key
// Sign up at https://openweathermap.org and subscribe to One Call API 3.0
// 1,000 free calls/day — this project uses ~200/day at 7-minute refresh
#define OWM_API_KEY         "your_openweathermap_api_key"

// api.bible API key
// Sign up at https://scripture.api.bible and create an app
// Used to fetch AMP translation verse text
// Base URL: https://rest.api.bible
#define BIBLE_API_KEY       "your_api_bible_key"

// YouVersion Platform API key
// Sign up at https://developers.youversion.com
// Used to fetch the verse of the day passage reference
// Header: x-yvp-app-key
#define YOUVERSION_API_KEY  "your_youversion_api_key"
```

### 3. Configure locations and preferences

Open `src/config.h` and update:

- **Primary city** — `PRIMARY_CITY_NAME`, `PRIMARY_CITY_LAT`, `PRIMARY_CITY_LON`
- **Multi-city locations** — the `CITIES[]` array (up to 3 cities)
- **Refresh interval** — `REFRESH_INTERVAL_MS` (default 7 minutes)
- **Units** — `TEMP_UNIT`, `WIND_UNIT`, `PRESSURE_UNIT` (imperial by default)
- **Time format** — `TIME_FORMAT_24H` (false = 12h, true = 24h)
- **Timezone** — update the POSIX timezone string in `ntpSync()` in `main.cpp`

### 4. Install PlatformIO

Install the [PlatformIO extension for VS Code](https://platformio.org/install/ide?install=vscode). PlatformIO will automatically download all library dependencies on first build.

### 5. Build and upload

1. Plug in the board via USB-C
2. Click **Build** (✓) in the VS Code bottom toolbar to confirm clean compile
3. Hold **BOOT**, tap **RST**, release **BOOT** to enter download mode
4. Click **Upload** (→)
5. Open **Serial Monitor** (plug icon) at 115200 baud to watch the boot sequence

---

## View Rotation

| Time | View displayed |
|------|---------------|
| Top of every hour (:00) | Verse of the Day |
| All other refreshes | Standard → Forecast → Standard → Cities (repeating) |

The verse is fetched once per calendar day and cached in RTC memory. Weather is fetched on every wake cycle if data is older than 30 minutes.

---

## Button Controls

| Button | Action |
|--------|--------|
| MENU (left) | Advance to next view |
| EXIT (right) | Return to Standard view |
| Scroll UP | Advance to next view |
| Scroll DOWN | Go back one view |
| Scroll PRESS | Force immediate weather refresh |

Buttons wake the device from deep sleep via `ext1` GPIO interrupt.

---

## API Services

| Service | Purpose | Free Tier |
|---------|---------|-----------|
| [OpenWeatherMap](https://openweathermap.org) | Weather data | 1,000 calls/day (One Call 3.0) |
| [YouVersion Platform](https://developers.youversion.com) | Verse of the Day reference | Available with app registration |
| [api.bible](https://scripture.api.bible) | AMP verse text | 5,000 calls/month |

---

## RTC Memory

The ESP32-S3 has 8KB of RTC SRAM that survives deep sleep. This project stores ~1.1KB of cached data including:

- Current weather conditions for the primary city
- 5-day forecast
- Current conditions for all three cities
- Today's Bible verse text and reference
- View rotation state
- Fetch timestamps and validity flags

This means the display renders immediately on wake from cached data, with a fresh fetch happening in the background.

---

## Troubleshooting

**Display shows "Connecting... waiting for data"**
- Check `WIFI_SSID` and `WIFI_PASSWORD` in `secrets.h`
- ESP32-S3 is 2.4GHz only — ensure you're connecting to a 2.4GHz network
- Add `WiFi.setMinSecurity(WIFI_AUTH_WPA2_PSK)` if your router uses WPA3

**Weather returns 401**
- Verify your OWM API key is active and subscribed to One Call 3.0
- New keys can take up to 2 hours to activate

**Bible verse returns 401**
- Verify your api.bible key at scripture.api.bible
- Confirm the AMP Bible is enabled for your app
- Base URL must be `https://rest.api.bible` (not `api.scripture.api.bible`)

**No serial output**
- The board uses hardware UART via the CH340 chip on `/dev/cu.usbserial-*`
- Open serial monitor on that port at 115200 baud
- Add `delay(3000)` at the top of `setup()` if early boot messages are missed

**Upload fails with "Unable to verify flash chip connection"**
- Hold BOOT, tap RST, release BOOT to manually enter download mode
- Reduce upload speed to 460800 in `platformio.ini`

---

## Roadmap

- [ ] Fast button wake — skip fetch, render from cache immediately
- [ ] Night mode — reduced refresh rate between 11pm–6am
- [ ] Moon phase on Standard view
- [ ] OTA firmware updates over WiFi
- [ ] Battery operation with power consumption optimisation

---

## Acknowledgements

- [GxEPD2](https://github.com/ZinggJM/GxEPD2) by Jean-Marc Zingg — excellent e-ink library
- [OpenWeatherMap](https://openweathermap.org) — weather data
- [YouVersion Platform](https://developers.youversion.com) — verse of the day
- [api.bible](https://scripture.api.bible) — AMP Bible text