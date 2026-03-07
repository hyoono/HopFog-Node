#include "node_client.h"
#include "config.h"
#include "xbee_comm.h"
#include "sd_storage.h"
#include <WiFi.h>

static NodeState state = STATE_UNREGISTERED;
static unsigned long lastRegisterMs  = 0;
static unsigned long lastHeartbeatMs = 0;
static unsigned long lastSyncMs      = 0;

// ── Helper: send a JSON command via XBee ────────────────────────────
static void sendCommand(JsonDocument& doc) {
    doc["node_id"] = NODE_ID;
    doc["ts"] = (long)(millis() / 1000);
    String json;
    serializeJson(doc, json);
    xbeeSendBroadcast(json.c_str(), json.length());
}

// ── Outgoing commands ───────────────────────────────────────────────

static void sendRegister() {
    JsonDocument doc;
    doc["cmd"] = "REGISTER";
    JsonObject params = doc["params"].to<JsonObject>();
    params["device_name"] = DEVICE_NAME;
    params["ip_address"] = WiFi.softAPIP().toString();
    params["status"] = "active";
    params["free_heap"] = (int)ESP.getFreeHeap();
    sendCommand(doc);
    dbgprintln("[Node] Sent REGISTER");
}

static void sendHeartbeat() {
    JsonDocument doc;
    doc["cmd"] = "HEARTBEAT";
    JsonObject params = doc["params"].to<JsonObject>();
    params["ip_address"] = WiFi.softAPIP().toString();
    params["uptime"] = (int)(millis() / 1000);
    params["free_heap"] = (int)ESP.getFreeHeap();
    sendCommand(doc);
}

static void sendSyncRequest() {
    JsonDocument doc;
    doc["cmd"] = "SYNC_REQUEST";
    sendCommand(doc);
    dbgprintln("[Node] Sent SYNC_REQUEST");
}

// ── Incoming command handlers ───────────────────────────────────────

static void handleRegisterAck() {
    dbgprintln("[Node] Got REGISTER_ACK — registered with admin!");
    state = STATE_REGISTERED;
    // Immediately request data sync
    sendSyncRequest();
    lastSyncMs = millis();
    state = STATE_SYNCING;
}

static void handlePong() {
    dbgprintln("[Node] Got PONG");
}

static void handleSyncData(JsonDocument& doc) {
    dbgprintln("[Node] Got SYNC_DATA — saving to SD card...");

    // Save each data category to its own file
    if (doc["users"].is<JsonArray>()) {
        JsonDocument usersDoc;
        usersDoc.set(doc["users"]);
        writeJsonFile(SD_USERS_FILE, usersDoc);
        dbgprintf("[Node] Saved %d users\n", doc["users"].as<JsonArray>().size());
    }

    if (doc["announcements"].is<JsonArray>()) {
        JsonDocument annDoc;
        annDoc.set(doc["announcements"]);
        writeJsonFile(SD_ANNOUNCE_FILE, annDoc);
        dbgprintf("[Node] Saved %d announcements\n", doc["announcements"].as<JsonArray>().size());
    }

    if (doc["conversations"].is<JsonArray>()) {
        JsonDocument convDoc;
        convDoc.set(doc["conversations"]);
        writeJsonFile(SD_CONVOS_FILE, convDoc);
    }

    if (doc["chat_messages"].is<JsonArray>()) {
        JsonDocument dmDoc;
        dmDoc.set(doc["chat_messages"]);
        writeJsonFile(SD_DMS_FILE, dmDoc);
    }

    if (doc["fog_nodes"].is<JsonArray>()) {
        JsonDocument fogDoc;
        fogDoc.set(doc["fog_nodes"]);
        writeJsonFile(SD_FOG_FILE, fogDoc);
    }

    state = STATE_RUNNING;
    lastHeartbeatMs = millis();
    dbgprintln("[Node] Sync complete — now in RUNNING state");
}

static void handleBroadcastMsg(JsonObject params) {
    // Admin sent a broadcast announcement — store it locally
    dbgprintf("[Node] Broadcast: %s\n", (const char*)(params["message"] | ""));

    JsonDocument doc;
    readJsonFile(SD_ANNOUNCE_FILE, doc);
    JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

    int newId = 1;
    for (JsonObject a : arr) {
        int id = a["id"] | 0;
        if (id >= newId) newId = id + 1;
    }

    JsonObject ann = arr.add<JsonObject>();
    ann["id"] = newId;
    ann["title"] = params["subject"] | params["message"] | "";
    ann["message"] = params["message"] | "";
    ann["created_at"] = String((long)(millis() / 1000));

    writeJsonFile(SD_ANNOUNCE_FILE, doc);
}

static void handleGetStats() {
    dbgprintln("[Node] Admin requested stats");
    JsonDocument doc;
    doc["cmd"] = "STATS_RESPONSE";
    JsonObject params = doc["params"].to<JsonObject>();
    params["free_heap"] = (int)ESP.getFreeHeap();
    params["uptime"] = (int)(millis() / 1000);
    params["ip_address"] = WiFi.softAPIP().toString();
    params["wifi_stations"] = WiFi.softAPgetStationNum();
    sendCommand(doc);
}

// ── Public API ──────────────────────────────────────────────────────

void nodeClientInit() {
    state = STATE_UNREGISTERED;
    lastRegisterMs = 0;
    lastHeartbeatMs = 0;
    lastSyncMs = 0;
    dbgprintln("[Node] Client initialized — will start REGISTER cycle");
}

void nodeClientLoop() {
    unsigned long now = millis();

    switch (state) {
    case STATE_UNREGISTERED:
        // Send REGISTER every 10 seconds until we get ACK
        if (now - lastRegisterMs >= REGISTER_INTERVAL_MS) {
            sendRegister();
            lastRegisterMs = now;
        }
        break;

    case STATE_REGISTERED:
        // Just registered — waiting to start sync (handled in handleRegisterAck)
        break;

    case STATE_SYNCING:
        // Waiting for SYNC_DATA — retry if no response
        if (now - lastSyncMs >= SYNC_RETRY_MS) {
            sendSyncRequest();
            lastSyncMs = now;
        }
        break;

    case STATE_RUNNING:
        // Send HEARTBEAT every 30 seconds
        if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
            sendHeartbeat();
            lastHeartbeatMs = now;
        }
        break;
    }
}

bool nodeClientHandleCommand(const char* payload, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) return false;

    const char* cmd = doc["cmd"];
    if (!cmd) return false;

    dbgprintf("[Node] RX cmd: %s\n", cmd);

    if (strcmp(cmd, "REGISTER_ACK") == 0) {
        handleRegisterAck();
    } else if (strcmp(cmd, "PONG") == 0) {
        handlePong();
    } else if (strcmp(cmd, "SYNC_DATA") == 0) {
        handleSyncData(doc);
    } else if (strcmp(cmd, "BROADCAST_MSG") == 0) {
        handleBroadcastMsg(doc["params"].as<JsonObject>());
    } else if (strcmp(cmd, "GET_STATS") == 0) {
        handleGetStats();
    } else {
        dbgprintf("[Node] Unknown admin command: %s\n", cmd);
        return false;
    }
    return true;
}

NodeState nodeClientGetState() {
    return state;
}
