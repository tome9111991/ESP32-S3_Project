#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>

// Simple display/touch test for ESP32-8048S043C:
// 800x480 RGB display, GT911 capacitive touch on I2C.

static constexpr int LCD_W = 800;
static constexpr int LCD_H = 480;
static constexpr int LCD_BL = 2;
static constexpr int LCD_PCLK_HZ = 16000000;
static constexpr bool ANIMATION_ENABLED = false;
static constexpr bool TOUCH_DRAW_ENABLED = false;
static constexpr uint32_t TOUCH_POLL_INTERVAL_MS = 15;
static constexpr uint32_t TOUCH_LOG_INTERVAL_MS = 80;
static constexpr uint32_t TOUCH_DRAW_INTERVAL_MS = 120;
static constexpr uint32_t ANIMATION_INTERVAL_MS = 120;

static constexpr int TOUCH_SDA = 19;
static constexpr int TOUCH_SCL = 20;
static constexpr int TOUCH_RST = 38;
static constexpr int TOUCH_INT = 18; // Often not connected on this board class.

static constexpr uint16_t GT911_STATUS_REG = 0x814E;
static constexpr uint16_t GT911_POINT_REG = 0x814F;
static constexpr uint16_t GT911_PRODUCT_ID_REG = 0x8140;

static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    40 /* DE */, 41 /* VSYNC */, 39 /* HSYNC */, 42 /* PCLK */,
    45 /* R0 */, 48 /* R1 */, 47 /* R2 */, 21 /* R3 */, 14 /* R4 */,
    5 /* G0 */, 6 /* G1 */, 7 /* G2 */, 15 /* G3 */, 16 /* G4 */, 4 /* G5 */,
    8 /* B0 */, 3 /* B1 */, 46 /* B2 */, 9 /* B3 */, 1 /* B4 */,
    0 /* hsync_polarity */, 8 /* hsync_front_porch */,
    4 /* hsync_pulse_width */, 16 /* hsync_back_porch */,
    0 /* vsync_polarity */, 4 /* vsync_front_porch */,
    4 /* vsync_pulse_width */, 4 /* vsync_back_porch */,
    1 /* pclk_active_neg */, LCD_PCLK_HZ /* prefer_speed */, false /* useBigEndian */,
    0 /* de_idle_high */, 1 /* pclk_idle_high */);

static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    LCD_W, LCD_H, rgbpanel, 0 /* rotation */, false /* auto_flush */);

static uint8_t touch_addr = 0;
static int last_touch_x = -1;
static int last_touch_y = -1;
static int last_draw_x = -1;
static int last_draw_y = -1;
static uint32_t touch_count = 0;

static void flushArea(int x, int y, int w, int h)
{
  if (w <= 0 || h <= 0) {
    return;
  }

  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > LCD_W) {
    w = LCD_W - x;
  }
  if (y + h > LCD_H) {
    h = LCD_H - y;
  }
  if (w <= 0 || h <= 0) {
    return;
  }

  uint16_t *fb = gfx->getFramebuffer();
  if (!fb) {
    return;
  }

  uint16_t *start = fb + ((int32_t)y * LCD_W) + x;
  size_t bytes = (((size_t)(h - 1) * LCD_W) + w) * sizeof(uint16_t);
  Cache_WriteBack_Addr((uint32_t)(uintptr_t)start, (uint32_t)bytes);
}

static void flushCircleArea(int x, int y, int radius)
{
  flushArea(x - radius - 1, y - radius - 1, (radius * 2) + 3, (radius * 2) + 3);
}

static void setBacklight(bool on)
{
  digitalWrite(LCD_BL, on ? HIGH : LOW);
}

static void backlightProbe()
{
  pinMode(LCD_BL, OUTPUT);
  Serial.println("Backlight GPIO2 probe: blink 3x, then ON");

  for (int i = 0; i < 3; ++i) {
    setBacklight(true);
    delay(250);
    setBacklight(false);
    delay(250);
  }

  setBacklight(true);
  delay(300);
}

static bool i2cWriteBytes(uint8_t addr, const uint8_t *data, size_t len)
{
  Wire.beginTransmission(addr);
  Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

static bool i2cReadReg(uint8_t addr, uint16_t reg, uint8_t *buf, size_t len)
{
  Wire.beginTransmission(addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  size_t got = Wire.requestFrom(addr, len, true);
  if (got != len) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }

  for (size_t i = 0; i < len; ++i) {
    buf[i] = Wire.read();
  }
  return true;
}

static void gt911ClearStatus()
{
  if (!touch_addr) {
    return;
  }

  const uint8_t clear_status[] = {
      (uint8_t)(GT911_STATUS_REG >> 8),
      (uint8_t)(GT911_STATUS_REG & 0xFF),
      0x00};
  i2cWriteBytes(touch_addr, clear_status, sizeof(clear_status));
}

static void gt911Reset()
{
  pinMode(TOUCH_INT, INPUT_PULLUP);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(20);
  digitalWrite(TOUCH_RST, HIGH);
  delay(120);
}

static uint8_t findGT911()
{
  const uint8_t candidates[] = {0x5D, 0x14};
  uint8_t id[4] = {};

  for (uint8_t addr : candidates) {
    if (i2cReadReg(addr, GT911_PRODUCT_ID_REG, id, sizeof(id))) {
      Serial.printf("GT911 at 0x%02X, product id: %c%c%c%c\n",
                    addr, id[0], id[1], id[2], id[3]);
      return addr;
    }
  }

  return 0;
}

static String scanI2C()
{
  String found;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      char tmp[8];
      snprintf(tmp, sizeof(tmp), "0x%02X ", addr);
      found += tmp;
    }
  }
  if (found.length() == 0) {
    found = "keine";
  }
  return found;
}

static bool readTouch(int16_t &x, int16_t &y)
{
  if (!touch_addr) {
    return false;
  }

  uint8_t status = 0;
  if (!i2cReadReg(touch_addr, GT911_STATUS_REG, &status, 1)) {
    return false;
  }

  if ((status & 0x80) == 0) {
    return false;
  }

  uint8_t points = status & 0x0F;
  if (points == 0 || points > 5) {
    gt911ClearStatus();
    return false;
  }

  uint8_t data[8] = {};
  bool ok = i2cReadReg(touch_addr, GT911_POINT_REG, data, sizeof(data));
  gt911ClearStatus();
  if (!ok) {
    return false;
  }

  x = data[1] | (data[2] << 8);
  y = data[3] | (data[4] << 8);
  return true;
}

static void drawCornerTarget(int x, int y, const char *label)
{
  gfx->drawCircle(x, y, 22, RGB565_WHITE);
  gfx->drawCircle(x, y, 10, RGB565_WHITE);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setTextSize(1);

  int text_x = x < 100 ? x + 28 : x - 88;
  int text_y = y < 100 ? y + 12 : y - 22;
  gfx->setCursor(text_x, text_y);
  gfx->print(label);
}

static void drawStaticScreen()
{
  gfx->fillScreen(RGB565_BLACK);

  gfx->fillRect(0, 0, LCD_W, 46, RGB565(26, 33, 42));
  gfx->setTextColor(RGB565_WHITE, RGB565(26, 33, 42));
  gfx->setTextSize(2);
  gfx->setCursor(14, 12);
  gfx->print("ESP32-8048S043C Display + Touch Test");

  const int bar_y = 64;
  const int bar_h = 76;
  const int bar_w = LCD_W / 8;
  const uint16_t colors[] = {
      RGB565_RED, RGB565_GREEN, RGB565_BLUE, RGB565_CYAN,
      RGB565_MAGENTA, RGB565_YELLOW, RGB565_WHITE, RGB565_BLACK};
  const char *names[] = {"Rot", "Gruen", "Blau", "Cyan", "Mag", "Gelb", "Weiss", "Schwarz"};
  for (int i = 0; i < 8; ++i) {
    gfx->fillRect(i * bar_w, bar_y, bar_w, bar_h, colors[i]);
    gfx->drawRect(i * bar_w, bar_y, bar_w, bar_h, RGB565_DARKGREY);
    gfx->setTextSize(1);
    gfx->setTextColor(i == 6 ? RGB565_BLACK : RGB565_WHITE, colors[i]);
    gfx->setCursor(i * bar_w + 10, bar_y + 10);
    gfx->print(names[i]);
  }

  for (int x = 0; x < LCD_W; x += 40) {
    gfx->drawFastVLine(x, 160, LCD_H - 160, RGB565(35, 50, 65));
  }
  for (int y = 160; y < LCD_H; y += 40) {
    gfx->drawFastHLine(0, y, LCD_W, RGB565(35, 50, 65));
  }

  gfx->drawRect(0, 0, LCD_W, LCD_H, RGB565_WHITE);
  drawCornerTarget(34, 184, "oben links");
  drawCornerTarget(LCD_W - 35, 184, "oben rechts");
  drawCornerTarget(34, LCD_H - 35, "unten links");
  drawCornerTarget(LCD_W - 35, LCD_H - 35, "unten rechts");

  gfx->fillRect(170, 178, 460, 84, RGB565(12, 18, 24));
  gfx->drawRect(170, 178, 460, 84, RGB565(100, 120, 140));
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_WHITE, RGB565(12, 18, 24));
  gfx->setCursor(190, 196);
  gfx->print("Touch: Finger auf das Display setzen");
  gfx->setTextSize(1);
  gfx->setCursor(190, 232);
  gfx->print("Kreise pruefen Orientierung und Randbereiche.");
  flushArea(0, 0, LCD_W, LCD_H);
}

static void drawStatus()
{
  gfx->fillRect(0, 144, LCD_W, 28, RGB565(18, 24, 31));
  gfx->setTextColor(RGB565_WHITE, RGB565(18, 24, 31));
  gfx->setTextSize(1);
  gfx->setCursor(12, 153);
  gfx->printf("Display: RGB 800x480, PCLK %dMHz, BL GPIO2", LCD_PCLK_HZ / 1000000);

  gfx->setCursor(330, 153);
  if (touch_addr) {
    gfx->printf("GT911: 0x%02X, SDA19/SCL20/RST38", touch_addr);
  } else {
    gfx->print("GT911 nicht gefunden");
  }
  flushArea(0, 144, LCD_W, 28);
}

static void drawTouchPoint(int16_t x, int16_t y, uint32_t count)
{
  if (last_draw_x >= 0 && last_draw_y >= 0) {
    gfx->drawCircle(last_draw_x, last_draw_y, 18, RGB565(80, 80, 80));
  }

  gfx->drawCircle(x, y, 18, RGB565_RED);
  gfx->drawFastHLine(x - 24, y, 49, RGB565_RED);
  gfx->drawFastVLine(x, y - 24, 49, RGB565_RED);

  gfx->fillRect(170, 280, 460, 44, RGB565_BLACK);
  gfx->drawRect(170, 280, 460, 44, RGB565_DARKGREY);
  gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(190, 294);
  gfx->printf("Touch #%lu  x=%d  y=%d", count, x, y);

  if (last_draw_x >= 0 && last_draw_y >= 0) {
    flushCircleArea(last_draw_x, last_draw_y, 24);
  }
  flushCircleArea(x, y, 24);
  flushArea(170, 280, 460, 44);

  last_draw_x = x;
  last_draw_y = y;
}

void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("ESP32-8048S043C Arduino_GFX display/touch test");
  backlightProbe();

  Serial.println("gfx->begin() start");
  bool gfx_ok = gfx->begin();
  Serial.printf("gfx->begin(): %s\n", gfx_ok ? "OK" : "FAIL");
  setBacklight(true);
  drawStaticScreen();

  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  gt911Reset();
  touch_addr = findGT911();

  Serial.print("I2C scan: ");
  String devices = scanI2C();
  Serial.println(devices);

  drawStatus();
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setTextSize(1);
  gfx->setCursor(190, 340);
  gfx->print("I2C Geraete: ");
  gfx->print(devices);
  flushArea(190, 340, 260, 16);

  gfx->fillRect(170, 360, 460, 18, RGB565_BLACK);
  gfx->drawRect(170, 360, 460, 18, RGB565_DARKGREY);
  gfx->setCursor(190, 365);
  gfx->print(ANIMATION_ENABLED ? "Animation aktiv" : "Animation aus");
  flushArea(170, 360, 460, 18);

  gfx->fillRect(170, 390, 460, 18, RGB565_BLACK);
  gfx->drawRect(170, 390, 460, 18, RGB565_DARKGREY);
  gfx->setCursor(190, 395);
  gfx->print(TOUCH_DRAW_ENABLED ? "Touch-Zeichnen aktiv" : "Touch-Zeichnen aus, Serial aktiv");
  flushArea(170, 390, 460, 18);
}

void loop()
{
  static uint32_t last_anim = 0;
  static uint32_t last_touch_poll = 0;
  static uint32_t last_touch_log = 0;
  static uint32_t last_touch_draw = 0;
  static int anim_x = 0;
  uint32_t now = millis();

  if (now - last_touch_poll >= TOUCH_POLL_INTERVAL_MS) {
    last_touch_poll = now;
    int16_t x = 0;
    int16_t y = 0;
    if (readTouch(x, y)) {
      int dx = abs((int)x - last_touch_x);
      int dy = abs((int)y - last_touch_y);
      if ((now - last_touch_log >= TOUCH_LOG_INTERVAL_MS) && (last_touch_x < 0 || dx > 2 || dy > 2)) {
        last_touch_log = now;
        touch_count++;
        Serial.printf("Touch #%lu: x=%d y=%d\n", touch_count, x, y);
      }

      if (TOUCH_DRAW_ENABLED && (now - last_touch_draw >= TOUCH_DRAW_INTERVAL_MS) &&
          (last_draw_x < 0 || abs((int)x - last_draw_x) > 8 || abs((int)y - last_draw_y) > 8)) {
        last_touch_draw = now;
        drawTouchPoint(x, y, touch_count);
      }

      last_touch_x = x;
      last_touch_y = y;
    }
  }

  now = millis();
  if (ANIMATION_ENABLED && (now - last_anim > ANIMATION_INTERVAL_MS)) {
    last_anim = now;
    gfx->fillRect(170, 360, 460, 18, RGB565_BLACK);
    gfx->drawRect(170, 360, 460, 18, RGB565_DARKGREY);
    gfx->fillRect(171 + anim_x, 361, 28, 16, RGB565_CYAN);
    flushArea(170, 360, 460, 18);
    anim_x += 5;
    if (anim_x > 430) {
      anim_x = 0;
    }
  }

  delay(5);
}
