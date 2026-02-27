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
│  ESP32-CAM       │                         │    ESP32-CAM             │
│  + Web UI        │                         │    Headless API only     │
│  + Auth / DB     │                         │    + Local SD cache      │
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
locally on its SD card and relays changes back to the admin via XBee.

---

## Component Breakdown

### 1. Hardware Layer

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
         │ GPIO 32 (RX)  /  GPIO 33 (TX)
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
│  │  WiFi WebServer     │  │  XBee Serial (UART2)│  │
│  │  HTTP req/resp      │  │  JSON line protocol │  │
│  └─────────────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────┘
                        │
┌─────────────────────────────────────────────────────┐
│              Storage Layer                           │
│  ┌─────────────────────────────────────────────┐   │
│  │  SD Card (SD_MMC 1-bit)                     │   │
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
│  device  │ ──────► │  ESP32-CAM   │ ──────► │  ESP32-CAM │
│          │ ◄────── │              │ ◄────── │            │
└──────────┘  JSON   └──────────────┘  JSON   └────────────┘
                          │    ▲
                     write│    │read
                          ▼    │
                     ┌──────────────┐
                     │   SD Card    │
                     │  (JSON DB)   │
                     └──────────────┘
```

1. Client sends HTTP request to the node.
2. Node processes locally (read / write SD card).
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

### 6. SD Card Database

Same schema as admin for compatibility:

```
/sdcard/hopfog/
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

```
┌──────────────────────────────────────────┐
│  Flash (4 MB)                            │
│  ├─ Program code           ~150 KB       │
│  └─ (no HTML / PROGMEM pages)            │
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

Because the node has **no HTML pages** stored in PROGMEM, it has
significantly more free heap than the admin.

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
       │  ESP32-CAM  │ │         │ │         │
       │  + XBee     │ │         │ │         │
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
