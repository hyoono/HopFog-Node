#include "web_server.h"
#include "config.h"
#include "sd_storage.h"
#include "xbee_comm.h"
#include "node_client.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// Forward declarations for api_handlers.cpp
extern void registerApiHandlers(AsyncWebServer& server);

// Cached AP IP string — set once in setupWebServer(), used in onNotFound()
static String apIpStr;

void setupWebServer(AsyncWebServer& server) {
    apIpStr = WiFi.softAPIP().toString();

    // CORS headers for mobile app
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Handle CORS preflight and captive-portal redirects
    server.onNotFound([](AsyncWebServerRequest* request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
            return;
        }

        // Android / iOS / Windows captive-portal detection endpoints.
        // If the Host header is NOT our domain, redirect to it so the
        // phone opens the captive-portal browser pointing at hopfog.com.
        String host = request->host();
        if (host.length() > 0 && host != CAPTIVE_DOMAIN && host != apIpStr) {
            request->redirect(String("http://") + CAPTIVE_DOMAIN + request->url());
            return;
        }

        request->send(404, "application/json", "{\"error\":\"Not found\"}");
    });

    registerApiHandlers(server);
}
