#pragma once
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_579_GDEY0579T93.h>
#include <Adafruit_GFX.h>
#include "types.h"

// ============================================================
//  DISPLAY MODULE
//  Driver:  GxEPD2_579_GDEY0579T93 (SSD1683 dual-chip)
//  Resolution: 792 x 272  landscape
//  Colors:  Black & White only
// ============================================================

// Call once at startup after PWR pin is HIGH
void displayInit();

// Full screen clear to white — call before first render
void displayClear();

// ---- Four view renderers ----
void drawStandardView(const CurrentWeather& wx, const ForecastDay forecast[5]);
void drawFiveDayView(const CurrentWeather& wx, const ForecastDay forecast[5]);
void drawMultiCityView(const CityWeather cities[NUM_CITIES]);
void drawVerseView(const VerseOfDay& verse);

// Stale data indicator — small "!" in top-right corner
// Call after any view draw if data is stale
void drawStaleIndicator();

// Expose display object so main.cpp can call display.hibernate()
extern GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> display;
