#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <rom/ets_sys.h>
#include <esp_log.h>
#include <esp_wifi.h>

#include "config.h"
#include "sd_storage.h"
#include "xbee_comm.h"
#include "node_client.h"
#include "web_server.h"
#include "battery.h"
#include "led_status.h"

AsyncWebServer server(HTTP_PORT);
DNSServer      dnsServer;

static void nullPutc(char c) { (void)c; }

void setup() {
    // STEP 0: Silence ALL UART0 output — THIS IS THE KEY FIX
    // ESP-IDF components (WiFi, SPI, AsyncTCP) use ets_printf() which
    // outputs to UART0 regardless of CORE_DEBUG_LEVEL. This corrupts
    // XBee API frames. Install a no-op putc to swallow everything.
    ets_install_putc1(nullPutc);
    esp_log_level_set("*", ESP_LOG_NONE);

    // Step 1: Disable ESP32-CAM flash LED
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, LOW);

    // Step 2: LED blink delay (~1.8 seconds) — matches working test project
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, HIGH);  // LED off (active LOW)
    for (int i = 0; i < 3; i++) {
        digitalWrite(STATUS_LED_PIN, LOW);  delay(300);
        digitalWrite(STATUS_LED_PIN, HIGH); delay(300);
    }

    // Step 3: XBee init — Serial.begin(9600)
    xbeeInit();

    // Step 4: XBee receive callback
    xbeeSetReceiveCallback([](const char* payload, size_t len) {
        nodeClientHandleCommand(payload, len);
    });

    // Step 5: SD card (don't halt on failure — XBee still works)
    initSDCard();

    // Step 6: Node client
    nodeClientInit();

    // Step 6b: Flash LED status (must init before battery for GPIO 4 time-sharing)
    ledStatusInit();

    // Step 6c: Battery sensor (INA219 on GPIO 4 SDA / GPIO 0 SCL)
    // GPIO 4 is time-shared with flash LED PWM — batteryInit pauses LED
    // briefly for I2C detection. If sensor not found, runs with LED only.
    batteryInit(4, 0);

    // Step 7: WiFi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CONN);
    delay(100);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);    // Max power for range
    esp_wifi_set_ps(WIFI_PS_NONE);           // Disable power save

    // Step 8: DNS + Web server
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    setupWebServer(server);
    server.begin();

    // Step 9: Wait for XBee network
    delay(3000);
}

void loop() {
    dnsServer.processNextRequest();
    xbeeProcessIncoming();
    nodeClientLoop();

    // Update LED status every ~200ms
    static unsigned long lastLedMs = 0;
    if (millis() - lastLedMs > 200) {
        lastLedMs = millis();
        ConnectionStatus conn;
        if (nodeClientGetLastPongMs() > 0 &&
            millis() - nodeClientGetLastPongMs() < 30000) {
            conn = CONN_CONNECTED;
        } else if (nodeClientGetState() != STATE_UNREGISTERED) {
            conn = CONN_SEARCHING;
        } else {
            conn = CONN_DISCONNECTED;
        }
        BatteryInfo bat = batteryRead();
        ledStatusUpdate(conn, bat.percentage, bat.status == BAT_CHARGING);
    }

    delay(10);  // Prevent 100% CPU spin; matches working test project timing
}
