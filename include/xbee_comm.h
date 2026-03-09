#ifndef XBEE_COMM_H
#define XBEE_COMM_H

#include <Arduino.h>

#define XBEE_START_DELIM   0x7E
#define XBEE_TX_REQUEST    0x10
#define XBEE_TX_STATUS     0x8B
#define XBEE_MODEM_STATUS  0x8A
#define XBEE_RX_PACKET     0x90
#define XBEE_AT_COMMAND    0x08
#define XBEE_AT_RESPONSE   0x88
#define XBEE_MAX_FRAME     512

struct XBeeConfig {
    bool    valid;            // true if at least one AT response received
    int     ap_mode;          // AP parameter (1 = API mode 1)
    int     coordinator;      // CE parameter (1 = coordinator, 0 = router)
    uint16_t pan_id;          // ID parameter
    uint16_t my_addr;         // MY parameter
    int     responses;        // how many AT responses received (expect 4)
};

struct XBeeStats {
    uint32_t totalRxBytes;
    uint32_t totalTxBytes;
    uint32_t rxFramesParsed;
    uint32_t rxDataFrames;
    uint32_t txFramesSent;
    uint32_t txStatusOK;
    uint32_t txStatusFail;
    uint32_t checksumErrors;
    uint32_t frameTimeouts;
    uint32_t modemStatusCount;
    uint8_t  lastModemStatus;
};

typedef void (*XBeeReceiveCB)(const char* payload, size_t len);

void            xbeeInit();
uint8_t         xbeeSendBroadcast(const char* payload, size_t len);
void            xbeeProcessIncoming();
void            xbeeSetReceiveCallback(XBeeReceiveCB cb);
void            xbeeQueryConfig();
const XBeeConfig& xbeeGetConfig();
const XBeeStats& xbeeGetStats();

#endif // XBEE_COMM_H
