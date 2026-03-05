/*
 * HopFog Node – Range Extender
 *
 * Headless node that connects to the HopFog admin (HopFog-Web) via XBee.
 * Replicates the admin API endpoints locally so nearby clients can use
 * the same REST calls without reaching the admin directly.
 *
 * Supported boards (select via PlatformIO environment):
 *   esp32cam  – ESP32-CAM (AI-Thinker), XBee on Serial2, SD card storage
 *   d1_mini   – Wemos D1 Mini (ESP8266), XBee on SoftwareSerial, LittleFS
 *
 * Features:
 *   - XBee serial link to admin
 *   - WiFi REST API (no web UI, no login page)
 *   - Persistent local storage (SD card or LittleFS)
 *   - Auto-registration with admin on startup
 *   - Periodic heartbeat / keep-alive
 *   - Message relay between local clients and admin
 */

// ========================================
// Platform-specific includes
// ========================================
#ifdef ARDUINO_ARCH_ESP32
  #include <WiFi.h>
  #include <WebServer.h>
  #include <SD_MMC.h>
  #include <DNSServer.h>
#elif defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <LittleFS.h>
  #include <SoftwareSerial.h>
  #include <DNSServer.h>
#endif

#include <ArduinoJson.h>

// ========================================
// Try to load user config; fall back to defaults
// Copy include/config.h.example → include/config.h
// ========================================
#if __has_include("config.h")
  #include "config.h"
#endif

// ── Defaults (overridden by config.h when present) ──────────────
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
#ifndef WEB_SERVER_PORT
  #define WEB_SERVER_PORT 80
#endif
#ifndef HEARTBEAT_INTERVAL_MS
  #define HEARTBEAT_INTERVAL_MS 30000
#endif
#ifndef SYNC_INTERVAL_MS
  #define SYNC_INTERVAL_MS 60000
#endif
#ifndef REGISTER_RETRY_MS
  #define REGISTER_RETRY_MS 10000
#endif

// ── WiFi Access Point defaults ──────────────────────────────────
#ifndef AP_SSID
  #define AP_SSID "HopFog-Network"
#endif
#ifndef AP_PASSWORD
  #define AP_PASSWORD "hopfog123"
#endif
#ifndef AP_CHANNEL
  #define AP_CHANNEL 6
#endif
#ifndef DNS_DOMAIN
  #define DNS_DOMAIN "hopfog.com"
#endif
#ifndef ADMIN_USER_ID
  #define ADMIN_USER_ID 1
#endif

// ── Board-specific pin defaults ─────────────────────────────────
#ifdef ARDUINO_ARCH_ESP32
  // ESP32-CAM: GPIO 13 (RX) and GPIO 12 (TX) are free when
  // SD_MMC runs in 1-bit mode. GPIO 32/33 are NOT available on
  // the AI-Thinker ESP32-CAM (used by camera / on-board LED).
  #ifndef XBEE_RX_PIN
    #define XBEE_RX_PIN 13
  #endif
  #ifndef XBEE_TX_PIN
    #define XBEE_TX_PIN 12
  #endif
#elif defined(ARDUINO_ARCH_ESP8266)
  // Wemos D1 Mini: D5 = GPIO 14, D6 = GPIO 12
  #ifndef XBEE_RX_PIN
    #define XBEE_RX_PIN 14   // D5
  #endif
  #ifndef XBEE_TX_PIN
    #define XBEE_TX_PIN 12   // D6
  #endif
#endif

// ========================================
// Platform abstractions
// ========================================
#ifdef ARDUINO_ARCH_ESP32
  WebServer server(WEB_SERVER_PORT);
  #define xbeeSerial Serial2
#elif defined(ARDUINO_ARCH_ESP8266)
  ESP8266WebServer server(WEB_SERVER_PORT);
  SoftwareSerial xbeeSerial(XBEE_RX_PIN, XBEE_TX_PIN);
#endif

// ========================================
// Globals
// ========================================
bool storageAvailable = false;

// DNS server for captive portal (resolves hopfog.com → node IP)
DNSServer dnsServer;

// Statistics
int fogNodesCount  = 0;
int activeFogNodes = 0;
int totalMessages  = 0;

// Database file paths (admin device management)
const char* FOG_NODES_FILE = "/hopfog/fog_nodes.json";
const char* MESSAGES_FILE  = "/hopfog/messages.json";
const char* STATS_FILE     = "/hopfog/stats.json";

// Mobile app data files (synced from admin via XBee)
const char* USERS_FILE         = "/hopfog/users.json";
const char* CONVERSATIONS_FILE = "/hopfog/conversations.json";
const char* CHAT_MESSAGES_FILE = "/hopfog/chat_messages.json";
const char* ANNOUNCEMENTS_FILE = "/hopfog/announcements.json";

// Timing
unsigned long lastHeartbeat = 0;
unsigned long lastSync      = 0;
unsigned long lastRegister  = 0;

// Registration state – retry REGISTER until admin ACKs
bool registeredWithAdmin = false;

// ── XBee API mode 1 constants ───────────────────────────────────
#define XBEE_START_DELIM  0x7E
#define XBEE_TX_REQUEST   0x10   // Transmit Request frame type
#define XBEE_RX_PACKET    0x90   // Receive Packet frame type
#define XBEE_TX_STATUS    0x8B   // Transmit Status frame type
#define XBEE_MAX_FRAME    512    // max frame data buffer
#define XBEE_RX_HDR_SIZE  12     // 0x90 header: type(1) + src64(8) + src16(2) + options(1)
#define XBEE_TX_HDR_SIZE  14     // 0x10 header: type(1) + id(1) + dst64(8) + dst16(2) + radius(1) + options(1)

// API frame receive state machine
enum RxState { WAIT_DELIM, GOT_LEN_HI, GOT_LEN_LO, READING_DATA, GOT_CHECKSUM };
static RxState  rxState     = WAIT_DELIM;
static uint16_t rxFrameLen  = 0;
static uint16_t rxIdx       = 0;
static uint8_t  rxFrame[XBEE_MAX_FRAME];
static uint8_t  rxChecksum  = 0;

// API frame send counter
static uint8_t frameIdCounter = 0;

// ========================================
// Storage helpers (SD_MMC on ESP32-CAM,
//                  LittleFS on ESP8266)
// ========================================

#ifdef ARDUINO_ARCH_ESP32
// ---- ESP32-CAM: SD_MMC 1-bit mode ----
bool initStorage() {
    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("[SD] Mount failed");
        return false;
    }
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("[SD] No card inserted");
        return false;
    }
    Serial.printf("[SD] Card type: %s, Size: %llu MB\n",
                  (cardType == CARD_MMC  ? "MMC"  :
                   cardType == CARD_SD   ? "SD"   :
                   cardType == CARD_SDHC ? "SDHC" : "UNKNOWN"),
                  SD_MMC.cardSize() / (1024 * 1024));
    return true;
}

void ensureDirectory(const char* path) {
    if (!SD_MMC.exists(path)) SD_MMC.mkdir(path);
}

String readFile(const char* path) {
    if (!storageAvailable) return "[]";
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return "[]";
    String content = f.readString();
    f.close();
    return content.length() > 0 ? content : "[]";
}

bool writeFile(const char* path, const String& data) {
    if (!storageAvailable) return false;
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) return false;
    f.print(data);
    f.close();
    return true;
}

bool fileExists(const char* path) { return SD_MMC.exists(path); }

void populateStorageStats(JsonDocument& doc) {
    doc["storage_type"]    = "sd_card";
    doc["sd_card_size_mb"] = (uint32_t)(SD_MMC.cardSize() / (1024 * 1024));
    doc["sd_card_used_mb"] = (uint32_t)(SD_MMC.usedBytes() / (1024 * 1024));
}

#elif defined(ARDUINO_ARCH_ESP8266)
// ---- ESP8266: LittleFS ----
bool initStorage() {
    if (!LittleFS.begin()) {
        Serial.println("[FS] LittleFS mount failed");
        return false;
    }
    FSInfo info;
    LittleFS.info(info);
    Serial.printf("[FS] LittleFS ready – %u KB used / %u KB total\n",
                  (unsigned)(info.usedBytes / 1024),
                  (unsigned)(info.totalBytes / 1024));
    return true;
}

void ensureDirectory(const char* /* path */) {
    // LittleFS creates parent dirs automatically on file write
}

String readFile(const char* path) {
    if (!storageAvailable) return "[]";
    File f = LittleFS.open(path, "r");
    if (!f) return "[]";
    String content = f.readString();
    f.close();
    return content.length() > 0 ? content : "[]";
}

bool writeFile(const char* path, const String& data) {
    if (!storageAvailable) return false;
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.print(data);
    f.close();
    return true;
}

bool fileExists(const char* path) { return LittleFS.exists(path); }

void populateStorageStats(JsonDocument& doc) {
    FSInfo info;
    LittleFS.info(info);
    doc["storage_type"]     = "littlefs";
    doc["fs_total_kb"]      = (uint32_t)(info.totalBytes / 1024);
    doc["fs_used_kb"]       = (uint32_t)(info.usedBytes  / 1024);
}
#endif

// ========================================
// Database initialisation
// ========================================
void updateStats();  // forward declaration

void initDatabase() {
    ensureDirectory("/hopfog");
    if (!fileExists(FOG_NODES_FILE)) writeFile(FOG_NODES_FILE, "[]");
    if (!fileExists(MESSAGES_FILE))  writeFile(MESSAGES_FILE,  "[]");
    if (!fileExists(STATS_FILE))     writeFile(STATS_FILE,     "{}");
    // Mobile app data files
    if (!fileExists(USERS_FILE))         writeFile(USERS_FILE,         "[]");
    if (!fileExists(CONVERSATIONS_FILE)) writeFile(CONVERSATIONS_FILE, "[]");
    if (!fileExists(CHAT_MESSAGES_FILE)) writeFile(CHAT_MESSAGES_FILE, "[]");
    if (!fileExists(ANNOUNCEMENTS_FILE)) writeFile(ANNOUNCEMENTS_FILE, "[]");
    updateStats();
    Serial.println("[DB] Database initialised");
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
// Data access – Mobile app data
// ========================================

// Helper: parse JSON request body
bool parseJsonBody(DynamicJsonDocument& doc) {
    String body = server.arg("plain");
    if (body.isEmpty()) return false;
    return !deserializeJson(doc, body);
}

// Helper: get next ID from a JSON array
int getNextId(const char* filePath) {
    String raw = readFile(filePath);
    DynamicJsonDocument doc(8192);
    if (!deserializeJson(doc, raw) && doc.is<JsonArray>()) {
        int maxId = 0;
        for (JsonObject obj : doc.as<JsonArray>()) {
            int id = obj["id"] | 0;
            if (id > maxId) maxId = id;
        }
        return maxId + 1;
    }
    return 1;
}

// Find user by username or email
JsonObject findUser(DynamicJsonDocument& doc, const String& usernameOrEmail) {
    String raw = readFile(USERS_FILE);
    if (deserializeJson(doc, raw) || !doc.is<JsonArray>()) {
        doc.to<JsonArray>();
        return JsonObject();
    }
    for (JsonObject u : doc.as<JsonArray>()) {
        if (u["username"].as<String>() == usernameOrEmail ||
            u["email"].as<String>() == usernameOrEmail) {
            return u;
        }
    }
    return JsonObject();
}

// Find user by ID
JsonObject findUserById(DynamicJsonDocument& doc, int userId) {
    String raw = readFile(USERS_FILE);
    if (deserializeJson(doc, raw) || !doc.is<JsonArray>()) {
        doc.to<JsonArray>();
        return JsonObject();
    }
    for (JsonObject u : doc.as<JsonArray>()) {
        if ((u["id"] | 0) == userId) {
            return u;
        }
    }
    return JsonObject();
}

// Get username by user ID
String getUsernameById(int userId) {
    DynamicJsonDocument doc(8192);
    JsonObject user = findUserById(doc, userId);
    if (user.isNull()) return String("User ") + String(userId);
    return user["username"].as<String>();
}

// Get conversations for a user
String getConversationsForUser(int userId) {
    String raw = readFile(CONVERSATIONS_FILE);
    DynamicJsonDocument allConvs(8192);
    if (deserializeJson(allConvs, raw) || !allConvs.is<JsonArray>()) {
        return "[]";
    }

    DynamicJsonDocument result(4096);
    JsonArray arr = result.to<JsonArray>();
    for (JsonObject c : allConvs.as<JsonArray>()) {
        JsonArray participants = c["participants"];
        bool isMember = false;
        for (JsonVariant p : participants) {
            if (p.as<int>() == userId) { isMember = true; break; }
        }
        if (!isMember) continue;

        // Find contact name (the other participant)
        String contactName = c["name"] | "Chat";
        for (JsonVariant p : participants) {
            if (p.as<int>() != userId) {
                contactName = getUsernameById(p.as<int>());
                break;
            }
        }

        JsonObject entry = arr.createNestedObject();
        entry["conversation_id"] = c["id"] | 0;
        entry["contact_name"]    = contactName;
        entry["last_message"]    = c["last_message"] | "";
        entry["timestamp"]       = c["last_timestamp"] | "";
    }

    String out;
    serializeJson(result, out);
    return out;
}

// Get messages for a conversation
String getChatMessages(int conversationId, int userId) {
    String raw = readFile(CHAT_MESSAGES_FILE);
    DynamicJsonDocument allMsgs(16384);
    if (deserializeJson(allMsgs, raw) || !allMsgs.is<JsonArray>()) {
        return "[]";
    }

    DynamicJsonDocument result(8192);
    JsonArray arr = result.to<JsonArray>();
    for (JsonObject m : allMsgs.as<JsonArray>()) {
        if ((m["conversation_id"] | -1) != conversationId) continue;
        int senderId = m["sender_id"] | 0;
        JsonObject entry = arr.createNestedObject();
        entry["message_id"]          = m["id"] | 0;
        entry["message_text"]        = m["message_text"] | "";
        entry["sent_at"]             = m["sent_at"] | "";
        entry["sender_id"]           = senderId;
        entry["is_from_current_user"] = (senderId == userId);
        entry["sender_username"]     = getUsernameById(senderId);
    }

    String out;
    serializeJson(result, out);
    return out;
}

// Add a chat message
bool addChatMessage(int conversationId, int senderId, const String& text) {
    // Add to chat_messages.json
    // Note: sent_at uses uptime seconds (millis()/1000), not wall-clock time.
    // Absolute timestamps require NTP which is not available on all deployments.
    String raw = readFile(CHAT_MESSAGES_FILE);
    DynamicJsonDocument doc(16384);
    DeserializationError err = deserializeJson(doc, raw);
    JsonArray arr = (err || !doc.is<JsonArray>()) ? doc.to<JsonArray>() : doc.as<JsonArray>();

    int newId = 1;
    for (JsonObject m : arr) {
        int id = m["id"] | 0;
        if (id >= newId) newId = id + 1;
    }

    String ts = String(millis() / 1000);
    JsonObject msg = arr.createNestedObject();
    msg["id"]              = newId;
    msg["conversation_id"] = conversationId;
    msg["sender_id"]       = senderId;
    msg["message_text"]    = text;
    msg["sent_at"]         = ts;

    String out;
    serializeJson(doc, out);
    if (!writeFile(CHAT_MESSAGES_FILE, out)) return false;

    // Update last_message in conversation
    String convRaw = readFile(CONVERSATIONS_FILE);
    DynamicJsonDocument convDoc(8192);
    if (!deserializeJson(convDoc, convRaw) && convDoc.is<JsonArray>()) {
        for (JsonObject c : convDoc.as<JsonArray>()) {
            if ((c["id"] | -1) == conversationId) {
                c["last_message"]   = text;
                c["last_timestamp"] = ts;
                break;
            }
        }
        String convOut;
        serializeJson(convDoc, convOut);
        writeFile(CONVERSATIONS_FILE, convOut);
    }

    return true;
}

// Find or create a conversation between two users
int findOrCreateConversation(int user1, int user2) {
    String raw = readFile(CONVERSATIONS_FILE);
    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, raw);
    JsonArray arr = (err || !doc.is<JsonArray>()) ? doc.to<JsonArray>() : doc.as<JsonArray>();

    // Search for existing conversation
    for (JsonObject c : arr) {
        JsonArray p = c["participants"];
        if (p.size() == 2) {
            int a = p[0] | 0, b = p[1] | 0;
            if ((a == user1 && b == user2) || (a == user2 && b == user1)) {
                return c["id"] | 0;
            }
        }
    }

    // Create new conversation
    int newId = 1;
    for (JsonObject c : arr) {
        int id = c["id"] | 0;
        if (id >= newId) newId = id + 1;
    }

    JsonObject conv = arr.createNestedObject();
    conv["id"] = newId;
    JsonArray participants = conv.createNestedArray("participants");
    participants.add(user1);
    participants.add(user2);
    conv["name"]           = "";
    conv["last_message"]   = "";
    conv["last_timestamp"] = "";

    String out;
    serializeJson(doc, out);
    writeFile(CONVERSATIONS_FILE, out);
    return newId;
}

// Find or create the SOS conversation (user to admin)
int findOrCreateSosConversation(int userId) {
    return findOrCreateConversation(userId, ADMIN_USER_ID);
}

// ========================================
// Statistics
// ========================================
void updateStats() {
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
    {
        String raw = readFile(MESSAGES_FILE);
        DynamicJsonDocument doc(16384);
        if (!deserializeJson(doc, raw) && doc.is<JsonArray>()) {
            totalMessages = doc.as<JsonArray>().size();
        }
    }
}

void saveStats() {
    if (!storageAvailable) return;
    StaticJsonDocument<256> doc;
    doc["fog_nodes_count"]  = fogNodesCount;
    doc["active_fog_nodes"] = activeFogNodes;
    doc["total_messages"]   = totalMessages;
    String out;
    serializeJson(doc, out);
    writeFile(STATS_FILE, out);
}

// ========================================
// XBee API mode 1 helpers
// ========================================

// Build and send a 0x10 Transmit Request frame (broadcast)
// Frame: 0x7E | LenHi LenLo | 0x10 FrameID Dest64[8] Dest16[2] Radius Options | payload | Checksum
uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    if (len == 0 || len > XBEE_MAX_FRAME - XBEE_TX_HDR_SIZE) {
        Serial.printf("[XBEE] TX payload too large (%d bytes, max %d)\n",
                      len, XBEE_MAX_FRAME - XBEE_TX_HDR_SIZE);
        return 0;
    }

    uint16_t frameDataLen = XBEE_TX_HDR_SIZE + len;
    // Frame ID 0 disables TX status responses per XBee spec; keep IDs 1-255
    if (++frameIdCounter == 0) frameIdCounter = 1;
    uint8_t fid = frameIdCounter;

    uint8_t hdr[XBEE_TX_HDR_SIZE] = {
        XBEE_TX_REQUEST,            // [0]  frame type
        fid,                        // [1]  frame ID
        0x00, 0x00, 0x00, 0x00,     // [2-5]  64-bit dest high
        0x00, 0x00, 0xFF, 0xFF,     // [6-9]  64-bit dest low (broadcast)
        0xFF, 0xFE,                 // [10-11] 16-bit dest (broadcast)
        0x00,                       // [12] broadcast radius
        0x00                        // [13] options
    };

    // Checksum = 0xFF - (sum of all frame data bytes)
    uint8_t cksum = 0;
    for (int i = 0; i < XBEE_TX_HDR_SIZE; i++) cksum += hdr[i];
    for (size_t i = 0; i < len; i++) cksum += (uint8_t)payload[i];
    cksum = 0xFF - cksum;

    // Write frame
    xbeeSerial.write(XBEE_START_DELIM);
    xbeeSerial.write((uint8_t)(frameDataLen >> 8));   // length MSB
    xbeeSerial.write((uint8_t)(frameDataLen & 0xFF)); // length LSB
    xbeeSerial.write(hdr, XBEE_TX_HDR_SIZE);          // frame header
    xbeeSerial.write((const uint8_t*)payload, len);   // RF data
    xbeeSerial.write(cksum);                          // checksum
    xbeeSerial.flush();

    return fid;
}

void xbeeSend(const String& json) {
    xbeeSendBroadcast(json.c_str(), json.length());
    Serial.printf("[XBEE-TX] %s\n", json.c_str());
}

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

void xbeeRegister() {
    StaticJsonDocument<256> params;
    JsonObject p = params.to<JsonObject>();
    p["ip_address"]       = WiFi.localIP().toString();
    p["device_name"]      = NODE_ID;
    p["status"]           = "active";
    p["free_heap"]        = ESP.getFreeHeap();
    p["storage_available"] = storageAvailable;
    xbeeSendCommand("REGISTER", &p);
}

void xbeeHeartbeat() {
    StaticJsonDocument<256> params;
    JsonObject p = params.to<JsonObject>();
    p["ip_address"]       = WiFi.localIP().toString();
    p["uptime"]           = millis() / 1000;
    p["free_heap"]        = ESP.getFreeHeap();
    p["fog_nodes"]        = fogNodesCount;
    p["messages"]         = totalMessages;
    p["storage_available"] = storageAvailable;
    xbeeSendCommand("HEARTBEAT", &p);
}

void xbeeRequestSync() {
    xbeeSendCommand("SYNC_REQUEST");
}

void xbeeRelayMessage(const String& from, const String& to, const String& message) {
    StaticJsonDocument<512> params;
    JsonObject p = params.to<JsonObject>();
    p["from"]    = from;
    p["to"]      = to;
    p["message"] = message;
    xbeeSendCommand("RELAY_MSG", &p);
}

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
    Serial.printf("[XBEE-RX] (%d bytes) %s\n", line.length(), line.c_str());

    // Use DynamicJsonDocument large enough for SYNC_DATA payloads
    // (users + announcements + conversations + chat_messages + fog_nodes + messages)
    DynamicJsonDocument doc(16384);
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        Serial.printf("[XBEE] JSON parse error: %s (input %d bytes)\n",
                      err.c_str(), line.length());
        return;
    }

    const char* cmd = doc["cmd"];
    if (!cmd) return;

    String command(cmd);

    if (command == "PONG") {
        Serial.println("[XBEE] Admin responded to heartbeat");
    }
    else if (command == "REGISTER_ACK") {
        if (!registeredWithAdmin) {
            registeredWithAdmin = true;
            Serial.println("[XBEE] Registered with admin successfully");
            // Trigger immediate data sync after first registration
            xbeeRequestSync();
        }
    }
    else if (command == "SYNC_DATA") {
        int synced = 0;
        if (doc.containsKey("fog_nodes")) {
            String nodesStr;
            serializeJson(doc["fog_nodes"], nodesStr);
            writeFile(FOG_NODES_FILE, nodesStr);
            synced++;
        }
        if (doc.containsKey("messages")) {
            String msgsStr;
            serializeJson(doc["messages"], msgsStr);
            writeFile(MESSAGES_FILE, msgsStr);
            synced++;
        }
        // Mobile app data sync
        if (doc.containsKey("users")) {
            String usersStr;
            serializeJson(doc["users"], usersStr);
            writeFile(USERS_FILE, usersStr);
            synced++;
        }
        if (doc.containsKey("conversations")) {
            String convsStr;
            serializeJson(doc["conversations"], convsStr);
            writeFile(CONVERSATIONS_FILE, convsStr);
            synced++;
        }
        if (doc.containsKey("chat_messages")) {
            String chatStr;
            serializeJson(doc["chat_messages"], chatStr);
            writeFile(CHAT_MESSAGES_FILE, chatStr);
            synced++;
        }
        if (doc.containsKey("announcements")) {
            String annStr;
            serializeJson(doc["announcements"], annStr);
            writeFile(ANNOUNCEMENTS_FILE, annStr);
            synced++;
        }
        updateStats();
        saveStats();
        Serial.printf("[XBEE] Data sync complete (%d collections, doc %d/%d bytes)\n",
                      synced, doc.memoryUsage(), doc.capacity());
    }
    else if (command == "BROADCAST_MSG") {
        const char* from    = doc["params"]["from"];
        const char* to      = doc["params"]["to"];
        const char* message = doc["params"]["message"];
        if (from && to && message) {
            addMessage(from, to, message);
            Serial.println("[XBEE] Broadcast message stored locally");
        }
    }
    else if (command == "ADD_FOG_NODE") {
        const char* name   = doc["params"]["device_name"];
        const char* ip     = doc["params"]["ip_address"];
        const char* status = doc["params"]["status"];
        if (name && ip) {
            addFogNode(name, ip, status ? status : "active");
            Serial.println("[XBEE] Fog node added from admin");
        }
    }
    else if (command == "RELAY_CHAT_MSG") {
        // Admin relays a chat message from a mobile user on the admin side.
        // Fields are nested under "params" (consistent with all other commands).
        JsonObject p = doc["params"];
        int convId   = p["conversation_id"] | 0;
        int senderId = p["sender_id"]       | 0;
        const char* text = p["message_text"];
        if (!text) text = p["message"];
        if (convId > 0 && senderId > 0 && text) {
            addChatMessage(convId, senderId, String(text));
            Serial.println("[XBEE] Chat message relayed from admin");
        }
    }
    else if (command == "SOS_ALERT") {
        // Admin relays an SOS alert.
        // Fields are nested under "params" (consistent with all other commands).
        JsonObject p = doc["params"];
        int userId = p["user_id"] | 0;
        if (userId > 0) {
            int convId = findOrCreateSosConversation(userId);
            Serial.printf("[XBEE] SOS alert for user %d (conv %d)\n", userId, convId);
        }
    }
    else if (command == "GET_STATS") {
        // Only respond if the request is addressed to this node (or to all)
        if (doc.containsKey("node_id") && doc["node_id"].is<const char*>()) {
            const char* targetNode = doc["node_id"];
            if (strcmp(targetNode, NODE_ID) != 0
                && strcmp(targetNode, "all") != 0) {
                return;  // not for us
            }
        }
        StaticJsonDocument<512> resp;
        resp["cmd"]              = "STATS_RESPONSE";
        resp["node_id"]          = NODE_ID;
        resp["fog_nodes_count"]  = fogNodesCount;
        resp["active_fog_nodes"] = activeFogNodes;
        resp["total_messages"]   = totalMessages;
        resp["free_heap"]        = ESP.getFreeHeap();
        resp["uptime"]           = millis() / 1000;
        resp["ip_address"]       = WiFi.localIP().toString();
        resp["storage_available"] = storageAvailable;
        String out;
        serializeJson(resp, out);
        xbeeSend(out);
    }
    else {
        Serial.printf("[XBEE] Unknown command: %s\n", cmd);
    }
}

// ========================================
// XBee API mode 1 receive state machine
// ========================================
void xbeeProcessIncoming() {
    while (xbeeSerial.available()) {
        uint8_t b = xbeeSerial.read();

        switch (rxState) {
        case WAIT_DELIM:
            if (b == XBEE_START_DELIM) rxState = GOT_LEN_HI;
            break;

        case GOT_LEN_HI:
            rxFrameLen = (uint16_t)b << 8;
            rxState = GOT_LEN_LO;
            break;

        case GOT_LEN_LO:
            rxFrameLen |= b;
            rxIdx = 0;
            rxChecksum = 0;
            if (rxFrameLen > 0 && rxFrameLen <= XBEE_MAX_FRAME) {
                rxState = READING_DATA;
            } else {
                Serial.printf("[XBEE] Invalid frame length %d, ignoring\n", rxFrameLen);
                rxState = WAIT_DELIM;
            }
            break;

        case READING_DATA:
            rxFrame[rxIdx++] = b;
            rxChecksum += b;
            if (rxIdx >= rxFrameLen) rxState = GOT_CHECKSUM;
            break;

        case GOT_CHECKSUM:
            rxChecksum += b;
            if (rxChecksum == 0xFF) {
                // Valid frame
                uint8_t frameType = rxFrame[0];

                if (frameType == XBEE_RX_PACKET && rxFrameLen > XBEE_RX_HDR_SIZE) {
                    // 0x90 Receive Packet:
                    //   [0]       0x90
                    //   [1-8]     64-bit source address
                    //   [9-10]    16-bit source address
                    //   [11]      receive options
                    //   [12..]    RF data (the JSON payload)
                    size_t rfLen = rxFrameLen - XBEE_RX_HDR_SIZE;

                    // Strip trailing newline/CR if present
                    while (rfLen > 0 && (rxFrame[XBEE_RX_HDR_SIZE + rfLen - 1] == '\n'
                                      || rxFrame[XBEE_RX_HDR_SIZE + rfLen - 1] == '\r'))
                        rfLen--;

                    if (rfLen > 0) {
                        rxFrame[XBEE_RX_HDR_SIZE + rfLen] = '\0';
                        handleXBeeData(String((const char*)&rxFrame[XBEE_RX_HDR_SIZE]));
                    }
                }
                else if (frameType == XBEE_TX_STATUS && rxFrameLen >= 7) {
                    // 0x8B Transmit Status — byte [5] is delivery status
                    uint8_t delivery = rxFrame[5];
                    if (delivery != 0) {
                        Serial.printf("[XBEE] TX delivery failed (0x%02X)\n", delivery);
                    }
                }
            } else {
                Serial.println("[XBEE] Frame checksum error");
            }
            rxState = WAIT_DELIM;
            break;
        }
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
    doc["node_id"]           = NODE_ID;
    doc["fog_nodes_count"]   = fogNodesCount;
    doc["active_fog_nodes"]  = activeFogNodes;
    doc["total_messages"]    = totalMessages;
    doc["ip_address"]        = WiFi.localIP().toString();
    doc["free_heap"]         = ESP.getFreeHeap();
    doc["uptime"]            = millis() / 1000;
    doc["storage_available"] = storageAvailable;
    if (storageAvailable) {
        populateStorageStats(doc);
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
        xbeeRelayMessage(from, to, message);
        server.send(200, "application/json",
                    "{\"success\":true,\"message\":\"Message stored and relayed to admin\"}");
    } else {
        server.send(500, "application/json",
                    "{\"success\":false,\"message\":\"Failed to store message\"}");
    }
}

void handleRelay() {
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

// ========================================
// Mobile app API handlers
// ========================================

// POST /login – mobile app login
void handleMobileLogin() {
    DynamicJsonDocument body(512);
    if (!parseJsonBody(body)) {
        server.send(400, "application/json",
                    "{\"success\":false,\"message\":\"Invalid JSON body\"}");
        return;
    }
    String username = body["username"] | "";
    if (username.isEmpty()) {
        server.send(400, "application/json",
                    "{\"success\":false,\"message\":\"Missing username\"}");
        return;
    }

    DynamicJsonDocument usersDoc(8192);
    JsonObject user = findUser(usersDoc, username);
    if (user.isNull()) {
        server.send(401, "application/json",
                    "{\"success\":false,\"message\":\"Invalid credentials\"}");
        return;
    }

    StaticJsonDocument<512> resp;
    resp["success"] = true;
    JsonObject u = resp.createNestedObject("user");
    u["user_id"]       = user["id"] | 0;
    u["username"]      = user["username"] | "";
    u["has_agreed_sos"] = user["has_agreed_sos"] | false;
    String out;
    serializeJson(resp, out);
    server.send(200, "application/json", out);
}

// GET /status – server status
void handleMobileStatus() {
    server.send(200, "application/json", "{\"online\":true}");
}

// GET /conversations – list conversations for a user
void handleMobileConversations() {
    int userId = server.arg("user_id").toInt();
    if (userId <= 0) {
        server.send(400, "application/json", "[]");
        return;
    }
    server.send(200, "application/json", getConversationsForUser(userId));
}

// GET /messages – get messages for a conversation
void handleMobileMessages() {
    int conversationId = server.arg("conversation_id").toInt();
    int userId         = server.arg("user_id").toInt();
    if (conversationId <= 0) {
        server.send(400, "application/json", "[]");
        return;
    }
    server.send(200, "application/json", getChatMessages(conversationId, userId));
}

// POST /send – send a chat message
void handleMobileSend() {
    DynamicJsonDocument body(512);
    if (!parseJsonBody(body)) {
        server.send(400, "application/json",
                    "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
    }
    int conversationId = body["conversation_id"] | 0;
    int senderId       = body["sender_id"] | 0;
    String text        = body["message_text"] | "";
    if (conversationId <= 0 || senderId <= 0 || text.isEmpty()) {
        server.send(400, "application/json",
                    "{\"success\":false,\"message\":\"Missing fields\"}");
        return;
    }

    if (addChatMessage(conversationId, senderId, text)) {
        // Relay to admin via XBee
        StaticJsonDocument<512> params;
        JsonObject p = params.to<JsonObject>();
        p["conversation_id"] = conversationId;
        p["sender_id"]       = senderId;
        p["message_text"]    = text;
        xbeeSendCommand("RELAY_CHAT_MSG", &p);
        server.send(200, "application/json",
                    "{\"success\":true,\"message\":\"sent\",\"secondsRemaining\":0}");
    } else {
        server.send(500, "application/json",
                    "{\"success\":false,\"message\":\"Failed to store\"}");
    }
}

// GET /users – list all users (excluding current)
void handleMobileUsers() {
    int currentUserId = server.arg("user_id").toInt();
    String raw = readFile(USERS_FILE);
    DynamicJsonDocument allUsers(8192);
    if (deserializeJson(allUsers, raw) || !allUsers.is<JsonArray>()) {
        server.send(200, "application/json", "[]");
        return;
    }
    DynamicJsonDocument result(4096);
    JsonArray arr = result.to<JsonArray>();
    for (JsonObject u : allUsers.as<JsonArray>()) {
        int uid = u["id"] | 0;
        if (uid == currentUserId) continue;
        if (!(u["is_active"] | true)) continue;
        JsonObject entry = arr.createNestedObject();
        entry["id"]       = uid;
        entry["username"] = u["username"] | "";
    }
    String out;
    serializeJson(result, out);
    server.send(200, "application/json", out);
}

// POST /create-chat – find or create a chat with another user
void handleMobileCreateChat() {
    DynamicJsonDocument body(256);
    if (!parseJsonBody(body)) {
        server.send(400, "application/json",
                    "{\"error\":\"Invalid JSON\"}");
        return;
    }
    int user1 = body["user1_id"] | 0;
    int user2 = body["user2_id"] | 0;
    if (user1 <= 0 || user2 <= 0) {
        server.send(400, "application/json",
                    "{\"error\":\"Missing user IDs\"}");
        return;
    }
    int convId = findOrCreateConversation(user1, user2);
    String contactName = getUsernameById(user2);
    StaticJsonDocument<256> resp;
    resp["conversation_id"] = convId;
    resp["contact_name"]    = contactName;
    String out;
    serializeJson(resp, out);
    server.send(200, "application/json", out);
}

// POST /sos – find or create an SOS chat
void handleMobileSos() {
    DynamicJsonDocument body(256);
    if (!parseJsonBody(body)) {
        server.send(400, "application/json",
                    "{\"error\":\"Invalid JSON\"}");
        return;
    }
    int userId = body["user_id"] | 0;
    if (userId <= 0) {
        server.send(400, "application/json",
                    "{\"error\":\"Missing user_id\"}");
        return;
    }
    int convId = findOrCreateSosConversation(userId);
    StaticJsonDocument<256> resp;
    resp["conversation_id"] = convId;
    resp["contact_name"]    = "Admin (SOS)";
    String out;
    serializeJson(resp, out);

    // Relay SOS to admin via XBee
    StaticJsonDocument<256> params;
    JsonObject p = params.to<JsonObject>();
    p["user_id"]         = userId;
    p["conversation_id"] = convId;
    xbeeSendCommand("SOS_ALERT", &p);

    server.send(200, "application/json", out);
}

// GET /new-messages – get messages newer than last_id
void handleMobileNewMessages() {
    int lastId = server.arg("last_id").toInt();
    int userId = server.arg("user_id").toInt();
    String raw = readFile(CHAT_MESSAGES_FILE);
    DynamicJsonDocument allMsgs(16384);
    if (deserializeJson(allMsgs, raw) || !allMsgs.is<JsonArray>()) {
        server.send(200, "application/json", "[]");
        return;
    }
    DynamicJsonDocument result(8192);
    JsonArray arr = result.to<JsonArray>();
    for (JsonObject m : allMsgs.as<JsonArray>()) {
        if ((m["id"] | 0) <= lastId) continue;
        int senderId = m["sender_id"] | 0;
        JsonObject entry = arr.createNestedObject();
        entry["message_id"]          = m["id"] | 0;
        entry["message_text"]        = m["message_text"] | "";
        entry["sent_at"]             = m["sent_at"] | "";
        entry["sender_id"]           = senderId;
        entry["is_from_current_user"] = (senderId == userId);
        entry["sender_username"]     = getUsernameById(senderId);
    }
    String out;
    serializeJson(result, out);
    server.send(200, "application/json", out);
}

// POST /agree-sos – mark user as agreed to SOS terms
void handleMobileAgreeSos() {
    DynamicJsonDocument body(256);
    if (!parseJsonBody(body)) {
        server.send(400, "application/json",
                    "{\"success\":false}");
        return;
    }
    int userId = body["user_id"] | 0;
    if (userId <= 0) {
        server.send(400, "application/json",
                    "{\"success\":false}");
        return;
    }
    // Update user's has_agreed_sos flag
    String raw = readFile(USERS_FILE);
    DynamicJsonDocument doc(8192);
    if (!deserializeJson(doc, raw) && doc.is<JsonArray>()) {
        for (JsonObject u : doc.as<JsonArray>()) {
            if ((u["id"] | 0) == userId) {
                u["has_agreed_sos"] = true;
                break;
            }
        }
        String out;
        serializeJson(doc, out);
        writeFile(USERS_FILE, out);
    }
    server.send(200, "application/json", "{\"success\":true}");
}

// POST /change-password – change user password (relay to admin)
void handleMobileChangePassword() {
    DynamicJsonDocument body(512);
    if (!parseJsonBody(body)) {
        server.send(400, "application/json",
                    "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
    }
    int userId = body["user_id"] | 0;
    if (userId <= 0) {
        server.send(400, "application/json",
                    "{\"success\":false,\"message\":\"Missing user_id\"}");
        return;
    }
    // Relay to admin – password changes happen on admin
    StaticJsonDocument<512> params;
    JsonObject p = params.to<JsonObject>();
    p["user_id"]      = userId;
    p["old_password"] = body["old_password"] | "";
    p["new_password"] = body["new_password"] | "";
    xbeeSendCommand("CHANGE_PASSWORD", &p);
    server.send(200, "application/json",
                "{\"success\":true,\"message\":\"Password change relayed to admin\"}");
}

// GET /announcements – list announcements
void handleMobileAnnouncements() {
    server.send(200, "application/json", readFile(ANNOUNCEMENTS_FILE));
}

void handleNotFound() {
    server.send(404, "application/json", "{\"error\":\"Not Found\"}");
}

// ========================================
// Setup
// ========================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n\nHopFog Node – Range Extender");
    Serial.println("============================");
    Serial.println("[XBEE] Using API mode 1 (binary framed packets)");

#ifdef ARDUINO_ARCH_ESP32
    Serial.println("[BOARD] ESP32-CAM (AI-Thinker)");
    xbeeSerial.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);
#elif defined(ARDUINO_ARCH_ESP8266)
    Serial.println("[BOARD] Wemos D1 Mini (ESP8266)");
    xbeeSerial.begin(XBEE_BAUD);
#endif
    Serial.printf("[XBEE] Started at %d baud (RX=%d, TX=%d)\n",
                  XBEE_BAUD, XBEE_RX_PIN, XBEE_TX_PIN);

    // Storage
    storageAvailable = initStorage();
    if (storageAvailable) {
        initDatabase();
    } else {
        Serial.println("[STORAGE] Running without persistent storage");
    }

    // WiFi – AP+STA mode (AP for mobile clients, STA for backhaul)
    WiFi.mode(WIFI_AP_STA);

    // Start Access Point
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);
    Serial.printf("[WIFI-AP] SSID: %s  IP: %s\n",
                  AP_SSID, WiFi.softAPIP().toString().c_str());

    // Start DNS server – resolves hopfog.com to this node's AP IP
    dnsServer.start(53, DNS_DOMAIN, WiFi.softAPIP());
    Serial.printf("[DNS] %s → %s\n",
                  DNS_DOMAIN, WiFi.softAPIP().toString().c_str());

    // Connect to upstream WiFi (for XBee backhaul / admin access)
    Serial.printf("[WIFI-STA] Connecting to %s ", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WIFI-STA] Connected – IP: %s\n",
                      WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WIFI-STA] Connection failed – AP still works");
    }

    // Admin device management API
    server.on("/api/health",               HTTP_GET,  handleHealth);
    server.on("/api/stats",                HTTP_GET,  handleStats);
    server.on("/api/fog-devices",          HTTP_GET,  handleGetFogNodes);
    server.on("/api/fog-devices/register", HTTP_POST, handleAddFogNode);
    server.on("/api/messages",             HTTP_GET,  handleGetMessages);
    server.on("/api/messages",             HTTP_POST, handleAddMessage);
    server.on("/api/xbee/broadcast",       HTTP_POST, handleRelay);

    // Mobile app API – same endpoints as hopfog.com
    server.on("/login",           HTTP_POST, handleMobileLogin);
    server.on("/status",          HTTP_GET,  handleMobileStatus);
    server.on("/conversations",   HTTP_GET,  handleMobileConversations);
    server.on("/messages",        HTTP_GET,  handleMobileMessages);
    server.on("/send",            HTTP_POST, handleMobileSend);
    server.on("/users",           HTTP_GET,  handleMobileUsers);
    server.on("/create-chat",     HTTP_POST, handleMobileCreateChat);
    server.on("/sos",             HTTP_POST, handleMobileSos);
    server.on("/new-messages",    HTTP_GET,  handleMobileNewMessages);
    server.on("/agree-sos",       HTTP_POST, handleMobileAgreeSos);
    server.on("/change-password", HTTP_POST, handleMobileChangePassword);
    server.on("/announcements",   HTTP_GET,  handleMobileAnnouncements);

    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("[HTTP] API server started");

    // Send first REGISTER attempt via XBee (retries happen in loop)
    xbeeRegister();
    lastRegister = millis();

    Serial.println("============================\n");
}

// ========================================
// Loop
// ========================================
void loop() {
    dnsServer.processNextRequest();
    server.handleClient();

    // ---- XBee receive (API mode 1 frame parser) ----
    xbeeProcessIncoming();

    unsigned long now = millis();

    // ---- Registration retry (until admin ACKs) ----
    if (!registeredWithAdmin) {
        if (now - lastRegister >= REGISTER_RETRY_MS) {
            lastRegister = now;
            Serial.println("[XBEE] Retrying REGISTER with admin...");
            xbeeRegister();
        }
    }

    // ---- Heartbeat & Sync (only after registration confirmed) ----
    if (registeredWithAdmin) {
        if (now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
            lastHeartbeat = now;
            if (WiFi.status() == WL_CONNECTED) {
                xbeeHeartbeat();
            } else {
                WiFi.mode(WIFI_AP_STA);
                WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            }
        }

        if (now - lastSync >= SYNC_INTERVAL_MS) {
            lastSync = now;
            xbeeRequestSync();
        }
    }
}
