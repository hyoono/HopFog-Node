# HopFog Node – ESP32-CAM Range Extender

A headless ESP32-CAM node that connects to the [HopFog-Web](https://github.com/hyoono/HopFog-Web) admin system via XBee radio and extends its range by replicating API services locally.

## Overview

The HopFog Node acts as a **router / range extender** for the HopFog network:

- Communicates with the admin ESP32-CAM over **XBee** (Serial)
- Exposes the same **REST API endpoints** so nearby clients can talk to the node instead of the admin
- Stores data locally on an **SD card** (same JSON format as the admin)
- **No web interface** – purely headless JSON API
- Auto-registers with the admin and sends periodic heartbeats
- Relays messages and fog-node registrations back to the admin

## Hardware Requirements

| Component | Notes |
|-----------|-------|
| ESP32-CAM (AI-Thinker) | Same board used by the admin |
| XBee module (e.g. XBee S2C / XBee3) | For long-range serial link to admin |
| MicroSD card (4-32 GB, FAT32) | Persistent local storage |
| FTDI / USB-to-TTL programmer | For uploading firmware |
| 5 V power supply (≥ 500 mA) | USB or external |
| Logic-level shifter (3.3 V) | If XBee is 3.3 V only |

## Quick Start

1. **Install Arduino IDE** and ESP32 board support ([guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)).
2. Install the **ArduinoJson** library (v6.x) via *Tools → Manage Libraries*.
3. Copy `esp32_node/config.h.example` to `esp32_node/config.h` and fill in your WiFi credentials, node ID, and XBee pins.
4. Open `esp32_node/esp32_node.ino` in Arduino IDE.
5. Select board **AI Thinker ESP32-CAM** and the correct serial port.
6. Connect IO0 → GND, upload, then disconnect IO0 and press RESET.
7. Open Serial Monitor at 115200 baud to see the node boot.

## API Endpoints

All responses are JSON. No authentication is required.

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/health` | Node health check |
| GET | `/api/stats` | Node statistics (heap, uptime, counts) |
| GET | `/api/fognodes` | List fog nodes (local cache) |
| POST | `/api/fognodes/add` | Add a fog node (also relayed to admin) |
| GET | `/api/messages` | List messages (local cache) |
| POST | `/api/messages/add` | Add a message (also relayed to admin) |
| POST | `/api/relay` | Forward raw JSON to admin via XBee |

### Example – add a message

```bash
curl -X POST http://<node-ip>/api/messages/add \
     -d "from=alice&to=bob&message=Hello+from+node"
```

## XBee Communication Protocol

The node and admin exchange newline-delimited JSON over XBee serial. Each frame has at least a `cmd` field.

### Node → Admin

| Command | Description |
|---------|-------------|
| `REGISTER` | Node startup registration |
| `HEARTBEAT` | Periodic keep-alive with stats |
| `SYNC_REQUEST` | Ask admin for a full data dump |
| `RELAY_MSG` | Forward a user message |
| `RELAY_FOG_NODE` | Forward a fog-node registration |
| `STATS_RESPONSE` | Reply to admin's `GET_STATS` |

### Admin → Node

| Command | Description |
|---------|-------------|
| `REGISTER_ACK` | Acknowledge registration |
| `PONG` | Heartbeat response |
| `SYNC_DATA` | Full data payload (fog nodes + messages) |
| `BROADCAST_MSG` | Push a message to this node |
| `ADD_FOG_NODE` | Push a fog-node record |
| `GET_STATS` | Request this node's stats |

## Project Structure

```
├── esp32_node/
│   ├── esp32_node.ino      # Main firmware
│   ├── config.h.example    # Configuration template
│   └── test_api.py         # Python API test script
├── ARCHITECTURE.md          # System architecture document
├── WIRING_GUIDE.md          # ESP32-CAM + XBee wiring details
├── Readme.md                # This file
└── .gitignore
```

## Testing

After flashing the node and confirming WiFi connection via Serial Monitor:

```bash
cd esp32_node
python test_api.py <node-ip>
```

## Differences from HopFog-Web (Admin)

| Feature | Admin (HopFog-Web) | Node (this repo) |
|---------|-------------------|-------------------|
| Web dashboard | ✅ HTML UI | ❌ Headless JSON API only |
| Authentication | ✅ Login / token | ❌ Open API |
| Database | JSON on SD card | JSON on SD card (same format) |
| XBee role | Coordinator | Router / end-device |
| Message flow | Originates broadcasts | Relays to/from admin |

## License

Same as the original HopFog-Web project.

## See Also

- [HopFog-Web](https://github.com/hyoono/HopFog-Web) – Admin side
- [ARCHITECTURE.md](ARCHITECTURE.md) – Detailed architecture
- [WIRING_GUIDE.md](WIRING_GUIDE.md) – Hardware wiring