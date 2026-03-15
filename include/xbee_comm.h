#ifndef XBEE_COMM_H
#define XBEE_COMM_H

#include <Arduino.h>

#define XBEE_START_DELIM   0x7E
#define XBEE_TX_REQUEST    0x10
#define XBEE_RX_PACKET     0x90
#define XBEE_TX_STATUS     0x8B
#define XBEE_MODEM_STATUS  0x8A
#define XBEE_MAX_FRAME     512

struct XBeeStats {
    uint32_t totalRxBytes;
    uint32_t totalTxBytes;
    uint32_t rxFramesParsed;
    uint32_t txFramesSent;
    uint32_t txStatusOK;
    uint32_t txStatusFail;
    uint32_t modemStatusCount;
    uint8_t  lastModemStatus;
};

typedef void (*XBeeReceiveCB)(const char* payload, size_t len);

void            xbeeInit();
uint8_t         xbeeSendBroadcast(const char* payload, size_t len);
void            xbeeProcessIncoming();
void            xbeeSetReceiveCallback(XBeeReceiveCB cb);
const XBeeStats& xbeeGetStats();

#endif // XBEE_COMM_H
