#pragma once
#include <Arduino.h>
#include "types.h"

// Fetch today's verse via YouVersion VOTD + api.bible AMP text
// Returns true on success
bool fetchVerseOfDay(VerseOfDay& out);

// Strip HTML tags and normalise whitespace in place
void cleanVerseText(char* text, size_t maxLen);