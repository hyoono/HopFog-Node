# HopFog Node – Architecture

## System Overview

```
┌───────────────────────────────────────────────────────────────────────┐
│                        HopFog Network                                │
└───────────────────────────────────────────────────────────────────────┘

                       XBee Radio Link
┌──────────────────┐ ◄────────────────────► ┌──────────────────────────┐
│  HopFog Admin    │   (900 MHz / 2.4 GHz)  │    HopFog Node           │
│  (HopFog-Web)    │                         │    (this repo)           │
│  ESP32-CAM       │                         │    ESP32-CAM  or         │
│  + Web UI        │                         │    Wemos D1 Mini         │
│  + Auth / DB     │                         │    Headless API only     │
└──────────────────┘                         └──────────────────────────┘
        ▲                                              ▲
        │ WiFi                                         │ WiFi
        ▼                                              ▼
   ┌──────────┐                                   ┌──────────┐
   │ Admin's  │                                   │ Client   │
   │ Browser  │                                   │ devices  │
   └──────────┘                                   └──────────┘
```

The **Node** extends the admin's coverage area. Any client within WiFi
range of the node can use the same REST API calls. The node stores data
locally and relays changes back to the admin via XBee.

---

## Board Variants

| | ESP32-CAM (AI-Thinker) | Wemos D1 Mini (ESP8266) |
|---|---|---|
| CPU | Dual-core 240 MHz | Single-core 80 MHz |
| RAM | 520 KB | 80 KB |
| Flash | 4 MB | 4 MB |
| XBee serial | Hardware UART2 (GPIO 13 / 12) | SoftwareSerial (D5 / D6) |
| Storage | SD card (SD_MMC 1-bit) | LittleFS (on-chip flash) |
| PlatformIO env | `esp32cam` | `d1_mini` |

---

## Component Breakdown

### 1. Hardware Layer

#### ESP32-CAM
```
┌─────────────────────────────────────────────────────┐
│             ESP32-CAM (AI-Thinker)                  │
│                                                     │
│  ┌────────────────────────────────────────────┐    │
│  │  ESP32 Dual-Core 240 MHz                   │    │
│  │  520 KB RAM · 4 MB Flash                   │    │
│  └────────────────────────────────────────────┘    │
│                                                     │
│  ┌───────────────┐  ┌───────────────────────┐     │
│  │  SD Card Slot │  │  WiFi 802.11 b/g/n    │     │
│  │  FAT32, ≤32GB │  │  2.4 GHz              │     │
│  └───────────────┘  └───────────────────────┘     │
│                                                     │
│  Camera hardware present but NOT used               │
└─────────────────────────────────────────────────────┘
         │ GPIO 13 (RX)  /  GPIO 12 (TX)
         ▼
┌─────────────────────────────────────────────────────┐
│              XBee Radio Module                      │
│  UART 9600 baud · Mesh / P2P topology              │
└─────────────────────────────────────────────────────┘
```

#### Wemos D1 Mini
```
┌─────────────────────────────────────────────────────┐
│             Wemos D1 Mini (ESP8266)                 │
│                                                     │
│  ┌────────────────────────────────────────────┐    │
│  │  ESP8266 Single-Core 80 MHz                │    │
│  │  80 KB RAM · 4 MB Flash                    │    │
│  └────────────────────────────────────────────┘    │
│                                                     │
│  ┌───────────────────────┐                         │
│  │  WiFi 802.11 b/g/n    │                         │
│  │  2.4 GHz              │                         │
│  └───────────────────────┘                         │
│                                                     │
│  Built-in USB for programming + serial              │
└─────────────────────────────────────────────────────┘
         │ D5/GPIO 14 (RX)  /  D6/GPIO 12 (TX)
         │ (SoftwareSerial)
         ▼
┌─────────────────────────────────────────────────────┐
│              XBee Radio Module                      │
│  UART 9600 baud · Mesh / P2P topology              │
└─────────────────────────────────────────────────────┘
```

### 2. Software Stack

```
┌─────────────────────────────────────────────────────┐
│                   API Layer                          │
│  ┌─────────────────────────────────────────────┐   │
│  │  REST API (JSON only – no HTML)             │   │
│  │  /api/health · /api/stats · /api/fognodes   │   │
│  │  /api/messages · /api/relay                 │   │
│  └─────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
                        │
┌─────────────────────────────────────────────────────┐
│              Communication Layer                     │
│  ┌─────────────────────┐  ┌─────────────────────┐  │
│  │  WiFi WebServer     │  │  XBee Serial         │  │
│  │  HTTP req/resp      │  │  JSON line protocol  │  │
│  └─────────────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────┘
                        │
┌─────────────────────────────────────────────────────┐
│              Storage Layer                           │
│  ┌─────────────────────────────────────────────┐   │
│  │  ESP32-CAM: SD Card (SD_MMC 1-bit)          │   │
│  │  D1 Mini:   LittleFS (on-chip flash)        │   │
│  │                                              │   │
│  │  /hopfog/fog_nodes.json                     │   │
│  │  /hopfog/messages.json                      │   │
│  │  /hopfog/stats.json                         │   │
│  └─────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

### 3. Data Flow

```
┌──────────┐         ┌──────────────┐         ┌────────────┐
│  Client  │  HTTP   │  Node        │  XBee   │  Admin     │
│  device  │ ──────► │  (any board) │ ──────► │  ESP32-CAM │
│          │ ◄────── │              │ ◄────── │            │
└──────────┘  JSON   └──────────────┘  JSON   └────────────┘
                          │    ▲
                     write│    │read
                          ▼    │
                     ┌──────────────┐
                     │  Storage     │
                     │  SD / LFS   │
                     └──────────────┘
```

1. Client sends HTTP request to the node.
2. Node processes locally (read / write storage).
3. Node relays the change to admin via XBee.
4. Admin may push updates back (sync, broadcasts).

### 4. XBee Protocol

All XBee traffic is newline-delimited JSON:

```json
{"cmd":"HEARTBEAT","node_id":"node-01","ts":12345,"params":{...}}
```

| Direction | Commands |
|-----------|----------|
| Node → Admin | `REGISTER`, `HEARTBEAT`, `SYNC_REQUEST`, `RELAY_MSG`, `RELAY_FOG_NODE`, `STATS_RESPONSE` |
| Admin → Node | `REGISTER_ACK`, `PONG`, `SYNC_DATA`, `BROADCAST_MSG`, `ADD_FOG_NODE`, `GET_STATS` |

### 5. API Endpoint Map

```
/api/
├── GET  /api/health         → {"status":"ok","node_id":"...","uptime":...}
├── GET  /api/stats          → System statistics
├── GET  /api/fognodes       → List fog nodes (local cache)
├── POST /api/fognodes/add   → Add fog node + relay to admin
├── GET  /api/messages       → List messages (local cache)
├── POST /api/messages/add   → Add message + relay to admin
└── POST /api/relay          → Forward raw JSON to admin via XBee
```

### 6. Storage Database

Same schema as admin for compatibility:

```
/hopfog/
├── fog_nodes.json   # [{id, device_name, ip_address, status, added_at}, ...]
├── messages.json    # [{id, from, to, message, timestamp, node}, ...]
└── stats.json       # {fog_nodes_count, active_fog_nodes, total_messages}
```

### 7. Timing

| Event | Interval |
|-------|----------|
| Heartbeat to admin | 30 s (configurable) |
| Data sync request | 60 s (configurable) |
| WiFi reconnect attempt | On heartbeat if disconnected |

### 8. Memory Budget

#### ESP32-CAM
```
┌──────────────────────────────────────────┐
│  Flash (4 MB)                            │
│  └─ Program code           ~150 KB       │
├──────────────────────────────────────────┤
│  RAM (520 KB)                            │
│  ├─ WebServer buffers       ~60 KB       │
│  ├─ JSON parsing            ~20 KB       │
│  ├─ XBee buffers            ~4 KB        │
│  ├─ Stack & variables       ~30 KB       │
│  └─ Free heap              ~400 KB       │
├──────────────────────────────────────────┤
│  SD Card (up to 32 GB)                   │
│  └─ Persistent JSON database             │
└──────────────────────────────────────────┘
```

#### Wemos D1 Mini
```
┌──────────────────────────────────────────┐
│  Flash (4 MB)                            │
│  ├─ Program code           ~300 KB       │
│  └─ LittleFS partition     ~1 MB         │
│     └─ Persistent JSON database          │
├──────────────────────────────────────────┤
│  RAM (80 KB)                             │
│  ├─ WebServer buffers       ~8 KB        │
│  ├─ JSON parsing            ~10 KB       │
│  ├─ SoftwareSerial buf      ~1 KB        │
│  ├─ Stack & variables       ~10 KB       │
│  └─ Free heap              ~40 KB        │
└──────────────────────────────────────────┘
```

### 9. Security Model

```
┌──────────────────────────────────────────┐
│  No authentication on node API           │
│  └─ Suitable for trusted local network   │
│                                          │
│  XBee link is unencrypted by default     │
│  └─ Enable XBee AES encryption if needed │
│                                          │
│  HTTP only (no TLS)                      │
│  └─ Do not expose to internet            │
└──────────────────────────────────────────┘
```

### 10. Deployment Topology

```
                    ┌─────────────┐
                    │   Admin     │
                    │  ESP32-CAM  │
                    │  + XBee     │
                    └──────┬──────┘
                           │ XBee
              ┌────────────┼────────────┐
              │            │            │
       ┌──────▼──────┐ ┌──▼──────┐ ┌──▼──────┐
       │   Node A    │ │  Node B │ │  Node C │
       │  ESP32-CAM  │ │  D1 Mini│ │  D1 Mini│
       │  + XBee     │ │  + XBee │ │  + XBee │
       └─────────────┘ └─────────┘ └─────────┘
            ▲               ▲           ▲
       WiFi │          WiFi │      WiFi │
            ▼               ▼           ▼
       ┌─────────┐   ┌─────────┐  ┌─────────┐
       │ Clients │   │ Clients │  │ Clients │
       └─────────┘   └─────────┘  └─────────┘
```

Multiple nodes can be deployed to blanket a larger area. Each node
independently caches data and relays changes to the central admin.
Nodes can be a mix of ESP32-CAM and Wemos D1 Mini boards.
