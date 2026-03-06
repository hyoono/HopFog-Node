#ifndef XBEE_COMM_H
#define XBEE_COMM_H

#include <Arduino.h>
#include <ArduinoJson.h>

// ── API Mode 1 Constants ────────────────────────────────────────────
#define XBEE_START_DELIM   0x7E
#define XBEE_TX_REQUEST    0x10   // Transmit Request frame type
#define XBEE_RX_PACKET     0x90   // Receive Packet frame type
#define XBEE_TX_STATUS     0x8B   // Transmit Status frame type
#define XBEE_MAX_FRAME     512

// ── Callback for received RF data ───────────────────────────────────
typedef void (*XBeeReceiveCB)(const char* payload, size_t len);

// ── Public API ──────────────────────────────────────────────────────
void xbeeInit();
uint8_t xbeeSendBroadcast(const char* payload, size_t len);
void xbeeProcessIncoming();
void xbeeSetReceiveCallback(XBeeReceiveCB cb);

#endif // XBEE_COMM_H
