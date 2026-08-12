# 🌌 Orb Display — Orbital Mechanics & Viewport Math

This guide details the astronomical formulas, satellite tracking algorithms, and display clipping mathematics implemented in `orb_display.ino`.

---

## ☀️ 1. Solar System Orrery Math

The Solar System mode uses **Simplified Keplerian Planetary Calculations** based on Paul Schlyter's astronomical models. It determines real-time angular positions for Mercury, Venus, Earth, and Mars natively on-chip without requiring external API calls.

### Step 1: Calculate Elapsed Epoch Time (d)

All planetary positions are calculated relative to the standard astronomical epoch **J2000.0** (January 1, 2000 at 12:00 UTC).

The firmware computes elapsed decimal days $d$ from the current UTC timestamp:

$$d = \text{JD} - 2451545.0$$

Where $\text{JD}$ is the Julian Day Number calculated inside `daysSinceJ2000()`.

### Step 2: Compute Planet Angle (Mean Longitude L)

Every planet orbits the Sun at a constant average angular speed. The planet's current orbital angle (Mean Longitude $L$) is calculated as:

$$L = (L_0 + L_1 \cdot d) \pmod{360^\circ}$$

* $L_0$: Base longitude at epoch J2000.0 (in degrees).
* $L_1$: Daily angular motion speed (in degrees per day).
* $d$: Elapsed days (modified by user warp speed `g_speed`).

| Planet | $L_0$ (Base Angle) | $L_1$ (Degrees / Day) | Approximate Period |
| :--- | :--- | :--- | :--- |
| **Mercury (MER)** | $252.25^\circ$ | $4.0923^\circ$ | ~88 days |
| **Venus (VEN)** | $181.98^\circ$ | $1.6021^\circ$ | ~225 days |
| **Earth (EAR)** | $100.46^\circ$ | $0.9856^\circ$ | ~365 days |
| **Mars (MAR)** | $355.45^\circ$ | $0.5240^\circ$ | ~687 days |

### Step 3: Scale Radii for Small Displays

Rendering true Astronomical Units ($\text{AU}$) to scale would crush Mercury into the Sun pixel while pushing Mars far off-screen.

To create a visually balanced concentric model, orbit radii $R$ are scaled proportionally using the square root of their semi-major axis ($\sqrt{a}$):

$$R = R_{\text{max}} \cdot \frac{\sqrt{a}}{\sqrt{a_{\text{Mars}}}}$$

### Step 4: Polar to Screen Cartesian Coordinates

Trigonometry converts the polar coordinates (angle $L$, radius $R$) into OLED screen pixel locations $(x, y)$:

$$x = C_x + R \cdot \cos(L)$$

$$y = C_y + R \cdot \sin(L)$$

Where $(C_x, C_y)$ is the dynamic viewport center calibration point (default: `64, 32`).

---

## 🛰️ 2. Satellite Radar Math

The Satellite Radar mode parses standard **Two-Line Element (TLE)** orbital data sets from CelesTrak to simulate overhead passes on a 2D circular radar view.

```text
TLE Line 2 Example (ISS):
2 25544  51.6416  34.6120 0005168  72.0125 210.0912 15.49521175438848
         ▲        ▲                                 ▲
         │        │                                 └── Mean Motion (revs/day)
         │        └──────────────────────────────────── RAAN (deg)
         └───────────────────────────────────────────── Inclination (deg)
```

### Step 1: Calculate Orbital Period (T)

Mean Motion $M$ represents revolutions per day. The satellite's orbital period $T$ in seconds is calculated via:

$$T = \left(\frac{1440}{M}\right) \cdot 60$$

### Step 2: Compute Instantaneous Position (Azimuth & Elevation)

Using the current elapsed time $t$ (in seconds), the satellite's orbital phase angle $\theta$ is:

$$\theta = \left(\frac{t \bmod T}{T}\right) \cdot 360^\circ$$

The **Azimuth** ($Az$, compass heading) and **Elevation** ($El$, angle above horizon) are calculated as:

$$Az = (\text{RAAN} + \theta) \pmod{360^\circ}$$

$$El = \text{Inclination} \cdot \sin(\theta)$$

### Step 3: Map 3D Dome Sky to 2D Radar View

On the radar screen layout:
* **Center point ($C_x, C_y$):** Zenith (directly overhead, $El = 90^\circ$).
* **Outer Ring:** Horizon ($El = 0^\circ$).

Radial distance $R_{\text{sat}}$ from the center point decreases as satellite elevation increases:

$$R_{\text{sat}} = R_{\text{viewport}} \cdot \left(1 - \frac{\text{Clamp}(El, 0^\circ, 90^\circ)}{90^\circ}\right)$$

Final pixel coordinates on screen:

$$x_{\text{sat}} = C_x + R_{\text{sat}} \cdot \cos(Az)$$

$$y_{\text{sat}} = C_y + R_{\text{sat}} \cdot \sin(Az)$$

---

## 🔍 3. Viewport & Circular Lens Clipping

Because the physical display sits behind a round magnifying lens, rendering graphics outside the physical lens aperture creates visual distortion around the bezel edges.

The firmware enforces circular boundary checks using the Pythagorean distance formula:

$$(x - C_x)^2 + (y - C_y)^2 \le R_{\text{viewport}}^2$$

```cpp
inline bool isInsideViewport(int16_t x, int16_t y) {
  int32_t dx = x - g_cx;
  int32_t dy = y - g_cy;
  return (dx * dx + dy * dy) <= ((int32_t)g_radius * g_radius);
}
```

Every line-drawing routine (`drawClippedLine`) and pixel handler (`drawClippedPixel`) evaluates this boundary condition before writing bytes to the U8g2 frame buffer.