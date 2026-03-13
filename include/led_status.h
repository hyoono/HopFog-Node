#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <Arduino.h>

// Connection status
enum ConnectionStatus {
    CONN_DISCONNECTED,   // No admin connected — RED constant
    CONN_SEARCHING,      // PING sent, waiting for PONG — YELLOW pulsing
    CONN_CONNECTED       // PONG received within 30s — GREEN constant
};

// LED GPIO pins (external RGB LED + built-in status LED)
#define LED_R  12   // External red LED
#define LED_G  16   // External green LED
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
