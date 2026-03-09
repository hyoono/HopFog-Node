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

    // 1. XBee FIRST — Serial.begin(9600) before anything else
    xbeeInit();

    // 2. Callback
    xbeeSetReceiveCallback([](const char* payload, size_t len) {
        if (!nodeClientHandleCommand(payload, len)) {
            dbgprintf("[XBee] Unhandled: %.80s\n", payload);
        }
    });

#ifndef XBEE_USES_UART0
    Serial.begin(115200);
    delay(500);
#endif

    // 3. SD card
    if (!initSDCard()) {
        while (true) delay(1000);
    }

    // 4. Node client
    nodeClientInit();

    // 5. WiFi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CONN);
    delay(100);
    esp_wifi_set_ps(WIFI_PS_NONE);

    // 6. DNS + Web server
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    setupWebServer(server);
    server.begin();

    // 7. Wait for XBee network
    delay(3000);
}

void loop() {
    dnsServer.processNextRequest();
    xbeeProcessIncoming();
    nodeClientLoop();
    yield();
}
