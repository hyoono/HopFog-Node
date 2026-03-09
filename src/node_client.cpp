#include "node_client.h"
#include "config.h"
#include "xbee_comm.h"
#include "sd_storage.h"
#include <WiFi.h>

static NodeState state = STATE_UNREGISTERED;
static unsigned long lastRegisterMs  = 0;
static unsigned long lastHeartbeatMs = 0;
static unsigned long lastSyncMs      = 0;

// ── Sync accumulation buffers — one JsonDocument per data part ───────
static JsonDocument syncUsers;
static JsonDocument syncAnnouncements;
static JsonDocument syncConversations;
static JsonDocument syncChatMessages;
static JsonDocument syncFogNodes;

static void initSyncBuffers() {
    syncUsers.to<JsonArray>();
    syncAnnouncements.to<JsonArray>();
    syncConversations.to<JsonArray>();
    syncChatMessages.to<JsonArray>();
    syncFogNodes.to<JsonArray>();
}

// ── Helper: send a JSON command via XBee ────────────────────────────
static void sendCommand(JsonDocument& doc) {
    doc["node_id"] = NODE_ID;
    // REMOVED: doc["ts"] = (long)(millis() / 1000);
    // ts adds ~10 bytes and pushes messages over the 72-byte broadcast limit
    String json;
    serializeJson(doc, json);
    xbeeSendBroadcast(json.c_str(), json.length());
}

// ── Outgoing commands ───────────────────────────────────────────────

static void sendRegister() {
    // MUST be < 72 bytes total for ZigBee broadcast!
    // Old version was ~155 bytes and was SILENTLY DROPPED by XBee.
    JsonDocument doc;
    doc["cmd"] = "REGISTER";
    JsonObject p = doc["params"].to<JsonObject>();
    p["name"] = DEVICE_NAME;
    // DO NOT add ip_address, status, free_heap — makes payload too large
    sendCommand(doc);
    dbgprintln("[Node] Sent REGISTER");
}

static void sendHeartbeat() {
    // MUST be < 72 bytes for ZigBee broadcast
    JsonDocument doc;
    doc["cmd"] = "HEARTBEAT";
    JsonObject p = doc["params"].to<JsonObject>();
    p["up"] = (int)(millis() / 1000);
    p["heap"] = (int)(ESP.getFreeHeap() / 1024);  // KB not bytes
    sendCommand(doc);
}

static void sendSyncRequest() {
    initSyncBuffers();  // Clear buffers before receiving new sync
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

    const char* part = doc["part"] | "";

    if (strlen(part) == 0) {
        // Legacy single SYNC_DATA (backward compatible)
        if (doc["users"].is<JsonArray>()) {
            JsonDocument d; d.set(doc["users"]);
            writeJsonFile(SD_USERS_FILE, d);
        }
        if (doc["announcements"].is<JsonArray>()) {
            JsonDocument d; d.set(doc["announcements"]);
            writeJsonFile(SD_ANNOUNCE_FILE, d);
        }
        if (doc["conversations"].is<JsonArray>()) {
            JsonDocument d; d.set(doc["conversations"]);
            writeJsonFile(SD_CONVOS_FILE, d);
        }
        if (doc["chat_messages"].is<JsonArray>()) {
            JsonDocument d; d.set(doc["chat_messages"]);
            writeJsonFile(SD_DMS_FILE, d);
        }
        if (doc["fog_nodes"].is<JsonArray>()) {
            JsonDocument d; d.set(doc["fog_nodes"]);
            writeJsonFile(SD_FOG_FILE, d);
        }
        state = STATE_RUNNING;
        lastHeartbeatMs = millis();
        dbgprintln("[Node] Sync complete — now in RUNNING state");
        return;
    }

    // ── Record-by-record format: "n" = count (final msg for this part) ──
    if (doc["n"].is<int>()) {
        // All records for this part have been sent — write buffer to SD
        if (strcmp(part, "users") == 0) {
            writeJsonFile(SD_USERS_FILE, syncUsers);
        } else if (strcmp(part, "announcements") == 0) {
            writeJsonFile(SD_ANNOUNCE_FILE, syncAnnouncements);
        } else if (strcmp(part, "conversations") == 0) {
            writeJsonFile(SD_CONVOS_FILE, syncConversations);
        } else if (strcmp(part, "chat_messages") == 0) {
            writeJsonFile(SD_DMS_FILE, syncChatMessages);
        } else if (strcmp(part, "fog_nodes") == 0) {
            writeJsonFile(SD_FOG_FILE, syncFogNodes);
        }
        return;
    }

    // ── Record-by-record format: "d" = single record object ─────────
    if (doc["d"].is<JsonObject>()) {
        JsonObject record = doc["d"].as<JsonObject>();
        if (strcmp(part, "users") == 0) {
            syncUsers.as<JsonArray>().add(record);
        } else if (strcmp(part, "announcements") == 0) {
            syncAnnouncements.as<JsonArray>().add(record);
        } else if (strcmp(part, "conversations") == 0) {
            syncConversations.as<JsonArray>().add(record);
        } else if (strcmp(part, "chat_messages") == 0) {
            syncChatMessages.as<JsonArray>().add(record);
        } else if (strcmp(part, "fog_nodes") == 0) {
            syncFogNodes.as<JsonArray>().add(record);
        }
        return;
    }

    // ── Chunked format: "data" = array of records ───────────────────
    JsonDocument saveDoc;
    if (doc["data"].is<JsonArray>()) {
        saveDoc.set(doc["data"]);
    } else {
        saveDoc.to<JsonArray>();
    }

    // If admin truncated data down to an empty array, skip writing
    // to preserve whatever the node already has on SD card.
    bool truncated = doc["truncated"] | false;
    JsonArray dataArr = saveDoc.as<JsonArray>();
    if (truncated && dataArr.size() == 0) {
        dbgprintf("[Node] Skipping empty truncated %s\n", part);
        return;
    }

    if (strcmp(part, "users") == 0) {
        writeJsonFile(SD_USERS_FILE, saveDoc);
    } else if (strcmp(part, "announcements") == 0) {
        writeJsonFile(SD_ANNOUNCE_FILE, saveDoc);
    } else if (strcmp(part, "conversations") == 0) {
        writeJsonFile(SD_CONVOS_FILE, saveDoc);
    } else if (strcmp(part, "chat_messages") == 0) {
        writeJsonFile(SD_DMS_FILE, saveDoc);
    } else if (strcmp(part, "fog_nodes") == 0) {
        writeJsonFile(SD_FOG_FILE, saveDoc);
    }
}

static void handleSyncDone() {
    state = STATE_RUNNING;
    lastHeartbeatMs = millis();
    // Clear sync buffers for next sync
    initSyncBuffers();
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
    JsonObject p = doc["params"].to<JsonObject>();
    p["heap"] = (int)(ESP.getFreeHeap() / 1024);
    p["up"] = (int)(millis() / 1000);
    sendCommand(doc);
}

// ── Public API ──────────────────────────────────────────────────────

void nodeClientInit() {
    state = STATE_UNREGISTERED;
    lastRegisterMs = 0;
    lastHeartbeatMs = 0;
    lastSyncMs = 0;
    initSyncBuffers();
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
    } else if (strcmp(cmd, "SYNC_DONE") == 0) {
        handleSyncDone();
    } else if (strcmp(cmd, "PING") == 0) {
        // Admin sends PING every 10s — reply with PONG
        JsonDocument pong;
        pong["cmd"] = "PONG";
        sendCommand(pong);
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

void nodeClientTriggerRegister() {
    sendRegister();
    state = STATE_UNREGISTERED;
    lastRegisterMs = millis();
}

void nodeClientTriggerSync() {
    sendSyncRequest();
    state = STATE_SYNCING;
    lastSyncMs = millis();
}
