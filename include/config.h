#ifndef CONFIG_H
#define CONFIG_H

// ── WiFi Access Point ───────────────────────────────────────────────
#define AP_SSID       "HopFog-Node-01"
#define AP_PASSWORD   "changeme123"
#define AP_CHANNEL    6
#define AP_MAX_CONN   4

// ── Flash LED ───────────────────────────────────────────────────────
// GPIO 4 is the ESP32-CAM flash LED (active HIGH).  Driven LOW at boot
// to keep the bright white LED off.
#define FLASH_LED_PIN     4

// ── Status LED ──────────────────────────────────────────────────────
// GPIO 33 is the ESP32-CAM on-board red LED (active LOW).
#define STATUS_LED_PIN    33

// ── Web Server ──────────────────────────────────────────────────────
#define HTTP_PORT 80

// ── DNS / Captive Portal ────────────────────────────────────────────
#define DNS_PORT          53
#define CAPTIVE_DOMAIN    "hopfog.com"

// ── Node Identity ───────────────────────────────────────────────────
#define NODE_ID       "node-01"
#define DEVICE_NAME   "Node01"          // Short — ZigBee broadcast limit is ~84 bytes

// ── SD Card (ESP32-CAM built-in slot — SPI mode) ────────────────────
//
// Uses SPI (NOT SD_MMC) to avoid GPIO conflicts.
//
// ESP32-CAM SD slot hardware wiring:
//   CS   = GPIO 13
//   CLK  = GPIO 14
//   MISO = GPIO 2
//   MOSI = GPIO 15
//
#define SD_CS_PIN       13
#define SD_SPI_CLK      14
#define SD_SPI_MISO      2
#define SD_SPI_MOSI     15

#define SD_DB_DIR           "/db"
#define SD_USERS_FILE       "/db/users.json"
#define SD_ANNOUNCE_FILE    "/db/announcements.json"
#define SD_CONVOS_FILE      "/db/conversations.json"
#define SD_DMS_FILE         "/db/direct_messages.json"
#define SD_FOG_FILE         "/db/fog_devices.json"
#define SD_MSGS_FILE        "/db/messages.json"

// ── XBee S2C (ZigBee) ──────────────────────────────────────────────
// Uses UART0 (Serial) on native IOMUX pins.
//   GPIO 1 = U0TXD → XBee DIN
//   GPIO 3 = U0RXD ← XBee DOUT
//
// USB Serial Monitor is NOT available when XBee is connected.
// All debug output is compiled out via dbgprintf/dbgprintln macros.
//
// ⚠️ Disconnect the XBee before uploading firmware.
#define XBEE_BAUD       9600
#define XBEE_TX_PIN     1
#define XBEE_RX_PIN     3

// ── Timing ─────────────────────────────────────────────────────────
#define REGISTER_INTERVAL_MS   10000
#define HEARTBEAT_INTERVAL_MS  30000
#define SYNC_RETRY_MS          15000

// ── JSON buffer ────────────────────────────────────────────────────
#ifdef BOARD_HAS_PSRAM
  #define JSON_DOC_SIZE  16384
#else
  #define JSON_DOC_SIZE   8192
#endif

// ── Debug output macros ──────────────────────────────────────────────
// Always disabled — UART0 is used for XBee, no USB Serial Monitor.
#define dbgprintf(...)     do {} while(0)
#define dbgprintln(x)      do {} while(0)

#endif // CONFIG_H
