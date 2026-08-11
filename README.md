# ORB DISPLAY

> **ESP32-C3 + SH1106/SSD1306 OLED — Monochromatic Orb Display with Web Mission Control**

A production-ready firmware for an ESP32-C3 driving a 128×64 monochromatic OLED through a non-centred round lens viewport. Three rendering modes + a futuristic dark-mode web dashboard served directly from the chip.

---

## Hardware

| Component | Notes |
|-----------|-------|
| **MCU** | ESP32-C3 Super Mini |
| **Display** | SH1106 128×64 OLED (I²C) |
| **Lens** | Camera Lens |
| **Power** | 3.7V Li-Ion Battery |

### I²C Wiring (ESP32-C3 defaults)

```
OLED SDA  →  GPIO 1
OLED SCL  →  GPIO 3
OLED VCC  →  3.3 V
OLED GND  →  GND
```

Change `SDA_PIN` / `SCL_PIN` in `orb_display.ino` if your board differs.

---

## Required Libraries

Install all via **Arduino IDE → Sketch → Include Library → Manage Libraries**:

| Library | Author | Minimum Version |
|---------|--------|----------------|
| **U8g2** | olikraus | 2.35.x |
| **ESPAsyncWebServer** | ESP32Async (formerly lacamera) | 3.x |
| **AsyncTCP** | ESP32Async | 3.x |
| **ArduinoJson** | Benoit Blanchon | 7.x |

> **Board package**: `esp32` by Espressif, version **3.x** or later  
> Install via `File → Preferences → Additional Boards Manager URLs`:  
> `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

---

## Project Structure

```
orb-display/
├── orb_display.ino   ← main sketch (include this in Arduino IDE)
├── web_ui.h          ← embedded HTML/CSS/JS dashboard (PROGMEM)
└── README.md         ← this file
```

---

## Quick Start

1. Open `orb_display.ino` in Arduino IDE (or PlatformIO).
2. Edit the WiFi credentials at the top of the file:
   ```cpp
   #define WIFI_SSID      "YOUR_SSID"
   #define WIFI_PASSWORD  "YOUR_PASSWORD"
   ```
3. Select your display driver (SH1106 or SSD1306) — uncomment the correct line.
4. Select **Board → ESP32-C3 Dev Module** (or your specific board).
5. Flash at **115200 baud**.
6. Open Serial Monitor to confirm boot and get the IP address.
7. Navigate to **http://orb.local** (or the IP shown) in a browser.

---

## Web Dashboard

The dashboard is served on port 80 and auto-polls every 5 seconds.

### Tabs

| Tab | Purpose |
|-----|---------|
| **MODE SELECT** | Switch between Solar Orrery, Satellite Radar, Custom HUD |
| **CALIBRATION** | Drag sliders for Center X/Y and Radius; live preview updates the canvas |
| **SOLAR SYSTEM** | Speed warp slider + preset buttons (1×…10K×) |
| **RADAR** | Select ISS / Hubble / Tiangong; trigger TLE refresh |
| **CUSTOM TEXT** | Type or pick preset messages; sent to display instantly |
| **TELEMETRY** | Live heap, NTP status, viewport parameters |

### Calibration Preview Canvas

The Calibration tab includes a real-time 512×256 pixel canvas that mirrors the physical 128×64 OLED panel — it shows the circular viewport mask, crosshair and centre-dot as you drag the sliders.

---

## REST API

All endpoints are on port 80.

| Method | Path | Body | Description |
|--------|------|------|-------------|
| `GET` | `/` | — | Serve web dashboard HTML |
| `GET` | `/status` | — | JSON: all current settings + telemetry |
| `POST` | `/calibrate` | `{"cx":64,"cy":32,"radius":28}` | Update viewport; saves to Preferences |
| `POST` | `/mode` | `{"mode":0}` | 0=Solar, 1=Radar, 2=Text |
| `POST` | `/speed` | `{"speed":100.0}` | Speed warp (1–50000) |
| `POST` | `/radar` | `{"target":0}` | 0=ISS, 1=Hubble, 2=Tiangong |
| `POST` | `/text` | `{"text":"HELLO"}` | Custom HUD message (max 127 chars) |
| `POST` | `/fetchtle` | — | Force TLE refresh immediately |

---

## Viewport Calibration

The three configurable parameters let you compensate for a non-centred physical lens:

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| `center_x` | 64 | 0–128 | Horizontal centre of the round lens on the OLED |
| `center_y` | 32 | 0–64 | Vertical centre |
| `radius` | 28 | 10–64 | Radius of the visible area |

All drawing functions (`drawClippedPixel`, `drawClippedLine`, orbit circles, starfield, etc.) check `(x − cx)² + (y − cy)² ≤ radius²` before writing any pixel.

Settings are stored in ESP32 NVS via `Preferences.h` and survive reboots.

---

## Modes

### Mode 0 — Solar System Orrery

- **Keplerian math**: Mean Longitude `L = L₀ + L₁·d` (degrees, d = days since J2000.0)
- Planets: Mercury, Venus, Earth (+Moon), Mars
- **Speed warp**: accumulates fractional days per frame; 100× means 100 simulated days per real day
- **Sun**: 3×3 solid core + animated 8-ray checkerboard flare halo
- **Mars**: rendered as a 7×3 Saturn-style micro-bitmap
- **Starfield**: 12 static background stars with 75% flicker duty
- **HUD header**: UTC date + current zodiac constellation
- **Target lock**: animated 5×5 corner-bracket that smoothly pursues each planet in turn, with angle readout label

### Mode 1 — Satellite Radar

- **TLE data**: fetched hourly from CelesTrak via plain HTTP GET
- **Orbit model**: simplified circular approximation from TLE inclination / RAAN / mean motion — gives visually realistic sweep motion; not a full SGP4 propagator
- **Sweep trail**: 24-frame ring buffer, rendered with increasing step size for depth fade effect
- **Compass**: N/E labels inside the viewport ring

### Mode 2 — Custom Text HUD

- Scrolling text bounded by the circular viewport (clip-window based)
- Animated rotating 4-triangle sci-fi accent ring
- Three nested rings (solid, dashed, solid) for depth

---

## Code Architecture

```
loop()
├── NTP retry (30 s interval)
├── TLE fetch (1 h interval, ~1-2 s blocking)
└── renderFrame() (33 ms = ~30 FPS)
    ├── drawSolarSystem()
    ├── drawRadar()
    └── drawCustomText()
```

**Zero `delay()` calls** in the main render path. The TLE fetch (`WiFiClient`) is the only blocking call and runs at most once per hour for ~1–2 s total.

**Memory**: All coordinate arrays are statically pre-allocated. No heap allocation inside the draw loop.

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Display blank | Check I²C wiring and SDA/SCL pin defines |
| Display shows garbage | Swap SH1106 ↔ SSD1306 driver line in sketch |
| `orb.local` not resolving | Try the raw IP shown in Serial Monitor |
| TLE fetch fails | Check your router's DNS, or try HTTPS via a port-forwarding proxy |
| Planets look wrong | Verify NTP is synced (Telemetry tab, "NTP ✓") |
| Heap < 10 KB | Reduce `TRAIL_LEN` or disable one mode |

---

## License

MIT — do whatever you want, attribution appreciated.
