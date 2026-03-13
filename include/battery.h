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
    int      percentage;   // 0-100
    BatteryStatus status;
};

/// Initialize INA219 sensor on given I2C pins.
/// Returns true if sensor found, false otherwise.
/// Note: On ESP32-CAM, GPIO 14/15 are SD card SPI pins — use 21/22 or other safe pins.
bool batteryInit(int sdaPin = 21, int sclPin = 22);

/// Read current battery state. Returns safe defaults if sensor unavailable.
BatteryInfo batteryRead();

/// Convert BatteryStatus to short string for JSON ("full","normal","low","critical","charging","unknown")
const char* batteryStatusStr(BatteryStatus s);

#endif // BATTERY_H
