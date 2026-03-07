#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_log.h>
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

#ifdef XBEE_USES_UART0
    esp_log_level_set("*", ESP_LOG_NONE);
#else
    Serial.begin(115200);
    delay(500);
#endif

    // 1. SD card FIRST — before xbeeInit() so SPI init completes
    //    before we configure UART0
    if (!initSDCard()) {
        dbgprintln("[FATAL] SD card init failed – halting.");
        while (true) delay(1000);
    }

    // 2. WiFi access point
    dbgprintf("[WiFi] Starting AP \"%s\"\n", AP_SSID);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CONN);
    delay(100);
    esp_wifi_set_ps(WIFI_PS_NONE);
    dbgprintf("[WiFi] AP running — IP: %s\n", WiFi.softAPIP().toString().c_str());

    // 3. XBee — init AFTER SD and WiFi so Serial.begin(9600) has the
    //    "last word" on UART0 configuration
    xbeeInit();

    // 4. Node client + XBee callback (immediately after xbeeInit)
    nodeClientInit();
    xbeeSetReceiveCallback([](const char* payload, size_t len) {
        if (!nodeClientHandleCommand(payload, len)) {
            dbgprintf("[XBee] Unhandled: %.80s\n", payload);
        }
    });

    // 5. DNS captive portal
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    dbgprintf("[DNS] Captive portal running\n");

    // 6. Web server
    setupWebServer(server);
    server.begin();
    dbgprintln("[Web] Server started on port 80");

    // 7. Wait for XBee network to stabilise (coordinator needs 2-4s
    //    to form network, router needs time to join)
    delay(2000);

    dbgprintln("[Node] Setup complete — starting REGISTER cycle");
}

void loop() {
    dnsServer.processNextRequest();
    xbeeProcessIncoming();
    nodeClientLoop();
    yield();
}
