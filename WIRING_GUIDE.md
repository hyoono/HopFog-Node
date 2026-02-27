# Wiring Guide – HopFog Node

This guide covers wiring for both supported boards:
- **ESP32-CAM** (AI-Thinker)
- **Wemos D1 Mini** (ESP8266)

## Components

1. **ESP32-CAM** or **Wemos D1 Mini** (pick one)
2. **XBee module** (e.g. XBee S2C, XBee3)
3. **XBee breakout / adapter board** (provides 2.54 mm headers and 3.3 V regulation)
4. **MicroSD card** (4-32 GB, FAT32, Class 10) – ESP32-CAM only
5. **FTDI / USB-to-TTL programmer** – ESP32-CAM only (D1 Mini has built-in USB)
6. **5 V power supply** (≥ 500 mA)
7. **Jumper wires** (female-to-female)

---

## 1. ESP32-CAM ↔ XBee Wiring

The node uses **Serial2** (UART2) re-mapped to GPIO 13 / GPIO 12.

> **Why not GPIO 32 / 33?** On the AI-Thinker ESP32-CAM, GPIO 32 is
> the camera PWDN pin and GPIO 33 drives the on-board red LED. Neither
> is freely available for general-purpose serial I/O. GPIO 13 and
> GPIO 12 are free when the SD card runs in 1-bit mode.

```
ESP32-CAM               XBee Module
=========               ===========
GPIO 13  (RX) ◄──────── DOUT (TX)
GPIO 12  (TX) ────────► DIN  (RX)
GND           ────────── GND
3.3V          ────────── VCC (3.3V)
```

| ESP32-CAM Pin | Direction | XBee Pin | Notes |
|---------------|-----------|----------|-------|
| GPIO 13 | Input | DOUT (TX) | ESP32 receives data from XBee |
| GPIO 12 | Output | DIN (RX) | ESP32 sends data to XBee |
| GND | — | GND | Common ground |
| 3.3V | — | VCC | XBee requires 3.3 V (NOT 5 V) |

### GPIO 12 Strapping Note

GPIO 12 is a strapping pin that selects flash voltage at boot. Keep
it **LOW during power-on** (which is the idle state of a UART TX line,
so this is normally fine). If you experience boot failures, disconnect
the XBee during programming and reconnect afterwards.

### SD Card

Insert a **FAT32-formatted** microSD card into the slot on the back of
the ESP32-CAM. The firmware creates the `/hopfog/` directory
automatically on first boot.

### ESP32-CAM ↔ FTDI Wiring (Programming)

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

## 2. Wemos D1 Mini ↔ XBee Wiring

The D1 Mini only has one hardware UART (used for USB serial), so XBee
communication uses **SoftwareSerial** on pins D5 and D6.

```
D1 Mini                  XBee Module
=======                  ===========
D5 / GPIO 14  (RX) ◄──── DOUT (TX)
D6 / GPIO 12  (TX) ─────► DIN  (RX)
GND            ────────── GND
3.3V           ────────── VCC (3.3V)
```

| D1 Mini Pin | GPIO | Direction | XBee Pin | Notes |
|-------------|------|-----------|----------|-------|
| D5 | 14 | Input | DOUT (TX) | D1 receives data from XBee |
| D6 | 12 | Output | DIN (RX) | D1 sends data to XBee |
| GND | — | — | GND | Common ground |
| 3.3V | — | — | VCC | XBee requires 3.3 V |

### D1 Mini Pinout Quick Reference

```
        ┌──── USB ────┐
    RST │ o          o │ TX  (GPIO 1)
     A0 │ o          o │ RX  (GPIO 3)
  D0/16 │ o          o │ D1  (GPIO 5)
  D5/14 │ o  ◄── RX  o │ D2  (GPIO 4)
  D6/12 │ o  ──► TX  o │ D3  (GPIO 0)
  D7/13 │ o          o │ D4  (GPIO 2) LED
  D8/15 │ o          o │ GND
   3.3V │ o          o │ 5V
        └─────────────┘
```

### Storage

No SD card needed. Data is stored in **LittleFS** (on-chip flash).

### Programming

Just plug in the USB cable and run `pio run -e d1_mini -t upload`.

---

## 3. XBee Configuration

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

## 4. Power

| Source | Notes |
|--------|-------|
| USB cable | Easiest for D1 Mini; also works for ESP32-CAM via FTDI |
| USB 5 V adapter | For standalone operation |
| Battery pack | 5 V regulated output, ≥ 500 mA |

The XBee draws ~50 mA when transmitting. Power it from the board's
3.3 V pin or from a separate regulated 3.3 V supply.

---

## 5. Troubleshooting

| Problem | Solution |
|---------|----------|
| Upload fails (ESP32-CAM) | Ensure IO0 → GND; press RESET when "Connecting…" appears |
| Upload fails (D1 Mini) | Check USB cable is data-capable (not charge-only) |
| No serial output | Set baud to 115200; press RESET |
| XBee no communication | Check PAN ID matches; verify TX/RX are crossed correctly |
| SD card not detected (ESP32-CAM) | Re-format as FAT32; try a different card |
| Boot loop on ESP32-CAM | GPIO 12 pulled HIGH at boot – disconnect XBee, flash, reconnect |
| Node doesn't register | Check admin XBee is powered and in coordinator mode |

---

## 6. Safety

- Power the ESP32-CAM at **5 V** (not 3.3 V) for stable operation.
- Power the XBee at **3.3 V** (not 5 V) – higher voltage will damage it.
- The Wemos D1 Mini can be powered via USB (5 V) or the 5 V pin.
- Verify polarity before connecting.
- Handle boards by their edges to avoid ESD damage.
