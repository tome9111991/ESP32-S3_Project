#define LGFX_USE_V1

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Wire.h>

#include <driver/gpio.h>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>

static constexpr int LCD_W = 800;
static constexpr int LCD_H = 480;
static constexpr int LCD_BL = 2;

static constexpr int TOUCH_SDA = 19;
static constexpr int TOUCH_SCL = 20;
static constexpr int TOUCH_RST = 38;
static constexpr int TOUCH_INT = 18;

static constexpr uint16_t GT911_STATUS_REG = 0x814E;
static constexpr uint16_t GT911_POINT_REG = 0x814F;
static constexpr uint16_t GT911_PRODUCT_ID_REG = 0x8140;

class LGFX : public lgfx::LGFX_Device
{
public:
  lgfx::Bus_RGB _bus_instance;
  lgfx::Panel_RGB _panel_instance;

  LGFX()
  {
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width = LCD_W;
      cfg.memory_height = LCD_H;
      cfg.panel_width = LCD_W;
      cfg.panel_height = LCD_H;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }

    {
      auto cfg = _panel_instance.config_detail();
      cfg.use_psram = 1;
      _panel_instance.config_detail(cfg);
    }

    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;

      cfg.pin_d0 = GPIO_NUM_8;   // B0
      cfg.pin_d1 = GPIO_NUM_3;   // B1
      cfg.pin_d2 = GPIO_NUM_46;  // B2
      cfg.pin_d3 = GPIO_NUM_9;   // B3
      cfg.pin_d4 = GPIO_NUM_1;   // B4
      cfg.pin_d5 = GPIO_NUM_5;   // G0
      cfg.pin_d6 = GPIO_NUM_6;   // G1
      cfg.pin_d7 = GPIO_NUM_7;   // G2
      cfg.pin_d8 = GPIO_NUM_15;  // G3
      cfg.pin_d9 = GPIO_NUM_16;  // G4
      cfg.pin_d10 = GPIO_NUM_4;  // G5
      cfg.pin_d11 = GPIO_NUM_45; // R0
      cfg.pin_d12 = GPIO_NUM_48; // R1
      cfg.pin_d13 = GPIO_NUM_47; // R2
      cfg.pin_d14 = GPIO_NUM_21; // R3
      cfg.pin_d15 = GPIO_NUM_14; // R4

      cfg.pin_henable = GPIO_NUM_40;
      cfg.pin_vsync = GPIO_NUM_41;
      cfg.pin_hsync = GPIO_NUM_39;
      cfg.pin_pclk = GPIO_NUM_42;
      cfg.freq_write = 14000000;

      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 8;
      cfg.hsync_pulse_width = 4;
      cfg.hsync_back_porch = 16;
      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 4;
      cfg.vsync_pulse_width = 4;
      cfg.vsync_back_porch = 4;
      cfg.pclk_idle_high = 1;
      _bus_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);

    setPanel(&_panel_instance);
  }
};

static LGFX lcd;
static uint8_t touch_addr = 0;
static int last_x = -1;
static int last_y = -1;
static uint32_t touch_count = 0;
static volatile bool lcd_init_done = false;
static volatile bool lcd_init_ok = false;

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
  return found.length() ? found : "keine";
}

static void drawCornerTarget(int x, int y, const char *label)
{
  lcd.drawCircle(x, y, 22, TFT_WHITE);
  lcd.drawCircle(x, y, 10, TFT_WHITE);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setCursor(x < 100 ? x + 28 : x - 88, y < 100 ? y + 12 : y - 22);
  lcd.print(label);
}

static void drawStaticScreen()
{
  lcd.fillScreen(TFT_BLACK);
  lcd.fillRect(0, 0, LCD_W, 46, lcd.color888(26, 33, 42));
  lcd.setTextColor(TFT_WHITE, lcd.color888(26, 33, 42));
  lcd.setTextSize(2);
  lcd.setCursor(14, 12);
  lcd.print("ESP32-8048S043C LovyanGFX Test");

  const int bar_y = 64;
  const int bar_h = 76;
  const int bar_w = LCD_W / 8;
  const uint32_t colors[] = {
      TFT_RED, TFT_GREEN, TFT_BLUE, TFT_CYAN,
      TFT_MAGENTA, TFT_YELLOW, TFT_WHITE, TFT_BLACK};
  const char *names[] = {"Rot", "Gruen", "Blau", "Cyan", "Mag", "Gelb", "Weiss", "Schwarz"};

  for (int i = 0; i < 8; ++i) {
    lcd.fillRect(i * bar_w, bar_y, bar_w, bar_h, colors[i]);
    lcd.drawRect(i * bar_w, bar_y, bar_w, bar_h, TFT_DARKGREY);
    lcd.setTextSize(1);
    lcd.setTextColor(i == 6 ? TFT_BLACK : TFT_WHITE, colors[i]);
    lcd.setCursor(i * bar_w + 10, bar_y + 10);
    lcd.print(names[i]);
  }

  for (int x = 0; x < LCD_W; x += 40) {
    lcd.drawFastVLine(x, 160, LCD_H - 160, lcd.color888(35, 50, 65));
  }
  for (int y = 160; y < LCD_H; y += 40) {
    lcd.drawFastHLine(0, y, LCD_W, lcd.color888(35, 50, 65));
  }

  drawCornerTarget(34, 184, "oben links");
  drawCornerTarget(LCD_W - 35, 184, "oben rechts");
  drawCornerTarget(34, LCD_H - 35, "unten links");
  drawCornerTarget(LCD_W - 35, LCD_H - 35, "unten rechts");

  lcd.fillRect(170, 178, 460, 84, lcd.color888(12, 18, 24));
  lcd.drawRect(170, 178, 460, 84, lcd.color888(100, 120, 140));
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE, lcd.color888(12, 18, 24));
  lcd.setCursor(190, 196);
  lcd.print("Touch: Finger auf das Display setzen");
}

static void drawStatus(const String &devices)
{
  lcd.fillRect(0, 144, LCD_W, 28, lcd.color888(18, 24, 31));
  lcd.setTextColor(TFT_WHITE, lcd.color888(18, 24, 31));
  lcd.setTextSize(1);
  lcd.setCursor(12, 153);
  lcd.printf("LovyanGFX RGB 800x480, PCLK 14MHz");

  lcd.setCursor(330, 153);
  if (touch_addr) {
    lcd.printf("GT911: 0x%02X, I2C: %s", touch_addr, devices.c_str());
  } else {
    lcd.printf("GT911 nicht gefunden, I2C: %s", devices.c_str());
  }
}

static void drawTouchPoint(int16_t x, int16_t y)
{
  if (last_x >= 0 && last_y >= 0) {
    lcd.drawCircle(last_x, last_y, 18, lcd.color888(80, 80, 80));
  }

  lcd.drawCircle(x, y, 18, TFT_RED);
  lcd.drawFastHLine(x - 24, y, 49, TFT_RED);
  lcd.drawFastVLine(x, y - 24, 49, TFT_RED);

  lcd.fillRect(170, 280, 460, 44, TFT_BLACK);
  lcd.drawRect(170, 280, 460, 44, TFT_DARKGREY);
  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setCursor(190, 294);
  lcd.printf("Touch #%lu  x=%d  y=%d", ++touch_count, x, y);

  last_x = x;
  last_y = y;
}

static void lcdInitTask(void *param)
{
  (void)param;
  Serial.printf("lcd.init() running on core %d\n", xPortGetCoreID());
  lcd_init_ok = lcd.init();
  lcd_init_done = true;
  vTaskDelete(nullptr);
}

void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("ESP32-8048S043C LovyanGFX core0 display/touch test");
  Serial.printf("setup() running on core %d\n", xPortGetCoreID());
  backlightProbe();

  Serial.println("lcd.init() start on core 0");
  xTaskCreatePinnedToCore(lcdInitTask, "lcd_init", 8192, nullptr, 3, nullptr, 0);
  while (!lcd_init_done) {
    delay(10);
  }
  bool lcd_ok = lcd_init_ok;
  Serial.printf("lcd.init(): %s\n", lcd_ok ? "OK" : "FAIL");
  if (!lcd_ok) {
    Serial.println("LCD init failed, drawing skipped to avoid crash");
    while (true) {
      setBacklight(true);
      delay(500);
      setBacklight(false);
      delay(500);
    }
  }
  setBacklight(true);
  lcd.setRotation(0);
  lcd.setColorDepth(16);
  drawStaticScreen();

  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  gt911Reset();
  touch_addr = findGT911();

  String devices = scanI2C();
  Serial.print("I2C scan: ");
  Serial.println(devices);
  drawStatus(devices);
}

void loop()
{
  static uint32_t last_anim = 0;
  static int anim_x = 0;

  int16_t x = 0;
  int16_t y = 0;
  if (readTouch(x, y)) {
    Serial.printf("Touch #%lu: x=%d y=%d\n", touch_count + 1, x, y);
    drawTouchPoint(x, y);
  }

  if (millis() - last_anim > 40) {
    last_anim = millis();
    lcd.fillRect(170, 360, 460, 18, TFT_BLACK);
    lcd.drawRect(170, 360, 460, 18, TFT_DARKGREY);
    lcd.fillRect(171 + anim_x, 361, 28, 16, TFT_CYAN);
    anim_x += 5;
    if (anim_x > 430) {
      anim_x = 0;
    }
  }

  delay(5);
}
