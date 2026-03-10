#include "node_client.h"
#include "config.h"
#include "xbee_comm.h"
#include "sd_storage.h"
#include <WiFi.h>

static NodeState state = STATE_UNREGISTERED;
static unsigned long lastRegisterMs  = 0;
static unsigned long lastHeartbeatMs = 0;
static unsigned long lastSyncMs      = 0;
static unsigned long lastCleanupMs   = 0;

// Message cleanup — delete direct messages older than 48 hours
#define MESSAGE_TTL_SECONDS  172800  // 48 hours
#define CLEANUP_INTERVAL_MS  600000  // Run cleanup every 10 minutes

// ZigBee broadcast payload limit — messages larger than this are silently dropped
static const int XBEE_MAX_BROADCAST_BYTES = 72;
// SC chunk size for string fields (conservative to fit in broadcast)
static const int SC_CHUNK_SIZE = 40;
// Delay between XBee transmissions to avoid overwhelming the radio
static const int XBEE_TX_DELAY_MS = 50;

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

// Helper: find the sync buffer for a given part name
static JsonDocument* getSyncBuffer(const char* part) {
    if (strcmp(part, "users") == 0) return &syncUsers;
    if (strcmp(part, "announcements") == 0) return &syncAnnouncements;
    if (strcmp(part, "conversations") == 0) return &syncConversations;
    if (strcmp(part, "chat_messages") == 0) return &syncChatMessages;
    if (strcmp(part, "fog_nodes") == 0) return &syncFogNodes;
    return nullptr;
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

static void handleSyncContinuation(JsonDocument& doc) {
    // SC = Sync Continuation: appends text to a field in a previously
    // received record. Used for long text fields that exceeded the
    // 240-byte XBee unicast limit.
    //
    // Format: {"cmd":"SC","p":"announcements","s":0,"k":"body","v":"...text chunk..."}
    //   p = part name
    //   s = seq number (index into the sync buffer for that part)
    //   k = field key to append to
    //   v = text chunk to append
    const char* part = doc["p"] | "";
    int seq = doc["s"] | -1;
    const char* key = doc["k"] | "";
    const char* val = doc["v"] | "";
    if (seq < 0 || strlen(key) == 0 || strlen(part) == 0) {
        dbgprintln("[Node] SC: invalid params");
        return;
    }

    JsonDocument* buf = getSyncBuffer(part);
    if (!buf) return;

    JsonArray arr = buf->as<JsonArray>();
    if (seq >= (int)arr.size()) {
        dbgprintf("[Node] SC: seq %d not found in %s\n", seq, part);
        return;
    }

    JsonObject rec = arr[seq].as<JsonObject>();
    // Append v to existing value of key
    rec[key] = rec[key].as<String>() + val;
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

// ── Compact SYNC_DATA format handler ────────────────────────────────
// Admin now sends "c":"SD" with "n" for node_id, "p" for part,
// "n2" for count, "d" for record data
static void handleCompactSyncData(JsonDocument& doc) {
    const char* part = doc["p"] | "";
    if (strlen(part) == 0) return;

    // Count message: "n2" = number of records for this part
    if (doc["n2"].is<int>()) {
        // Write accumulated buffer to SD
        JsonDocument* buf = getSyncBuffer(part);
        if (buf) {
            if (strcmp(part, "users") == 0) {
                writeJsonFile(SD_USERS_FILE, *buf);
            } else if (strcmp(part, "announcements") == 0) {
                writeJsonFile(SD_ANNOUNCE_FILE, *buf);
            } else if (strcmp(part, "conversations") == 0) {
                writeJsonFile(SD_CONVOS_FILE, *buf);
            } else if (strcmp(part, "chat_messages") == 0) {
                writeJsonFile(SD_DMS_FILE, *buf);
            } else if (strcmp(part, "fog_nodes") == 0) {
                writeJsonFile(SD_FOG_FILE, *buf);
            }
        }
        return;
    }

    // Single record: "d" = record object
    if (doc["d"].is<JsonObject>()) {
        JsonObject record = doc["d"].as<JsonObject>();
        JsonDocument* buf = getSyncBuffer(part);
        if (buf) {
            buf->as<JsonArray>().add(record);
        }
    }
}

// ── Handle RELAY_CHAT_MSG from admin ────────────────────────────────
// When admin relays a chat message from a user on the admin network to
// a user on the node network, save it to SD so the recipient sees it.
static void handleRelayChatMsg(JsonObject params) {
    int convId = params["conversation_id"] | 0;
    int senderId = params["sender_id"] | 0;
    const char* text = params["message_text"] | "";
    if (convId <= 0 || senderId <= 0 || strlen(text) == 0) return;

    JsonDocument doc;
    readJsonFile(SD_DMS_FILE, doc);
    JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

    int maxId = 0;
    for (JsonObject m : arr) {
        int id = m["id"] | 0;
        if (id > maxId) maxId = id;
    }

    JsonObject msg = arr.add<JsonObject>();
    msg["id"] = maxId + 1;
    msg["conversation_id"] = convId;
    msg["sender_id"] = senderId;
    msg["message_text"] = text;
    msg["sent_at"] = (long)(millis() / 1000);

    writeJsonFile(SD_DMS_FILE, doc);
}

// ── Handle SOS_ALERT from admin ─────────────────────────────────────
// Store incoming SOS alert as an announcement
static void handleSosAlert(JsonObject params) {
    const char* username = params["username"] | "User";

    JsonDocument doc;
    readJsonFile(SD_ANNOUNCE_FILE, doc);
    JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

    int maxId = 0;
    for (JsonObject a : arr) {
        int id = a["id"] | 0;
        if (id > maxId) maxId = id;
    }

    String title = "SOS Alert from " + String(username);
    JsonObject ann = arr.add<JsonObject>();
    ann["id"] = maxId + 1;
    ann["title"] = title;
    ann["message"] = "Emergency SOS alert";
    ann["created_at"] = String((long)(millis() / 1000));

    writeJsonFile(SD_ANNOUNCE_FILE, doc);
}

// ── SYNC_BACK: send local data back to admin ───────────────────────

static void sendSyncBackPart(const char* partName, const char* sdFile) {
    JsonDocument fileDoc;
    readJsonFile(sdFile, fileDoc);
    JsonArray arr = fileDoc.is<JsonArray>() ? fileDoc.as<JsonArray>()
                                            : fileDoc.to<JsonArray>();
    int sent = 0;
    for (JsonVariant item : arr) {
        JsonObject rec = item.as<JsonObject>();

        JsonDocument msg;
        msg["cmd"] = "SYNC_BACK";
        msg["node_id"] = NODE_ID;
        msg["part"] = partName;
        msg["seq"] = sent;
        JsonObject d = msg["d"].to<JsonObject>();
        for (JsonPair kv : rec) {
            d[kv.key()] = kv.value();
        }

        String json;
        serializeJson(msg, json);

        if ((int)json.length() <= XBEE_MAX_BROADCAST_BYTES) {
            xbeeSendBroadcast(json.c_str(), json.length());
        } else {
            // Too large for broadcast — send skeleton + SC chunks
            JsonDocument skelMsg;
            skelMsg["cmd"] = "SYNC_BACK";
            skelMsg["node_id"] = NODE_ID;
            skelMsg["part"] = partName;
            skelMsg["seq"] = sent;
            JsonObject skelD = skelMsg["d"].to<JsonObject>();
            for (JsonPair kv : rec) {
                if (kv.value().is<const char*>()) {
                    skelD[kv.key()] = "";
                } else {
                    skelD[kv.key()] = kv.value();
                }
            }
            String skelJson;
            serializeJson(skelMsg, skelJson);
            xbeeSendBroadcast(skelJson.c_str(), skelJson.length());
            delay(XBEE_TX_DELAY_MS);

            // Send string fields as SC
            for (JsonPair kv : rec) {
                if (!kv.value().is<const char*>()) continue;
                const char* val = kv.value().as<const char*>();
                if (strlen(val) == 0) continue;

                int offset = 0;
                int fullLen = strlen(val);
                while (offset < fullLen) {
                    int end = (offset + SC_CHUNK_SIZE < fullLen)
                              ? offset + SC_CHUNK_SIZE : fullLen;

                    JsonDocument sc;
                    sc["cmd"] = "SC";
                    sc["p"] = partName;
                    sc["s"] = sent;
                    sc["k"] = kv.key().c_str();
                    sc["v"] = String(val).substring(offset, end);
                    String scJson;
                    serializeJson(sc, scJson);
                    if ((int)scJson.length() <= XBEE_MAX_BROADCAST_BYTES) {
                        xbeeSendBroadcast(scJson.c_str(), scJson.length());
                    }
                    delay(XBEE_TX_DELAY_MS);
                    offset = end;
                }
            }
        }

        sent++;
        delay(XBEE_TX_DELAY_MS);
    }

    // Count message
    JsonDocument countMsg;
    countMsg["cmd"] = "SYNC_BACK";
    countMsg["node_id"] = NODE_ID;
    countMsg["part"] = partName;
    countMsg["n"] = sent;
    String countJson;
    serializeJson(countMsg, countJson);
    xbeeSendBroadcast(countJson.c_str(), countJson.length());
    delay(XBEE_TX_DELAY_MS);
}

static void sendSyncBack() {
    // Only sync parts that the node generates locally
    sendSyncBackPart("chat_messages", SD_DMS_FILE);
    sendSyncBackPart("conversations", SD_CONVOS_FILE);

    JsonDocument done;
    done["cmd"] = "SYNC_BACK_DONE";
    done["node_id"] = NODE_ID;
    String doneJson;
    serializeJson(done, doneJson);
    xbeeSendBroadcast(doneJson.c_str(), doneJson.length());
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

static void cleanupOldMessages() {
    JsonDocument doc;
    readJsonFile(SD_DMS_FILE, doc);
    if (!doc.is<JsonArray>()) return;

    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() == 0) return;

    // Find the latest timestamp
    long latestTs = 0;
    for (JsonObject m : arr) {
        long ts = m["sent_at"] | 0L;
        if (ts > latestTs) latestTs = ts;
    }
    if (latestTs == 0) return;

    long cutoff = latestTs - MESSAGE_TTL_SECONDS;
    int removed = 0;
    int i = 0;
    while (i < (int)arr.size()) {
        JsonObject m = arr[i].as<JsonObject>();
        long ts = m["sent_at"] | 0L;
        if (ts > 0 && ts < cutoff) {
            arr.remove(i);
            removed++;
        } else {
            i++;
        }
    }

    if (removed > 0) {
        writeJsonFile(SD_DMS_FILE, doc);
    }
}

void nodeClientLoop() {
    unsigned long now = millis();

    // Periodic message cleanup
    if (now - lastCleanupMs >= CLEANUP_INTERVAL_MS) {
        lastCleanupMs = now;
        cleanupOldMessages();
    }

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

    // Check for compact format first: "c" key
    const char* compactCmd = doc["c"] | (const char*)nullptr;
    const char* legacyCmd  = doc["cmd"] | (const char*)nullptr;

    if (compactCmd) {
        // ── Compact format from admin sync ──
        if (strcmp(compactCmd, "SD") == 0) {
            handleCompactSyncData(doc);
            return true;
        } else if (strcmp(compactCmd, "SC") == 0) {
            handleSyncContinuation(doc);
            return true;
        } else if (strcmp(compactCmd, "DONE") == 0) {
            handleSyncDone();
            return true;
        }
    }

    if (!legacyCmd) return false;  // No command found

    dbgprintf("[Node] RX cmd: %s\n", legacyCmd);

    // ── Legacy format ──
    if (strcmp(legacyCmd, "REGISTER_ACK") == 0) {
        handleRegisterAck();
    } else if (strcmp(legacyCmd, "PONG") == 0) {
        handlePong();
    } else if (strcmp(legacyCmd, "SYNC_DATA") == 0) {
        handleSyncData(doc);
    } else if (strcmp(legacyCmd, "SC") == 0) {
        handleSyncContinuation(doc);
    } else if (strcmp(legacyCmd, "SYNC_DONE") == 0) {
        handleSyncDone();
    } else if (strcmp(legacyCmd, "PING") == 0) {
        // Admin sends PING every 10s — reply with PONG
        JsonDocument pong;
        pong["cmd"] = "PONG";
        sendCommand(pong);
    } else if (strcmp(legacyCmd, "BROADCAST_MSG") == 0) {
        handleBroadcastMsg(doc["params"].as<JsonObject>());
    } else if (strcmp(legacyCmd, "RELAY_CHAT_MSG") == 0) {
        handleRelayChatMsg(doc["params"].as<JsonObject>());
    } else if (strcmp(legacyCmd, "SOS_ALERT") == 0) {
        handleSosAlert(doc["params"].as<JsonObject>());
    } else if (strcmp(legacyCmd, "GET_STATS") == 0) {
        handleGetStats();
    } else {
        dbgprintf("[Node] Unknown admin command: %s\n", legacyCmd);
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

void nodeClientTriggerSyncBack() {
    sendSyncBack();
}
