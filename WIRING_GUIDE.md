# Wiring Guide – ESP32-CAM + XBee (HopFog Node)

## Components

1. **ESP32-CAM** (AI-Thinker) – camera not used
2. **XBee module** (e.g. XBee S2C, XBee3)
3. **XBee breakout / adapter board** (provides 2.54 mm headers and 3.3 V regulation)
4. **MicroSD card** (4-32 GB, FAT32, Class 10)
5. **FTDI / USB-to-TTL programmer** (for uploading firmware)
6. **5 V power supply** (≥ 500 mA)
7. **Jumper wires** (female-to-female)
8. **3.3 V logic-level shifter** (optional – see note below)

---

## 1. ESP32-CAM ↔ XBee Wiring (Operation)

The node uses **Serial2** (UART2) for XBee communication.

```
ESP32-CAM               XBee Module
=========               ===========
GPIO 32  (RX) ◄──────── DOUT (TX)
GPIO 33  (TX) ────────► DIN  (RX)
GND           ────────── GND
3.3V          ────────── VCC (3.3V)
```

| ESP32-CAM Pin | Direction | XBee Pin | Notes |
|---------------|-----------|----------|-------|
| GPIO 32 | Input | DOUT (TX) | ESP32 receives data from XBee |
| GPIO 33 | Output | DIN (RX) | ESP32 sends data to XBee |
| GND | — | GND | Common ground |
| 3.3V | — | VCC | XBee requires 3.3 V (NOT 5 V) |

### Logic-Level Note

Both the ESP32 GPIO and XBee operate at **3.3 V** logic, so a level
shifter is usually not needed. If your XBee breakout board already has
an on-board 3.3 V regulator and you are powering it from 5 V, just
connect the signal lines directly.

### GPIO 33 and the Red LED

GPIO 33 drives the built-in red LED on the AI-Thinker ESP32-CAM.
When the node transmits data to the XBee the LED will briefly flicker,
providing a useful visual indicator of outgoing traffic.

---

## 2. ESP32-CAM ↔ FTDI Wiring (Programming)

This is the same wiring as the admin (HopFog-Web) guide:

```
ESP32-CAM          FTDI Programmer
=========          ===============
GND     ────────── GND
5V      ────────── VCC (5V)
U0R     ────────── TX
U0T     ────────── RX
IO0     ────────── GND    ← only during upload
```

After uploading, **disconnect IO0 from GND** and press RESET.

---

## 3. Full Wiring (Programming + XBee)

During development you may want both the FTDI (for serial monitor) and
the XBee connected at the same time:

```
                   ┌─────────────────────┐
                   │   ESP32-CAM         │
                   │   (AI-Thinker)      │
       GND ──── 1 │ o                 o │ 36  GPIO 32 ──── XBee DOUT
      3.3V ──── 2 │ o                 o │ 35  GND
     GPIO4 ──── 3 │ o                 o │ 34  5V
  FTDI TX ──── 4 │ o  U0R             o │ 33  GPIO 33 ──── XBee DIN
  FTDI RX ──── 5 │ o  U0T             o │ 32  GPIO 1
       GND ──── 6 │ o                 o │ 31  GPIO 3
    5V (FTDI) ─ 7 │ o                 o │ 30  GND ──── XBee GND
                   └─────────────────────┘
                                3.3V (pin 2) ──── XBee VCC
```

---

## 4. SD Card

Insert a **FAT32-formatted** microSD card into the slot on the back of
the ESP32-CAM. The firmware creates the `/hopfog/` directory and JSON
database files automatically on first boot.

---

## 5. XBee Configuration

Use **XCTU** (Digi's configuration tool) to set up the XBee modules:

### Node XBee (Router / End-Device)

| Setting | Value |
|---------|-------|
| Function Set | Zigbee Router AT / Zigbee End Device AT |
| PAN ID | Must match admin |
| Baud Rate | 9600 (default) |
| API Mode | Transparent (AT) |

### Admin XBee (Coordinator)

| Setting | Value |
|---------|-------|
| Function Set | Zigbee Coordinator AT |
| PAN ID | Same as node |
| Baud Rate | 9600 |

Both modules must be on the **same PAN ID** and use the same baud rate.

---

## 6. Power

| Source | Notes |
|--------|-------|
| FTDI programmer | Convenient during development (powers ESP32-CAM via 5 V pin) |
| USB 5 V adapter | For standalone operation; connect 5 V and GND |
| Battery pack | 5 V regulated output, ≥ 500 mA |

The XBee draws ~50 mA when transmitting. Power it from the ESP32-CAM's
3.3 V pin or from a separate regulated 3.3 V supply.

---

## 7. Troubleshooting

| Problem | Solution |
|---------|----------|
| Upload fails | Ensure IO0 → GND; press RESET when "Connecting…" appears |
| No serial output | Set baud to 115200; press RESET |
| XBee no communication | Check PAN ID matches; verify TX/RX are crossed correctly |
| SD card not detected | Re-format as FAT32; try a different card; check board selection is AI-Thinker |
| Node doesn't register | Check admin XBee is powered and in coordinator mode |
| Red LED always on | Normal idle state of GPIO 33; flickers during XBee TX |

---

## 8. Safety

- Power the ESP32-CAM at **5 V** (not 3.3 V) for stable operation.
- Power the XBee at **3.3 V** (not 5 V) – higher voltage will damage it.
- Verify polarity before connecting.
- Handle boards by their edges to avoid ESD damage.
