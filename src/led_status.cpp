#include "led_status.h"

void ledStatusInit() {
    if (LED_R >= 0) {
        pinMode(LED_R, OUTPUT);
        digitalWrite(LED_R, LOW);
    }
    if (LED_G >= 0) {
        pinMode(LED_G, OUTPUT);
        digitalWrite(LED_G, LOW);
    }
    if (LED_B >= 0) {
        pinMode(LED_B, OUTPUT);
        digitalWrite(LED_B, HIGH);  // Active LOW
    }
}

// Internal helper: set RGB LED color
static void setLed(bool r, bool g, bool b) {
    if (LED_R >= 0) { digitalWrite(LED_R, r ? HIGH : LOW); }
    if (LED_G >= 0) { digitalWrite(LED_G, g ? HIGH : LOW); }
    if (LED_B >= 0) { digitalWrite(LED_B, b ? LOW : HIGH); }  // Active LOW
}

void ledStatusUpdate(ConnectionStatus conn, int batPct, bool charging) {
    // Battery takes priority for critical/low
    if (batPct >= 0 && batPct < 5) {
        // Critical battery — RED quick pulse
        bool on = (millis() / 200) % 2 == 0;
        setLed(on, false, false);
        return;
    }

    if (charging) {
        // Charging — ORANGE constant (R+G, same as YELLOW with digital GPIO)
        setLed(true, true, false);
        return;
    }

    if (batPct >= 0 && batPct < 15) {
        // Low battery — YELLOW constant (R+G)
        setLed(true, true, false);
        return;
    }

    // Connection status
    switch (conn) {
        case CONN_DISCONNECTED:
            // RED constant
            setLed(true, false, false);
            break;
        case CONN_SEARCHING: {
            // YELLOW pulsing
            bool on = (millis() / 500) % 2 == 0;
            setLed(on, on, false);
            break;
        }
        case CONN_CONNECTED:
            // GREEN constant
            setLed(false, true, false);
            break;
    }
}
