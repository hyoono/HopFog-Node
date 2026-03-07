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
    // Disable ESP32-CAM flash LED immediately (GPIO 4 = flash LED transistor)
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, LOW);

#ifdef XBEE_USES_UART0
    // CRITICAL: suppress ALL serial log output on UART0.
    // UART0 is shared between XBee and ESP-IDF log system.
    // Any log output corrupts XBee API frames.
    esp_log_level_set("*", ESP_LOG_NONE);
#else
    Serial.begin(115200);
    delay(500);
#endif

    // 1. XBee — init FIRST, before SD/WiFi/web server
    //    This calls Serial.begin(9600) internally.
    xbeeInit();

    // 2. SD card
    if (!initSDCard()) {
        dbgprintln("[FATAL] SD card init failed – halting.");
        while (true) delay(1000);
    }

    // 3. WiFi access point
    dbgprintf("[WiFi] Starting AP \"%s\"\n", AP_SSID);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CONN);
    delay(100);
    esp_wifi_set_ps(WIFI_PS_NONE);  // Disable power-saving for stability
    dbgprintf("[WiFi] AP running — IP: %s\n", WiFi.softAPIP().toString().c_str());

    // 4. DNS captive portal — resolve every hostname to our AP IP
    //    so mobile users can type "hopfog.com" instead of 192.168.4.1
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    dbgprintf("[DNS] Captive portal running — %s → %s\n",
                  CAPTIVE_DOMAIN, WiFi.softAPIP().toString().c_str());

    // 5. Web server with mobile API endpoints
    setupWebServer(server);
    server.begin();
    dbgprintln("[Web] Server started on port 80");

    // 6. Node client + XBee callback
    nodeClientInit();
    xbeeSetReceiveCallback([](const char* payload, size_t len) {
        if (!nodeClientHandleCommand(payload, len)) {
            dbgprintf("[XBee] Unhandled: %.80s\n", payload);
        }
    });

    dbgprintln("[Node] Setup complete — starting REGISTER cycle");
}

void loop() {
    dnsServer.processNextRequest();
    xbeeProcessIncoming();
    nodeClientLoop();
    yield();
}
