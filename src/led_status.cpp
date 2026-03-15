#include "led_status.h"
#include "config.h"

// LEDC PWM settings for flash LED on GPIO 4
#define LED_CHANNEL  0
#define LED_FREQ     5000   // 5 kHz PWM
#define LED_RES      8      // 8-bit resolution (0-255)

static bool initialized = false;
static uint8_t currentDuty = 0;

void ledStatusInit() {
    ledcSetup(LED_CHANNEL, LED_FREQ, LED_RES);
    ledcAttachPin(FLASH_LED_PIN, LED_CHANNEL);
    ledcWrite(LED_CHANNEL, 0);  // Start off
    initialized = true;
}

void ledStatusPause() {
    if (!initialized) return;
    ledcDetachPin(FLASH_LED_PIN);
}

void ledStatusResume() {
    if (!initialized) return;
    ledcAttachPin(FLASH_LED_PIN, LED_CHANNEL);
    ledcWrite(LED_CHANNEL, currentDuty);
}

void ledStatusUpdate(ConnectionStatus conn, int batPct, bool charging) {
    if (!initialized) return;

    // Critical battery (<5%): fast blink (10Hz)
    if (batPct >= 0 && batPct < 5) {
        currentDuty = (millis() / 100) % 2 == 0 ? 8 : 0;
        ledcWrite(LED_CHANNEL, currentDuty);
        return;
    }

    // Charging: slow breathe effect
    if (charging) {
        int phase = (millis() / 20) % 256;
        int val = phase < 128 ? phase : (255 - phase);
        currentDuty = val / 32;  // 0-7 range for subtle glow
        ledcWrite(LED_CHANNEL, currentDuty);
        return;
    }

    // Low battery (5-15%): double blink pattern
    if (batPct >= 0 && batPct < 15) {
        int phase = (millis() / 100) % 10;
        currentDuty = (phase == 0 || phase == 2) ? 8 : 0;
        ledcWrite(LED_CHANNEL, currentDuty);
        return;
    }

    // Connection status
    switch (conn) {
        case CONN_DISCONNECTED:
            currentDuty = 0;  // Off
            break;
        case CONN_SEARCHING: {
            // Slow pulse (breathe)
            int phase = (millis() / 15) % 256;
            int val = phase < 128 ? phase : (255 - phase);
            currentDuty = val / 32;  // 0-7 range for subtle glow
            break;
        }
        case CONN_CONNECTED:
            currentDuty = 8;  // Solid dim glow (~3% duty)
            break;
    }
    ledcWrite(LED_CHANNEL, currentDuty);
}
