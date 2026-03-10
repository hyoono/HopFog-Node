#include "web_server.h"
#include "config.h"
#include "sd_storage.h"
#include "xbee_comm.h"
#include "node_client.h"
#include "auth.h"
#include <ArduinoJson.h>

// ── Helper: relay a JSON command to admin via XBee ──────────────────
static void relayToAdmin(JsonDocument& doc) {
    doc["node_id"] = NODE_ID;
    doc["ts"] = (long)(millis() / 1000);
    String json;
    serializeJson(doc, json);
    xbeeSendBroadcast(json.c_str(), json.length());
}

// ── Helper: read a JSON file and send as response ───────────────────
static void sendJsonFileResponse(AsyncWebServerRequest* request, const char* path) {
    JsonDocument doc;
    if (readJsonFile(path, doc)) {
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    } else {
        request->send(200, "application/json", "[]");
    }
}

void registerApiHandlers(AsyncWebServer& server) {

    // ── GET /status ─────────────────────────────────────────────────
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["online"] = true;
        doc["node_id"] = NODE_ID;
        doc["device_name"] = DEVICE_NAME;
        doc["state"] = (int)nodeClientGetState();
        doc["free_heap"] = (int)ESP.getFreeHeap();
        doc["uptime"] = (int)(millis() / 1000);
        doc["wifi_stations"] = WiFi.softAPgetStationNum();
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // ── POST /login ─────────────────────────────────────────────────
    server.on("/login", HTTP_POST, [](AsyncWebServerRequest* request) {
        // Body handled in onBody
    }, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len,
                size_t index, size_t total) {
        JsonDocument reqDoc;
        DeserializationError err = deserializeJson(reqDoc, data, len);
        if (err) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        const char* username = reqDoc["username"];
        const char* password = reqDoc["password"];
        if (!username || !password) {
            request->send(400, "application/json",
                          "{\"error\":\"Missing username or password\"}");
            return;
        }

        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);
        JsonArray users = usersDoc.as<JsonArray>();

        for (JsonObject user : users) {
            if (strcmp(user["username"] | "", username) != 0) continue;

            // Check is_active
            int active = user["is_active"] | 1;
            if (active == 0) {
                request->send(403, "application/json",
                              "{\"success\":false,\"error\":\"Account inactive\"}");
                return;
            }

            // Verify password against password_hash (salt:sha256hex format)
            const char* storedHash = user["password_hash"] | "";
            if (strlen(storedHash) > 0 && verifyPassword(String(password), String(storedHash))) {
                JsonDocument respDoc;
                respDoc["success"] = true;
                JsonObject u = respDoc["user"].to<JsonObject>();
                u["user_id"] = user["id"] | 0;
                u["username"] = user["username"] | "";
                u["email"] = user["email"] | "";
                u["has_agreed_sos"] = (user["has_agreed_sos"] | 0) == 1;
                String response;
                serializeJson(respDoc, response);
                request->send(200, "application/json", response);
                return;
            }

            request->send(401, "application/json",
                          "{\"success\":false,\"error\":\"Invalid credentials\"}");
            return;
        }

        request->send(401, "application/json",
                      "{\"success\":false,\"error\":\"Invalid credentials\"}");
    });

    // ── GET /announcements ──────────────────────────────────────────
    server.on("/announcements", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendJsonFileResponse(request, SD_ANNOUNCE_FILE);
    });

    // ── GET /users ──────────────────────────────────────────────────
    server.on("/users", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendJsonFileResponse(request, SD_USERS_FILE);
    });

    // ── GET /conversations ──────────────────────────────────────────
    server.on("/conversations", HTTP_GET, [](AsyncWebServerRequest* request) {
        String userId = request->hasParam("user_id")
                        ? request->getParam("user_id")->value() : "";

        if (userId.length() == 0) {
            request->send(400, "application/json",
                          "{\"error\":\"user_id required\"}");
            return;
        }

        int uid = userId.toInt();

        JsonDocument convoDoc;
        readJsonFile(SD_CONVOS_FILE, convoDoc);

        JsonDocument dmDoc;
        readJsonFile(SD_DMS_FILE, dmDoc);

        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);

        JsonDocument resp;
        JsonArray arr = resp.to<JsonArray>();

        for (JsonObject c : convoDoc.as<JsonArray>()) {
            int u1 = c["user1_id"] | 0;
            int u2 = c["user2_id"] | 0;
            if (u1 != uid && u2 != uid) continue;

            int otherId = (u1 == uid) ? u2 : u1;
            String contactName = "Unknown";
            for (JsonObject u : usersDoc.as<JsonArray>()) {
                if ((u["id"] | 0) == otherId) {
                    contactName = u["username"] | "Unknown";
                    break;
                }
            }

            // Find last message
            int convoId = c["id"] | 0;
            String lastMsg = "";
            String lastTs = "";
            unsigned long latestTime = 0;
            for (JsonObject m : dmDoc.as<JsonArray>()) {
                if ((m["conversation_id"] | 0) != convoId) continue;
                unsigned long ts = m["sent_at"] | m["created_at"].as<unsigned long>();
                if (ts >= latestTime) {
                    latestTime = ts;
                    lastMsg = m["message_text"] | "";
                    lastTs = String(ts);
                }
            }

            JsonObject o = arr.add<JsonObject>();
            o["conversation_id"] = convoId;
            o["contact_name"] = contactName;
            o["last_message"] = lastMsg.length() > 0 ? lastMsg : JsonVariant();
            o["timestamp"] = lastTs.length() > 0 ? lastTs : JsonVariant();
        }

        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);
    });

    // ── GET /messages ───────────────────────────────────────────────
    server.on("/messages", HTTP_GET, [](AsyncWebServerRequest* request) {
        String convId = request->hasParam("conversation_id")
                        ? request->getParam("conversation_id")->value() : "";
        String userId = request->hasParam("user_id")
                        ? request->getParam("user_id")->value() : "";

        if (convId.length() == 0 || userId.length() == 0) {
            request->send(400, "application/json",
                          "{\"error\":\"conversation_id and user_id required\"}");
            return;
        }

        int cid = convId.toInt();
        int uid = userId.toInt();

        JsonDocument doc;
        readJsonFile(SD_DMS_FILE, doc);

        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);

        JsonDocument resp;
        JsonArray arr = resp.to<JsonArray>();

        for (JsonObject m : doc.as<JsonArray>()) {
            if ((m["conversation_id"] | 0) != cid) continue;

            int senderId = m["sender_id"] | 0;
            String senderName = "Unknown";
            for (JsonObject u : usersDoc.as<JsonArray>()) {
                if ((u["id"] | 0) == senderId) {
                    senderName = u["username"] | "Unknown";
                    break;
                }
            }

            // Use sent_at or created_at (admin uses sent_at, node uses created_at)
            unsigned long ts = m["sent_at"] | 0UL;
            if (ts == 0 && m["created_at"].is<const char*>()) {
                ts = String(m["created_at"].as<const char*>()).toInt();
            } else if (ts == 0) {
                ts = m["created_at"] | 0UL;
            }

            JsonObject o = arr.add<JsonObject>();
            o["message_id"] = m["id"];
            o["message_text"] = m["message_text"];
            o["sent_at"] = String(ts);
            o["sender_id"] = senderId;
            o["is_from_current_user"] = (senderId == uid);
            o["sender_username"] = senderName;
        }

        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);
    });

    // ── POST /send ──────────────────────────────────────────────────
    server.on("/send", HTTP_POST, [](AsyncWebServerRequest* request) {
        // Body handled in onBody
    }, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len,
                size_t index, size_t total) {
        JsonDocument reqDoc;
        DeserializationError err = deserializeJson(reqDoc, data, len);
        if (err) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        int convId = reqDoc["conversation_id"] | 0;
        int senderId = reqDoc["sender_id"] | 0;
        const char* text = reqDoc["message_text"] | reqDoc["message"] | "";

        if (convId == 0 || senderId == 0 || strlen(text) == 0) {
            request->send(400, "application/json",
                          "{\"error\":\"Missing conversation_id, sender_id, or message\"}");
            return;
        }

        // Store locally
        JsonDocument msgsDoc;
        readJsonFile(SD_DMS_FILE, msgsDoc);
        JsonArray msgs = msgsDoc.is<JsonArray>()
                         ? msgsDoc.as<JsonArray>() : msgsDoc.to<JsonArray>();

        int newId = 1;
        for (JsonObject m : msgs) {
            int id = m["id"] | 0;
            if (id >= newId) newId = id + 1;
        }

        JsonObject newMsg = msgs.add<JsonObject>();
        newMsg["id"] = newId;
        newMsg["conversation_id"] = convId;
        newMsg["sender_id"] = senderId;
        newMsg["message_text"] = text;
        newMsg["sent_at"] = (long)(millis() / 1000);

        writeJsonFile(SD_DMS_FILE, msgsDoc);

        // Relay to admin via XBee
        JsonDocument relayDoc;
        relayDoc["cmd"] = "RELAY_CHAT_MSG";
        JsonObject params = relayDoc["params"].to<JsonObject>();
        params["conversation_id"] = convId;
        params["sender_id"] = senderId;
        params["message_text"] = text;
        relayToAdmin(relayDoc);

        request->send(200, "application/json",
                      "{\"success\":true,\"message\":\"sent\",\"secondsRemaining\":0}");
    });

    // ── POST /create-chat ───────────────────────────────────────────
    server.on("/create-chat", HTTP_POST, [](AsyncWebServerRequest* request) {
        // Body handled in onBody
    }, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len,
                size_t index, size_t total) {
        JsonDocument reqDoc;
        DeserializationError err = deserializeJson(reqDoc, data, len);
        if (err) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        int user1Id = reqDoc["user1_id"] | 0;
        int user2Id = reqDoc["user2_id"] | 0;

        if (user1Id <= 0 || user2Id <= 0) {
            request->send(400, "application/json",
                          "{\"error\":\"user1_id and user2_id required\"}");
            return;
        }

        // Find existing or create new conversation
        JsonDocument convDoc;
        readJsonFile(SD_CONVOS_FILE, convDoc);
        JsonArray convs = convDoc.is<JsonArray>()
                          ? convDoc.as<JsonArray>() : convDoc.to<JsonArray>();

        int convoId = 0;
        for (JsonObject c : convs) {
            int u1 = c["user1_id"] | 0;
            int u2 = c["user2_id"] | 0;
            if ((u1 == user1Id && u2 == user2Id) ||
                (u1 == user2Id && u2 == user1Id)) {
                convoId = c["id"] | 0;
                break;
            }
        }

        if (convoId == 0) {
            int newId = 1;
            for (JsonObject c : convs) {
                int id = c["id"] | 0;
                if (id >= newId) newId = id + 1;
            }
            JsonObject nc = convs.add<JsonObject>();
            nc["id"] = newId;
            nc["user1_id"] = user1Id;
            nc["user2_id"] = user2Id;
            nc["is_sos"] = false;
            nc["created_at"] = String((long)(millis() / 1000));
            writeJsonFile(SD_CONVOS_FILE, convDoc);
            convoId = newId;
        }

        // Look up contact name
        int otherId = user2Id;
        String contactName = "Unknown";
        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);
        for (JsonObject u : usersDoc.as<JsonArray>()) {
            if ((u["id"] | 0) == otherId) {
                contactName = u["username"] | "Unknown";
                break;
            }
        }

        // Return format matching admin's /create-chat
        JsonDocument resp;
        resp["conversation_id"] = convoId;
        resp["contact_name"] = contactName;
        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);
    });

    // ── POST /sos ───────────────────────────────────────────────────
    server.on("/sos", HTTP_POST, [](AsyncWebServerRequest* request) {
        // Body handled in onBody
    }, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len,
                size_t index, size_t total) {
        JsonDocument reqDoc;
        DeserializationError err = deserializeJson(reqDoc, data, len);
        if (err) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        int userId = reqDoc["user_id"] | 0;
        if (userId <= 0) {
            request->send(400, "application/json", "{\"error\":\"user_id required\"}");
            return;
        }

        // Find first admin user
        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);
        int adminId = 0;
        String adminName = "Admin";
        for (JsonObject u : usersDoc.as<JsonArray>()) {
            String role = u["role"] | "";
            if (role == "admin" && (u["is_active"] | 1)) {
                adminId = u["id"] | 0;
                adminName = u["username"] | "Admin";
                break;
            }
        }

        if (adminId == 0) {
            request->send(500, "application/json",
                          "{\"error\":\"No admin user found\"}");
            return;
        }

        // Find or create SOS conversation
        JsonDocument convDoc;
        readJsonFile(SD_CONVOS_FILE, convDoc);
        JsonArray convs = convDoc.is<JsonArray>()
                          ? convDoc.as<JsonArray>() : convDoc.to<JsonArray>();

        int convoId = 0;
        for (JsonObject c : convs) {
            int u1 = c["user1_id"] | 0;
            int u2 = c["user2_id"] | 0;
            if ((u1 == userId && u2 == adminId) ||
                (u1 == adminId && u2 == userId)) {
                convoId = c["id"] | 0;
                break;
            }
        }

        if (convoId == 0) {
            int newId = 1;
            for (JsonObject c : convs) {
                int id = c["id"] | 0;
                if (id >= newId) newId = id + 1;
            }
            JsonObject nc = convs.add<JsonObject>();
            nc["id"] = newId;
            nc["user1_id"] = userId;
            nc["user2_id"] = adminId;
            nc["is_sos"] = true;
            nc["created_at"] = String((long)(millis() / 1000));
            writeJsonFile(SD_CONVOS_FILE, convDoc);
            convoId = newId;
        }

        // Relay SOS to admin via XBee
        JsonDocument relayDoc;
        relayDoc["cmd"] = "SOS_ALERT";
        JsonObject params = relayDoc["params"].to<JsonObject>();
        params["user_id"] = userId;
        params["conversation_id"] = convoId;
        relayDoc["node_id"] = NODE_ID;
        String json;
        serializeJson(relayDoc, json);
        xbeeSendBroadcast(json.c_str(), json.length());

        // Return format matching admin's /sos
        JsonDocument resp;
        resp["conversation_id"] = convoId;
        resp["contact_name"] = adminName;
        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);
    });

    // ── GET /new-messages ───────────────────────────────────────────
    server.on("/new-messages", HTTP_GET, [](AsyncWebServerRequest* request) {
        String userId = request->hasParam("user_id")
                        ? request->getParam("user_id")->value() : "";
        String lastIdStr = request->hasParam("last_id")
                           ? request->getParam("last_id")->value() : "0";

        int uid = userId.toInt();
        int lastId = lastIdStr.toInt();

        JsonDocument dmDoc;
        readJsonFile(SD_DMS_FILE, dmDoc);

        JsonDocument convoDoc;
        readJsonFile(SD_CONVOS_FILE, convoDoc);

        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);

        JsonDocument resp;
        JsonArray arr = resp.to<JsonArray>();

        for (JsonObject m : dmDoc.as<JsonArray>()) {
            if ((m["id"] | 0) <= lastId) continue;

            int cid = m["conversation_id"] | 0;
            bool userInConvo = false;
            for (JsonObject c : convoDoc.as<JsonArray>()) {
                if ((c["id"] | 0) != cid) continue;
                int u1 = c["user1_id"] | 0;
                int u2 = c["user2_id"] | 0;
                if (u1 == uid || u2 == uid) userInConvo = true;
                break;
            }
            if (uid > 0 && !userInConvo) continue;

            int senderId = m["sender_id"] | 0;
            String senderName = "Unknown";
            for (JsonObject u : usersDoc.as<JsonArray>()) {
                if ((u["id"] | 0) == senderId) {
                    senderName = u["username"] | "Unknown";
                    break;
                }
            }

            unsigned long ts = m["sent_at"] | 0UL;
            if (ts == 0 && m["created_at"].is<const char*>()) {
                ts = String(m["created_at"].as<const char*>()).toInt();
            } else if (ts == 0) {
                ts = m["created_at"] | 0UL;
            }

            JsonObject o = arr.add<JsonObject>();
            o["message_id"] = m["id"];
            o["message_text"] = m["message_text"];
            o["sent_at"] = String(ts);
            o["sender_id"] = senderId;
            o["is_from_current_user"] = (senderId == uid);
            o["sender_username"] = senderName;
        }

        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);
    });

    // ── POST /agree-sos ─────────────────────────────────────────────
    server.on("/agree-sos", HTTP_POST, [](AsyncWebServerRequest* request) {
        // Body handled in onBody
    }, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len,
                size_t index, size_t total) {
        JsonDocument reqDoc;
        DeserializationError err = deserializeJson(reqDoc, data, len);
        if (err) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        int userId = reqDoc["user_id"] | 0;

        // Update user's sos_agreed flag locally
        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);
        JsonArray users = usersDoc.as<JsonArray>();

        for (JsonObject user : users) {
            if ((user["id"] | 0) == userId) {
                user["sos_agreed"] = true;
                break;
            }
        }

        writeJsonFile(SD_USERS_FILE, usersDoc);

        request->send(200, "application/json",
                      "{\"success\":true,\"message\":\"SOS agreement recorded\"}");
    });

    // ── POST /change-password ───────────────────────────────────────
    server.on("/change-password", HTTP_POST, [](AsyncWebServerRequest* request) {
        // Body handled in onBody
    }, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len,
                size_t index, size_t total) {
        JsonDocument reqDoc;
        DeserializationError err = deserializeJson(reqDoc, data, len);
        if (err) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        int userId = reqDoc["user_id"] | 0;
        const char* oldPw = reqDoc["old_password"] | "";
        const char* newPw = reqDoc["new_password"] | "";

        if (userId == 0 || strlen(oldPw) == 0 || strlen(newPw) == 0) {
            request->send(400, "application/json",
                          "{\"error\":\"Missing user_id, old_password, or new_password\"}");
            return;
        }

        // Verify old password and update locally
        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);
        JsonArray users = usersDoc.as<JsonArray>();

        bool found = false;
        for (JsonObject user : users) {
            if ((user["id"] | 0) == userId) {
                const char* storedHash = user["password_hash"] | "";
                if (!verifyPassword(String(oldPw), String(storedHash))) {
                    request->send(401, "application/json",
                                  "{\"success\":false,\"error\":\"Wrong old password\"}");
                    return;
                }
                user["password_hash"] = hashPassword(String(newPw));
                found = true;
                break;
            }
        }

        if (!found) {
            request->send(404, "application/json",
                          "{\"success\":false,\"error\":\"User not found\"}");
            return;
        }

        writeJsonFile(SD_USERS_FILE, usersDoc);

        // Relay to admin via XBee
        JsonDocument relayDoc;
        relayDoc["cmd"] = "CHANGE_PASSWORD";
        JsonObject params = relayDoc["params"].to<JsonObject>();
        params["user_id"] = userId;
        params["old_password"] = oldPw;
        params["new_password"] = newPw;
        relayToAdmin(relayDoc);

        request->send(200, "application/json",
                      "{\"success\":true,\"message\":\"Password changed\"}");
    });

    // ── POST /api/trigger/register — manually trigger REGISTER ────────
    server.on("/api/trigger/register", HTTP_POST, [](AsyncWebServerRequest* request) {
        nodeClientTriggerRegister();
        request->send(200, "application/json",
                      "{\"success\":true,\"message\":\"REGISTER sent\"}");
    });

    // ── POST /api/trigger/sync — manually trigger SYNC_REQUEST ──────
    server.on("/api/trigger/sync", HTTP_POST, [](AsyncWebServerRequest* request) {
        nodeClientTriggerSync();
        request->send(200, "application/json",
                      "{\"success\":true,\"message\":\"SYNC_REQUEST sent\"}");
    });

    // ── POST /api/trigger/sync-back — send local data back to admin ──
    server.on("/api/trigger/sync-back", HTTP_POST, [](AsyncWebServerRequest* request) {
        nodeClientTriggerSyncBack();
        request->send(200, "application/json",
                      "{\"success\":true,\"message\":\"SYNC_BACK sent\"}");
    });

    // ── GET /api/xbee/status — XBee diagnostic counters ─────────────
    server.on("/api/xbee/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        const XBeeStats& s = xbeeGetStats();
        JsonDocument doc;
        doc["totalRxBytes"]     = s.totalRxBytes;
        doc["totalTxBytes"]     = s.totalTxBytes;
        doc["rxFramesParsed"]   = s.rxFramesParsed;
        doc["txFramesSent"]     = s.txFramesSent;
        doc["txStatusOK"]       = s.txStatusOK;
        doc["txStatusFail"]     = s.txStatusFail;
        doc["modemStatusCount"] = s.modemStatusCount;
        doc["lastModemStatus"]  = s.lastModemStatus;
        doc["uptimeSeconds"]    = (int)(millis() / 1000);
        doc["nodeState"]        = (int)nodeClientGetState();
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
}
