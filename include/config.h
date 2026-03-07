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

// ── Web Server ──────────────────────────────────────────────────────
#define HTTP_PORT 80

// ── DNS / Captive Portal ────────────────────────────────────────────
// A local DNS server resolves ALL queries to the AP IP (192.168.4.1).
// This lets mobile users type "hopfog.com" in a browser instead of
// remembering the numeric IP.  Also triggers the OS captive-portal
// detection on Android / iOS, popping up the login page automatically.
#define DNS_PORT          53
#define CAPTIVE_DOMAIN    "hopfog.com"

// ── Node Identity ───────────────────────────────────────────────────
#define NODE_ID       "node-01"
#define DEVICE_NAME   "HopFog-Node-01"

// ── SD Card (ESP32-CAM built-in slot — SPI mode) ────────────────────
//
// Uses SPI (NOT SD_MMC) to avoid GPIO conflicts.  SD_MMC permanently
// claims GPIO 12/13 via IOMUX even in 1-bit mode.
//
// ESP32-CAM SD slot hardware wiring:
//   CS   = GPIO 13 (was DAT3 in SDMMC mode)
//   CLK  = GPIO 14
//   MISO = GPIO 2  (was DAT0)
//   MOSI = GPIO 15 (was CMD)
//
#ifdef ESP32CAM_SPI_SD
  #define SD_CS_PIN       13
  #define SD_SPI_CLK      14
  #define SD_SPI_MISO      2
  #define SD_SPI_MOSI     15
#endif

#define SD_DB_DIR           "/db"
#define SD_USERS_FILE       "/db/users.json"
#define SD_ANNOUNCE_FILE    "/db/announcements.json"
#define SD_CONVOS_FILE      "/db/conversations.json"
#define SD_DMS_FILE         "/db/direct_messages.json"
#define SD_FOG_FILE         "/db/fog_devices.json"
#define SD_MSGS_FILE        "/db/messages.json"

// ── XBee S2C (ZigBee) ──────────────────────────────────────────────
// Uses UART0 (Serial) on native IOMUX pins — most reliable option.
//   GPIO 1 = U0TXD → XBee DIN  (pin 3 on XBee module)
//   GPIO 3 = U0RXD ← XBee DOUT (pin 2 on XBee module)
//   IOMUX native — no GPIO matrix remapping, no conflicts.
//
// Trade-off: USB Serial Monitor is NOT available.
// All debug output is compiled out via dbgprintf/dbgprintln macros.
// Use the admin web serial monitor (/admin/messaging/testing) instead.
//
// ⚠️ GPIO 1/3 are the USB programming pins.
//    Disconnect the XBee before uploading firmware.
#define XBEE_BAUD       9600
#define XBEE_TX_PIN     1     // U0TXD → XBee DIN (IOMUX native)
#define XBEE_RX_PIN     3     // U0RXD ← XBee DOUT (IOMUX native)
#define XBEE_USES_UART0 1     // UART0 is XBee — serial debug disabled

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
// Disabled when UART0 is used for XBee (no USB Serial Monitor available).
// All debug output goes through these macros so the compiler can strip it.
#ifdef XBEE_USES_UART0
  #define dbgprintf(...)     do {} while(0)
  #define dbgprintln(x)      do {} while(0)
#else
  #define dbgprintf(...)     Serial.printf(__VA_ARGS__)
  #define dbgprintln(x)      Serial.println(x)
#endif

#endif // CONFIG_H
