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
#include <Fonts/FreeSerifItalic18pt7b.h>

// ============================================================
//  DISPLAY OBJECT
//  Constructor: (CS, DC, RST, BUSY)
//  SPI pins configured separately via SPIClass
// ============================================================
SPIClass epd_spi(HSPI);

GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> epd(
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

    epd_spi.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, -1);
    epd.epd2.selectSPI(epd_spi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    epd.init(115200, true, 2, false);
    epd.setRotation(0);     // landscape, USB-C on left
    epd.setTextColor(CLR_BLACK);
    epd.setTextWrap(false);
    Serial.println("[display] Init OK");
}

void displayClear() {
    epd.clearScreen();
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
    epd.drawFastHLine(0, TOPBAR_H, W, CLR_BLACK);

    // Left: location or view name
    epd.setFont(&FreeSansBold9pt7b);
    epd.setCursor(10, TOPBAR_H - 10);
    epd.print(leftText);

    // Right: date + time
    char dateBuf[32], timeBuf[16], dtBuf[52];
    formatDate(epoch, dateBuf, sizeof(dateBuf));
    formatTime(epoch, timeBuf, sizeof(timeBuf));
    snprintf(dtBuf, sizeof(dtBuf), "%s  |  %s", dateBuf, timeBuf);

    epd.setFont(&FreeSans9pt7b);
    int16_t x1, y1; uint16_t tw, th;
    epd.getTextBounds(dtBuf, 0, 0, &x1, &y1, &tw, &th);
    epd.setCursor(W - tw - 10, TOPBAR_H - 10);
    epd.print(dtBuf);
}

// Draw a vertical divider line in the body area
static void drawVDivider(int x) {
    epd.drawFastVLine(x, BODY_Y, BODY_H, CLR_BLACK);
}

// Draw a horizontal divider within a column region
static void drawHDivider(int y, int x0, int x1) {
    epd.drawFastHLine(x0, y, x1 - x0, CLR_BLACK);
}

// Centred text in a bounding box
static void drawCentredText(const GFXfont* font, const char* str,
                            int bx, int by, int bw, int bh) {
    epd.setFont(font);
    int16_t x1, y1; uint16_t tw, th;
    epd.getTextBounds(str, 0, 0, &x1, &y1, &tw, &th);
    int cx = bx + (bw - (int)tw) / 2 - x1;
    int cy = by + (bh + (int)th) / 2;
    epd.setCursor(cx, cy);
    epd.print(str);
}

// Small muted label above a value
static void drawStatLabel(const char* label, int x, int y) {
    epd.setFont(&FreeSans9pt7b);
    epd.setCursor(x, y);
    epd.print(label);
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
            epd.fillCircle(cx, cy, r, CLR_BLACK);
            epd.fillCircle(cx, cy, r - 4, CLR_WHITE);
            // Rays
            for (int i = 0; i < 8; i++) {
                float ang = i * PI / 4.0f;
                int x1 = cx + (r + 3) * cos(ang);
                int y1 = cy + (r + 3) * sin(ang);
                int x2 = cx + (r + 9) * cos(ang);
                int y2 = cy + (r + 9) * sin(ang);
                epd.drawLine(x1, y1, x2, y2, CLR_BLACK);
            }
            break;
        }
        case ICON_CLEAR_NIGHT: {
            // Crescent moon
            epd.fillCircle(cx, cy, r, CLR_BLACK);
            epd.fillCircle(cx + r / 2, cy - r / 4, r, CLR_WHITE);
            break;
        }
        case ICON_FEW_CLOUDS:
        case ICON_SCATTERED: {
            // Small sun + cloud
            epd.fillCircle(cx - r / 3, cy - r / 3, r / 2, CLR_BLACK);
            epd.fillCircle(cx - r / 3, cy - r / 3, r / 2 - 3, CLR_WHITE);
            // Cloud body
            epd.fillRoundRect(cx - r, cy, r * 2, r, r / 2, CLR_BLACK);
            epd.fillCircle(cx - r / 3, cy, r / 2 + 2, CLR_BLACK);
            epd.fillCircle(cx + r / 3, cy, r / 2 + 4, CLR_BLACK);
            epd.fillRoundRect(cx - r + 2, cy + 2, r * 2 - 4, r - 4, r / 2 - 2, CLR_WHITE);
            epd.fillCircle(cx - r / 3, cy, r / 2, CLR_WHITE);
            epd.fillCircle(cx + r / 3, cy, r / 2 + 2, CLR_WHITE);
            break;
        }
        case ICON_BROKEN: {
            // Simple cloud only
            epd.fillRoundRect(cx - r, cy - r / 3, r * 2, r, r / 2, CLR_BLACK);
            epd.fillCircle(cx - r / 3, cy - r / 3, r / 2 + 2, CLR_BLACK);
            epd.fillCircle(cx + r / 3, cy - r / 3, r / 2 + 4, CLR_BLACK);
            epd.fillRoundRect(cx - r + 2, cy - r / 3 + 2, r * 2 - 4, r - 4, r / 2 - 2, CLR_WHITE);
            epd.fillCircle(cx - r / 3, cy - r / 3, r / 2, CLR_WHITE);
            epd.fillCircle(cx + r / 3, cy - r / 3, r / 2 + 2, CLR_WHITE);
            break;
        }
        case ICON_SHOWER_RAIN:
        case ICON_RAIN: {
            // Cloud + rain drops
            epd.fillRoundRect(cx - r, cy - r / 2, r * 2, r - 4, r / 2, CLR_BLACK);
            epd.fillCircle(cx - r / 3, cy - r / 2, r / 2 + 2, CLR_BLACK);
            epd.fillCircle(cx + r / 3, cy - r / 2, r / 2 + 4, CLR_BLACK);
            epd.fillRoundRect(cx - r + 2, cy - r / 2 + 2, r * 2 - 4, r - 8, r / 2 - 2, CLR_WHITE);
            epd.fillCircle(cx - r / 3, cy - r / 2, r / 2, CLR_WHITE);
            epd.fillCircle(cx + r / 3, cy - r / 2, r / 2 + 2, CLR_WHITE);
            // Drops
            for (int i = -1; i <= 1; i++)
                epd.drawLine(cx + i * (r / 2), cy + 4, cx + i * (r / 2) - 3, cy + 12, CLR_BLACK);
            break;
        }
        case ICON_THUNDERSTORM: {
            // Cloud + lightning bolt
            epd.fillRoundRect(cx - r, cy - r / 2, r * 2, r - 4, r / 2, CLR_BLACK);
            epd.fillCircle(cx - r / 3, cy - r / 2, r / 2 + 2, CLR_BLACK);
            epd.fillCircle(cx + r / 3, cy - r / 2, r / 2 + 4, CLR_BLACK);
            epd.fillRoundRect(cx - r + 2, cy - r / 2 + 2, r * 2 - 4, r - 8, r / 2 - 2, CLR_WHITE);
            epd.fillCircle(cx - r / 3, cy - r / 2, r / 2, CLR_WHITE);
            epd.fillCircle(cx + r / 3, cy - r / 2, r / 2 + 2, CLR_WHITE);
            // Bolt
            epd.drawLine(cx + 4, cy + 2, cx - 2, cy + 10, CLR_BLACK);
            epd.drawLine(cx - 2, cy + 10, cx + 3, cy + 10, CLR_BLACK);
            epd.drawLine(cx + 3, cy + 10, cx - 4, cy + 20, CLR_BLACK);
            break;
        }
        case ICON_SNOW: {
            // Snowflake (6 lines)
            for (int i = 0; i < 6; i++) {
                float ang = i * PI / 3.0f;
                epd.drawLine(cx, cy,
                                 cx + r * cos(ang), cy + r * sin(ang),
                                 CLR_BLACK);
            }
            epd.fillCircle(cx, cy, 3, CLR_BLACK);
            break;
        }
        case ICON_MIST:
        default: {
            // Three horizontal hazy lines
            for (int i = -1; i <= 1; i++)
                epd.drawFastHLine(cx - r, cy + i * (r / 2), r * 2, CLR_BLACK);
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
                       uint32_t nextSunriseEpoch, uint32_t nowEpoch) {

    bool isNight = (nowEpoch > sunsetEpoch || nowEpoch < sunriseEpoch);

    if (!isNight) {
        // ---- DAYTIME: solid arc ----
        const int STEPS = 60;
        for (int i = 0; i < STEPS; i++) {
            float ang1 = PI - (float)i       / STEPS * PI;
            float ang2 = PI - (float)(i + 1) / STEPS * PI;
            int x1 = cx + arcR * cos(ang1);
            int y1 = cy - arcR * sin(ang1);
            int x2 = cx + arcR * cos(ang2);
            int y2 = cy - arcR * sin(ang2);
            epd.drawLine(x1, y1, x2, y2, CLR_BLACK);
        }

        // Tick marks
        epd.drawFastVLine(cx - arcR, cy - 4, 8, CLR_BLACK);
        epd.drawFastVLine(cx + arcR, cy - 4, 8, CLR_BLACK);

        // Sun dot
        if (nowEpoch >= sunriseEpoch && nowEpoch <= sunsetEpoch
            && sunsetEpoch > sunriseEpoch) {
            float progress = (float)(nowEpoch - sunriseEpoch) /
                             (float)(sunsetEpoch - sunriseEpoch);
            float ang = PI - progress * PI;
            int sx = cx + arcR * cos(ang);
            int sy = cy - arcR * sin(ang);
            epd.fillCircle(sx, sy, 5, CLR_BLACK);
            epd.fillCircle(sx, sy, 3, CLR_WHITE);
            epd.fillCircle(sx, sy, 1, CLR_BLACK);
        }

    } else {
        // ---- NIGHTTIME: dashed arc ----
        const int STEPS = 60;
        for (int i = 0; i < STEPS; i++) {
            // Skip every other segment for dashed effect
            if (i % 3 == 2) continue;
            float ang1 = PI - (float)i       / STEPS * PI;
            float ang2 = PI - (float)(i + 1) / STEPS * PI;
            int x1 = cx + arcR * cos(ang1);
            int y1 = cy - arcR * sin(ang1);
            int x2 = cx + arcR * cos(ang2);
            int y2 = cy - arcR * sin(ang2);
            epd.drawLine(x1, y1, x2, y2, CLR_BLACK);
        }

        // Tick marks
        epd.drawFastVLine(cx - arcR, cy - 4, 8, CLR_BLACK);
        epd.drawFastVLine(cx + arcR, cy - 4, 8, CLR_BLACK);

        // Moon dot position along arc
        // Arc runs from sunset (left=180°) to next sunrise (right=0°)
        uint32_t arcStart = sunsetEpoch;
        uint32_t arcEnd   = nextSunriseEpoch;

        if (arcEnd > arcStart && nowEpoch >= arcStart) {
            float progress = (float)(nowEpoch - arcStart) /
                             (float)(arcEnd - arcStart);
            progress = constrain(progress, 0.0f, 1.0f);
            float ang = PI - progress * PI;
            int mx = cx + arcR * cos(ang);
            int my = cy - arcR * sin(ang);

            // Filled crescent moon
            epd.fillCircle(mx, my, 6, CLR_BLACK);          // full circle
            epd.fillCircle(mx + 3, my - 2, 5, CLR_WHITE);  // offset cutout = crescent
        }
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

    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(CLR_WHITE);

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

            // Horizontally centre each item, keep original vertical positions
            int16_t x1, y1; uint16_t tw, th;

            // Label
            epd.setFont(&FreeSans9pt7b);
            epd.getTextBounds("UV INDEX", 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(COL1_W/2 - tw/2 - x1, y0 + 14);
            epd.print("UV INDEX");

            // Big number
            snprintf(buf, sizeof(buf), "%d", wx.uvIndex);
            epd.setFont(&FreeSansBold24pt7b);
            epd.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(COL1_W/2 - tw/2 - x1, y0 + rowH - 22);
            epd.print(buf);

            // Sub-label
            const char* uvLabel = wx.uvIndex <= 2 ? "Low" :
                                wx.uvIndex <= 5 ? "Moderate" :
                                wx.uvIndex <= 7 ? "High" : "Very High";
            epd.setFont(&FreeSans9pt7b);
            epd.getTextBounds(uvLabel, 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(COL1_W/2 - tw/2 - x1, y0 + rowH - 4);
            epd.print(uvLabel);
        }
        // Humidity
        {
            int y0 = BODY_Y + rowH;
            drawHDivider(y0 + rowH, 0, COL1_W);
            int16_t x1, y1; uint16_t tw, th;

            epd.setFont(&FreeSans9pt7b);
            epd.getTextBounds("HUMIDITY", 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(COL1_W/2 - tw/2 - x1, y0 + 14);
            epd.print("HUMIDITY");

            snprintf(buf, sizeof(buf), "%d%%", wx.humidity);
            epd.setFont(&FreeSansBold24pt7b);
            epd.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(COL1_W/2 - tw/2 - x1, y0 + rowH - 22);
            epd.print(buf);
        }
        // Pressure
        {
            int y0 = BODY_Y + rowH * 2;
            int16_t x1, y1; uint16_t tw, th;

            epd.setFont(&FreeSans9pt7b);
            epd.getTextBounds("PRESSURE", 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(COL1_W/2 - tw/2 - x1, y0 + 14);
            epd.print("PRESSURE");

            snprintf(buf, sizeof(buf), "%.1f", wx.pressure);
            epd.setFont(&FreeSansBold24pt7b);
            epd.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(COL1_W/2 - tw/2 - x1, y0 + rowH - 22);
            epd.print(buf);

            epd.setFont(&FreeSans9pt7b);
            epd.getTextBounds("inHg", 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(COL1_W/2 - tw/2 - x1, y0 + rowH - 4);
            epd.print("inHg");
        }
        // ---- COL 2: Temp block (upper) + Arc + Bottom row ----
        int col2MidY = BODY_Y + BODY_H / 2;

        // Icon + Temp side by side, horizontally centred in COL2
        {
            int iconR = 22;
            int iconY = BODY_Y + BODY_H / 4 - 15;

            // Measure temp string width
            snprintf(buf, sizeof(buf), "%d\xB0", (int)round(wx.temp));
            epd.setFont(&FreeSansBold24pt7b);
            int16_t x1, y1; uint16_t tw, th;
            epd.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);

            // Total block = icon diameter + gap + text width
            int iconDiam = iconR * 2;
            int gap      = 12;
            int blockW   = iconDiam + gap + (int)tw;

            // Centre block within COL2
            int blockStartX = COL2_X + (COL2_W - blockW) / 2;
            int iconCX      = blockStartX + iconR;
            int tempX       = blockStartX + iconDiam + gap - x1;

            drawWeatherIcon(wx.conditionIcon, iconCX, iconY, iconR);

            epd.setFont(&FreeSansBold24pt7b);
            epd.setCursor(tempX, iconY + (int)th / 2);
            epd.print(buf);

            // Feels like + condition centred below
            snprintf(buf, sizeof(buf), "Feels like %d\xB0", (int)round(wx.feelsLike));
            drawCentredText(&FreeSans9pt7b, buf,
                            COL2_X, iconY + (int)th / 2 + 6, COL2_W, 18);
            drawCentredText(&FreeSans9pt7b, wx.condition,
                            COL2_X, iconY + (int)th / 2 + 22, COL2_W, 18);
        }

        // Divider between upper and lower halves of COL2
        drawHDivider(col2MidY, COL2_X, COL3_X);

        // Arc in lower half of COL2
        {
            int arcCX = COL2_X + COL2_W / 2;
            int arcCY = BODY_Y + BODY_H - 14;
            int arcR  = 52;
            drawSunArc(arcCX, arcCY, arcR,
                        wx.sunrise, wx.sunset,
                        wx.nextSunrise, (uint32_t)now);
        }

        // Bottom row: Sunrise | Rain | Sunset  (below arc labels)
        {
            int rowY  = col2MidY + 8;
            int thirdW = COL2_W / 3;

            // Sunrise
            char srBuf[12];
            formatTime(wx.sunrise, srBuf, sizeof(srBuf));
            epd.setFont(&FreeSans9pt7b);
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
        // Wind
        {
            int y0 = BODY_Y;
            drawHDivider(y0 + rowH, COL3_X, W);
            int16_t x1, y1; uint16_t tw, th;
            int cx = COL3_X + COL1_W/2;

            epd.setFont(&FreeSans9pt7b);
            epd.getTextBounds("WIND", 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(cx - tw/2 - x1, y0 + 14);
            epd.print("WIND");

            snprintf(buf, sizeof(buf), "%.0f", wx.windSpeed);
            epd.setFont(&FreeSansBold24pt7b);
            epd.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(cx - tw/2 - x1, y0 + rowH - 22);
            epd.print(buf);

            snprintf(buf, sizeof(buf), "mph %s", windDegToCardinal(wx.windDeg));
            epd.setFont(&FreeSans9pt7b);
            epd.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(cx - tw/2 - x1, y0 + rowH - 4);
            epd.print(buf);
        }
        // Precip Chance
        {
            int y0 = BODY_Y + rowH;
            drawHDivider(y0 + rowH, COL3_X, W);
            int16_t x1, y1; uint16_t tw, th;
            int cx = COL3_X + COL1_W/2;

            epd.setFont(&FreeSans9pt7b);
            epd.getTextBounds("PRECIP CHANCE", 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(cx - tw/2 - x1, y0 + 14);
            epd.print("PRECIP CHANCE");

            snprintf(buf, sizeof(buf), "%d%%", wx.precipChance);
            epd.setFont(&FreeSansBold24pt7b);
            epd.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(cx - tw/2 - x1, y0 + rowH - 22);
            epd.print(buf);
        }
        // Visibility
        {
            int y0 = BODY_Y + rowH * 2;
            int16_t x1, y1; uint16_t tw, th;
            int cx = COL3_X + COL1_W/2;

            epd.setFont(&FreeSans9pt7b);
            epd.getTextBounds("VISIBILITY", 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(cx - tw/2 - x1, y0 + 14);
            epd.print("VISIBILITY");

            snprintf(buf, sizeof(buf), "%.0f", wx.visibility);
            epd.setFont(&FreeSansBold24pt7b);
            epd.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(cx - tw/2 - x1, y0 + rowH - 22);
            epd.print(buf);

            epd.setFont(&FreeSans9pt7b);
            epd.getTextBounds("mi", 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(cx - tw/2 - x1, y0 + rowH - 4);
            epd.print("mi");
        }
    } while (epd.nextPage());
}

// ============================================================
//  VIEW 2: FIVE DAY FORECAST
//  5 equal columns, each ~158px wide
// ============================================================
void drawFiveDayView(const CurrentWeather& wx, const ForecastDay forecast[5]) {
    time_t now = time(nullptr);
    char buf[32];
    const int COL_W = W / 5;   // 158px

    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(CLR_WHITE);
        drawTopBar(PRIMARY_CITY_NAME, (uint32_t)now);

        for (int i = 0; i < 5; i++) {
            int x0 = i * COL_W;
            if (i > 0) epd.drawFastVLine(x0, BODY_Y, BODY_H, CLR_BLACK);

            const ForecastDay& d = forecast[i];
            int cx = x0 + COL_W / 2;

            // Day name
            epd.setFont(&FreeSansBold12pt7b);
            drawCentredText(&FreeSansBold12pt7b, d.day, x0, BODY_Y, COL_W, 24);

            // Divider under day name
            epd.drawFastHLine(x0, BODY_Y + 26, COL_W, CLR_BLACK);

            // Icon
            drawWeatherIcon(d.conditionIcon, cx, BODY_Y + 58, 20);

            // High temp
            snprintf(buf, sizeof(buf), "%d\xB0", (int)round(d.high));
            drawCentredText(&FreeSansBold24pt7b, buf, x0, BODY_Y + 88, COL_W, 34);

            // Low temp (muted — draw slightly offset for visual hierarchy)
            snprintf(buf, sizeof(buf), "%d\xB0", (int)round(d.low));
            drawCentredText(&FreeSans9pt7b, buf, x0, BODY_Y + 124, COL_W, 18);

            // Divider
            epd.drawFastHLine(x0, BODY_Y + 146, COL_W, CLR_BLACK);

            // Detail rows
            int detY = BODY_Y + 178;
            int detX = x0 + 8;

            snprintf(buf, sizeof(buf), "Rain: %d%%", d.precipChance);
            epd.setFont(&FreeSans9pt7b);
            epd.setCursor(detX, detY);      epd.print(buf);

            snprintf(buf, sizeof(buf), "Wind: %d mph", d.windSpeed);
            epd.setCursor(detX, detY + 18); epd.print(buf);

            snprintf(buf, sizeof(buf), "Cloud: %d%%", d.cloudPct);
            epd.setCursor(detX, detY + 36); epd.print(buf);
        }

    } while (epd.nextPage());
}

// ============================================================
//  VIEW 3: MULTI-CITY
//  3 rows, each H/3 tall
// ============================================================
void drawMultiCityView(const CityWeather cities[NUM_CITIES]) {
    time_t now = time(nullptr);
    char buf[32];
    const int ROW_H = BODY_H / 3;
    const int HALF_W = W / 2;          // 396px per column

    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(CLR_WHITE);
        drawTopBar("Multiple Locations", (uint32_t)now);

        // Vertical divider between columns
        epd.drawFastVLine(HALF_W, BODY_Y, BODY_H, CLR_BLACK);

        for (int i = 0; i < NUM_CITIES; i++) {
            const CityWeather& c   = cities[i];
            const CityConfig&  cfg = CITIES[i];

            int col  = i / 3;           // 0 = left, 1 = right
            int row  = i % 3;           // 0, 1, 2
            int x0   = col * HALF_W;
            int y0   = BODY_Y + row * ROW_H;
            int midY = y0 + ROW_H / 2;

            // Row divider
            if (row > 0) epd.drawFastHLine(x0, y0, HALF_W, CLR_BLACK);

            // City name
            epd.setFont(&FreeSansBold12pt7b);
            epd.setCursor(x0 + 14, midY + 6);
            epd.print(cfg.name);

            // Icon
            drawWeatherIcon(c.conditionIcon, x0 + HALF_W - 110, midY, 18);

            // Temp
            snprintf(buf, sizeof(buf), "%d\xB0", (int)round(c.temp));
            epd.setFont(&FreeSansBold24pt7b);
            int16_t x1, y1; uint16_t tw, th;
            epd.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
            epd.setCursor(x0 + HALF_W - (int)tw - 14 - x1, midY + 12);
            epd.print(buf);
        }

    } while (epd.nextPage());
}

// ============================================================
//  VIEW 4: BIBLE VERSE
//  Large opening quote mark on left, verse text right
// ============================================================
void drawVerseView(const VerseOfDay& verse) {
    time_t now = time(nullptr);
    char buf[48];

    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(CLR_WHITE);
        drawTopBar("\x04 Verse of the Day", (uint32_t)now);  // \x04 = diamond glyph

        // Large opening quote — decorative, drawn as big " character
        epd.setFont(&FreeSansBold24pt7b);
        epd.setCursor(18, BODY_Y + 60);
        epd.print("\"");

        // Verse text — wrapped manually in bounding box
        // Box: x=70 to x=720, y=BODY_Y+8, height=BODY_H-30
        int textX   = 70;
        int textMaxW = 720 - textX;
        int textY   = BODY_Y + 45;
        int lineH   = 30;

        epd.setFont(&FreeSerifItalic18pt7b);
        
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
                epd.getTextBounds(testLine.c_str(), 0, 0, &x1, &y1, &tw, &th);
                if ((int)tw > textMaxW && !line.isEmpty()) {
                    epd.setCursor(textX, curY);
                    epd.print(line);
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
            epd.setCursor(textX, curY);
            epd.print(line);
            curY += lineH;
        }

        // Reference + translation
        snprintf(buf, sizeof(buf), "— %s  (%s)", verse.reference, verse.translation);
        epd.setFont(&FreeSansBold9pt7b);
        int16_t x1, y1; uint16_t tw, th;
        epd.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
        epd.setCursor(W - (int)tw - 20, curY + 10);
        epd.print(buf);

    } while (epd.nextPage());
}

// ============================================================
//  STALE INDICATOR
//  Small "!" badge top-right, drawn AFTER a view is rendered
//  using partial window to avoid a full redraw
// ============================================================
void drawStaleIndicator() {
    epd.setPartialWindow(W - 28, 4, 24, 28);
    epd.firstPage();
    do {
        epd.fillRect(W - 28, 4, 24, 28, CLR_BLACK);
        epd.setFont(&FreeSansBold12pt7b);
        epd.setTextColor(CLR_WHITE);
        epd.setCursor(W - 22, 26);
        epd.print("!");
        epd.setTextColor(CLR_BLACK);
    } while (epd.nextPage());
}
