// xbee_comm.cpp — XBee S2C API Mode 1 driver (rebuilt from scratch)
//
// Based on the working XBEE_COMM_TEST project.
// Uses UART0 (Serial) on native IOMUX pins GPIO 1 (TX) / GPIO 3 (RX).

#include "xbee_comm.h"
#include "config.h"
#include <stdarg.h>

static HardwareSerial& xbeeSerial = Serial;
static XBeeReceiveCB rxCallback = nullptr;
static uint8_t frameIdCounter = 0;

// Diagnostic counters
static XBeeStats stats = {};

// Frame receive state machine
enum RxState { WAIT_DELIM, LEN_HI, LEN_LO, FRAME_DATA, CHECKSUM };
static RxState   rxState      = WAIT_DELIM;
static uint16_t  rxFrameLen   = 0;
static uint16_t  rxIdx        = 0;
static uint8_t   rxFrame[XBEE_MAX_FRAME];
static uint8_t   rxChecksum   = 0;
static uint32_t  rxFrameStart = 0;
static const uint32_t FRAME_TIMEOUT_MS = 1000;

void xbeeInit() {
    xbeeSerial.begin(XBEE_BAUD);

    // Flush bootloader garbage (bootloader runs at 115200, XBee at 9600)
    {
        uint32_t lastByte = millis();
        while (millis() - lastByte < 100) {
            if (xbeeSerial.available()) {
                xbeeSerial.read();
                lastByte = millis();
            }
        }
    }

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

    // 6 separate writes — matches the working test project
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

void xbeeFlushRx() {
    rxState = WAIT_DELIM;
    while (xbeeSerial.available()) xbeeSerial.read();
}

static void handleCompleteFrame() {
    stats.rxFramesParsed++;
    uint8_t ft = rxFrame[0];

    switch (ft) {
    case XBEE_RX_PACKET:
        if (rxFrameLen < 13) break;
        {
            const char* rfData = (const char*)&rxFrame[12];
            size_t rfLen = rxFrameLen - 12;
            while (rfLen > 0 && (rfData[rfLen - 1] == '\n' || rfData[rfLen - 1] == '\r')) rfLen--;
            if (rfLen > 0) {
                rxFrame[12 + rfLen] = '\0';
                stats.rxDataFrames++;
                dbgprintf("[XBee] RX 0x90 (%d B): %.80s\n", (int)rfLen, rfData);
                if (rxCallback) rxCallback(rfData, rfLen);
            }
        }
        break;
    case XBEE_TX_STATUS:
        if (rxFrameLen < 7) break;
        if (rxFrame[5] == 0x00) { stats.txStatusOK++; }
        else { stats.txStatusFail++; dbgprintf("[XBee] TX FAIL 0x%02X\n", rxFrame[5]); }
        break;
    case XBEE_MODEM_STATUS:
        if (rxFrameLen >= 2) {
            stats.modemStatusCount++;
            stats.lastModemStatus = rxFrame[1];
            dbgprintf("[XBee] Modem status: 0x%02X\n", rxFrame[1]);
        }
        break;
    case XBEE_TX_REQUEST:
        break;  // self-echo, ignore silently
    default:
        dbgprintf("[XBee] Unknown frame 0x%02X\n", ft);
        break;
    }
}

void xbeeProcessIncoming() {
    if (rxState != WAIT_DELIM && millis() - rxFrameStart > FRAME_TIMEOUT_MS) {
        stats.frameTimeouts++;
        rxState = WAIT_DELIM;
    }
    while (xbeeSerial.available()) {
        uint8_t b = xbeeSerial.read();
        stats.totalRxBytes++;
        switch (rxState) {
        case WAIT_DELIM:
            if (b == XBEE_START_DELIM) { rxState = LEN_HI; rxFrameStart = millis(); }
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
            else stats.checksumErrors++;
            rxState = WAIT_DELIM;
            break;
        }
    }
}
