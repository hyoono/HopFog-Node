# HopFog Node – Range Extender

A headless node that connects to the [HopFog-Web](https://github.com/hyoono/HopFog-Web) admin system via XBee radio and extends its range by replicating API services locally.

## Supported Platforms

| Platform | XBee Serial | Storage | Build / Run |
|----------|------------|---------|-------------|
| **ESP32-CAM** (AI-Thinker) | Hardware UART2 (GPIO 13 RX / GPIO 12 TX) | SD card (SD_MMC 1-bit) | `pio run -e esp32cam` |
| **Wemos D1 Mini** (ESP8266) | SoftwareSerial (D5 RX / D6 TX) | LittleFS (on-chip flash) | `pio run -e d1_mini` |
| **Windows PC** (proof of concept) | USB serial (pyserial) | Local JSON files | `python pc_node/hopfog_node.py` |

## Overview

The HopFog Node acts as a **router / range extender** for the HopFog network:

- Creates its own **WiFi Access Point** ("HopFog-Network") so mobile devices can connect
- Built-in **DNS server** resolves `hopfog.com` to the node's own IP — the [HopFogMobile](https://github.com/MasterRoxy/HopFogMobile) app works without any URL changes
- Communicates with the admin ESP32-CAM over **XBee** (Serial)
- Exposes the same **REST API endpoints** as hopfog.com so the mobile app and admin tools work seamlessly
- Stores data locally (**SD card** on ESP32-CAM, **LittleFS** on D1 Mini)
- **No web interface** – purely headless JSON API
- Auto-registers with the admin and sends periodic heartbeats
- Relays messages and fog-node registrations back to the admin

## Hardware Requirements

### ESP32-CAM variant

| Component | Notes |
|-----------|-------|
| ESP32-CAM (AI-Thinker) | Same board used by the admin |
| XBee module (e.g. XBee S2C / XBee3) | For long-range serial link to admin |
| MicroSD card (4-32 GB, FAT32) | Persistent local storage |
| FTDI / USB-to-TTL programmer | For uploading firmware |
| 5 V power supply (≥ 500 mA) | USB or external |

### Wemos D1 Mini variant (alternative)

| Component | Notes |
|-----------|-------|
| Wemos D1 Mini (ESP8266) | Compact, USB-powered dev board |
| XBee module (e.g. XBee S2C / XBee3) | For long-range serial link to admin |
| 3.3 V logic-level shifter | If XBee is 3.3 V only (D1 GPIOs are 3.3 V) |

No SD card needed – data is stored in on-chip LittleFS flash.

## Quick Start (PlatformIO)

1. Install [PlatformIO](https://platformio.org/install) (VS Code extension or CLI).
2. Copy `include/config.h.example` → `include/config.h` and fill in your WiFi credentials, node ID, etc.
3. Build and upload:

```bash
# ESP32-CAM
pio run -e esp32cam -t upload

# Wemos D1 Mini
pio run -e d1_mini -t upload
```

4. Open Serial Monitor at 115200 baud:

```bash
pio device monitor
```

## Quick Start (Windows PC – Proof of Concept)

No embedded hardware needed. Plug in an XBee via USB, enable your
PC's Wi-Fi hotspot, and run:

```bash
cd pc_node
pip install -r requirements.txt
copy config.example.json config.json   # edit with your COM port
python hopfog_node.py
```

See [`pc_node/README.md`](pc_node/README.md) for full details.

## API Endpoints

All responses are JSON. No authentication is required.
Endpoint paths match [hopfog.com](https://github.com/hyoono/HopFog-Web) so that
the [HopFogMobile](https://github.com/MasterRoxy/HopFogMobile) app and admin
tools work seamlessly — **no URL changes needed** in the mobile app.

### Admin / Device Management

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/health` | Node health check |
| GET | `/api/stats` | Node statistics (heap, uptime, counts) |
| GET | `/api/fog-devices` | List fog devices (local cache) |
| POST | `/api/fog-devices/register` | Register a fog device (also relayed to admin) |
| GET | `/api/messages` | List messages (local cache) |
| POST | `/api/messages` | Send a message (also relayed to admin) |
| POST | `/api/xbee/broadcast` | Forward raw JSON to admin via XBee |

### Mobile App (same as hopfog.com)

| Method | Path | Description |
|--------|------|-------------|
| POST | `/login` | Authenticate a mobile user |
| GET | `/status` | Server online check |
| GET | `/conversations` | List conversations for a user |
| GET | `/messages` | Get messages for a conversation |
| POST | `/send` | Send a chat message |
| GET | `/users` | List available users |
| POST | `/create-chat` | Find or create a 1-on-1 chat |
| POST | `/sos` | Create an SOS chat with admin |
| GET | `/new-messages` | Poll for new messages |
| POST | `/agree-sos` | Mark SOS agreement |
| POST | `/change-password` | Change password (relayed to admin) |
| GET | `/announcements` | Get announcements |

### Example – send a chat message

```bash
curl -X POST http://hopfog.com/send \
     -H "Content-Type: application/json" \
     -d '{"conversation_id":1,"sender_id":2,"message_text":"Hello!"}'
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
├── platformio.ini            # PlatformIO build configuration
├── src/
│   └── main.cpp              # Firmware (both boards via #ifdef)
├── include/
│   └── config.h.example      # Configuration template
├── pc_node/                   # Windows PC proof of concept
│   ├── hopfog_node.py         # Python node (Flask + pyserial)
│   ├── requirements.txt       # Python dependencies
│   ├── config.example.json    # Configuration template
│   └── README.md              # PC-specific setup guide
├── test_api.py               # Python API test script
├── ARCHITECTURE.md            # System architecture document
├── WIRING_GUIDE.md            # Hardware wiring details
├── Readme.md                  # This file
└── .gitignore
```

## Testing

After flashing the node and confirming WiFi connection via Serial Monitor:

```bash
python test_api.py <node-ip>
```

## Differences from HopFog-Web (Admin)

| Feature | Admin (HopFog-Web) | Node (this repo) |
|---------|-------------------|-------------------|
| Web dashboard | ✅ HTML UI | ❌ Headless JSON API only |
| Authentication | ✅ Login / token | ❌ Open API |
| Database | JSON on SD card | JSON on SD card, LittleFS, or local files |
| XBee role | Coordinator | Router / end-device |
| Message flow | Originates broadcasts | Relays to/from admin |

## License

Same as the original HopFog-Web project.

## See Also

- [HopFog-Web](https://github.com/hyoono/HopFog-Web) – Admin side
- [ARCHITECTURE.md](ARCHITECTURE.md) – Detailed architecture
- [WIRING_GUIDE.md](WIRING_GUIDE.md) – Hardware wiring