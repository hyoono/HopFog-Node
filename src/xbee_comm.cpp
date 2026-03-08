// ═════════════════════════════════════════════════════════════════════
//  xbee_comm.cpp — XBee S2C API Mode 1 driver (rebuilt from scratch)
//
//  Uses UART0 (Serial) on native IOMUX pins GPIO 1 (TX) / GPIO 3 (RX).
//  Matches the init sequence from the working XBEE_COMM_TEST project:
//    - Serial.begin(9600) only — no Serial.end() before it
//    - Called FIRST in setup(), before SD card or WiFi
//    - No esp_log_level_set() — CORE_DEBUG_LEVEL=0 is sufficient
// ═════════════════════════════════════════════════════════════════════

#include "xbee_comm.h"
#include "config.h"

// ── UART handle ─────────────────────────────────────────────────────
static HardwareSerial& xbeeSerial = Serial;

// ── Callback ────────────────────────────────────────────────────────
static XBeeReceiveCB rxCallback = nullptr;

// ── TX frame ID counter (1–255, wraps) ──────────────────────────────
static uint8_t frameIdCounter = 0;

// ── Diagnostic counters ─────────────────────────────────────────────
static XBeeStats stats = {};

// ── Frame receive state machine ─────────────────────────────────────
enum RxState { WAIT_DELIM, LEN_HI, LEN_LO, FRAME_DATA, CHECKSUM };
static RxState   rxState      = WAIT_DELIM;
static uint16_t  rxFrameLen   = 0;
static uint16_t  rxIdx        = 0;
static uint8_t   rxFrame[XBEE_MAX_FRAME];
static uint8_t   rxChecksum   = 0;
static uint32_t  rxFrameStart = 0;   // millis() when frame started

// Abandon an incomplete frame if no new byte arrives within this time.
// XBee at 9600 baud sends ~960 bytes/sec ≈ 1.04 ms/byte.
// A max-length frame (512 bytes) takes ~530 ms.  1000 ms is generous.
static const uint32_t FRAME_TIMEOUT_MS = 1000;

// ─────────────────────────────────────────────────────────────────────
//  xbeeInit — configure UART0 for XBee at 9600 baud
// ─────────────────────────────────────────────────────────────────────
void xbeeInit() {
    // Match the working test project exactly:
    //   Serial.begin(9600);  — no Serial.end(), no delay, no pin args.
    // On ESP32 UART0 this uses native IOMUX pins GPIO 1 (TX) / GPIO 3 (RX).
    xbeeSerial.begin(XBEE_BAUD);

    // Flush any boot-loader garbage that arrived at 115200 baud.
    // Wait until no new bytes arrive for 100ms.
    {
        uint32_t lastByte = millis();
        while (millis() - lastByte < 100) {
            if (xbeeSerial.available()) {
                xbeeSerial.read();
                lastByte = millis();
            }
        }
    }

    // Zero diagnostic counters
    memset(&stats, 0, sizeof(stats));

    dbgprintf("[XBee] UART0 ready — API mode 1, baud=%d, TX=GPIO%d, RX=GPIO%d\n",
              XBEE_BAUD, XBEE_TX_PIN, XBEE_RX_PIN);
}

// ─────────────────────────────────────────────────────────────────────
//  xbeeSendBroadcast — build and send a 0x10 Transmit Request
// ─────────────────────────────────────────────────────────────────────
uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    if (len == 0 || len > XBEE_MAX_FRAME - 18) return 0;

    if (++frameIdCounter == 0) frameIdCounter = 1;
    uint8_t fid = frameIdCounter;
    uint16_t frameDataLen = 14 + len;

    // 14-byte header for 0x10 Transmit Request (broadcast)
    uint8_t hdr[14] = {
        XBEE_TX_REQUEST, fid,
        0x00, 0x00, 0x00, 0x00,  // 64-bit dest: 0x000000000000FFFF
        0x00, 0x00, 0xFF, 0xFF,
        0xFF, 0xFE,              // 16-bit dest: 0xFFFE (broadcast)
        0x00,                    // broadcast radius (0 = max hops)
        0x00                     // options
    };

    // Checksum = 0xFF − (sum of all frame-data bytes)
    uint8_t cksum = 0;
    for (int i = 0; i < 14; i++) cksum += hdr[i];
    for (size_t i = 0; i < len; i++) cksum += (uint8_t)payload[i];
    cksum = 0xFF - cksum;

    // Assemble the complete frame in one buffer for an atomic write
    uint8_t buf[XBEE_MAX_FRAME];
    size_t pos = 0;
    buf[pos++] = XBEE_START_DELIM;
    buf[pos++] = (uint8_t)(frameDataLen >> 8);
    buf[pos++] = (uint8_t)(frameDataLen & 0xFF);
    memcpy(&buf[pos], hdr, 14); pos += 14;
    memcpy(&buf[pos], payload, len); pos += len;
    buf[pos++] = cksum;

    xbeeSerial.write(buf, pos);
    xbeeSerial.flush();

    stats.totalTxBytes += pos;
    stats.txFramesSent++;

    dbgprintf("[XBee] TX id=%d len=%d\n", fid, (int)len);
    return fid;
}

// ─────────────────────────────────────────────────────────────────────
//  xbeeSetReceiveCallback
// ─────────────────────────────────────────────────────────────────────
void xbeeSetReceiveCallback(XBeeReceiveCB cb) {
    rxCallback = cb;
}

// ─────────────────────────────────────────────────────────────────────
//  xbeeGetStats — return read-only reference to diagnostic counters
// ─────────────────────────────────────────────────────────────────────
const XBeeStats& xbeeGetStats() {
    return stats;
}

// ─────────────────────────────────────────────────────────────────────
//  handleCompleteFrame — dispatch a validated API frame
// ─────────────────────────────────────────────────────────────────────
static void handleCompleteFrame() {
    stats.rxFramesParsed++;
    uint8_t ft = rxFrame[0];

    switch (ft) {

    // ── 0x90  Receive Packet (RF data from remote XBee) ─────────
    case XBEE_RX_PACKET:
        if (rxFrameLen < 13) break;  // need at least 12-byte header + 1 data
        {
            const char* rfData = (const char*)&rxFrame[12];
            size_t rfLen = rxFrameLen - 12;

            // Strip trailing CR/LF
            while (rfLen > 0 && (rfData[rfLen - 1] == '\n' || rfData[rfLen - 1] == '\r'))
                rfLen--;

            if (rfLen > 0) {
                rxFrame[12 + rfLen] = '\0';
                stats.rxDataFrames++;
                dbgprintf("[XBee] RX 0x90 (%d B): %.80s\n", (int)rfLen, rfData);
                if (rxCallback) rxCallback(rfData, rfLen);
            }
        }
        break;

    // ── 0x8B  Transmit Status ───────────────────────────────────
    case XBEE_TX_STATUS:
        if (rxFrameLen < 7) break;
        {
            uint8_t delivery = rxFrame[5];
            if (delivery == 0x00) {
                stats.txStatusOK++;
                dbgprintf("[XBee] TX OK (id=%d)\n", rxFrame[1]);
            } else {
                stats.txStatusFail++;
                dbgprintf("[XBee] TX FAIL 0x%02X (id=%d)\n", delivery, rxFrame[1]);
            }
        }
        break;

    // ── 0x8A  Modem Status ──────────────────────────────────────
    case XBEE_MODEM_STATUS:
        if (rxFrameLen >= 2) {
            stats.modemStatusCount++;
            stats.lastModemStatus = rxFrame[1];
            dbgprintf("[XBee] Modem status: 0x%02X\n", rxFrame[1]);
        }
        break;

    // ── 0x10  Self-echo (our own TX looping back on UART0) ──────
    case XBEE_TX_REQUEST:
        break;  // silently ignore

    default:
        dbgprintf("[XBee] Unknown frame 0x%02X\n", ft);
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────
//  xbeeProcessIncoming — call from loop(), feeds bytes into the parser
// ─────────────────────────────────────────────────────────────────────
void xbeeProcessIncoming() {
    // Timeout: abandon an incomplete frame if no bytes for FRAME_TIMEOUT_MS
    if (rxState != WAIT_DELIM) {
        if (millis() - rxFrameStart > FRAME_TIMEOUT_MS) {
            stats.frameTimeouts++;
            dbgprintf("[XBee] Frame timeout (state=%d)\n", rxState);
            rxState = WAIT_DELIM;
        }
    }

    while (xbeeSerial.available()) {
        uint8_t b = xbeeSerial.read();
        stats.totalRxBytes++;

        switch (rxState) {

        case WAIT_DELIM:
            if (b == XBEE_START_DELIM) {
                rxState = LEN_HI;
                rxFrameStart = millis();
            }
            break;

        case LEN_HI:
            rxFrameLen = (uint16_t)b << 8;
            rxState = LEN_LO;
            break;

        case LEN_LO:
            rxFrameLen |= b;
            if (rxFrameLen == 0 || rxFrameLen >= XBEE_MAX_FRAME) {
                dbgprintf("[XBee] Bad frame length %d\n", rxFrameLen);
                rxState = WAIT_DELIM;
            } else {
                rxIdx = 0;
                rxChecksum = 0;
                rxState = FRAME_DATA;
            }
            break;

        case FRAME_DATA:
            rxFrame[rxIdx++] = b;
            rxChecksum += b;
            if (rxIdx >= rxFrameLen) rxState = CHECKSUM;
            break;

        case CHECKSUM:
            rxChecksum += b;
            if (rxChecksum == 0xFF) {
                handleCompleteFrame();
            } else {
                stats.checksumErrors++;
                dbgprintf("[XBee] Checksum error 0x%02X\n", rxChecksum);
            }
            rxState = WAIT_DELIM;
            break;
        }
    }
}
