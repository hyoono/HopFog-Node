#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "sd_storage.h"
#include "xbee_comm.h"
#include "node_client.h"
#include "web_server.h"

AsyncWebServer server(HTTP_PORT);
DNSServer      dnsServer;

void setup() {
    // Disable ESP32-CAM flash LED
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, LOW);

#ifndef XBEE_USES_UART0
    Serial.begin(115200);
    delay(500);
#endif

    // 1. SD card FIRST (while UART0 is still at bootloader 115200 baud)
    if (!initSDCard()) {
        while (true) delay(1000);
    }

    // 2. Wait for bootloader UART0 output (115200 baud) and XBee module
    //    boot-up to finish. Matches test project's ~1.8s blink delay.
    delay(2000);

    // 3. XBee — Serial.begin(9600) AFTER all SPI is done
    xbeeInit();

    // 4. AT command probe
    xbeeQueryConfig();

    // 5. Node client + callback
    nodeClientInit();
    xbeeSetReceiveCallback([](const char* payload, size_t len) {
        if (!nodeClientHandleCommand(payload, len)) {
            dbgprintf("[XBee] Unhandled: %.80s\n", payload);
        }
    });

    // 6. WiFi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CONN);
    delay(100);
    esp_wifi_set_ps(WIFI_PS_NONE);

    // 7. DNS + Web server
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    setupWebServer(server);
    server.begin();

    // 8. Wait for XBee network
    delay(3000);
}

void loop() {
    dnsServer.processNextRequest();
    xbeeProcessIncoming();
    nodeClientLoop();
    yield();
}
