# HopFog-Node

ESP32-CAM fog-computing node for the [HopFog](https://github.com/hyoono/HopFog-Web) network. The node connects to the admin coordinator over XBee ZigBee RF (2 XBee modules total — one coordinator, one router), creates its own WiFi access point, and serves a mobile-app REST API locally — enabling offline-first communication in disaster or infrastructure-limited scenarios.

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
- [Current Setup: Single Node](#current-setup-single-node)
- [Troubleshooting](#troubleshooting)
  - [SD_MMC vs SPI SD (Why SPI?)](#sd_mmc-vs-spi-sd-why-spi)
  - [Debugging Checklist](#debugging-checklist)
- [License](#license)

---

## Architecture Overview

This is a **2-XBee-module setup**: one coordinator (admin) and one router (node).

```
┌──────────────────────────────────────────────────────────┐
│                   ADMIN  (HopFog-Web)                    │
│  ESP32-CAM + XBee S2C (Coordinator, AP=1, CE=1)         │
│  WiFi AP "HopFog-Network"                                │
│  Admin dashboard + mobile app API                        │
│  SD card: users, broadcasts, messages, conversations     │
└────────────────────────┬─────────────────────────────────┘
                         │  XBee ZigBee RF (point-to-point)
                         │  API mode 1 binary frames
                         │  JSON payloads
┌────────────────────────┴─────────────────────────────────┐
│                   NODE  (HopFog-Node)                     │
│  ESP32-CAM + XBee S2C (Router, AP=1, CE=0)               │
│  WiFi AP "HopFog-Node-01"                                │
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

You need **2 XBee modules** total — one for the admin and one for the node.

| Component | Qty | Description |
|-----------|-----|-------------|
| AI-Thinker ESP32-CAM | 2 | One for admin, one for node |
| Digi XBee S2C or S2C Pro | 2 | One configured as Coordinator (admin), one as Router (node) |
| XBee breakout board | 2 | For breadboard mounting |
| Micro SD card | 2 | FAT32 formatted, one per ESP32-CAM |
| USB-to-serial programmer | 1 | FTDI or CP2102 for flashing |

---

## Wiring Guide

### ESP32-CAM ↔ XBee

```
ESP32-CAM            XBee Module
──────────           ───────────
GPIO 1  (TX)  ─────► DIN   (pin 3)
GPIO 3  (RX)  ◄───── DOUT  (pin 2)
3.3V          ─────► VCC   (pin 1)
GND           ─────► GND   (pin 10)
```

> **⚠️ GPIO 1/3 are the USB programming pins.** Disconnect the XBee before uploading firmware. USB Serial Monitor is **not available** when XBee is connected — use the admin web serial monitor (`/admin/messaging/testing`) for debugging instead.

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

Configure **both** XBee modules using Digi's [XCTU](https://www.digi.com/products/embedded-systems/digi-xbee/digi-xbee-tools/xctu) software **before** connecting them to the ESP32-CAMs. You need exactly 2 modules — one coordinator (admin) and one router (node).

### Module 1: Admin XBee (Coordinator)

| Parameter | Value | Description |
|-----------|-------|-------------|
| **AP** | `1` | API mode 1 (binary frames, no escaping) |
| **CE** | `1` | **Coordinator** — forms the network |
| **ID** | `1234` | PAN ID — must match the node XBee |
| **BD** | `3` | 9600 baud |

### Module 2: Node XBee (Router)

| Parameter | Value | Description |
|-----------|-------|-------------|
| **AP** | `1` | API mode 1 (binary frames, no escaping) |
| **CE** | `0` | **Router** — joins the coordinator's network |
| **ID** | `1234` | PAN ID — **must match the admin XBee** |
| **BD** | `3` | 9600 baud |
| **JV** | `1` | Join verification (router joins on power-up) |
| **DH** | `0` | Destination address high (broadcast) |
| **DL** | `FFFF` | Destination address low (broadcast) |

**After writing settings, power-cycle both XBee modules before connecting them to the ESP32-CAMs.**

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

USB Serial Monitor is **not available** when XBee is connected to GPIO 1/3 (the UART0 pins). All debug output is compiled out.

To debug XBee communication, use the admin web serial monitor at `/admin/messaging/testing` — it shows TX/RX activity in real time.

When XBee is **disconnected** (e.g., during development without XBee hardware), you can temporarily re-enable debug output by removing or commenting out the `XBEE_USES_UART0` define in `config.h` and rebuilding.

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
#define AP_SSID       "HopFog-Node-01"   // Node's WiFi network name
#define AP_PASSWORD   "changeme123"       // Set a strong password
#define AP_CHANNEL    6                   // Avoid channel 1 (admin)

// ── Node Identity ───────────────────────────────────────
#define NODE_ID       "node-01"           // Node identifier
#define DEVICE_NAME   "HopFog-Node-01"    // Human-readable name

// ── XBee (UART0) ───────────────────────────────────────
#define XBEE_TX_PIN   1                   // U0TXD → XBee DIN
#define XBEE_RX_PIN   3                   // U0RXD ← XBee DOUT
#define XBEE_BAUD     9600

// ── Timing ──────────────────────────────────────────────
#define REGISTER_INTERVAL_MS   10000      // REGISTER retry (ms)
#define HEARTBEAT_INTERVAL_MS  30000      // HEARTBEAT interval (ms)
#define SYNC_RETRY_MS          15000      // SYNC_REQUEST retry (ms)
```

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

## Current Setup: Single Node

This firmware is configured for a **2-XBee-module setup**: one admin coordinator and one node router. The node identity is hardcoded in `include/config.h`:

```cpp
#define NODE_ID       "node-01"
#define AP_SSID       "HopFog-Node-01"
```

The admin XBee (Coordinator, CE=1) and the node XBee (Router, CE=0) communicate point-to-point using broadcast frames on the same PAN ID (`ID=1234`).

```
  ┌───────┐         ┌───────┐
  │ Admin │◄──RF──►│Node-01│
  │(Coord)│         │(Router)│
  └───────┘         └───────┘
```

---

## Troubleshooting

### SD_MMC vs SPI SD (Why SPI?)

**This was the #1 cause of "no XBee communication" on ESP32-CAM boards.**

The ESP32-CAM's SD card slot can be accessed via either SD_MMC or SPI. The SD_MMC peripheral permanently claims GPIO 12 and 13 via **IOMUX** (as HS2_DATA2/HS2_DATA3), even in 1-bit mode. Since the XBee uses UART0 (GPIO 1 TX, GPIO 3 RX), using SD_MMC can cause unexpected pin conflicts.

**Solution (already applied):** The firmware uses **SPI mode** (`SD.begin()` with the HSPI bus) instead of `SD_MMC.begin()`. SPI mode uses only GPIO 13 (CS), 14 (CLK), 2 (MISO), and 15 (MOSI).

### Debugging Checklist

#### 1. Verify XBee hardware (2 modules)

- Power on both admin (coordinator) and node (router).
- Check XBee **ASSOC** LED: blinking = searching, steady = joined.
- In XCTU: read `AI` parameter on the node XBee → must be `0x00`.
- Verify both modules have the same PAN ID (`ID=1234`).

#### 2. Verify XBee diagnostics via web API

Connect to the node's WiFi AP (`HopFog-Node-01`) and browse to:

```
http://192.168.4.1/api/xbee/status
```

Check these values:
- `totalRxBytes > 0` — UART0 RX is working
- `txStatusOK > 0` — XBee is responding to TX frames
- `rxFramesParsed > 0` — Remote device data received

If `totalRxBytes == 0`:
- Check wiring: XBee DOUT → GPIO 3 (U0RXD)
- Check XBee power: 3.3V (NOT 5V)
- Check AP mode: AP must be 1 (not 0) in XCTU
- Verify `ets_install_putc1(nullPutc)` is the FIRST line in `setup()`

#### 3. Verify TX (node → admin)

- On the admin, open the Testing page → Serial Monitor section.
- Wait for the node to send REGISTER (every 10 seconds).
- Admin should show: `RX <- [0x90] from XXXX (...bytes) {"cmd":"REGISTER",...}`
- If admin shows nothing, the node's XBee is not transmitting or the XBees are not associated.

#### 4. Verify RX (admin → node)

- On the admin, click "Send Test Message".
- Check the node's XBee diagnostics page — `rxFramesParsed` should increment.
- If it doesn't, check the wiring (GPIO 1 → DIN, GPIO 3 ← DOUT).

#### 5. Full handshake

Expected sequence visible via the XBee diagnostics API:

1. Node sends `REGISTER` → `txFramesSent` increments
2. Admin responds with `REGISTER_ACK` → `rxFramesParsed` increments
3. Node sends `SYNC_REQUEST` → `txFramesSent` increments again
4. Admin responds with `SYNC_DATA` → `rxFramesParsed` increments again
5. Node enters `STATE_RUNNING` (state = 3) → visible in `/status` endpoint

---

## License

This project is part of the HopFog ecosystem. See the [HopFog-Web](https://github.com/hyoono/HopFog-Web) repository for licensing information.