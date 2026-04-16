#pragma once
#include <Arduino.h>
#include "types.h"

// ============================================================
//  BIBLE MODULE
//  api.bible has no VOTD endpoint — we maintain a curated list
//  of verse IDs and select one by dayOfYear % listSize.
//  Same verse shows all day. Rotates daily. Fully deterministic.
//
//  Endpoint: GET /bibles/{bibleId}/passages/{passageId}
//  Base URL:  https://api.scripture.api.bible/v1
// ============================================================

// Fetch the verse for today using day-of-year as index into
// the curated VERSE_LIST. Writes into out.
// Returns true on success.
bool fetchVerseOfDay(VerseOfDay& out);

// Return the verse ID string for a given day-of-year (0-364).
// Exposed for testing / serial debug.
const char* verseIdForDay(int dayOfYear);

// Strip api.bible HTML tags and normalise whitespace in place.
void cleanVerseText(char* text, size_t maxLen);
