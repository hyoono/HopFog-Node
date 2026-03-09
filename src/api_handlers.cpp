#include "web_server.h"
#include "config.h"
#include "sd_storage.h"
#include "xbee_comm.h"
#include "node_client.h"
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
            if (strcmp(user["username"] | "", username) == 0 &&
                strcmp(user["password"] | "", password) == 0) {
                JsonDocument respDoc;
                respDoc["success"] = true;
                respDoc["user"] = user;
                String response;
                serializeJson(respDoc, response);
                request->send(200, "application/json", response);
                return;
            }
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

        JsonDocument doc;
        readJsonFile(SD_CONVOS_FILE, doc);

        if (userId.length() > 0) {
            int uid = userId.toInt();
            JsonDocument filtered;
            JsonArray arr = filtered.to<JsonArray>();
            for (JsonObject conv : doc.as<JsonArray>()) {
                JsonArray participants = conv["participants"].as<JsonArray>();
                for (JsonVariant p : participants) {
                    if (p.as<int>() == uid) {
                        arr.add(conv);
                        break;
                    }
                }
            }
            String response;
            serializeJson(filtered, response);
            request->send(200, "application/json", response);
        } else {
            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        }
    });

    // ── GET /messages ───────────────────────────────────────────────
    server.on("/messages", HTTP_GET, [](AsyncWebServerRequest* request) {
        String convId = request->hasParam("conversation_id")
                        ? request->getParam("conversation_id")->value() : "";

        JsonDocument doc;
        readJsonFile(SD_DMS_FILE, doc);

        if (convId.length() > 0) {
            int cid = convId.toInt();
            JsonDocument filtered;
            JsonArray arr = filtered.to<JsonArray>();
            for (JsonObject msg : doc.as<JsonArray>()) {
                if ((msg["conversation_id"] | 0) == cid) {
                    arr.add(msg);
                }
            }
            String response;
            serializeJson(filtered, response);
            request->send(200, "application/json", response);
        } else {
            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        }
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
        newMsg["created_at"] = String((long)(millis() / 1000));

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
                      "{\"success\":true,\"message\":\"Message sent\"}");
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

        // Store locally
        JsonDocument convDoc;
        readJsonFile(SD_CONVOS_FILE, convDoc);
        JsonArray convs = convDoc.is<JsonArray>()
                          ? convDoc.as<JsonArray>() : convDoc.to<JsonArray>();

        int newId = 1;
        for (JsonObject c : convs) {
            int id = c["id"] | 0;
            if (id >= newId) newId = id + 1;
        }

        JsonObject newConv = convs.add<JsonObject>();
        newConv["id"] = newId;
        if (reqDoc["participants"].is<JsonArray>()) {
            newConv["participants"] = reqDoc["participants"];
        }
        newConv["created_at"] = String((long)(millis() / 1000));

        writeJsonFile(SD_CONVOS_FILE, convDoc);

        JsonDocument respDoc;
        respDoc["success"] = true;
        respDoc["conversation"] = newConv;
        String response;
        serializeJson(respDoc, response);
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

        // Relay SOS to admin via XBee immediately
        JsonDocument relayDoc;
        relayDoc["cmd"] = "SOS_ALERT";
        JsonObject params = relayDoc["params"].to<JsonObject>();
        params["user_id"] = userId;
        relayToAdmin(relayDoc);

        request->send(200, "application/json",
                      "{\"success\":true,\"message\":\"SOS alert sent\"}");
    });

    // ── GET /new-messages ───────────────────────────────────────────
    server.on("/new-messages", HTTP_GET, [](AsyncWebServerRequest* request) {
        String userId = request->hasParam("user_id")
                        ? request->getParam("user_id")->value() : "";
        String since = request->hasParam("since")
                       ? request->getParam("since")->value() : "0";

        long sinceTs = since.toInt();

        JsonDocument doc;
        readJsonFile(SD_DMS_FILE, doc);

        JsonDocument filtered;
        JsonArray arr = filtered.to<JsonArray>();

        for (JsonObject msg : doc.as<JsonArray>()) {
            long msgTs = 0;
            if (msg["created_at"].is<const char*>()) {
                msgTs = String(msg["created_at"].as<const char*>()).toInt();
            } else {
                msgTs = msg["created_at"] | 0;
            }
            if (msgTs > sinceTs) {
                arr.add(msg);
            }
        }

        String response;
        serializeJson(filtered, response);
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
                if (strcmp(user["password"] | "", oldPw) != 0) {
                    request->send(401, "application/json",
                                  "{\"success\":false,\"error\":\"Wrong old password\"}");
                    return;
                }
                user["password"] = newPw;
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
