/*
 * HopFog Node - ESP32-CAM Range Extender
 *
 * Headless node that connects to the HopFog admin (HopFog-Web) via XBee.
 * Replicates the admin API endpoints locally so nearby clients can use
 * the same REST calls without reaching the admin directly.
 *
 * Features:
 *  - XBee serial link to admin (Serial2 on GPIO 32/33)
 *  - WiFi REST API (no web UI, no login page)
 *  - SD card persistent storage (JSON, same schema as admin)
 *  - Auto-registration with admin on startup
 *  - Periodic heartbeat / keep-alive
 *  - Message relay between local clients and admin
 *
 * Hardware: ESP32-CAM (AI-Thinker) + XBee module
 */

#include <WiFi.h>
#include <WebServer.h>
#include <SD_MMC.h>
#include <ArduinoJson.h>

// ========================================
// Try to load user config; fall back to defaults
// Create esp32_node/config.h from config.h.example
// ========================================
#if __has_include("config.h")
#include "config.h"
#endif

// Defaults (overridden by config.h when present)
#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif
#ifndef NODE_ID
#define NODE_ID "node-01"
#endif
#ifndef XBEE_BAUD
#define XBEE_BAUD 9600
#endif
#ifndef XBEE_RX_PIN
#define XBEE_RX_PIN 32
#endif
#ifndef XBEE_TX_PIN
#define XBEE_TX_PIN 33
#endif
#ifndef WEB_SERVER_PORT
#define WEB_SERVER_PORT 80
#endif
#ifndef HEARTBEAT_INTERVAL_MS
#define HEARTBEAT_INTERVAL_MS 30000
#endif
#ifndef SYNC_INTERVAL_MS
#define SYNC_INTERVAL_MS 60000
#endif

// ========================================
// Pin / hardware constants
// ========================================
#define STATUS_LED 33          // built-in red LED (shared with XBEE_TX)

// ========================================
// Globals
// ========================================
WebServer server(WEB_SERVER_PORT);

bool sdCardAvailable = false;

// Statistics
int fogNodesCount   = 0;
int activeFogNodes  = 0;
int totalMessages   = 0;

// Database file paths on SD card (same layout as admin)
const char* FOG_NODES_FILE = "/hopfog/fog_nodes.json";
const char* MESSAGES_FILE  = "/hopfog/messages.json";
const char* STATS_FILE     = "/hopfog/stats.json";

// Timing
unsigned long lastHeartbeat = 0;
unsigned long lastSync      = 0;

// XBee receive buffer
String xbeeBuffer = "";

// ========================================
// SD Card helpers
// ========================================
bool initSDCard() {
    if (!SD_MMC.begin("/sdcard", true)) {   // 1-bit mode
        Serial.println("[SD] Mount failed");
        return false;
    }
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("[SD] No card inserted");
        return false;
    }
    Serial.printf("[SD] Card type: %s, Size: %llu MB\n",
                  (cardType == CARD_MMC ? "MMC" :
                   cardType == CARD_SD  ? "SD"  :
                   cardType == CARD_SDHC ? "SDHC" : "UNKNOWN"),
                  SD_MMC.cardSize() / (1024 * 1024));
    return true;
}

void ensureDirectory(const char* path) {
    if (!SD_MMC.exists(path)) {
        SD_MMC.mkdir(path);
    }
}

String readFile(const char* path) {
    if (!sdCardAvailable) return "[]";
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return "[]";
    String content = f.readString();
    f.close();
    return content.length() > 0 ? content : "[]";
}

bool writeFile(const char* path, const String& data) {
    if (!sdCardAvailable) return false;
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) return false;
    f.print(data);
    f.close();
    return true;
}

void initDatabase() {
    ensureDirectory("/hopfog");
    if (!SD_MMC.exists(FOG_NODES_FILE)) writeFile(FOG_NODES_FILE, "[]");
    if (!SD_MMC.exists(MESSAGES_FILE))  writeFile(MESSAGES_FILE,  "[]");
    if (!SD_MMC.exists(STATS_FILE))     writeFile(STATS_FILE,     "{}");
    updateStats();
    Serial.println("[DB] Database initialised on SD card");
}

// ========================================
// Data access – Fog Nodes
// ========================================
String getFogNodes() {
    return readFile(FOG_NODES_FILE);
}

bool addFogNode(const String& name, const String& ip, const String& status) {
    String raw = readFile(FOG_NODES_FILE);
    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, raw);
    JsonArray arr = (err || !doc.is<JsonArray>()) ? doc.to<JsonArray>() : doc.as<JsonArray>();

    JsonObject node = arr.createNestedObject();
    node["id"]          = arr.size();
    node["device_name"] = name;
    node["ip_address"]  = ip;
    node["status"]      = status;
    node["added_at"]    = millis() / 1000;

    String out;
    serializeJson(doc, out);
    bool ok = writeFile(FOG_NODES_FILE, out);
    if (ok) updateStats();
    return ok;
}

// ========================================
// Data access – Messages
// ========================================
String getMessages() {
    return readFile(MESSAGES_FILE);
}

bool addMessage(const String& from, const String& to, const String& message) {
    String raw = readFile(MESSAGES_FILE);
    DynamicJsonDocument doc(16384);
    DeserializationError err = deserializeJson(doc, raw);
    JsonArray arr = (err || !doc.is<JsonArray>()) ? doc.to<JsonArray>() : doc.as<JsonArray>();

    JsonObject msg = arr.createNestedObject();
    msg["id"]        = arr.size();
    msg["from"]      = from;
    msg["to"]        = to;
    msg["message"]   = message;
    msg["timestamp"] = millis() / 1000;
    msg["node"]      = NODE_ID;

    String out;
    serializeJson(doc, out);
    bool ok = writeFile(MESSAGES_FILE, out);
    if (ok) updateStats();
    return ok;
}

// ========================================
// Statistics
// ========================================
void updateStats() {
    // Count fog nodes
    {
        String raw = readFile(FOG_NODES_FILE);
        DynamicJsonDocument doc(8192);
        if (!deserializeJson(doc, raw) && doc.is<JsonArray>()) {
            fogNodesCount  = doc.as<JsonArray>().size();
            activeFogNodes = 0;
            for (JsonObject n : doc.as<JsonArray>()) {
                if (n["status"].as<String>() == "active") activeFogNodes++;
            }
        }
    }
    // Count messages
    {
        String raw = readFile(MESSAGES_FILE);
        DynamicJsonDocument doc(16384);
        if (!deserializeJson(doc, raw) && doc.is<JsonArray>()) {
            totalMessages = doc.as<JsonArray>().size();
        }
    }
}

void saveStats() {
    if (!sdCardAvailable) return;
    StaticJsonDocument<256> doc;
    doc["fog_nodes_count"]  = fogNodesCount;
    doc["active_fog_nodes"] = activeFogNodes;
    doc["total_messages"]   = totalMessages;
    String out;
    serializeJson(doc, out);
    writeFile(STATS_FILE, out);
}

// ========================================
// XBee helpers
// ========================================

// Send a JSON command to admin over XBee
void xbeeSend(const String& json) {
    Serial2.println(json);
    Serial.printf("[XBEE-TX] %s\n", json.c_str());
}

// Build a request envelope
void xbeeSendCommand(const char* cmd, JsonObject* params = nullptr) {
    StaticJsonDocument<512> doc;
    doc["cmd"]     = cmd;
    doc["node_id"] = NODE_ID;
    doc["ts"]      = millis() / 1000;
    if (params) {
        doc["params"] = *params;
    }
    String out;
    serializeJson(doc, out);
    xbeeSend(out);
}

// Register this node with the admin
void xbeeRegister() {
    StaticJsonDocument<256> params;
    JsonObject p = params.to<JsonObject>();
    p["ip_address"]    = WiFi.localIP().toString();
    p["device_name"]   = NODE_ID;
    p["status"]        = "active";
    p["free_heap"]     = ESP.getFreeHeap();
    p["sd_available"]  = sdCardAvailable;
    xbeeSendCommand("REGISTER", &p);
}

// Send periodic heartbeat to admin
void xbeeHeartbeat() {
    StaticJsonDocument<256> params;
    JsonObject p = params.to<JsonObject>();
    p["ip_address"]   = WiFi.localIP().toString();
    p["uptime"]       = millis() / 1000;
    p["free_heap"]    = ESP.getFreeHeap();
    p["fog_nodes"]    = fogNodesCount;
    p["messages"]     = totalMessages;
    p["sd_available"] = sdCardAvailable;
    xbeeSendCommand("HEARTBEAT", &p);
}

// Request a full data sync from admin
void xbeeRequestSync() {
    xbeeSendCommand("SYNC_REQUEST");
}

// Relay a message to admin (originated from a local client)
void xbeeRelayMessage(const String& from, const String& to, const String& message) {
    StaticJsonDocument<512> params;
    JsonObject p = params.to<JsonObject>();
    p["from"]    = from;
    p["to"]      = to;
    p["message"] = message;
    xbeeSendCommand("RELAY_MSG", &p);
}

// Relay a fog-node registration to admin
void xbeeRelayFogNode(const String& name, const String& ip, const String& status) {
    StaticJsonDocument<512> params;
    JsonObject p = params.to<JsonObject>();
    p["device_name"] = name;
    p["ip_address"]  = ip;
    p["status"]      = status;
    xbeeSendCommand("RELAY_FOG_NODE", &p);
}

// ========================================
// XBee incoming handler
// ========================================
void handleXBeeData(const String& line) {
    Serial.printf("[XBEE-RX] %s\n", line.c_str());

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        Serial.printf("[XBEE] JSON parse error: %s\n", err.c_str());
        return;
    }

    const char* cmd = doc["cmd"];
    if (!cmd) return;

    String command(cmd);

    if (command == "PONG") {
        Serial.println("[XBEE] Admin responded to heartbeat");
    }
    else if (command == "REGISTER_ACK") {
        Serial.println("[XBEE] Registered with admin successfully");
    }
    else if (command == "SYNC_DATA") {
        // Admin sends a full data dump
        if (doc.containsKey("fog_nodes")) {
            String nodesStr;
            serializeJson(doc["fog_nodes"], nodesStr);
            writeFile(FOG_NODES_FILE, nodesStr);
        }
        if (doc.containsKey("messages")) {
            String msgsStr;
            serializeJson(doc["messages"], msgsStr);
            writeFile(MESSAGES_FILE, msgsStr);
        }
        updateStats();
        saveStats();
        Serial.println("[XBEE] Data sync complete");
    }
    else if (command == "BROADCAST_MSG") {
        // Admin broadcasts a message to all nodes
        const char* from    = doc["params"]["from"];
        const char* to      = doc["params"]["to"];
        const char* message = doc["params"]["message"];
        if (from && to && message) {
            addMessage(from, to, message);
            Serial.println("[XBEE] Broadcast message stored locally");
        }
    }
    else if (command == "ADD_FOG_NODE") {
        // Admin pushes a new fog node record
        const char* name   = doc["params"]["device_name"];
        const char* ip     = doc["params"]["ip_address"];
        const char* status = doc["params"]["status"];
        if (name && ip) {
            addFogNode(name, ip, status ? status : "active");
            Serial.println("[XBEE] Fog node added from admin");
        }
    }
    else if (command == "GET_STATS") {
        // Admin requests this node's stats
        StaticJsonDocument<512> resp;
        resp["cmd"]              = "STATS_RESPONSE";
        resp["node_id"]          = NODE_ID;
        resp["fog_nodes_count"]  = fogNodesCount;
        resp["active_fog_nodes"] = activeFogNodes;
        resp["total_messages"]   = totalMessages;
        resp["free_heap"]        = ESP.getFreeHeap();
        resp["uptime"]           = millis() / 1000;
        resp["ip_address"]       = WiFi.localIP().toString();
        resp["sd_available"]     = sdCardAvailable;
        String out;
        serializeJson(resp, out);
        xbeeSend(out);
    }
    else {
        Serial.printf("[XBEE] Unknown command: %s\n", cmd);
    }
}

// ========================================
// HTTP API handlers (headless – no HTML)
// ========================================

void handleHealth() {
    StaticJsonDocument<256> doc;
    doc["status"]  = "ok";
    doc["node_id"] = NODE_ID;
    doc["uptime"]  = millis() / 1000;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

void handleStats() {
    StaticJsonDocument<512> doc;
    doc["node_id"]          = NODE_ID;
    doc["fog_nodes_count"]  = fogNodesCount;
    doc["active_fog_nodes"] = activeFogNodes;
    doc["total_messages"]   = totalMessages;
    doc["ip_address"]       = WiFi.localIP().toString();
    doc["free_heap"]        = ESP.getFreeHeap();
    doc["uptime"]           = millis() / 1000;
    doc["sd_card_available"] = sdCardAvailable;
    if (sdCardAvailable) {
        doc["sd_card_size_mb"] = (uint32_t)(SD_MMC.cardSize() / (1024 * 1024));
        doc["sd_card_used_mb"] = (uint32_t)(SD_MMC.usedBytes() / (1024 * 1024));
    }
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

void handleGetFogNodes() {
    server.send(200, "application/json", getFogNodes());
}

void handleAddFogNode() {
    if (server.method() != HTTP_POST) {
        server.send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
        return;
    }
    String name   = server.arg("device_name");
    String ip     = server.arg("ip_address");
    String status = server.arg("status");
    if (name.isEmpty() || ip.isEmpty()) {
        server.send(400, "application/json",
                    "{\"success\":false,\"message\":\"Missing device_name or ip_address\"}");
        return;
    }
    String st = status.length() > 0 ? status : "active";
    if (addFogNode(name, ip, st)) {
        // Also relay to admin
        xbeeRelayFogNode(name, ip, st);
        server.send(200, "application/json",
                    "{\"success\":true,\"message\":\"Fog node added and relayed to admin\"}");
    } else {
        server.send(500, "application/json",
                    "{\"success\":false,\"message\":\"Failed to add fog node\"}");
    }
}

void handleGetMessages() {
    server.send(200, "application/json", getMessages());
}

void handleAddMessage() {
    if (server.method() != HTTP_POST) {
        server.send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
        return;
    }
    String from    = server.arg("from");
    String to      = server.arg("to");
    String message = server.arg("message");
    if (from.isEmpty() || to.isEmpty() || message.isEmpty()) {
        server.send(400, "application/json",
                    "{\"success\":false,\"message\":\"Missing from, to, or message\"}");
        return;
    }
    if (addMessage(from, to, message)) {
        // Also relay to admin
        xbeeRelayMessage(from, to, message);
        server.send(200, "application/json",
                    "{\"success\":true,\"message\":\"Message stored and relayed to admin\"}");
    } else {
        server.send(500, "application/json",
                    "{\"success\":false,\"message\":\"Failed to store message\"}");
    }
}

void handleRelay() {
    // Explicit relay endpoint – forwards arbitrary JSON to admin via XBee
    if (server.method() != HTTP_POST) {
        server.send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
        return;
    }
    String body = server.arg("plain");
    if (body.isEmpty()) {
        server.send(400, "application/json",
                    "{\"success\":false,\"message\":\"Empty body\"}");
        return;
    }
    xbeeSend(body);
    server.send(200, "application/json",
                "{\"success\":true,\"message\":\"Relayed to admin via XBee\"}");
}

void handleNotFound() {
    server.send(404, "application/json", "{\"error\":\"Not Found\"}");
}

// ========================================
// Setup
// ========================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n\nHopFog Node - Range Extender");
    Serial.println("============================");

    // XBee on Serial2
    Serial2.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);
    Serial.printf("[XBEE] UART2 started at %d baud (RX=%d, TX=%d)\n",
                  XBEE_BAUD, XBEE_RX_PIN, XBEE_TX_PIN);

    // SD card
    sdCardAvailable = initSDCard();
    if (sdCardAvailable) {
        initDatabase();
    } else {
        Serial.println("[SD] Running without persistent storage");
    }

    // WiFi
    Serial.printf("[WIFI] Connecting to %s ", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WIFI] Connected – IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WIFI] Connection failed – will keep trying in background");
    }

    // API routes (headless – JSON only)
    server.on("/api/health",        HTTP_GET,  handleHealth);
    server.on("/api/stats",         HTTP_GET,  handleStats);
    server.on("/api/fognodes",      HTTP_GET,  handleGetFogNodes);
    server.on("/api/fognodes/add",  HTTP_POST, handleAddFogNode);
    server.on("/api/messages",      HTTP_GET,  handleGetMessages);
    server.on("/api/messages/add",  HTTP_POST, handleAddMessage);
    server.on("/api/relay",         HTTP_POST, handleRelay);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("[HTTP] API server started");

    // Register with admin via XBee
    if (WiFi.status() == WL_CONNECTED) {
        xbeeRegister();
    }

    Serial.println("============================\n");
}

// ========================================
// Loop
// ========================================
void loop() {
    server.handleClient();

    // ---- XBee receive ----
    while (Serial2.available()) {
        char c = Serial2.read();
        if (c == '\n') {
            xbeeBuffer.trim();
            if (xbeeBuffer.length() > 0) {
                handleXBeeData(xbeeBuffer);
            }
            xbeeBuffer = "";
        } else {
            xbeeBuffer += c;
        }
    }

    unsigned long now = millis();

    // ---- Heartbeat ----
    if (now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeat = now;
        if (WiFi.status() == WL_CONNECTED) {
            xbeeHeartbeat();
        } else {
            // Try reconnecting
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
    }

    // ---- Periodic sync ----
    if (now - lastSync >= SYNC_INTERVAL_MS) {
        lastSync = now;
        xbeeRequestSync();
    }
}
