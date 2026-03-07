#include "xbee_comm.h"
#include "config.h"
#include <driver/uart.h>
#include <driver/gpio.h>

// ESP32-CAM: use UART1 — UART2 defaults to GPIO 16/17 which are PSRAM pins
// Generic ESP32: use UART2 (no PSRAM conflict)
#ifdef ESP32CAM_SPI_SD
  static HardwareSerial& xbeeSerial = Serial1;
  #define XBEE_UART_NUM UART_NUM_1
#else
  static HardwareSerial& xbeeSerial = Serial2;
  #define XBEE_UART_NUM UART_NUM_2
#endif

static XBeeReceiveCB   rxCallback = nullptr;
static uint8_t         frameIdCounter = 0;

// ── Frame receive state machine ─────────────────────────────────────
enum RxState { WAIT_DELIM, GOT_LEN_HI, GOT_LEN_LO, READING_DATA, GOT_CHECKSUM };
static RxState   rxState = WAIT_DELIM;
static uint16_t  rxFrameLen = 0;
static uint16_t  rxIdx = 0;
static uint8_t   rxFrame[XBEE_MAX_FRAME];
static uint8_t   rxChecksum = 0;

void xbeeInit() {
    // Reset GPIO pins to ensure clean state — detaches from any
    // previous peripheral (SPI, SD_MMC, UART0) before claiming for XBee
    gpio_reset_pin((gpio_num_t)XBEE_TX_PIN);
    gpio_reset_pin((gpio_num_t)XBEE_RX_PIN);

    xbeeSerial.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);

    // Explicitly route UART signals to our chosen GPIO pins
    uart_set_pin(XBEE_UART_NUM, XBEE_TX_PIN, XBEE_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    Serial.printf("[XBee] UART%d started (API mode 1) — TX=GPIO%d  RX=GPIO%d  baud=%d\n",
                  (XBEE_UART_NUM == UART_NUM_1) ? 1 : 2,
                  XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
}

uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    if (len == 0 || len > XBEE_MAX_FRAME - 14) return 0;

    uint16_t frameDataLen = 14 + len;  // 14 bytes header + payload
    if (++frameIdCounter == 0) frameIdCounter = 1;
    uint8_t fid = frameIdCounter;

    // Build the 14-byte header for a 0x10 Transmit Request
    uint8_t hdr[14] = {
        XBEE_TX_REQUEST,          // Frame type: 0x10
        fid,                      // Frame ID (1-255)
        0x00, 0x00, 0x00, 0x00,   // 64-bit dest addr (broadcast)
        0x00, 0x00, 0xFF, 0xFF,   //   = 0x000000000000FFFF
        0xFF, 0xFE,               // 16-bit dest addr (broadcast)
        0x00,                     // Broadcast radius (0 = max)
        0x00                      // Options (0 = none)
    };

    // Calculate checksum: sum all frame data bytes (hdr + payload), then 0xFF - sum
    uint8_t cksum = 0;
    for (int i = 0; i < 14; i++) cksum += hdr[i];
    for (size_t i = 0; i < len; i++) cksum += (uint8_t)payload[i];
    cksum = 0xFF - cksum;

    // Write the complete frame: [0x7E] [LenHi] [LenLo] [header] [payload] [checksum]
    xbeeSerial.write(XBEE_START_DELIM);              // 0x7E
    xbeeSerial.write((uint8_t)(frameDataLen >> 8));   // Length high byte
    xbeeSerial.write((uint8_t)(frameDataLen & 0xFF)); // Length low byte
    xbeeSerial.write(hdr, 14);                        // Header (14 bytes)
    xbeeSerial.write((const uint8_t*)payload, len);   // Payload (JSON)
    xbeeSerial.write(cksum);                          // Checksum
    xbeeSerial.flush();                               // Wait for TX complete

    Serial.printf("[XBee] TX frame ID=%d (%d bytes)\n", fid, (int)len);
    return fid;
}

void xbeeSetReceiveCallback(XBeeReceiveCB cb) {
    rxCallback = cb;
}

void xbeeProcessIncoming() {
    while (xbeeSerial.available()) {
        uint8_t b = xbeeSerial.read();

        switch (rxState) {
        case WAIT_DELIM:
            if (b == XBEE_START_DELIM) rxState = GOT_LEN_HI;
            break;

        case GOT_LEN_HI:
            rxFrameLen = (uint16_t)b << 8;
            rxState = GOT_LEN_LO;
            break;

        case GOT_LEN_LO:
            rxFrameLen |= b;
            rxIdx = 0;
            rxChecksum = 0;
            if (rxFrameLen == 0 || rxFrameLen >= XBEE_MAX_FRAME) {
                Serial.printf("[XBee] Invalid frame length %d\n", rxFrameLen);
                rxState = WAIT_DELIM;
            } else {
                rxState = READING_DATA;
            }
            break;

        case READING_DATA:
            rxFrame[rxIdx++] = b;
            rxChecksum += b;
            if (rxIdx >= rxFrameLen) rxState = GOT_CHECKSUM;
            break;

        case GOT_CHECKSUM:
            rxChecksum += b;
            if (rxChecksum == 0xFF) {
                uint8_t frameType = rxFrame[0];

                if (frameType == XBEE_RX_PACKET && rxFrameLen >= 13) {
                    // 0x90 Receive Packet: extract RF data from byte 12 onward
                    const char* rfData = (const char*)&rxFrame[12];
                    size_t rfLen = rxFrameLen - 12;

                    // Strip trailing newlines
                    while (rfLen > 0 && (rfData[rfLen-1] == '\n' || rfData[rfLen-1] == '\r'))
                        rfLen--;

                    if (rfLen > 0 && rfLen < XBEE_MAX_FRAME - 12) {
                        rxFrame[12 + rfLen] = '\0';  // null-terminate
                        Serial.printf("[XBee] RX 0x90 (%d bytes): %.80s\n", (int)rfLen, rfData);
                        if (rxCallback) rxCallback(rfData, rfLen);
                    }
                } else if (frameType == XBEE_TX_STATUS && rxFrameLen >= 7) {
                    // 0x8B Transmit Status
                    uint8_t delivery = rxFrame[5];
                    if (delivery == 0) {
                        Serial.printf("[XBee] TX status: OK (frame %d)\n", rxFrame[1]);
                    } else {
                        Serial.printf("[XBee] TX status: FAILED 0x%02X (frame %d)\n",
                                      delivery, rxFrame[1]);
                    }
                } else if (frameType == XBEE_TX_REQUEST) {
                    // Self-echo — our own TX frame looping back. Ignore.
                    Serial.println("[XBee] Self-echo (0x10) — ignored");
                } else {
                    Serial.printf("[XBee] Unknown frame type 0x%02X\n", frameType);
                }
            } else {
                Serial.printf("[XBee] Checksum error (0x%02X != 0xFF)\n", rxChecksum);
            }
            rxState = WAIT_DELIM;
            break;
        }
    }
}
