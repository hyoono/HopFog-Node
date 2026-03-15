#include "battery.h"
#include "led_status.h"
#include <Wire.h>
#include <Adafruit_INA219.h>

static Adafruit_INA219 ina219;
static bool sensorAvailable = false;
static int i2cSDA = 4;
static int i2cSCL = 0;

// Internal cache — only do I2C reads every 5 seconds
static unsigned long lastReadMs = 0;
static BatteryInfo cachedInfo = {0.0f, -1, BAT_UNKNOWN};
#define BATTERY_READ_INTERVAL_MS 5000

bool batteryInit(int sdaPin, int sclPin) {
    i2cSDA = sdaPin;
    i2cSCL = sclPin;

    // Pause flash LED PWM so GPIO 4 can be used for I2C
    ledStatusPause();
    Wire.begin(i2cSDA, i2cSCL);
    sensorAvailable = ina219.begin();
    Wire.end();
    ledStatusResume();

    return sensorAvailable;
}

BatteryInfo batteryRead() {
    if (!sensorAvailable) return cachedInfo;

    // Rate-limit I2C reads to every 5 seconds (~2ms I2C pause)
    unsigned long now = millis();
    if (lastReadMs > 0 && (now - lastReadMs) < BATTERY_READ_INTERVAL_MS) {
        return cachedInfo;
    }
    lastReadMs = now;

    // Pause flash LED PWM, use GPIO 4 for I2C
    ledStatusPause();
    Wire.begin(i2cSDA, i2cSCL);

    float busVoltage = ina219.getBusVoltage_V();
    float current_mA = ina219.getCurrent_mA();

    Wire.end();
    ledStatusResume();

    cachedInfo.voltage = busVoltage;

    // Estimate percentage from voltage (3.0V = 0%, 4.2V = 100%)
    float pct = (busVoltage - 3.0f) / (4.2f - 3.0f) * 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    cachedInfo.percentage = (int)pct;

    // Determine status
    if (current_mA < -50.0f) {
        cachedInfo.status = BAT_CHARGING;
    } else if (cachedInfo.percentage >= 95) {
        cachedInfo.status = BAT_FULL;
    } else if (cachedInfo.percentage >= 15) {
        cachedInfo.status = BAT_NORMAL;
    } else if (cachedInfo.percentage >= 5) {
        cachedInfo.status = BAT_LOW;
    } else {
        cachedInfo.status = BAT_CRITICAL;
    }

    return cachedInfo;
}

const char* batteryStatusStr(BatteryStatus s) {
    switch (s) {
        case BAT_FULL:     return "full";
        case BAT_NORMAL:   return "normal";
        case BAT_LOW:      return "low";
        case BAT_CRITICAL: return "critical";
        case BAT_CHARGING: return "charging";
        default:           return "unknown";
    }
}
