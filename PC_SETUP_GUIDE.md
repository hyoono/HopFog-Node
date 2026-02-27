# HopFog Node – PC Deployment Guide

A step-by-step guide for deploying the HopFog Node on a Windows, macOS,
or Linux PC. When finished, mobile phones that connect to the PC's Wi-Fi
hotspot will be able to use the [HopFogMobile](https://github.com/MasterRoxy/HopFogMobile)
app exactly as if they were connected to the main `hopfog.com` server —
**no changes needed in the mobile app**.

---

## Table of Contents

1. [What You Need](#1-what-you-need)
2. [Install Python & Dependencies](#2-install-python--dependencies)
3. [Configure the Node](#3-configure-the-node)
4. [Set Up the Wi-Fi Hotspot](#4-set-up-the-wi-fi-hotspot)
5. [Redirect hopfog.com to the PC (DNS)](#5-redirect-hopfogcom-to-the-pc-dns)
6. [Set Up the XBee Radio (Optional)](#6-set-up-the-xbee-radio-optional)
7. [Seed User Accounts](#7-seed-user-accounts)
8. [Run the Node](#8-run-the-node)
9. [Connect the Mobile App](#9-connect-the-mobile-app)
10. [Verify Everything Works](#10-verify-everything-works)
11. [Running as a Background Service](#11-running-as-a-background-service)
12. [Troubleshooting](#12-troubleshooting)

---

## 1. What You Need

| Item | Required? | Notes |
|------|-----------|-------|
| PC (Windows 10/11, macOS, or Linux) | **Yes** | Any machine with Wi-Fi |
| Python 3.10 or newer | **Yes** | `python --version` to check |
| Wi-Fi adapter | **Yes** | Must support hotspot / AP mode |
| XBee USB adapter | Optional | Only needed for admin relay (e.g. Sparkfun XBee Explorer) |
| XBee module (e.g. XBee S2C, XBee3) | Optional | Paired with an admin node |

> **No XBee?** The node runs fine without one — the REST API still works.
> Messages are stored locally but won't relay to the admin until an XBee
> is connected.

---

## 2. Install Python & Dependencies

```bash
# Check Python version (need 3.10+)
python --version

# Navigate to the pc_node directory
cd pc_node

# Install dependencies
pip install -r requirements.txt
```

This installs:

| Package | Purpose |
|---------|---------|
| `flask` | HTTP REST API server |
| `pyserial` | XBee USB serial communication |

---

## 3. Configure the Node

```bash
# Copy the example config
copy config.example.json config.json     # Windows
# cp config.example.json config.json     # macOS / Linux
```

Edit `config.json`:

```json
{
    "node_id": "pc-node-01",
    "xbee_port": "COM3",
    "xbee_baud": 9600,
    "http_host": "0.0.0.0",
    "http_port": 80,
    "heartbeat_interval": 30,
    "sync_interval": 60,
    "data_dir": "hopfog_data"
}
```

### Key settings

| Field | What to set | Why |
|-------|-------------|-----|
| `node_id` | A unique name for this node | Identifies the node in the admin dashboard |
| `xbee_port` | Your XBee COM port (e.g. `COM5`, `/dev/ttyUSB0`) | See [step 6](#6-set-up-the-xbee-radio-optional) |
| `http_port` | **`80`** (recommended) | The mobile app connects to `http://hopfog.com` (port 80). Using port 80 means the app works without specifying a port. You may need admin/root privileges to bind to port 80. |
| `http_host` | `0.0.0.0` | Listens on all interfaces (hotspot + LAN) |

> **Tip:** You can override any setting from the command line without
> editing the file:
> ```bash
> python hopfog_node.py --port COM5 --http-port 80 --node-id my-pc
> ```

---

## 4. Set Up the Wi-Fi Hotspot

The PC creates a Wi-Fi network that mobile phones connect to.

### Windows 10 / 11

1. Open **Settings → Network & Internet → Mobile hotspot**.
2. Turn **Mobile hotspot** to **On**.
3. Set the network name to **`HopFog-Network`** (or any name you prefer).
4. Set a password (e.g. `hopfog123`).
5. Under **Share my Internet connection from**, pick your active
   connection (Ethernet or another Wi-Fi).
6. Note the **hotspot IP address** (usually `192.168.137.1`).

### macOS

1. Open **System Settings → General → Sharing → Internet Sharing**.
2. Share your connection from **Ethernet** (or Thunderbolt) to
   computers using **Wi-Fi**.
3. Click **Wi-Fi Options…** and set the name to `HopFog-Network`.
4. Enable **Internet Sharing**.
5. The hotspot IP is usually `192.168.2.1`.

### Linux (NetworkManager)

```bash
# Create a hotspot
nmcli device wifi hotspot ifname wlan0 \
    ssid HopFog-Network password hopfog123

# The hotspot IP is usually 10.42.0.1
ip addr show wlan0
```

---

## 5. Redirect hopfog.com to the PC (DNS)

The [HopFogMobile](https://github.com/MasterRoxy/HopFogMobile) app uses
`http://hopfog.com` as its server URL. When a phone is connected to the
PC's hotspot, we need `hopfog.com` to resolve to the PC's hotspot IP
instead of the real internet server.

> **On embedded hardware** (ESP32 / ESP8266), this is handled
> automatically by the built-in DNS server. On a PC you need to set it
> up manually using one of the methods below.

### Option A – Edit the hosts file on each phone (simplest for testing)

If you have rooted/jailbroken phones, add this line to the phone's
hosts file:

```
192.168.137.1   hopfog.com
```

*(Replace `192.168.137.1` with your actual hotspot IP.)*

This isn't practical for many phones, so Option B is preferred.

### Option B – Run a lightweight DNS server on the PC (recommended)

Install and run [dnsmasq](https://thekelleys.org.uk/dnsmasq/doc.html)
(Linux/macOS) or [Acrylic DNS Proxy](https://mayakron.altervista.org/support/acrylic/Home.htm) (Windows).

#### Linux / macOS (dnsmasq)

```bash
# Install
sudo apt install dnsmasq          # Debian / Ubuntu
# brew install dnsmasq            # macOS

# Add a single redirect rule
echo "address=/hopfog.com/192.168.137.1" | sudo tee /etc/dnsmasq.d/hopfog.conf

# Restart
sudo systemctl restart dnsmasq
```

Then configure the hotspot's DHCP to hand out the PC's IP as the DNS
server. With NetworkManager:

```bash
nmcli connection modify Hotspot ipv4.dns 192.168.137.1
nmcli connection up Hotspot
```

#### Windows (Acrylic DNS Proxy)

1. Download and install [Acrylic DNS Proxy](https://mayakron.altervista.org/support/acrylic/Home.htm).
2. Open **AcrylicHosts.txt** and add:

   ```
   192.168.137.1   hopfog.com
   ```

3. Restart the Acrylic service.
4. In the Mobile hotspot network adapter settings, set the DNS server
   to `192.168.137.1`.

#### Windows (Python one-liner — quick test)

For quick testing you can use a tiny Python DNS server. Save this as
`dns_redirect.py` and run it with admin privileges:

```python
"""Tiny DNS server that resolves hopfog.com to the hotspot IP."""
import socket, struct, sys

HOTSPOT_IP = sys.argv[1] if len(sys.argv) > 1 else "192.168.137.1"
DOMAIN = b"\x06hopfog\x03com\x00"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", 53))
print(f"DNS: hopfog.com -> {HOTSPOT_IP}  (Ctrl+C to stop)")

while True:
    data, addr = sock.recvfrom(512)
    # Build a minimal DNS response pointing to HOTSPOT_IP
    tid = data[:2]
    flags = b"\x81\x80"
    counts = b"\x00\x01\x00\x01\x00\x00\x00\x00"
    question = data[12:]
    answer = (b"\xc0\x0c"              # pointer to name in question
              b"\x00\x01\x00\x01"      # type A, class IN
              b"\x00\x00\x00\x3c"      # TTL 60 s
              b"\x00\x04"              # data length 4
              + socket.inet_aton(HOTSPOT_IP))
    sock.sendto(tid + flags + counts + question + answer, addr)
```

```bash
python dns_redirect.py 192.168.137.1
```

Then set your hotspot's DNS to `192.168.137.1`.

### Option C – Use the PC as the phone's proxy

Configure the phone's WiFi proxy to `192.168.137.1:80`. This avoids
DNS entirely but requires per-phone setup.

---

## 6. Set Up the XBee Radio (Optional)

If you have an XBee USB adapter:

1. **Plug in** the XBee USB adapter.
2. **Find the COM port:**

   | OS | How to find it |
   |----|----------------|
   | Windows | Device Manager → Ports (COM & LPT) → e.g. `COM3` |
   | macOS | `ls /dev/tty.usbserial-*` |
   | Linux | `ls /dev/ttyUSB*` |

3. **Set it in config.json** or pass `--port COM3` on the command line.

If you **don't** have an XBee, the node prints a warning and continues
with the REST API only. Messages are stored locally but won't sync with
the admin.

---

## 7. Seed User Accounts

The mobile app's `/login` endpoint checks against locally stored user
data. Before anyone can log in, user accounts must exist on the node.

There are two ways users appear on the node:

### A. Automatic sync from admin (production)

When the admin pushes a `SYNC_DATA` command via XBee, it includes user
accounts, conversations, and announcements. The node stores them
automatically.

### B. Manual seeding (for testing without XBee)

Create a `users.json` file in the data directory:

```bash
# From the pc_node directory:
mkdir -p hopfog_data

cat > hopfog_data/users.json << 'EOF'
[
  {
    "id": 1,
    "username": "admin",
    "email": "admin@hopfog.com",
    "role": "admin",
    "is_active": true,
    "has_agreed_sos": true
  },
  {
    "id": 2,
    "username": "alice",
    "email": "alice@hopfog.com",
    "role": "mobile",
    "is_active": true,
    "has_agreed_sos": false
  },
  {
    "id": 3,
    "username": "bob",
    "email": "bob@hopfog.com",
    "role": "mobile",
    "is_active": true,
    "has_agreed_sos": false
  }
]
EOF
```

> **Note:** The node accepts any password during login (it's a trusted
> local network). Password verification happens on the admin server.

---

## 8. Run the Node

```bash
cd pc_node

# Standard start (port 80 — requires admin/root)
sudo python hopfog_node.py --http-port 80

# Or on Windows (run terminal as Administrator)
python hopfog_node.py --http-port 80

# If you can't use port 80, use 8080 (but the mobile app URL
# would need http://hopfog.com:8080 which requires an app change)
python hopfog_node.py --http-port 8080
```

You should see:

```
HopFog Node – PC Proof of Concept
==================================
[NODE] ID: pc-node-01
[STORAGE] Data directory: hopfog_data
[XBEE] Opened COM3 at 9600 baud       ← or "Running without XBee"
[HTTP] Starting on http://0.0.0.0:80
==================================
```

---

## 9. Connect the Mobile App

1. On the phone, connect to the **HopFog-Network** Wi-Fi.
2. If DNS is configured (step 5), open the HopFogMobile app normally.
   It will connect to `http://hopfog.com` which resolves to the PC.
3. Log in with one of the seeded user accounts (e.g. `alice`).
4. You should see conversations, be able to send messages, and use SOS.

### What the mobile app sees

```
Phone → Wi-Fi "HopFog-Network" → PC hotspot
     → DNS: hopfog.com → 192.168.137.1
     → HTTP: POST /login, GET /conversations, POST /send, …
     → PC node serves all responses locally
     → XBee relays data to admin (if connected)
```

---

## 10. Verify Everything Works

### Quick health check

```bash
curl http://localhost/status
# {"online": true}

curl http://localhost/api/health
# {"status": "ok", "node_id": "pc-node-01", "uptime": 42}
```

### Full test suite

From the project root:

```bash
python test_api.py localhost
# or if using a non-standard port:
python test_api.py localhost:8080
```

Expected output:

```
Passed: 15/15
✓ All tests passed!
```

The test suite covers both admin endpoints (`/api/...`) and mobile
endpoints (`/login`, `/conversations`, `/send`, etc.).

### Manual mobile endpoint tests

```bash
# Login
curl -X POST http://localhost/login \
     -H "Content-Type: application/json" \
     -d '{"username":"alice","password":"anything"}'

# Create a chat
curl -X POST http://localhost/create-chat \
     -H "Content-Type: application/json" \
     -d '{"user1_id":2,"user2_id":3}'

# Send a message
curl -X POST http://localhost/send \
     -H "Content-Type: application/json" \
     -d '{"conversation_id":1,"sender_id":2,"message_text":"Hello Bob!"}'

# Read messages
curl "http://localhost/messages?conversation_id=1&user_id=2"

# List announcements
curl http://localhost/announcements
```

---

## 11. Running as a Background Service

### Windows (Task Scheduler)

1. Open **Task Scheduler** → **Create Task**.
2. Set **Trigger** to "At startup".
3. Set **Action** to:
   - Program: `python`
   - Arguments: `C:\path\to\pc_node\hopfog_node.py --http-port 80`
   - Start in: `C:\path\to\pc_node`
4. Check **Run with highest privileges** (for port 80).

### Linux (systemd)

Create `/etc/systemd/system/hopfog-node.service`:

```ini
[Unit]
Description=HopFog Node – PC Deployment
After=network.target

[Service]
Type=simple
WorkingDirectory=/path/to/pc_node
ExecStart=/usr/bin/python3 hopfog_node.py --http-port 80
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable hopfog-node
sudo systemctl start hopfog-node
```

### macOS (launchd)

Create `~/Library/LaunchAgents/com.hopfog.node.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.hopfog.node</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/bin/python3</string>
        <string>/path/to/pc_node/hopfog_node.py</string>
        <string>--http-port</string>
        <string>80</string>
    </array>
    <key>WorkingDirectory</key>
    <string>/path/to/pc_node</string>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
</dict>
</plist>
```

```bash
launchctl load ~/Library/LaunchAgents/com.hopfog.node.plist
```

---

## 12. Troubleshooting

### "Address already in use" on port 80

Another process is using port 80. Find and stop it:

```bash
# Windows
netstat -aon | findstr :80
taskkill /PID <pid> /F

# Linux / macOS
sudo lsof -i :80
sudo kill <pid>
```

Or use a different port: `--http-port 8080`.

### Mobile app can't connect

| Symptom | Fix |
|---------|-----|
| "Cannot reach HopFog server" | Check the phone is on the **HopFog-Network** Wi-Fi |
| DNS not resolving | Verify the DNS redirect (step 5). Try `nslookup hopfog.com` from the phone. |
| Connection refused | Make sure the node is running on port 80 and firewall allows it |
| Login fails (401) | Seed user accounts (step 7) or wait for admin XBee sync |

### Windows Firewall blocking connections

Allow Python through the firewall:

1. **Windows Security → Firewall → Allow an app** → **Add Python**.
2. Or from an admin command prompt:
   ```
   netsh advfirewall firewall add rule name="HopFog Node" ^
       dir=in action=allow protocol=TCP localport=80
   ```

### XBee not detected

1. Check the USB adapter is plugged in.
2. Verify the COM port in Device Manager.
3. Close any other serial monitor (XCTU, PuTTY, etc.) — only one
   program can use a COM port at a time.

### Data not persisting

Check the `hopfog_data/` directory exists and is writable:

```bash
ls -la hopfog_data/
```

If missing, the node creates it on startup. If files are empty (`[]`),
no data has been received yet — either seed manually (step 7) or wait
for an XBee sync.

---

## Quick Reference

```
pc_node/
├── hopfog_node.py         # The node server
├── requirements.txt       # Python dependencies (flask, pyserial)
├── config.example.json    # Configuration template
├── config.json            # Your config (git-ignored)
├── README.md              # Quick-reference doc
└── hopfog_data/           # Created at runtime
    ├── fog_nodes.json     # Fog device registry
    ├── messages.json      # Admin relay messages
    ├── stats.json         # Node statistics
    ├── users.json         # Mobile user accounts
    ├── conversations.json # Chat conversations
    ├── chat_messages.json # Chat message history
    └── announcements.json # Admin announcements
```

### All endpoints at a glance

| Method | Path | Source |
|--------|------|--------|
| GET | `/api/health` | Admin API |
| GET | `/api/stats` | Admin API |
| GET | `/api/fog-devices` | Admin API |
| POST | `/api/fog-devices/register` | Admin API |
| GET/POST | `/api/messages` | Admin API |
| POST | `/api/xbee/broadcast` | Admin API |
| POST | `/login` | Mobile app |
| GET | `/status` | Mobile app |
| GET | `/conversations` | Mobile app |
| GET | `/messages` | Mobile app |
| POST | `/send` | Mobile app |
| GET | `/users` | Mobile app |
| POST | `/create-chat` | Mobile app |
| POST | `/sos` | Mobile app |
| GET | `/new-messages` | Mobile app |
| POST | `/agree-sos` | Mobile app |
| POST | `/change-password` | Mobile app |
| GET | `/announcements` | Mobile app |

---

## See Also

- [Readme.md](Readme.md) – Project overview
- [ARCHITECTURE.md](ARCHITECTURE.md) – System architecture
- [WIRING_GUIDE.md](WIRING_GUIDE.md) – Embedded hardware wiring
- [pc_node/README.md](pc_node/README.md) – PC node quick reference
