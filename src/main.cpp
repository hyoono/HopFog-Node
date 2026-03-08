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

    // 1. XBee — FIRST, before anything else (matches working test project)
    //    Do NOT call esp_log_level_set() — CORE_DEBUG_LEVEL=0 is sufficient.
    xbeeInit();

    // 2. Node client + XBee callback (immediately after xbeeInit)
    nodeClientInit();
    xbeeSetReceiveCallback([](const char* payload, size_t len) {
        if (!nodeClientHandleCommand(payload, len)) {
            dbgprintf("[XBee] Unhandled: %.80s\n", payload);
        }
    });

    // 3. SD card
    if (!initSDCard()) {
        dbgprintln("[FATAL] SD card init failed – halting.");
        while (true) delay(1000);
    }

    // 4. WiFi access point
    dbgprintf("[WiFi] Starting AP \"%s\"\n", AP_SSID);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CONN);
    delay(100);
    esp_wifi_set_ps(WIFI_PS_NONE);
    dbgprintf("[WiFi] AP running — IP: %s\n", WiFi.softAPIP().toString().c_str());

    // 5. DNS captive portal
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    // 6. Web server
    setupWebServer(server);
    server.begin();

    // 7. Wait for XBee network — matches test project delay(3000)
    delay(3000);

    dbgprintln("[Node] Setup complete — starting REGISTER cycle");
}

void loop() {
    dnsServer.processNextRequest();
    xbeeProcessIncoming();
    nodeClientLoop();
    yield();
}
