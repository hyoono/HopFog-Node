#ifndef XBEE_COMM_H
#define XBEE_COMM_H

#include <Arduino.h>

// ── API Mode 1 frame types ──────────────────────────────────────────
#define XBEE_START_DELIM   0x7E
#define XBEE_TX_REQUEST    0x10   // Transmit Request
#define XBEE_TX_STATUS     0x8B   // Transmit Status
#define XBEE_MODEM_STATUS  0x8A   // Modem Status
#define XBEE_RX_PACKET     0x90   // Receive Packet (RF data in)
#define XBEE_MAX_FRAME     512

// ── Diagnostic counters ─────────────────────────────────────────────
struct XBeeStats {
    uint32_t totalRxBytes;       // raw bytes received on UART
    uint32_t totalTxBytes;       // raw bytes sent on UART
    uint32_t rxFramesParsed;     // valid API frames received
    uint32_t rxDataFrames;       // 0x90 Receive Packet frames
    uint32_t txFramesSent;       // 0x10 frames written
    uint32_t txStatusOK;         // 0x8B with delivery == 0
    uint32_t txStatusFail;       // 0x8B with delivery != 0
    uint32_t checksumErrors;     // frames with bad checksum
    uint32_t frameTimeouts;      // incomplete frames abandoned
    uint32_t modemStatusCount;   // 0x8A Modem Status events
    uint8_t  lastModemStatus;    // most recent modem status byte
};

// ── Callback for received RF data ───────────────────────────────────
typedef void (*XBeeReceiveCB)(const char* payload, size_t len);

// ── Public API ──────────────────────────────────────────────────────
void            xbeeInit();
uint8_t         xbeeSendBroadcast(const char* payload, size_t len);
void            xbeeProcessIncoming();
void            xbeeSetReceiveCallback(XBeeReceiveCB cb);
const XBeeStats& xbeeGetStats();

#endif // XBEE_COMM_H
