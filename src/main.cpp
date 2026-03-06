#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "sd_storage.h"
#include "xbee_comm.h"
#include "node_client.h"
#include "web_server.h"

AsyncWebServer server(HTTP_PORT);

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n========================================");
    Serial.println("   HopFog-Node  ESP32 Firmware");
    Serial.println("========================================");

    // 1. SD card
    if (!initSDCard()) {
        Serial.println("[FATAL] SD card init failed – halting.");
        while (true) delay(1000);
    }

    // 2. WiFi access point
    Serial.printf("[WiFi] Starting AP \"%s\"\n", AP_SSID);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CONN);
    delay(100);
    esp_wifi_set_ps(WIFI_PS_NONE);  // Disable power-saving for stability
    Serial.printf("[WiFi] AP running — IP: %s\n", WiFi.softAPIP().toString().c_str());

    // 3. Web server with mobile API endpoints
    setupWebServer(server);
    server.begin();
    Serial.println("[Web] Server started on port 80");

    // 4. XBee + node protocol
    xbeeInit();
    nodeClientInit();
    xbeeSetReceiveCallback([](const char* payload, size_t len) {
        if (!nodeClientHandleCommand(payload, len)) {
            Serial.printf("[XBee] Unhandled: %.80s\n", payload);
        }
    });

    Serial.println("[Node] Setup complete — starting REGISTER cycle");
}

void loop() {
    xbeeProcessIncoming();
    nodeClientLoop();
    yield();
}
