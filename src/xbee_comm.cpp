#include "xbee_comm.h"
#include "config.h"

static HardwareSerial& xbeeSerial = Serial;
static XBeeReceiveCB rxCallback = nullptr;
static uint8_t frameIdCounter = 0;
static XBeeStats stats = {};

enum RxState { WAIT_DELIM, LEN_HI, LEN_LO, FRAME_DATA, CHECKSUM };
static RxState   rxState      = WAIT_DELIM;
static uint16_t  rxFrameLen   = 0;
static uint16_t  rxIdx        = 0;
static uint8_t   rxFrame[XBEE_MAX_FRAME];
static uint8_t   rxChecksum   = 0;

void xbeeInit() {
    xbeeSerial.begin(XBEE_BAUD);
    memset(&stats, 0, sizeof(stats));
    dbgprintf("[XBee] UART0 ready — baud=%d TX=GPIO%d RX=GPIO%d\n",
              XBEE_BAUD, XBEE_TX_PIN, XBEE_RX_PIN);
}

uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    if (len == 0 || len > XBEE_MAX_FRAME - 18) return 0;

    if (++frameIdCounter == 0) frameIdCounter = 1;
    uint8_t fid = frameIdCounter;
    uint16_t frameDataLen = 14 + len;

    uint8_t hdr[14] = {
        XBEE_TX_REQUEST, fid,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
        0xFF, 0xFE, 0x00, 0x00
    };

    uint8_t cksum = 0;
    for (int i = 0; i < 14; i++) cksum += hdr[i];
    for (size_t i = 0; i < len; i++) cksum += (uint8_t)payload[i];
    cksum = 0xFF - cksum;

    xbeeSerial.write(XBEE_START_DELIM);
    xbeeSerial.write((uint8_t)(frameDataLen >> 8));
    xbeeSerial.write((uint8_t)(frameDataLen & 0xFF));
    xbeeSerial.write(hdr, 14);
    xbeeSerial.write((const uint8_t*)payload, len);
    xbeeSerial.write(cksum);
    xbeeSerial.flush();

    stats.totalTxBytes += 3 + 14 + len + 1;
    stats.txFramesSent++;
    dbgprintf("[XBee] TX id=%d len=%d\n", fid, (int)len);
    return fid;
}

void xbeeSetReceiveCallback(XBeeReceiveCB cb) { rxCallback = cb; }
const XBeeStats& xbeeGetStats() { return stats; }

static void handleCompleteFrame() {
    stats.rxFramesParsed++;
    uint8_t ft = rxFrame[0];

    if (ft == XBEE_RX_PACKET && rxFrameLen >= 13) {
        const char* rfData = (const char*)&rxFrame[12];
        size_t rfLen = rxFrameLen - 12;
        while (rfLen > 0 && (rfData[rfLen - 1] == '\n' || rfData[rfLen - 1] == '\r')) rfLen--;
        if (rfLen > 0) {
            rxFrame[12 + rfLen] = '\0';
            dbgprintf("[XBee] RX 0x90 (%d B): %.80s\n", (int)rfLen, rfData);
            if (rxCallback) rxCallback(rfData, rfLen);
        }
    } else if (ft == XBEE_TX_STATUS && rxFrameLen >= 7) {
        if (rxFrame[5] == 0x00) { stats.txStatusOK++; }
        else { stats.txStatusFail++; dbgprintf("[XBee] TX FAIL 0x%02X\n", rxFrame[5]); }
    } else if (ft == XBEE_MODEM_STATUS && rxFrameLen >= 2) {
        stats.modemStatusCount++;
        stats.lastModemStatus = rxFrame[1];
        dbgprintf("[XBee] Modem status: 0x%02X\n", rxFrame[1]);
    }
    // 0x10 self-echo and others: silently ignore
}

void xbeeProcessIncoming() {
    while (xbeeSerial.available()) {
        uint8_t b = xbeeSerial.read();
        stats.totalRxBytes++;
        switch (rxState) {
        case WAIT_DELIM:
            if (b == XBEE_START_DELIM) rxState = LEN_HI;
            break;
        case LEN_HI:
            rxFrameLen = (uint16_t)b << 8; rxState = LEN_LO; break;
        case LEN_LO:
            rxFrameLen |= b;
            if (rxFrameLen == 0 || rxFrameLen >= XBEE_MAX_FRAME) { rxState = WAIT_DELIM; }
            else { rxIdx = 0; rxChecksum = 0; rxState = FRAME_DATA; }
            break;
        case FRAME_DATA:
            rxFrame[rxIdx++] = b; rxChecksum += b;
            if (rxIdx >= rxFrameLen) rxState = CHECKSUM;
            break;
        case CHECKSUM:
            rxChecksum += b;
            if (rxChecksum == 0xFF) handleCompleteFrame();
            rxState = WAIT_DELIM;
            break;
        }
    }
}
