#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <Arduino.h>

// Connection status
enum ConnectionStatus {
    CONN_DISCONNECTED,   // No admin connected — RED constant
    CONN_SEARCHING,      // PING sent, waiting for PONG — YELLOW pulsing
    CONN_CONNECTED       // PONG received within 30s — GREEN constant
};

// LED GPIO pins
// On ESP32-CAM: GPIO 12 is a strapping pin (boot failure risk),
// GPIO 16 is PSRAM (pinMode crashes system). Only GPIO 33 is safe.
#define LED_R  -1   // Not available on ESP32-CAM (GPIO 12 = strapping pin)
#define LED_G  -1   // Not available on ESP32-CAM (GPIO 16 = PSRAM)
#define LED_B  33   // Built-in status LED (active LOW)

/// Initialize LED pins
void ledStatusInit();

/// Update LED based on connection status and battery info.
/// Call every ~200ms from loop().
/// @param conn     Current connection status
/// @param batPct   Battery percentage (0-100)
/// @param charging True if battery is charging
void ledStatusUpdate(ConnectionStatus conn, int batPct, bool charging);

#endif // LED_STATUS_H
