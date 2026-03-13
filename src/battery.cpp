#include "battery.h"
#include <Wire.h>
#include <Adafruit_INA219.h>

static Adafruit_INA219 ina219;
static bool sensorAvailable = false;

bool batteryInit(int sdaPin, int sclPin) {
    Wire.begin(sdaPin, sclPin);
    sensorAvailable = ina219.begin();
    return sensorAvailable;
}

BatteryInfo batteryRead() {
    BatteryInfo info;
    info.voltage = 0.0f;
    info.percentage = 0;
    info.status = BAT_UNKNOWN;

    if (!sensorAvailable) return info;

    float busVoltage = ina219.getBusVoltage_V();
    float current_mA = ina219.getCurrent_mA();

    info.voltage = busVoltage;

    // Estimate percentage from voltage (3.0V = 0%, 4.2V = 100%)
    float pct = (busVoltage - 3.0f) / (4.2f - 3.0f) * 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    info.percentage = (int)pct;

    // Determine status
    if (current_mA < -50.0f) {
        info.status = BAT_CHARGING;
    } else if (info.percentage >= 95) {
        info.status = BAT_FULL;
    } else if (info.percentage >= 15) {
        info.status = BAT_NORMAL;
    } else if (info.percentage >= 5) {
        info.status = BAT_LOW;
    } else {
        info.status = BAT_CRITICAL;
    }

    return info;
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
