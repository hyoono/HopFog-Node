#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <Arduino.h>

// Connection status
enum ConnectionStatus {
    CONN_DISCONNECTED,   // No admin connected — LED off
    CONN_SEARCHING,      // PING sent, waiting for PONG — slow pulse (breathe)
    CONN_CONNECTED       // PONG received within 30s — solid dim glow
};

// Flash LED (GPIO 4) — built into every ESP32-CAM board.
// Driven via LEDC PWM at low duty cycle (~3%) for subtle glow.
// Time-shared with INA219 I2C SDA (see battery.h).

/// Initialize flash LED PWM on GPIO 4.
void ledStatusInit();

/// Update flash LED based on connection status and battery info.
/// Call every ~200ms from loop().
/// @param conn     Current connection status
/// @param batPct   Battery percentage (0-100, or -1 if unknown)
/// @param charging True if battery is charging
void ledStatusUpdate(ConnectionStatus conn, int batPct, bool charging);

/// Pause flash LED PWM — releases GPIO 4 for I2C time-sharing.
void ledStatusPause();

/// Resume flash LED PWM — re-attaches GPIO 4 to LEDC after I2C.
void ledStatusResume();

#endif // LED_STATUS_H
