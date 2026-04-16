#include "display.h"
#include "config.h"
#include <SPI.h>
#include <time.h>
#include <math.h>

// Adafruit GFX fonts — sizes chosen to fit 792x272
#include <Fonts/FreeSansBold24pt7b.h>   // large temp
#include <Fonts/FreeSansBold12pt7b.h>   // medium labels
#include <Fonts/FreeSans9pt7b.h>        // small detail text
#include <Fonts/FreeSerifItalic12pt7b.h> // verse body
#include <Fonts/FreeSansBold9pt7b.h>    // small bold

// ============================================================
//  DISPLAY OBJECT
//  Constructor: (CS, DC, RST, BUSY)
//  SPI pins configured separately via SPIClass
// ============================================================
SPIClass epd_spi(HSPI);

GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> display(
    GxEPD2_579_GDEY0579T93(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

// ============================================================
//  COLOURS
// ============================================================
#define CLR_BLACK   GxEPD_BLACK
#define CLR_WHITE   GxEPD_WHITE

// ============================================================
//  LAYOUT CONSTANTS  (all in pixels)
// ============================================================
#define W           792
#define H           272
#define TOPBAR_H    36
#define BODY_Y      (TOPBAR_H + 1)
#define BODY_H      (H - TOPBAR_H - 1)

// View 1 column widths: 25% / 50% / 25%
#define COL1_W      198     // 792 * 0.25
#define COL2_X      198
#define COL2_W      396     // 792 * 0.50
#define COL3_X      594

// ============================================================
//  INIT
// ============================================================
void displayInit() {
    // Power up display panel
    pinMode(PIN_EPD_PWR, OUTPUT);
    digitalWrite(PIN_EPD_PWR, HIGH);
    delay(20);

    epd_spi.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);
    display.epd2.selectSPI(epd_spi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    display.init(115200, true, 2, false);
    display.setRotation(0);     // landscape, USB-C on left
    display.setTextColor(CLR_BLACK);
    display.setTextWrap(false);
    Serial.println("[display] Init OK");
}

void displayClear() {
    display.clearScreen();
}

// ============================================================
//  SHARED HELPERS
// ============================================================

// Format unix epoch -> "9:42 AM" or "09:42" depending on config
static void formatTime(uint32_t epoch, char* buf, size_t len) {
    time_t t = (time_t)epoch;
    struct tm* tm = localtime(&t);
    if (TIME_FORMAT_24H)
        strftime(buf, len, "%H:%M", tm);
    else
        strftime(buf, len, "%I:%M %p", tm);
    // Trim leading zero from 12h format
    if (!TIME_FORMAT_24H && buf[0] == '0') memmove(buf, buf + 1, strlen(buf));
}

// Format epoch -> "Wed, April 15"
static void formatDate(uint32_t epoch, char* buf, size_t len) {
    time_t t = (time_t)epoch;
    struct tm* tm = localtime(&t);
    strftime(buf, len, "%a, %B %d", tm);
}

// Draw top bar — shared across all views
static void drawTopBar(const char* leftText, uint32_t epoch) {
    // Background line
    display.drawFastHLine(0, TOPBAR_H, W, CLR_BLACK);

    // Left: location or view name
    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(10, TOPBAR_H - 10);
    display.print(leftText);

    // Right: date + time
    char dateBuf[32], timeBuf[16], dtBuf[52];
    formatDate(epoch, dateBuf, sizeof(dateBuf));
    formatTime(epoch, timeBuf, sizeof(timeBuf));
    snprintf(dtBuf, sizeof(dtBuf), "%s  |  %s", dateBuf, timeBuf);

    display.setFont(&FreeSans9pt7b);
    int16_t x1, y1; uint16_t tw, th;
    display.getTextBounds(dtBuf, 0, 0, &x1, &y1, &tw, &th);
    display.setCursor(W - tw - 10, TOPBAR_H - 10);
    display.print(dtBuf);
}

// Draw a vertical divider line in the body area
static void drawVDivider(int x) {
    display.drawFastVLine(x, BODY_Y, BODY_H, CLR_BLACK);
}

// Draw a horizontal divider within a column region
static void drawHDivider(int y, int x0, int x1) {
    display.drawFastHLine(x0, y, x1 - x0, CLR_BLACK);
}

// Centred text in a bounding box
static void drawCentredText(const GFXfont* font, const char* str,
                            int bx, int by, int bw, int bh) {
    display.setFont(font);
    int16_t x1, y1; uint16_t tw, th;
    display.getTextBounds(str, 0, 0, &x1, &y1, &tw, &th);
    int cx = bx + (bw - (int)tw) / 2 - x1;
    int cy = by + (bh + (int)th) / 2;
    display.setCursor(cx, cy);
    display.print(str);
}

// Small muted label above a value
static void drawStatLabel(const char* label, int x, int y) {
    display.setFont(&FreeSans9pt7b);
    display.setCursor(x, y);
    display.print(label);
}

// ============================================================
//  WEATHER ICON  (simple 1-bit bitmaps drawn with GFX primitives)
//  Each icon fits in a ~40x40 bounding box
// ============================================================
static void drawWeatherIcon(uint8_t iconIdx, int cx, int cy, int r) {
    // cx,cy = centre, r = radius of main circle (~20)
    switch (iconIdx) {
        case ICON_CLEAR_DAY: {
            // Filled sun circle
            display.fillCircle(cx, cy, r, CLR_BLACK);
            display.fillCircle(cx, cy, r - 4, CLR_WHITE);
            // Rays
            for (int i = 0; i < 8; i++) {
                float ang = i * PI / 4.0f;
                int x1 = cx + (r + 3) * cos(ang);
                int y1 = cy + (r + 3) * sin(ang);
                int x2 = cx + (r + 9) * cos(ang);
                int y2 = cy + (r + 9) * sin(ang);
                display.drawLine(x1, y1, x2, y2, CLR_BLACK);
            }
            break;
        }
        case ICON_CLEAR_NIGHT: {
            // Crescent moon
            display.fillCircle(cx, cy, r, CLR_BLACK);
            display.fillCircle(cx + r / 2, cy - r / 4, r, CLR_WHITE);
            break;
        }
        case ICON_FEW_CLOUDS:
        case ICON_SCATTERED: {
            // Small sun + cloud
            display.fillCircle(cx - r / 3, cy - r / 3, r / 2, CLR_BLACK);
            display.fillCircle(cx - r / 3, cy - r / 3, r / 2 - 3, CLR_WHITE);
            // Cloud body
            display.fillRoundRect(cx - r, cy, r * 2, r, r / 2, CLR_BLACK);
            display.fillCircle(cx - r / 3, cy, r / 2 + 2, CLR_BLACK);
            display.fillCircle(cx + r / 3, cy, r / 2 + 4, CLR_BLACK);
            display.fillRoundRect(cx - r + 2, cy + 2, r * 2 - 4, r - 4, r / 2 - 2, CLR_WHITE);
            display.fillCircle(cx - r / 3, cy, r / 2, CLR_WHITE);
            display.fillCircle(cx + r / 3, cy, r / 2 + 2, CLR_WHITE);
            break;
        }
        case ICON_BROKEN: {
            // Simple cloud only
            display.fillRoundRect(cx - r, cy - r / 3, r * 2, r, r / 2, CLR_BLACK);
            display.fillCircle(cx - r / 3, cy - r / 3, r / 2 + 2, CLR_BLACK);
            display.fillCircle(cx + r / 3, cy - r / 3, r / 2 + 4, CLR_BLACK);
            display.fillRoundRect(cx - r + 2, cy - r / 3 + 2, r * 2 - 4, r - 4, r / 2 - 2, CLR_WHITE);
            display.fillCircle(cx - r / 3, cy - r / 3, r / 2, CLR_WHITE);
            display.fillCircle(cx + r / 3, cy - r / 3, r / 2 + 2, CLR_WHITE);
            break;
        }
        case ICON_SHOWER_RAIN:
        case ICON_RAIN: {
            // Cloud + rain drops
            display.fillRoundRect(cx - r, cy - r / 2, r * 2, r - 4, r / 2, CLR_BLACK);
            display.fillCircle(cx - r / 3, cy - r / 2, r / 2 + 2, CLR_BLACK);
            display.fillCircle(cx + r / 3, cy - r / 2, r / 2 + 4, CLR_BLACK);
            display.fillRoundRect(cx - r + 2, cy - r / 2 + 2, r * 2 - 4, r - 8, r / 2 - 2, CLR_WHITE);
            display.fillCircle(cx - r / 3, cy - r / 2, r / 2, CLR_WHITE);
            display.fillCircle(cx + r / 3, cy - r / 2, r / 2 + 2, CLR_WHITE);
            // Drops
            for (int i = -1; i <= 1; i++)
                display.drawLine(cx + i * (r / 2), cy + 4, cx + i * (r / 2) - 3, cy + 12, CLR_BLACK);
            break;
        }
        case ICON_THUNDERSTORM: {
            // Cloud + lightning bolt
            display.fillRoundRect(cx - r, cy - r / 2, r * 2, r - 4, r / 2, CLR_BLACK);
            display.fillCircle(cx - r / 3, cy - r / 2, r / 2 + 2, CLR_BLACK);
            display.fillCircle(cx + r / 3, cy - r / 2, r / 2 + 4, CLR_BLACK);
            display.fillRoundRect(cx - r + 2, cy - r / 2 + 2, r * 2 - 4, r - 8, r / 2 - 2, CLR_WHITE);
            display.fillCircle(cx - r / 3, cy - r / 2, r / 2, CLR_WHITE);
            display.fillCircle(cx + r / 3, cy - r / 2, r / 2 + 2, CLR_WHITE);
            // Bolt
            display.drawLine(cx + 4, cy + 2, cx - 2, cy + 10, CLR_BLACK);
            display.drawLine(cx - 2, cy + 10, cx + 3, cy + 10, CLR_BLACK);
            display.drawLine(cx + 3, cy + 10, cx - 4, cy + 20, CLR_BLACK);
            break;
        }
        case ICON_SNOW: {
            // Snowflake (6 lines)
            for (int i = 0; i < 6; i++) {
                float ang = i * PI / 3.0f;
                display.drawLine(cx, cy,
                                 cx + r * cos(ang), cy + r * sin(ang),
                                 CLR_BLACK);
            }
            display.fillCircle(cx, cy, 3, CLR_BLACK);
            break;
        }
        case ICON_MIST:
        default: {
            // Three horizontal hazy lines
            for (int i = -1; i <= 1; i++)
                display.drawFastHLine(cx - r, cy + i * (r / 2), r * 2, CLR_BLACK);
            break;
        }
    }
}

// ============================================================
//  SUNRISE/SUNSET ARC WITH SUN POSITION
//  Draws a semicircle arc between sunrise and sunset positions.
//  Places a filled dot on the arc showing current sun position.
//  cx,cy = centre of arc base, arcR = radius
// ============================================================
static void drawSunArc(int cx, int cy, int arcR,
                       uint32_t sunriseEpoch, uint32_t sunsetEpoch,
                       uint32_t nowEpoch) {
    // Arc: left end = sunrise (180°), top = solar noon (90°), right = sunset (0°)
    // We draw from 180° to 0° (upper semicircle)

    // Draw arc using short line segments
    const int STEPS = 60;
    for (int i = 0; i < STEPS; i++) {
        float ang1 = PI - (float)i       / STEPS * PI;
        float ang2 = PI - (float)(i + 1) / STEPS * PI;
        int x1 = cx + arcR * cos(ang1);
        int y1 = cy - arcR * sin(ang1);   // negative because Y increases downward
        int x2 = cx + arcR * cos(ang2);
        int y2 = cy - arcR * sin(ang2);
        display.drawLine(x1, y1, x2, y2, CLR_BLACK);
    }

    // Small tick marks at sunrise and sunset ends
    display.drawFastVLine(cx - arcR, cy - 4, 8, CLR_BLACK);
    display.drawFastVLine(cx + arcR, cy - 4, 8, CLR_BLACK);

    // Sun dot position — only draw if between sunrise and sunset
    if (nowEpoch >= sunriseEpoch && nowEpoch <= sunsetEpoch && sunsetEpoch > sunriseEpoch) {
        float progress = (float)(nowEpoch - sunriseEpoch) /
                         (float)(sunsetEpoch - sunriseEpoch);   // 0.0 -> 1.0
        float ang = PI - progress * PI;                          // 180° -> 0°
        int sx = cx + arcR * cos(ang);
        int sy = cy - arcR * sin(ang);
        display.fillCircle(sx, sy, 5, CLR_BLACK);   // solid sun dot
        display.fillCircle(sx, sy, 3, CLR_WHITE);   // white centre for ring effect
        display.fillCircle(sx, sy, 1, CLR_BLACK);   // inner dot
    }
}

// ============================================================
//  VIEW 1: STANDARD DAY
//  Layout: COL1(25%) | COL2(50%) | COL3(25%)
//  COL1: UV / Humidity / Pressure
//  COL2: Icon + Temp + Feels + Condition | Arc | Sunrise / Rain / Sunset
//  COL3: Wind / Precip Chance / Visibility
// ============================================================
void drawStandardView(const CurrentWeather& wx, const ForecastDay forecast[5]) {
    time_t now = time(nullptr);
    char buf[64];

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(CLR_WHITE);

        // Top bar
        drawTopBar(PRIMARY_CITY_NAME, (uint32_t)now);

        // Column dividers
        drawVDivider(COL1_W);
        drawVDivider(COL3_X);

        // ---- COL 1: UV / Humidity / Pressure ----
        int rowH = BODY_H / 3;
        int statCols[3][2] = {
            { ICON_CLEAR_DAY,  0 },   // placeholder, unused
        };

        // UV
        {
            int y0 = BODY_Y;
            drawHDivider(y0 + rowH, 0, COL1_W);
            drawStatLabel("UV INDEX", 10, y0 + 14);
            snprintf(buf, sizeof(buf), "%d", wx.uvIndex);
            display.setFont(&FreeSansBold24pt7b);
            display.setCursor(10, y0 + rowH - 14);
            display.print(buf);
            const char* uvLabel = wx.uvIndex <= 2 ? "Low" :
                                  wx.uvIndex <= 5 ? "Moderate" :
                                  wx.uvIndex <= 7 ? "High" : "Very High";
            display.setFont(&FreeSans9pt7b);
            display.setCursor(10, y0 + rowH - 2);
            display.print(uvLabel);
        }
        // Humidity
        {
            int y0 = BODY_Y + rowH;
            drawHDivider(y0 + rowH, 0, COL1_W);
            drawStatLabel("HUMIDITY", 10, y0 + 14);
            snprintf(buf, sizeof(buf), "%d%%", wx.humidity);
            display.setFont(&FreeSansBold24pt7b);
            display.setCursor(10, y0 + rowH - 14);
            display.print(buf);
        }
        // Pressure
        {
            int y0 = BODY_Y + rowH * 2;
            drawStatLabel("PRESSURE", 10, y0 + 14);
            snprintf(buf, sizeof(buf), "%.1f", wx.pressure);
            display.setFont(&FreeSansBold24pt7b);
            display.setCursor(10, y0 + rowH - 14);
            display.print(buf);
            display.setFont(&FreeSans9pt7b);
            display.setCursor(10, y0 + rowH - 2);
            display.print("inHg");
        }

        // ---- COL 2: Temp block (upper) + Arc + Bottom row ----
        int col2MidY = BODY_Y + BODY_H / 2;

        // Icon + Temp side by side, centred vertically in upper half
        {
            int iconR = 22;
            int iconX = COL2_X + 50;
            int iconY = BODY_Y + BODY_H / 4;
            drawWeatherIcon(wx.conditionIcon, iconX, iconY, iconR);

            // Temp
            snprintf(buf, sizeof(buf), "%d\xB0", (int)round(wx.temp));
            display.setFont(&FreeSansBold24pt7b);
            int16_t x1, y1; uint16_t tw, th;
            display.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
            display.setCursor(iconX + iconR + 14, iconY + th / 2);
            display.print(buf);

            // Feels like + condition below temp
            snprintf(buf, sizeof(buf), "Feels like %d\xB0", (int)round(wx.feelsLike));
            display.setFont(&FreeSans9pt7b);
            display.setCursor(iconX + iconR + 14, iconY + th / 2 + 18);
            display.print(buf);
            display.setCursor(iconX + iconR + 14, iconY + th / 2 + 34);
            display.print(wx.condition);
        }

        // Divider between upper and lower halves of COL2
        drawHDivider(col2MidY, COL2_X, COL3_X);

        // Arc in lower half of COL2
        {
            int arcCX = COL2_X + COL2_W / 2;
            int arcCY = BODY_Y + BODY_H - 14;
            int arcR  = 52;
            drawSunArc(arcCX, arcCY, arcR,
                       wx.sunrise, wx.sunset, (uint32_t)now);
        }

        // Bottom row: Sunrise | Rain | Sunset  (below arc labels)
        {
            int rowY  = col2MidY + 8;
            int thirdW = COL2_W / 3;

            // Sunrise
            char srBuf[12];
            formatTime(wx.sunrise, srBuf, sizeof(srBuf));
            display.setFont(&FreeSans9pt7b);
            drawCentredText(&FreeSans9pt7b, "\x18 Sunrise",
                            COL2_X, rowY, thirdW, 16);
            drawCentredText(&FreeSansBold12pt7b, srBuf,
                            COL2_X, rowY + 16, thirdW, 22);

            // Rain
            snprintf(buf, sizeof(buf), wx.rainAmt > 0 ? "%.2f in" : "None", wx.rainAmt);
            drawCentredText(&FreeSans9pt7b, "RAIN",
                            COL2_X + thirdW, rowY, thirdW, 16);
            drawCentredText(&FreeSansBold12pt7b, buf,
                            COL2_X + thirdW, rowY + 16, thirdW, 22);

            // Sunset
            char ssBuf[12];
            formatTime(wx.sunset, ssBuf, sizeof(ssBuf));
            drawCentredText(&FreeSans9pt7b, "\x19 Sunset",
                            COL2_X + thirdW * 2, rowY, thirdW, 16);
            drawCentredText(&FreeSansBold12pt7b, ssBuf,
                            COL2_X + thirdW * 2, rowY + 16, thirdW, 22);
        }

        // ---- COL 3: Wind / Precip / Visibility ----
        {
            int y0 = BODY_Y;
            drawHDivider(y0 + rowH, COL3_X, W);
            drawStatLabel("WIND", COL3_X + 10, y0 + 14);
            snprintf(buf, sizeof(buf), "%.0f", wx.windSpeed);
            display.setFont(&FreeSansBold24pt7b);
            display.setCursor(COL3_X + 10, y0 + rowH - 14);
            display.print(buf);
            display.setFont(&FreeSans9pt7b);
            snprintf(buf, sizeof(buf), "mph %s", windDegToCardinal(wx.windDeg));
            display.setCursor(COL3_X + 10, y0 + rowH - 2);
            display.print(buf);
        }
        {
            int y0 = BODY_Y + rowH;
            drawHDivider(y0 + rowH, COL3_X, W);
            drawStatLabel("PRECIP CHANCE", COL3_X + 10, y0 + 14);
            snprintf(buf, sizeof(buf), "%d%%", wx.precipChance);
            display.setFont(&FreeSansBold24pt7b);
            display.setCursor(COL3_X + 10, y0 + rowH - 14);
            display.print(buf);
        }
        {
            int y0 = BODY_Y + rowH * 2;
            drawStatLabel("VISIBILITY", COL3_X + 10, y0 + 14);
            snprintf(buf, sizeof(buf), "%.0f", wx.visibility);
            display.setFont(&FreeSansBold24pt7b);
            display.setCursor(COL3_X + 10, y0 + rowH - 14);
            display.print(buf);
            display.setFont(&FreeSans9pt7b);
            display.setCursor(COL3_X + 10, y0 + rowH - 2);
            display.print("mi");
        }

    } while (display.nextPage());
}

// ============================================================
//  VIEW 2: FIVE DAY FORECAST
//  5 equal columns, each ~158px wide
// ============================================================
void drawFiveDayView(const CurrentWeather& wx, const ForecastDay forecast[5]) {
    time_t now = time(nullptr);
    char buf[32];
    const int COL_W = W / 5;   // 158px

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(CLR_WHITE);
        drawTopBar(PRIMARY_CITY_NAME, (uint32_t)now);

        for (int i = 0; i < 5; i++) {
            int x0 = i * COL_W;
            if (i > 0) display.drawFastVLine(x0, BODY_Y, BODY_H, CLR_BLACK);

            const ForecastDay& d = forecast[i];
            int cx = x0 + COL_W / 2;

            // Day name
            display.setFont(&FreeSansBold12pt7b);
            drawCentredText(&FreeSansBold12pt7b, d.day, x0, BODY_Y, COL_W, 24);

            // Divider under day name
            display.drawFastHLine(x0, BODY_Y + 26, COL_W, CLR_BLACK);

            // Icon
            drawWeatherIcon(d.conditionIcon, cx, BODY_Y + 58, 20);

            // High temp
            snprintf(buf, sizeof(buf), "%d\xB0", (int)round(d.high));
            drawCentredText(&FreeSansBold24pt7b, buf, x0, BODY_Y + 88, COL_W, 34);

            // Low temp (muted — draw slightly offset for visual hierarchy)
            snprintf(buf, sizeof(buf), "%d\xB0", (int)round(d.low));
            drawCentredText(&FreeSans9pt7b, buf, x0, BODY_Y + 124, COL_W, 18);

            // Divider
            display.drawFastHLine(x0, BODY_Y + 146, COL_W, CLR_BLACK);

            // Detail rows
            int detY = BODY_Y + 158;
            int detX = x0 + 8;

            snprintf(buf, sizeof(buf), "Rain: %d%%", d.precipChance);
            display.setFont(&FreeSans9pt7b);
            display.setCursor(detX, detY);      display.print(buf);

            snprintf(buf, sizeof(buf), "Wind: %d mph", d.windSpeed);
            display.setCursor(detX, detY + 18); display.print(buf);

            snprintf(buf, sizeof(buf), "Cloud: %d%%", d.cloudPct);
            display.setCursor(detX, detY + 36); display.print(buf);
        }

    } while (display.nextPage());
}

// ============================================================
//  VIEW 3: MULTI-CITY
//  3 rows, each H/3 tall
// ============================================================
void drawMultiCityView(const CityWeather cities[NUM_CITIES]) {
    time_t now = time(nullptr);
    char buf[48];
    const int ROW_H = BODY_H / NUM_CITIES;

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(CLR_WHITE);
        drawTopBar("Multiple Locations", (uint32_t)now);

        for (int i = 0; i < NUM_CITIES; i++) {
            const CityWeather& c   = cities[i];
            const CityConfig&  cfg = CITIES[i];

            int y0 = BODY_Y + i * ROW_H;
            if (i > 0) display.drawFastHLine(0, y0, W, CLR_BLACK);

            int midY = y0 + ROW_H / 2;

            // City name + label (left block, 160px wide)
            display.setFont(&FreeSansBold12pt7b);
            display.setCursor(14, midY - 4);
            display.print(cfg.name);
            display.setFont(&FreeSans9pt7b);
            display.setCursor(14, midY + 14);
            display.print(cfg.label);

            // Icon
            drawWeatherIcon(c.conditionIcon, 200, midY, 18);

            // Temp
            snprintf(buf, sizeof(buf), "%d\xB0", (int)round(c.temp));
            display.setFont(&FreeSansBold24pt7b);
            display.setCursor(242, midY + 12);
            display.print(buf);

            // Detail (right of temp, separated by thin line)
            display.drawFastVLine(370, y0 + 6, ROW_H - 12, CLR_BLACK);

            display.setFont(&FreeSans9pt7b);
            snprintf(buf, sizeof(buf), "Rain: %.2f in", c.rainAmt > 0 ? c.rainAmt : 0.0f);
            display.setCursor(382, midY - 6);
            display.print(buf);

            snprintf(buf, sizeof(buf), "Wind: %.0f mph %s",
                     c.windSpeed, windDegToCardinal(c.windDeg));
            display.setCursor(382, midY + 12);
            display.print(buf);

            // Local time (top right of row)
            char timeBuf[16];
            formatTime((uint32_t)now, timeBuf, sizeof(timeBuf));
            display.setFont(&FreeSans9pt7b);
            int16_t x1, y1; uint16_t tw, th;
            display.getTextBounds(timeBuf, 0, 0, &x1, &y1, &tw, &th);
            display.setCursor(W - tw - 10, y0 + 14);
            display.print(timeBuf);
        }

    } while (display.nextPage());
}

// ============================================================
//  VIEW 4: BIBLE VERSE
//  Large opening quote mark on left, verse text right
// ============================================================
void drawVerseView(const VerseOfDay& verse) {
    time_t now = time(nullptr);
    char buf[48];

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(CLR_WHITE);
        drawTopBar("\x04 Verse of the Day", (uint32_t)now);  // \x04 = diamond glyph

        // Large opening quote — decorative, drawn as big " character
        display.setFont(&FreeSansBold24pt7b);
        display.setCursor(18, BODY_Y + 60);
        display.print("\"");

        // Verse text — wrapped manually in bounding box
        // Box: x=70 to x=720, y=BODY_Y+8, height=BODY_H-30
        int textX   = 70;
        int textMaxW = 720 - textX;
        int textY   = BODY_Y + 28;
        int lineH   = 22;

        display.setFont(&FreeSerifItalic12pt7b);

        // Simple word-wrap
        String verseStr = String(verse.text);
        String line = "";
        String word = "";
        int curY = textY;

        for (int ci = 0; ci <= (int)verseStr.length(); ci++) {
            char ch = (ci < (int)verseStr.length()) ? verseStr[ci] : ' ';

            if (ch == ' ' || ci == (int)verseStr.length()) {
                String testLine = line.isEmpty() ? word : (line + " " + word);
                int16_t x1, y1; uint16_t tw, th;
                display.getTextBounds(testLine.c_str(), 0, 0, &x1, &y1, &tw, &th);
                if ((int)tw > textMaxW && !line.isEmpty()) {
                    display.setCursor(textX, curY);
                    display.print(line);
                    curY += lineH;
                    line = word;
                } else {
                    line = testLine;
                }
                word = "";
            } else {
                word += ch;
            }
            if (curY > BODY_Y + BODY_H - lineH * 2) break;  // no overflow
        }
        // Print last line
        if (!line.isEmpty() && curY <= BODY_Y + BODY_H - lineH * 2) {
            display.setCursor(textX, curY);
            display.print(line);
            curY += lineH;
        }

        // Reference + translation
        snprintf(buf, sizeof(buf), "— %s  (%s)", verse.reference, verse.translation);
        display.setFont(&FreeSansBold9pt7b);
        display.setCursor(textX, curY + 10);
        display.print(buf);

    } while (display.nextPage());
}

// ============================================================
//  STALE INDICATOR
//  Small "!" badge top-right, drawn AFTER a view is rendered
//  using partial window to avoid a full redraw
// ============================================================
void drawStaleIndicator() {
    display.setPartialWindow(W - 28, 4, 24, 28);
    display.firstPage();
    do {
        display.fillRect(W - 28, 4, 24, 28, CLR_BLACK);
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(CLR_WHITE);
        display.setCursor(W - 22, 26);
        display.print("!");
        display.setTextColor(CLR_BLACK);
    } while (display.nextPage());
}
