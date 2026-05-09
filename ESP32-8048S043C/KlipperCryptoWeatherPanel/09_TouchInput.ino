static bool touchI2cWriteBytes(uint8_t addr, const uint8_t* data, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

static bool touchI2cReadReg(uint8_t addr, uint16_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xff);
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

  for (size_t i = 0; i < len; i++) {
    buf[i] = Wire.read();
  }
  return true;
}

static int touchClampInt(int value, int low, int high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

static void gt911ClearStatus() {
  if (touchAddress == 0) {
    return;
  }

  const uint8_t clearStatus[] = {
    (uint8_t)(GT911_STATUS_REG >> 8),
    (uint8_t)(GT911_STATUS_REG & 0xff),
    0x00
  };
  touchI2cWriteBytes(touchAddress, clearStatus, sizeof(clearStatus));
}

static void gt911Reset() {
  pinMode(TOUCH_INT, INPUT_PULLUP);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(20);
  digitalWrite(TOUCH_RST, HIGH);
  delay(120);
}

static uint8_t findGT911() {
  const uint8_t candidates[] = {0x5d, 0x14};
  uint8_t id[4] = {};

  for (uint8_t addr : candidates) {
    if (touchI2cReadReg(addr, GT911_PRODUCT_ID_REG, id, sizeof(id))) {
      Serial.printf(
        "GT911 Touch: 0x%02X, Product ID %c%c%c%c\n",
        addr,
        id[0],
        id[1],
        id[2],
        id[3]
      );
      return addr;
    }
  }

  return 0;
}

static bool readTouchRaw(int16_t& x, int16_t& y) {
  if (touchAddress == 0) {
    return false;
  }

  uint8_t status = 0;
  if (!touchI2cReadReg(touchAddress, GT911_STATUS_REG, &status, 1)) {
    return false;
  }
  if ((status & 0x80) == 0) {
    return false;
  }

  uint8_t points = status & 0x0f;
  if (points == 0 || points > 5) {
    gt911ClearStatus();
    return false;
  }

  uint8_t data[8] = {};
  bool ok = touchI2cReadReg(touchAddress, GT911_POINT_REG, data, sizeof(data));
  gt911ClearStatus();
  if (!ok) {
    return false;
  }

  x = data[1] | (data[2] << 8);
  y = data[3] | (data[4] << 8);
  return true;
}

static void applyTouchCalibration(int16_t rawX, int16_t rawY, int16_t& x, int16_t& y) {
  float mappedX = (TOUCH_CAL_X_RX * rawX) + (TOUCH_CAL_X_RY * rawY) + TOUCH_CAL_X_C;
  float mappedY = (TOUCH_CAL_Y_RX * rawX) + (TOUCH_CAL_Y_RY * rawY) + TOUCH_CAL_Y_C;
  int mappedXi = touchClampInt((int)(mappedX + 0.5f), 0, LCD_W - 1);
  int mappedYi = touchClampInt((int)(mappedY + 0.5f), 0, LCD_H - 1);

  if (LCD_ROTATE_180) {
    mappedXi = (LCD_W - 1) - mappedXi;
    mappedYi = (LCD_H - 1) - mappedYi;
  }

  x = mappedXi;
  y = mappedYi;
}

static bool readTouchPoint(int16_t& x, int16_t& y) {
  int16_t rawX = 0;
  int16_t rawY = 0;
  if (!readTouchRaw(rawX, rawY)) {
    return false;
  }

  applyTouchCalibration(rawX, rawY, x, y);
  return true;
}

bool initTouchInput() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  gt911Reset();
  touchAddress = findGT911();
  if (touchAddress == 0) {
    Serial.println("GT911 Touch: nicht gefunden");
    return false;
  }

  gt911ClearStatus();
  Serial.println("GT911 Touch: bereit");
  return true;
}

void onTouchLongPress(int16_t x, int16_t y) {
  Serial.printf("Touch Long-Press: x=%d y=%d\n", x, y);
  openPopupMenu();
}

static void finishTouchPress() {
  if (isPopupMenuOpen()) {
    if (!touchLongPressHandled) {
      handlePopupMenuTouch(touchLastX, touchLastY);
    }
    return;
  }

  if (touchLongPressHandled) {
    return;
  }

  ScreenState target = touchStartX < (LCD_W / 2) ?
    previousScreenState(currentScreen) :
    nextScreenState(currentScreen);
  switchScreenFromTouch(target);
}

void handleTouchInput() {
  if (touchAddress == 0) {
    return;
  }

  unsigned long now = millis();
  if (now - lastTouchPoll < touchPollInterval) {
    return;
  }
  lastTouchPoll = now;

  int16_t x = 0;
  int16_t y = 0;
  if (readTouchPoint(x, y)) {
    if (!touchPressed) {
      touchPressed = true;
      touchLongPressHandled = false;
      touchStartX = x;
      touchPressedAt = now;
    }

    touchLastX = x;
    touchLastY = y;
    touchLastSeenAt = now;

    if (!touchLongPressHandled && now - touchPressedAt >= touchLongPressInterval) {
      touchLongPressHandled = true;
      lastScreenSwitch = now;
      onTouchLongPress(touchLastX, touchLastY);
    }
    return;
  }

  if (touchPressed && now - touchLastSeenAt >= touchReleaseInterval) {
    finishTouchPress();
    touchPressed = false;
    touchLongPressHandled = false;
  }
}
