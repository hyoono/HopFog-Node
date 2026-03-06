#include "web_server.h"
#include "config.h"
#include "sd_storage.h"
#include "xbee_comm.h"
#include "node_client.h"
#include <ArduinoJson.h>

// Forward declarations for api_handlers.cpp
extern void registerApiHandlers(AsyncWebServer& server);

void setupWebServer(AsyncWebServer& server) {
    // CORS headers for mobile app
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Handle CORS preflight
    server.onNotFound([](AsyncWebServerRequest* request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404, "application/json", "{\"error\":\"Not found\"}");
        }
    });

    registerApiHandlers(server);
}
