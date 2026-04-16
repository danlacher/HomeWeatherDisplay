#include "bible.h"
#include "config.h"
#include "secrets.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ============================================================
//  CURATED VERSE LIST
//  52 verses — one per week, cycling by dayOfYear % 52.
//  api.bible passage IDs use format BOOK.CHAPTER.VERSE
//  Multi-verse passages use BOOK.CHAPTER.VERSE-BOOK.CHAPTER.VERSE
// ============================================================
static const char* VERSE_LIST[] = {
    "JER.29.11",          // For I know the plans I have for you...
    "PSA.23.1-6",         // The Lord is my shepherd...
    "JHN.3.16",           // For God so loved the world...
    "PHP.4.13",           // I can do all things through Christ...
    "ROM.8.28",           // All things work together for good...
    "ISA.41.10",          // Fear not, for I am with you...
    "PSA.46.1",           // God is our refuge and strength...
    "GAL.5.22-23",        // The fruit of the Spirit...
    "HEB.11.1",           // Faith is the substance of things hoped for...
    "2TI.1.7",            // God has not given us a spirit of fear...
    "1COR.10.13",         // No temptation has overtaken you...
    "PRO.22.6",           // Train up a child in the way...
    "ISA.40.31",          // Those who wait on the Lord...
    "JOS.1.9",            // Be strong and courageous...
    "HEB.12.2",           // Looking unto Jesus, the author of faith...
    "MAT.11.28-30",       // Come to Me, all you who are weary...
    "ROM.10.9-10",        // If you confess with your mouth...
    "PHP.2.3-4",          // Do nothing from selfish ambition...
    "MAT.5.43-44",        // Love your enemies...
    "PSA.119.105",        // Your word is a lamp to my feet...
    "PRO.3.5-6",          // Trust in the Lord with all your heart...
    "ROM.12.2",           // Do not be conformed to this world...
    "1COR.13.4-7",        // Love is patient, love is kind...
    "EPH.2.8-9",          // By grace you have been saved through faith...
    "PHP.4.6-7",          // Be anxious for nothing...
    "PSA.91.1-2",         // He who dwells in the secret place...
    "ISA.40.28-29",       // The everlasting God does not faint...
    "MAT.6.33",           // Seek first the kingdom of God...
    "ROM.5.8",            // God demonstrates His own love...
    "1JN.4.19",           // We love because He first loved us...
    "PSA.139.14",         // I am fearfully and wonderfully made...
    "EPH.6.10-11",        // Be strong in the Lord...
    "COL.3.23",           // Whatever you do, do it heartily...
    "JHN.14.6",           // I am the way, the truth, and the life...
    "ROM.8.38-39",        // Nothing can separate us from the love of God...
    "MAT.5.14-16",        // You are the light of the world...
    "PSA.37.4",           // Delight yourself in the Lord...
    "ISA.43.2",           // When you pass through the waters...
    "1COR.15.57",         // Thanks be to God who gives us the victory...
    "2CHR.7.14",          // If My people humble themselves...
    "JHN.10.10",          // I have come that they may have life...
    "ROM.15.13",          // The God of hope fill you with all joy...
    "PSA.27.1",           // The Lord is my light and my salvation...
    "EPH.3.20",           // Now to Him who is able to do exceedingly abundantly...
    "MAT.28.19-20",       // Go and make disciples of all nations...
    "1PET.5.7",           // Cast all your anxiety on Him...
    "PRO.31.25",          // She is clothed with strength and dignity...
    "NUM.6.24-26",        // The Lord bless you and keep you...
    "LAM.3.22-23",        // His mercies are new every morning...
    "REV.21.4",           // He will wipe away every tear...
    "PHP.1.6",            // He who began a good work in you...
    "JHN.16.33",          // In this world you will have trouble...
};

static const int VERSE_COUNT = sizeof(VERSE_LIST) / sizeof(VERSE_LIST[0]);

// ============================================================
//  PUBLIC: verseIdForDay
// ============================================================
const char* verseIdForDay(int dayOfYear) {
    return VERSE_LIST[dayOfYear % VERSE_COUNT];
}

// ============================================================
//  CLEAN VERSE TEXT
//  Strips HTML tags, pilcrow chars, collapses whitespace.
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

    // Trim trailing space
    while (dst > text && *(dst - 1) == ' ') dst--;
    *dst = '\0';
}

// ============================================================
//  INTERNAL: HTTP GET with api-key header
// ============================================================
static String httpGetBible(const String& url) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(10000);
    http.addHeader("api-key", BIBLE_API_KEY);
    http.addHeader("Accept",  "application/json");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[bible] HTTP %d for %s\n", code, url.c_str());
        http.end();
        return "";
    }

    String body = http.getString();
    http.end();
    return body;
}

// ============================================================
//  INTERNAL: get current day-of-year from system time
//  Requires NTP to have been synced before calling.
// ============================================================
static int currentDayOfYear() {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    return t->tm_yday;           // 0 = Jan 1
}

// ============================================================
//  FETCH VERSE OF DAY
//
//  Passage endpoint response shape:
//  {
//    "data": {
//      "id":        "JER.29.11",
//      "reference": "Jeremiah 29:11",
//      "content":   "<p class=\"p\"><span ...>For I know...</span></p>",
//      "copyright": "..."
//    }
//  }
// ============================================================
bool fetchVerseOfDay(VerseOfDay& out) {
    int dayOfYear  = currentDayOfYear();
    const char* id = verseIdForDay(dayOfYear);

    Serial.printf("[bible] Day %d → verse ID: %s\n", dayOfYear, id);

    // Build URL with query params to minimise markup in response
    String url = String(BIBLE_PASSAGE_URL) + id;
    url += "?content-type=text";
    url += "&include-notes=false";
    url += "&include-titles=false";
    url += "&include-chapter-numbers=false";
    url += "&include-verse-numbers=false";
    url += "&include-verse-spans=false";

    String body = httpGetBible(url);
    if (body.isEmpty()) {
        Serial.println("[bible] Empty response");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[bible] JSON parse error: %s\n", err.c_str());
        return false;
    }

    JsonObjectConst data = doc["data"];
    if (data.isNull()) {
        Serial.println("[bible] Missing 'data' in response");
        return false;
    }

    // --- reference ---
    const char* ref = data["reference"] | id;   // fall back to raw ID
    strncpy(out.reference, ref, sizeof(out.reference) - 1);
    out.reference[sizeof(out.reference) - 1] = '\0';

    // --- verse text ---
    // With content-type=text the content field is a plain string
    const char* content = data["content"] | "";
    if (strlen(content) == 0) {
        Serial.println("[bible] Empty content in response");
        return false;
    }

    strncpy(out.text, content, sizeof(out.text) - 1);
    out.text[sizeof(out.text) - 1] = '\0';
    cleanVerseText(out.text, sizeof(out.text));

    if (strlen(out.text) == 0) {
        Serial.println("[bible] Verse text empty after cleaning");
        return false;
    }

    // --- translation ---
    strncpy(out.translation, BIBLE_TRANSLATION, sizeof(out.translation) - 1);
    out.translation[sizeof(out.translation) - 1] = '\0';

    Serial.printf("[bible] %s — \"%s\"\n", out.reference, out.text);
    return true;
}
