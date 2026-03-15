#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

// Battery status codes
enum BatteryStatus {
    BAT_UNKNOWN,
    BAT_FULL,
    BAT_NORMAL,
    BAT_LOW,
    BAT_CRITICAL,
    BAT_CHARGING
};

struct BatteryInfo {
    float    voltage;      // Volts
    int      percentage;   // 0-100, or -1 if unknown
    BatteryStatus status;
};

/// Initialize INA219 sensor on given I2C pins.
/// Returns true if sensor found, false otherwise.
///
/// ESP32-CAM I2C pins (time-shared with flash LED):
///   SDA = GPIO 4  (flash LED pin — brief ~2ms I2C pause every 5s)
///   SCL = GPIO 0  (free after boot, pull-up helps normal boot)
///
/// DANGER — NEVER use these pins for I2C on ESP32-CAM:
///   GPIO 16/17 = PSRAM CS/CLK — will crash system!
///   GPIO 21/22 = Camera data pins — causes instability!
///   GPIO 12    = VDD_SDIO strapping pin — I2C pull-up causes 1.8V boot!
///   GPIO 2/13/14/15 = SD card (SPI mode) — will corrupt SD!
bool batteryInit(int sdaPin = 4, int sclPin = 0);

/// Read current battery state. Returns safe defaults if sensor unavailable.
/// Internally caches reads — only does I2C every 5 seconds.
/// Time-shares GPIO 4 between I2C and flash LED PWM automatically.
BatteryInfo batteryRead();

/// Convert BatteryStatus to short string for JSON ("full","normal","low","critical","charging","unknown")
const char* batteryStatusStr(BatteryStatus s);

#endif // BATTERY_H
