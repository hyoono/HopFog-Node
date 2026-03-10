#include "web_server.h"
#include "config.h"
#include "sd_storage.h"
#include "xbee_comm.h"
#include "node_client.h"
#include <WiFi.h>
#include <ArduinoJson.h>

extern void registerApiHandlers(AsyncWebServer& server);

void setupWebServer(AsyncWebServer& server) {
    // CORS headers for mobile app
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods",
                                         "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers",
                                         "Content-Type, Authorization");

    // ── Captive portal detection (prevents login popup) ─────────────
    // Android
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(204);
    });
    server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(204);
    });

    // Apple / iOS
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html",
                      "<HTML><HEAD><TITLE>Success</TITLE></HEAD>"
                      "<BODY>Success</BODY></HTML>");
    });
    server.on("/library/test/success.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html",
                      "<HTML><HEAD><TITLE>Success</TITLE></HEAD>"
                      "<BODY>Success</BODY></HTML>");
    });

    // Windows NCSI
    server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "Microsoft NCSI");
    });
    server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "Microsoft Connect Test");
    });

    // Firefox
    server.on("/canonical.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html",
                      "<HTML><HEAD><TITLE>Success</TITLE></HEAD>"
                      "<BODY>Success</BODY></HTML>");
    });
    server.on("/success.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "success\n");
    });

    // Register API endpoints
    registerApiHandlers(server);

    // 404 — NO captive portal redirect! Just return 404.
    server.onNotFound([](AsyncWebServerRequest* request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
            return;
        }
        request->send(404, "application/json", "{\"error\":\"Not found\"}");
    });
}
