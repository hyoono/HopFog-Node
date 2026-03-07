# HopFog-Node

ESP32-CAM fog-computing node for the [HopFog](https://github.com/hyoono/HopFog-Web) mesh network. Each node joins the admin coordinator over XBee ZigBee RF, creates its own WiFi access point, and serves a mobile-app REST API locally — enabling offline-first communication in disaster or infrastructure-limited scenarios.

## Table of Contents

- [Architecture Overview](#architecture-overview)
- [Hardware Requirements](#hardware-requirements)
- [Wiring Guide](#wiring-guide)
- [XBee Module Configuration](#xbee-module-configuration)
- [Project Structure](#project-structure)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Build](#build)
  - [Flash](#flash)
  - [Serial Monitor](#serial-monitor)
- [SD Card Preparation](#sd-card-preparation)
- [Configuration](#configuration)
- [Communication Protocol](#communication-protocol)
  - [XBee API Mode 1 Frames](#xbee-api-mode-1-frames)
  - [JSON Command Format](#json-command-format)
  - [Node → Admin Commands](#node--admin-commands)
  - [Admin → Node Commands](#admin--node-commands)
  - [Command Flow](#command-flow)
- [REST API Reference](#rest-api-reference)
- [Multi-Node Deployment](#multi-node-deployment)
- [Troubleshooting](#troubleshooting)
  - [SD_MMC vs SPI SD (Why SPI?)](#sd_mmc-vs-spi-sd-why-spi)
  - [Debugging Checklist](#debugging-checklist)
- [License](#license)

---

## Architecture Overview

```
┌──────────────────────────────────────────────────────────┐
│                   ADMIN  (HopFog-Web)                    │
│  ESP32-CAM + XBee S2C Pro (Coordinator, AP=1, CE=1)      │
│  WiFi AP "HopFog-Network"                                │
│  Admin dashboard + mobile app API                        │
│  SD card: users, broadcasts, messages, conversations     │
└────────────────────────┬─────────────────────────────────┘
                         │  XBee ZigBee RF
                         │  API mode 1 binary frames
                         │  JSON payloads
┌────────────────────────┴─────────────────────────────────┐
│                   NODE  (HopFog-Node)                     │
│  ESP32-CAM + XBee S2C (Router, AP=1, CE=0)               │
│  WiFi AP "HopFog-Node-XX"                                │
│  Mobile app REST API (same endpoints as admin)           │
│  SD card: synced copy of users, broadcasts, messages     │
└──────────────────────────────────────────────────────────┘
```

**How it works:**

1. Node boots, sends `REGISTER` to admin via XBee every 10 seconds.
2. Admin replies with `REGISTER_ACK`.
3. Node sends `SYNC_REQUEST` to get all data (users, announcements, conversations, etc.).
4. Admin replies with `SYNC_DATA` containing a full database dump.
5. Node stores synced data on its SD card.
6. Node creates its own WiFi AP for local mobile phones.
7. A DNS captive portal resolves **hopfog.com** (and all other hostnames) to the node's IP, so users can simply type `hopfog.com` in their browser.
8. Mobile phones connect to the node's WiFi and use the same API endpoints as the admin.
9. Node relays user actions (send message, SOS, etc.) to admin via XBee.
10. Node sends `HEARTBEAT` every 30 seconds; admin replies `PONG`.

---

## Hardware Requirements

| Component | Description |
|-----------|-------------|
| AI-Thinker ESP32-CAM | Microcontroller with built-in micro SD card slot |
| Digi XBee S2C or S2C Pro | ZigBee radio module |
| XBee breakout board | For breadboard mounting |
| Micro SD card | FAT32 formatted |
| USB-to-serial programmer | FTDI or CP2102 for flashing |

---

## Wiring Guide

### ESP32-CAM ↔ XBee

```
ESP32-CAM            XBee Module
──────────           ───────────
GPIO 3  (TX)  ─────► DIN   (pin 3)
GPIO 12 (RX)  ◄───── DOUT  (pin 2)
3.3V          ─────► VCC   (pin 1)
GND           ─────► GND   (pin 10)
```

> **⚠️ GPIO 3 is shared with USB-to-serial RX.** Disconnect the programming adapter before running with XBee connected. Serial Monitor **output** (TX on GPIO 1) still works normally.

> **⚠️ GPIO 12 boot-strapping note:** GPIO 12 controls the flash voltage at boot. If the ESP32 fails to boot with XBee connected, disconnect XBee DOUT from GPIO 12 during power-on and reconnect after boot.

### SD Card

The ESP32-CAM's built-in SD card slot is accessed via **SPI mode** (HSPI bus), not SD_MMC. This avoids the IOMUX conflict where SD_MMC permanently claims GPIO 12/13, which would block XBee serial communication. No external wiring needed — just insert a FAT32-formatted micro SD card.

SPI pin mapping (fixed by ESP32-CAM hardware):

| Signal | GPIO | SD slot function |
|--------|------|-----------------|
| CS | 13 | DAT3 |
| CLK | 14 | CLK |
| MISO | 2 | DAT0 |
| MOSI | 15 | CMD |

---

## XBee Module Configuration

Configure each XBee module using Digi's [XCTU](https://www.digi.com/products/embedded-systems/digi-xbee/digi-xbee-tools/xctu) software **before** connecting it to the ESP32.

### Node XBee (Router)

| Parameter | Value | Description |
|-----------|-------|-------------|
| **AP** | `1` | API mode 1 (binary frames, no escaping) |
| **CE** | `0` | Router (joins coordinator's network) |
| **ID** | `1234` | PAN ID — **must match the admin XBee** |
| **BD** | `3` | 9600 baud |
| **JV** | `1` | Join verification (router joins on power-up) |
| **DH** | `0` | Destination address high (broadcast) |
| **DL** | `FFFF` | Destination address low (broadcast) |

### Admin XBee (Coordinator)

| Parameter | Value |
|-----------|-------|
| **AP** | `1` |
| **CE** | `1` (Coordinator) |
| **ID** | `1234` (same PAN ID) |
| **BD** | `3` (9600 baud) |

**After writing settings, power-cycle the XBee before connecting to the ESP32.**

### Verifying XBee Association

In XCTU, read the `AI` (Association Indication) parameter on the node XBee:

| Value | Meaning |
|-------|---------|
| `0x00` | Successfully joined coordinator ✅ |
| `0x21` | Scan found no PANs (coordinator not powered?) |
| `0x22` | No valid PAN found (wrong PAN ID?) |
| `0x23` | Join failed (authentication issue?) |

---

## Project Structure

```
HopFog-Node/
├── platformio.ini              # PlatformIO build configuration
├── include/
│   ├── config.h                # WiFi, pins, SD paths, timing constants
│   ├── xbee_comm.h             # XBee API mode 1 driver header
│   ├── node_client.h           # Node protocol client header
│   ├── sd_storage.h            # SD card read/write header
│   └── web_server.h            # Web server + API handler header
├── src/
│   ├── main.cpp                # setup() and loop() entry point
│   ├── xbee_comm.cpp           # XBee binary frame TX/RX driver
│   ├── node_client.cpp         # REGISTER/HEARTBEAT/SYNC state machine
│   ├── sd_storage.cpp          # SD card init + JSON file read/write
│   ├── web_server.cpp          # WiFi AP + CORS + web server setup
│   └── api_handlers.cpp        # REST API endpoint handlers
└── data/
    └── sd/
        └── db/                 # Default empty JSON files for SD card
            ├── users.json
            ├── announcements.json
            ├── conversations.json
            ├── direct_messages.json
            ├── fog_devices.json
            └── messages.json
```

### Module Responsibilities

| Module | File(s) | Role |
|--------|---------|------|
| **Config** | `config.h` | All compile-time constants (WiFi SSID/password, GPIO pins, timing intervals, file paths) |
| **XBee Driver** | `xbee_comm.h/cpp` | Build and send 0x10 TX Request frames; parse incoming 0x90 RX Packet and 0x8B TX Status frames via a byte-level state machine |
| **Node Client** | `node_client.h/cpp` | Protocol state machine (`UNREGISTERED` → `REGISTERED` → `SYNCING` → `RUNNING`); sends REGISTER/HEARTBEAT/SYNC_REQUEST; handles admin responses |
| **SD Storage** | `sd_storage.h/cpp` | Mount SD via SPI (HSPI bus); create `/db` directory and default files; generic JSON read/write |
| **Web Server** | `web_server.h`, `web_server.cpp`, `api_handlers.cpp` | WiFi AP setup, CORS headers, 12 REST API endpoints for the mobile app |
| **Main** | `main.cpp` | Initialization sequence (SD → WiFi → Web Server → XBee → Node Client) and main loop |

---

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) CLI or VS Code extension
- USB-to-serial programmer connected to the ESP32-CAM
- FAT32-formatted micro SD card inserted in the ESP32-CAM

### Build

```bash
pio run -e esp32cam
```

### Flash

```bash
pio run -e esp32cam --target upload
```

> **Note:** The ESP32-CAM requires GPIO 0 to be connected to GND during power-on to enter flash mode. After flashing, disconnect GPIO 0 from GND and reset the board.

### Serial Monitor

```bash
pio device monitor
```

This opens a 115200-baud serial monitor. You should see output like:

```
========================================
   HopFog-Node  ESP32 Firmware
========================================
[SD] SPI mode (HSPI) — mounted OK
[SD] Card size: 7437MB
[WiFi] Starting AP "HopFog-Node-01"
[WiFi] AP running — IP: 192.168.4.1
[DNS] Captive portal running — hopfog.com → 192.168.4.1
[Web] Server started on port 80
[XBee] UART1 started (API mode 1) — TX=GPIO3  RX=GPIO12  baud=9600
[Node] Client initialized — will start REGISTER cycle
[Node] Setup complete — starting REGISTER cycle
[Node] Sent REGISTER
```

---

## SD Card Preparation

Format the micro SD card as **FAT32** and insert it into the ESP32-CAM. The firmware automatically creates the required directory and files on first boot:

```
/db/
├── users.json           # []
├── announcements.json   # []
├── conversations.json   # []
├── direct_messages.json # []
├── fog_devices.json     # []
└── messages.json        # []
```

Alternatively, you can pre-populate the SD card by copying the files from the `data/sd/db/` directory in this repository.

---

## Configuration

All configuration is in `include/config.h`. Edit before building:

```cpp
// ── WiFi Access Point ───────────────────────────────────
#define AP_SSID       "HopFog-Node-01"   // Change for each node
#define AP_PASSWORD   "changeme123"       // Set a strong password
#define AP_CHANNEL    6                   // Avoid channel 1 (admin)

// ── Node Identity ───────────────────────────────────────
#define NODE_ID       "node-01"           // Unique ID for this node
#define DEVICE_NAME   "HopFog-Node-01"    // Human-readable name

// ── XBee Pins ───────────────────────────────────────────
#define XBEE_TX_PIN   3                   // ESP32 TX → XBee DIN (GPIO 3)
#define XBEE_RX_PIN   12                  // ESP32 RX ← XBee DOUT

// ── Timing ──────────────────────────────────────────────
#define REGISTER_INTERVAL_MS   10000      // REGISTER retry (ms)
#define HEARTBEAT_INTERVAL_MS  30000      // HEARTBEAT interval (ms)
#define SYNC_RETRY_MS          15000      // SYNC_REQUEST retry (ms)
```

> **Important:** Each node in the mesh must have a unique `NODE_ID` and `AP_SSID`.

---

## Communication Protocol

### XBee API Mode 1 Frames

All XBee serial communication uses binary API mode 1 frames:

```
┌──────┬──────────────┬───────────────────────────────────┬──────────┐
│ 0x7E │ Length (2B)   │ Frame Data (variable)             │ Checksum │
│ 1B   │ Hi Lo        │ [Type][...header...][payload]      │ 1B       │
└──────┴──────────────┴───────────────────────────────────┴──────────┘
```

**Checksum:** `0xFF - (sum of all frame data bytes)`

#### 0x10 Transmit Request (Node → Air → Admin)

| Offset | Size | Value | Description |
|--------|------|-------|-------------|
| 0 | 1 | `0x10` | Frame type |
| 1 | 1 | 1–255 | Frame ID |
| 2–9 | 8 | `00 00 00 00 00 00 FF FF` | 64-bit destination (broadcast) |
| 10–11 | 2 | `FF FE` | 16-bit destination (broadcast) |
| 12 | 1 | `0x00` | Broadcast radius (max) |
| 13 | 1 | `0x00` | Options |
| 14+ | N | — | RF payload (JSON string) |

#### 0x90 Receive Packet (Air → Node)

| Offset | Size | Description |
|--------|------|-------------|
| 0 | 1 | Frame type (`0x90`) |
| 1–8 | 8 | 64-bit source address |
| 9–10 | 2 | 16-bit source address |
| 11 | 1 | Receive options |
| 12+ | N | RF payload (JSON string) |

#### 0x8B Transmit Status (XBee → ESP32)

| Offset | Size | Description |
|--------|------|-------------|
| 0 | 1 | Frame type (`0x8B`) |
| 1 | 1 | Frame ID (matches TX request) |
| 2–3 | 2 | 16-bit destination |
| 4 | 1 | Retry count |
| 5 | 1 | Delivery status (`0x00` = success) |
| 6 | 1 | Discovery status |

### JSON Command Format

All JSON payloads use this structure:

```json
{
  "cmd": "COMMAND_NAME",
  "node_id": "node-01",
  "ts": 12345,
  "params": { ... }
}
```

### Node → Admin Commands

| Command | When | Params |
|---------|------|--------|
| `REGISTER` | Every 10s until ACK | `device_name`, `ip_address`, `status`, `free_heap` |
| `HEARTBEAT` | Every 30s after registered | `ip_address`, `uptime`, `free_heap` |
| `SYNC_REQUEST` | After `REGISTER_ACK` | _(none)_ |
| `RELAY_CHAT_MSG` | Mobile user sends a message | `conversation_id`, `sender_id`, `message_text` |
| `SOS_ALERT` | Mobile user triggers SOS | `user_id` |
| `CHANGE_PASSWORD` | Mobile user changes password | `user_id`, `old_password`, `new_password` |
| `STATS_RESPONSE` | Reply to `GET_STATS` | `free_heap`, `uptime`, `ip_address`, `wifi_stations` |

### Admin → Node Commands

| Command | When | Payload |
|---------|------|---------|
| `REGISTER_ACK` | Reply to `REGISTER` | — |
| `PONG` | Reply to `HEARTBEAT` | — |
| `SYNC_DATA` | Reply to `SYNC_REQUEST` | `users`, `announcements`, `conversations`, `chat_messages`, `fog_nodes` |
| `BROADCAST_MSG` | Admin creates an announcement | `subject`, `message` |
| `GET_STATS` | Admin requests node diagnostics | — |

### Command Flow

```
Node                                     Admin
  │                                        │
  ├──► REGISTER ──────────────────────────►│
  │    (every 10s)                         │
  │◄──────────────────── REGISTER_ACK ◄────┤
  │                                        │
  ├──► SYNC_REQUEST ──────────────────────►│
  │◄──────────────────── SYNC_DATA ◄───────┤
  │    (users, announcements, etc.)        │
  │                                        │
  ├──► HEARTBEAT ─────────────────────────►│
  │    (every 30s)                         │
  │◄──────────────────── PONG ◄────────────┤
  │                                        │
  │    [Mobile user sends message]         │
  ├──► RELAY_CHAT_MSG ────────────────────►│
  │                                        │
  │    [Admin creates broadcast]           │
  │◄──────────────────── BROADCAST_MSG ◄───┤
```

---

## REST API Reference

The node serves these endpoints on its WiFi AP. Mobile phones connect to the node's WiFi and access the API via **http://hopfog.com** (or `192.168.4.1`). A built-in DNS captive portal resolves `hopfog.com` to the AP IP automatically.

### `GET /status`

Health check and node diagnostics.

**Response:**
```json
{
  "online": true,
  "node_id": "node-01",
  "device_name": "HopFog-Node-01",
  "state": 3,
  "free_heap": 180000,
  "uptime": 3600,
  "wifi_stations": 2
}
```

State values: `0` = UNREGISTERED, `1` = REGISTERED, `2` = SYNCING, `3` = RUNNING.

---

### `POST /login`

Authenticate a user against synced `users.json`.

**Request:**
```json
{ "username": "alice", "password": "secret" }
```

**Success (200):**
```json
{ "success": true, "user": { "id": 1, "username": "alice", ... } }
```

**Failure (401):**
```json
{ "success": false, "error": "Invalid credentials" }
```

---

### `GET /announcements`

Return all announcements from synced data.

**Response:** JSON array of announcement objects.

---

### `GET /users`

Return all users from synced data.

**Response:** JSON array of user objects.

---

### `GET /conversations`

Return conversations, optionally filtered by user.

**Query params:** `?user_id=1` (optional)

**Response:** JSON array of conversation objects.

---

### `GET /messages`

Return messages, optionally filtered by conversation.

**Query params:** `?conversation_id=1` (optional)

**Response:** JSON array of message objects.

---

### `POST /send`

Send a chat message. Stores locally and relays to admin via XBee.

**Request:**
```json
{
  "conversation_id": 1,
  "sender_id": 2,
  "message_text": "Hello from the node!"
}
```

**Response:**
```json
{ "success": true, "message": "Message sent" }
```

---

### `POST /create-chat`

Create a new conversation. Stores locally.

**Request:**
```json
{ "participants": [1, 2] }
```

**Response:**
```json
{ "success": true, "conversation": { "id": 1, "participants": [1, 2], ... } }
```

---

### `POST /sos`

Trigger an SOS alert. Relayed to admin immediately via XBee.

**Request:**
```json
{ "user_id": 1 }
```

**Response:**
```json
{ "success": true, "message": "SOS alert sent" }
```

---

### `GET /new-messages`

Poll for messages created after a given timestamp.

**Query params:** `?since=12345` (seconds since boot)

**Response:** JSON array of message objects created after the `since` timestamp.

---

### `POST /agree-sos`

Record a user's agreement to an SOS alert.

**Request:**
```json
{ "user_id": 1 }
```

**Response:**
```json
{ "success": true, "message": "SOS agreement recorded" }
```

---

### `POST /change-password`

Change a user's password. Updates locally and relays to admin via XBee.

**Request:**
```json
{ "user_id": 1, "old_password": "old", "new_password": "new" }
```

**Success (200):**
```json
{ "success": true, "message": "Password changed" }
```

**Failure (401):**
```json
{ "success": false, "error": "Wrong old password" }
```

---

## Multi-Node Deployment

Multiple nodes can be deployed with the same firmware. Each node needs:

1. **Unique `NODE_ID` and `AP_SSID`** in `config.h`:
   ```cpp
   #define NODE_ID   "node-02"
   #define AP_SSID   "HopFog-Node-02"
   ```

2. **Its own XBee module** configured as a Router (`CE=0`) on the same PAN ID (`ID=1234`) as the admin Coordinator.

3. **A separate micro SD card** formatted as FAT32.

The XBee mesh handles multi-hop routing transparently — nodes do not need line-of-sight to the admin.

```
  ┌───────┐         ┌───────┐         ┌───────┐
  │ Admin │◄──RF──►│Node-01│◄──RF──►│Node-02│
  │ (Coord)│         │(Router)│         │(Router)│
  └───────┘         └───────┘         └───────┘
       ▲                                   │
       └───────── multi-hop RF ────────────┘
```

---

## Troubleshooting

### SD_MMC vs SPI SD (Why SPI?)

**This was the #1 cause of "no XBee communication" on ESP32-CAM boards.**

The ESP32-CAM's SD card slot can be accessed via either SD_MMC or SPI. The SD_MMC peripheral permanently claims GPIO 12 and 13 via **IOMUX** (as HS2_DATA2/HS2_DATA3), even in 1-bit mode. IOMUX has hardware priority over the GPIO matrix that UART2 uses — so XBee RX on GPIO 12 fails silently.

**Solution (already applied):** The firmware uses **SPI mode** (`SD.begin()` with the HSPI bus) instead of `SD_MMC.begin()`. SPI mode uses only GPIO 13 (CS), 14 (CLK), 2 (MISO), and 15 (MOSI), leaving GPIO 3 and 12 free for XBee UART1.

XBee TX was also moved from GPIO 13 → GPIO 3 since GPIO 13 is now the SD SPI chip-select pin (and GPIO 4 is the flash LED).

### Debugging Checklist

#### 1. Verify XBee hardware

- Power on both admin and node.
- Check XBee **ASSOC** LED: blinking = searching, steady = joined.
- In XCTU: read `AI` parameter on node XBee → must be `0x00`.

#### 2. Verify ESP32 serial output

Open the serial monitor (`pio device monitor`) and look for:

```
[XBee] UART1 started (API mode 1) — TX=GPIO3  RX=GPIO12  baud=9600
[Node] Sent REGISTER
```

If `[Node] Sent REGISTER` does not appear every 10 seconds, the main loop or timer is not running.

#### 3. Verify TX (node → admin)

- On the admin, open the Testing page → Serial Monitor section.
- Wait for the node to send REGISTER.
- Admin should show: `RX <- [0x90] from XXXX (...bytes) {"cmd":"REGISTER",...}`
- If admin shows nothing, the node's XBee is not transmitting or the XBees are not associated.

#### 4. Verify RX (admin → node)

- On the admin, click "Send Test Message".
- Node serial monitor should show: `[XBee] RX 0x90 (...bytes): {"cmd":"BROADCAST_MSG",...}`
- If node shows nothing, check the wiring (GPIO 3 → DIN, GPIO 12 ← DOUT) and verify SPI SD mode is active.

#### 5. Full handshake

Expected serial output during a successful handshake:

```
[Node] Sent REGISTER
[XBee] TX status: OK (frame 1)
[XBee] RX 0x90 (28 bytes): {"cmd":"REGISTER_ACK",...}
[Node] Got REGISTER_ACK — registered with admin!
[Node] Sent SYNC_REQUEST
[XBee] RX 0x90 (1234 bytes): {"cmd":"SYNC_DATA","users":[...],...}
[Node] Sync complete — now in RUNNING state
```

---

## License

This project is part of the HopFog ecosystem. See the [HopFog-Web](https://github.com/hyoono/HopFog-Web) repository for licensing information.