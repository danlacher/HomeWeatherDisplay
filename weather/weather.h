#pragma once
#include <Arduino.h>
#include "types.h"

// ============================================================
//  WEATHER MODULE
//  Fetches from OWM One Call API 3.0
//  Populates CurrentWeather, ForecastDay[5], and CityWeather
// ============================================================

// Fetch current + forecast for the primary city.
// Writes into rtcData.current and rtcData.forecast[].
// Returns true on success.
bool fetchPrimaryWeather(CurrentWeather& current, ForecastDay forecast[5]);

// Fetch current conditions only for a single city.
// Used to populate rtcData.cities[].
// Returns true on success.
bool fetchCityWeather(float lat, float lon, CityWeather& out);

// Fetch all three multi-city snapshots.
// Calls fetchCityWeather() for each entry in CITIES[].
// Returns true if all three succeed.
bool fetchAllCities(CityWeather cities[NUM_CITIES]);

// Convenience — run all weather fetches in one call.
// Populates current, forecast, and cities.
// Returns true if primary fetch succeeded (city fetches are best-effort).
bool fetchAllWeather(CurrentWeather& current, ForecastDay forecast[5], CityWeather cities[NUM_CITIES]);
