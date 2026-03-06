#ifndef CONFIG_H
#define CONFIG_H

// ── WiFi Access Point ───────────────────────────────────────────────
// Each node creates its own WiFi network.
// Mobile phones connect to this to access the local API.
#define AP_SSID       "HopFog-Node-01"    // Change for each node!
#define AP_PASSWORD   "changeme123"
#define AP_CHANNEL    6                    // Use different channel from admin (1)
#define AP_MAX_CONN   4

// ── Web Server ──────────────────────────────────────────────────────
#define HTTP_PORT 80

// ── Node Identity ───────────────────────────────────────────────────
#define NODE_ID       "node-01"           // Unique ID for this node
#define DEVICE_NAME   "HopFog-Node-01"    // Human-readable name

// ── SD Card (ESP32-CAM built-in slot, 1-bit SD_MMC mode) ───────────
#define SD_DB_DIR           "/db"
#define SD_USERS_FILE       "/db/users.json"
#define SD_ANNOUNCE_FILE    "/db/announcements.json"
#define SD_CONVOS_FILE      "/db/conversations.json"
#define SD_DMS_FILE         "/db/direct_messages.json"
#define SD_FOG_FILE         "/db/fog_devices.json"
#define SD_MSGS_FILE        "/db/messages.json"

// ── XBee S2C (ZigBee) ──────────────────────────────────────────────
// Uses UART2 (Serial2) so UART0 (Serial) stays free for Serial Monitor.
#define XBEE_BAUD       9600
#define XBEE_TX_PIN     13    // ESP32 TX → XBee DIN  (pin 3)
#define XBEE_RX_PIN     12    // ESP32 RX ← XBee DOUT (pin 2)

// ── Timing ─────────────────────────────────────────────────────────
#define REGISTER_INTERVAL_MS   10000   // Send REGISTER every 10s until ACK
#define HEARTBEAT_INTERVAL_MS  30000   // Send HEARTBEAT every 30s after registered
#define SYNC_RETRY_MS          15000   // Retry SYNC_REQUEST if no response

// ── JSON buffer ────────────────────────────────────────────────────
#ifdef BOARD_HAS_PSRAM
  #define JSON_DOC_SIZE  16384
#else
  #define JSON_DOC_SIZE   8192
#endif

#endif // CONFIG_H
