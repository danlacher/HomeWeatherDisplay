#include "bible.h"
#include "config.h"
#include "secrets.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

#include <HardwareSerial.h>
extern HardwareSerial Serial0;

// ============================================================
//  CLEAN VERSE TEXT
// ============================================================
void cleanVerseText(char* text, size_t maxLen) {
    if (!text || maxLen == 0) return;

    char*  src       = text;
    char*  dst       = text;
    bool   inTag     = false;
    bool   lastSpace = false;

    while (*src && (size_t)(dst - text) < maxLen - 1) {
        char c = *src++;

        if (c == '<') { inTag = true;  continue; }
        if (c == '>') { inTag = false; continue; }
        if (inTag)    continue;

        // Strip UTF-8 pilcrow ¶ (0xC2 0xB6)
        if ((unsigned char)c == 0xC2 && (unsigned char)*src == 0xB6) {
            src++; continue;
        }

        // Normalise whitespace
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';

        if (c == ' ') {
            if (lastSpace) continue;
            lastSpace = true;
        } else {
            lastSpace = false;
        }

        *dst++ = c;
    }

    while (dst > text && *(dst - 1) == ' ') dst--;
    *dst = '\0';
}

// ============================================================
//  INTERNAL: HTTP GET with single header
// ============================================================
static String httpGetWithHeader(const String& url,
                                 const char* headerKey,
                                 const char* headerVal) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(10000);
    http.addHeader(headerKey, headerVal);
    http.addHeader("Accept", "application/json");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial0.printf("[bible] HTTP %d for %s\n", code, url.c_str());
        http.end();
        return "";
    }

    String body = http.getString();
    http.end();
    return body;
}

// ============================================================
//  STEP 1: Get today's passage ID from YouVersion
//  Returns passage ID string e.g. "ROM.1.17" or empty on fail
// ============================================================
static String fetchPassageId() {
    // YouVersion day is 1-366, tm_yday is 0-365
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    int day = t->tm_yday + 1;

    String url = String(YOUVERSION_VOTD_URL) + day;
    Serial0.printf("[bible] YouVersion VOTD day %d: %s\n", day, url.c_str());

    String body = httpGetWithHeader(url, "x-yvp-app-key", YOUVERSION_API_KEY);
    if (body.isEmpty()) return "";

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial0.printf("[bible] YouVersion JSON error: %s\n", err.c_str());
        return "";
    }

    const char* passageId = doc["passage_id"] | "";
    if (strlen(passageId) == 0) {
        Serial0.println("[bible] No passage_id in YouVersion response");
        return "";
    }

    Serial0.printf("[bible] Got passage ID: %s\n", passageId);
    return String(passageId);
}

// ============================================================
//  STEP 2: Fetch AMP verse text from api.bible
// ============================================================
static bool fetchAmpText(const String& passageId, VerseOfDay& out) {
    String url = String(BIBLE_PASSAGE_URL) + passageId;
    url += "?content-type=text";
    url += "&include-notes=false";
    url += "&include-titles=false";
    url += "&include-chapter-numbers=false";
    url += "&include-verse-numbers=false";
    url += "&include-verse-spans=false";

    Serial0.printf("[bible] api.bible fetch: %s\n", url.c_str());

    String body = httpGetWithHeader(url, "api-key", BIBLE_API_KEY);
    if (body.isEmpty()) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial0.printf("[bible] api.bible JSON error: %s\n", err.c_str());
        return false;
    }

    JsonObjectConst data = doc["data"];
    if (data.isNull()) {
        Serial0.println("[bible] Missing 'data' in api.bible response");
        return false;
    }

    // Reference
    const char* ref = data["reference"] | passageId.c_str();
    strncpy(out.reference, ref, sizeof(out.reference) - 1);
    out.reference[sizeof(out.reference) - 1] = '\0';

    // Verse text
    const char* content = data["content"] | "";
    if (strlen(content) == 0) {
        Serial0.println("[bible] Empty content in api.bible response");
        return false;
    }

    strncpy(out.text, content, sizeof(out.text) - 1);
    out.text[sizeof(out.text) - 1] = '\0';
    cleanVerseText(out.text, sizeof(out.text));

    if (strlen(out.text) == 0) {
        Serial0.println("[bible] Verse text empty after cleaning");
        return false;
    }

    // Translation
    strncpy(out.translation, BIBLE_TRANSLATION, sizeof(out.translation) - 1);
    out.translation[sizeof(out.translation) - 1] = '\0';

    Serial0.printf("[bible] %s — \"%s\"\n", out.reference, out.text);
    return true;
}

// ============================================================
//  PUBLIC: fetchVerseOfDay
//  Two-step: YouVersion VOTD → api.bible AMP text
// ============================================================
bool fetchVerseOfDay(VerseOfDay& out) {
    Serial0.println("[bible] Fetching verse of the day...");

    // Step 1 — get passage ID from YouVersion
    String passageId = fetchPassageId();
    if (passageId.isEmpty()) {
        Serial0.println("[bible] YouVersion fetch failed");
        return false;
    }

    // Step 2 — get AMP text from api.bible
    bool ok = fetchAmpText(passageId, out);
    if (!ok) {
        Serial0.println("[bible] api.bible fetch failed");
        return false;
    }

    return true;
}