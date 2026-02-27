# HopFog Node – Windows PC Proof of Concept

A desktop version of the HopFog Node firmware that runs as a Python
script on Windows (or macOS / Linux). It exposes the same REST API
and speaks the same XBee protocol, but uses a USB-connected XBee
instead of GPIO serial and local JSON files instead of SD / LittleFS.

> **This is a proof-of-concept** for testing the HopFog architecture
> before embedded hardware arrives.

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

Same as the embedded firmware:

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/health` | Health check |
| GET | `/api/stats` | Node statistics |
| GET | `/api/fognodes` | List fog nodes |
| POST | `/api/fognodes/add` | Add fog node (relayed to admin) |
| GET | `/api/messages` | List messages |
| POST | `/api/messages/add` | Add message (relayed to admin) |
| POST | `/api/relay` | Forward raw JSON to admin via XBee |

### Example

```bash
curl http://localhost:8080/api/health
curl -X POST http://localhost:8080/api/messages/add \
     -d "from=alice&to=bob&message=Hello+from+PC"
```

## Running Without XBee

If no XBee is plugged in, the script prints a warning and continues
with the REST API only. This is useful for testing the HTTP side
without any hardware.

## Data Storage

JSON files are stored in a local `hopfog_data/` directory (configurable):

```
hopfog_data/
├── fog_nodes.json
├── messages.json
└── stats.json
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
