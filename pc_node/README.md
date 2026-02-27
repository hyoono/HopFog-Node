# HopFog Node – Windows PC Proof of Concept

A desktop version of the HopFog Node firmware that runs as a Python
script on Windows (or macOS / Linux). It exposes the same REST API
and speaks the same XBee protocol, but uses a USB-connected XBee
instead of GPIO serial and local JSON files instead of SD / LittleFS.

The mobile app ([HopFogMobile](https://github.com/MasterRoxy/HopFogMobile))
works with this node **without any URL changes** — just point DNS for
`hopfog.com` at the PC's hotspot IP.

> **For a full step-by-step deployment walkthrough, see
> [`PC_SETUP_GUIDE.md`](../PC_SETUP_GUIDE.md).**

## How It Works

```
┌─────────────────────────────────────────────────────────────┐
│  Windows PC  (Wi-Fi hotspot enabled)                        │
│                                                             │
│  ┌───────────────────────────┐  ┌────────────────────────┐ │
│  │  hopfog_node.py           │  │  XBee USB Adapter      │ │
│  │  Flask REST API :8080     │  │  (e.g. Sparkfun        │ │
│  │  + XBee serial thread     │──│   XBee Explorer)       │ │
│  └───────────────────────────┘  └────────────────────────┘ │
│               ▲                            │               │
│          WiFi │                       XBee │ radio         │
│               │                            ▼               │
│        ┌──────────┐                ┌─────────────┐         │
│        │ Client   │                │  Admin      │         │
│        │ devices  │                │  ESP32-CAM  │         │
│        └──────────┘                └─────────────┘         │
└─────────────────────────────────────────────────────────────┘
```

## Prerequisites

- **Python 3.10+**
- **XBee USB adapter** (e.g. Sparkfun XBee Explorer, Digi XBIB, or any
  FTDI-based XBee breakout with a USB port)
- **Windows Mobile Hotspot** enabled so client devices can connect

## Quick Start

```bash
cd pc_node

# Install dependencies
pip install -r requirements.txt

# Copy and edit configuration
copy config.example.json config.json   # Windows
# cp config.example.json config.json   # macOS / Linux

# Edit config.json – set your XBee COM port, etc.

# Run
python hopfog_node.py
```

### Command-Line Overrides

```bash
python hopfog_node.py --port COM5 --http-port 9090 --node-id pc-test
```

| Flag | Description | Default |
|------|-------------|---------|
| `--config` | Path to config JSON | `config.json` |
| `--port` | XBee serial port | `COM3` |
| `--baud` | XBee baud rate | `9600` |
| `--http-port` | HTTP API port | `8080` |
| `--node-id` | Node identifier | `pc-node-01` |

## Finding the XBee COM Port

1. Plug in the XBee USB adapter.
2. Open **Device Manager** → **Ports (COM & LPT)**.
3. Note the COM port number (e.g. `COM3`).
4. Put it in `config.json` or pass `--port COM3`.

On macOS / Linux the port is usually `/dev/ttyUSB0` or
`/dev/tty.usbserial-*`.

## Setting Up the Windows Hotspot

1. **Settings → Network & Internet → Mobile hotspot** → turn **On**.
2. Note the hotspot IP (usually `192.168.137.1`).
3. Connect client devices to the hotspot.
4. Clients reach the API at `http://192.168.137.1:8080/api/health`.

## API Endpoints

Same as the embedded firmware and hopfog.com:

### Admin / Device Management

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/health` | Health check |
| GET | `/api/stats` | Node statistics |
| GET | `/api/fog-devices` | List fog devices |
| POST | `/api/fog-devices/register` | Register fog device (relayed to admin) |
| GET | `/api/messages` | List messages |
| POST | `/api/messages` | Send message (relayed to admin) |
| POST | `/api/xbee/broadcast` | Forward raw JSON to admin via XBee |

### Mobile App (same paths as hopfog.com)

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

### Example

```bash
curl http://localhost:8080/api/health
curl -X POST http://localhost:8080/send \
     -H "Content-Type: application/json" \
     -d '{"conversation_id":1,"sender_id":2,"message_text":"Hello!"}'
```

## Running Without XBee

If no XBee is plugged in, the script prints a warning and continues
with the REST API only. This is useful for testing the HTTP side
without any hardware.

## Data Storage

JSON files are stored in a local `hopfog_data/` directory (configurable):

```
hopfog_data/
├── fog_nodes.json       # Fog device registry
├── messages.json        # Admin relay messages
├── stats.json           # Node statistics
├── users.json           # Mobile user accounts (synced from admin)
├── conversations.json   # Chat conversations
├── chat_messages.json   # Chat message history
└── announcements.json   # Admin announcements
```

## Testing

Use the existing test script from the project root:

```bash
python ../test_api.py localhost:8080
```

## Limitations (Proof of Concept)

- Single-threaded Flask dev server (not production-grade).
- No TLS / authentication (same as the firmware).
- XBee serial errors are logged but not retried automatically.
- Intended for local prototyping only.
