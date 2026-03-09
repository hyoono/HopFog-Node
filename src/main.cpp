#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <rom/ets_sys.h>
#include <esp_log.h>

#include "config.h"
#include "sd_storage.h"
#include "xbee_comm.h"
#include "node_client.h"
#include "web_server.h"

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

    // Step 7: WiFi AP (simple — no setTxPower, no esp_wifi_set_ps)
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CONN);
    delay(100);

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
    delay(10);  // Prevent 100% CPU spin; matches working test project timing
}
