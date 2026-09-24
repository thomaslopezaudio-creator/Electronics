# LED strip controller — ESP32-S3-N16R8 + WS2812B

Local web app on your WiFi that drives an addressable 5V LED strip.

## 1. Wiring

```
 5V power supply                 ESP32-S3 devkit                LED strip
 ┌────────────┐                  ┌──────────────┐               ┌──────────┐
 │  +5V  ─────┼──────────────────┼──────────────┼───────────────┤ 5V       │
 │            │                  │              │               │          │
 │  GND  ─────┼─────────┬────────┤ GND          │               │          │
 │            │         └────────┼──────────────┼───────────────┤ GND      │
 └────────────┘                  │              │   330 Ω       │          │
                                 │ GPIO 4 ──────┼───/\/\/───────┤ DI      │
                                 │              │               └──────────┘
                                 │ USB-C ◄──────┼── laptop (power + flashing)
                                 └──────────────┘
```

Rules, in order of how much they matter:

1. **Common ground.** The strip's GND, the power supply's GND, and the ESP32's
   GND must all be connected together. Without it the data signal has no
   reference and the strip shows garbage or nothing. This is the #1 mistake.
2. **The strip's 5V comes from the power supply, never from the ESP32.**
   The ESP32's 5V pin is just the USB cable passed through; a strip can pull
   amps. Each WS2812B pixel draws up to 60 mA at full white
   (`60 LEDs × 60 mA = 3.6 A`). Size the supply for that, or set
   `MAX_MILLIAMPS` in `main.cpp` below your supply's rating — FastLED will
   dim the strip to stay under it.
3. **Data goes into the DI end.** Strips have arrows printed on them; data
   flows in the arrow direction. Connecting to the DOUT end does nothing.
4. **330 Ω resistor in series with the data line**, close to the strip.
   Optional but cheap insurance against ringing and against the first pixel
   dying from a voltage spike.
5. **Optional: 1000 µF capacitor across 5V and GND at the strip's input.**
   Smooths the inrush when the supply turns on. Watch polarity (stripe = −).
6. Leave the ESP32 on USB while you develop. Don't also feed it 5V from the
   supply through the 5V/VIN pin unless you know your devkit has a diode on
   that path (many clones do, some don't — back-feeding USB is bad).

### Why GPIO 4?

The S3-N16R8 has pins you must not use: 0/3/45/46 (boot strapping),
19/20 (USB), 26–32 (flash), 33–37 (octal PSRAM). GPIO 4 is boring and
safe. GPIO 48 is the devkit's onboard RGB LED — we use it in milestone 0.

### 3.3 V data into a 5 V strip

The ESP32 outputs 3.3 V logic; WS2812B officially wants ≥ 0.7 × 5 V = 3.5 V
for a "high". In practice it almost always works with a short data wire.
If the first pixel flickers or shows wrong colors, fixes in order of effort:
shorter data wire → 74AHCT125 level shifter → power the strip at ~4.5 V
via a diode drop.

## 2. Milestones

**0. Toolchain check, no strip.** Plug the devkit in via the port labelled
`USB` (the S3's native port — it shows up as "USB JTAG/serial debug unit").
On this Mac the `UART`/`COM` port doesn't enumerate (no CH343 driver), and
the firmware is built to send `Serial` out the native port anyway. Then:

```bash
pio run -e onboard-test -t upload && pio device monitor
```

The little onboard LED should animate a rainbow. If it stays dark, try
`-DLED_PIN=38` in `platformio.ini` (board revision difference).

**1. Strip.** Wire per the diagram, power on the supply, then:

```bash
pio run -e strip -t upload
```

Set `NUM_LEDS` in `platformio.ini` to your real count first.

**2. Web app.** Copy `include/secrets.h.example` → `include/secrets.h`, fill
in your WiFi, re-upload. The serial monitor prints the URL. Open
`http://ledstrip.local` (or the IP) from any device on the same WiFi.

If WiFi fails for 20 s the board starts its own network `LEDStrip-Setup`
so the UI is still reachable at `http://192.168.4.1`.

## 3. Scenes and the daily schedule

Open **http://192.168.1.50** (bookmark the IP — `ledstrip.local` makes macOS/iOS
stall 5 s on every request). The page is one screen of things you actually touch:

- **Auto toggle** — follow the daily schedule, or hold whatever is on.
- **Brightness** — global.
- **Scene tiles** — tap one and the room changes. Colours are specified in
  Kelvin and converted on the board, so they follow the usual
  cool-and-bright-by-day / warm-and-dim-by-night convention.

Collapsed below: the schedule editor, per-area fine tuning, and the physical
layout (boundaries, flip). You set those once.

### The scenes

| Scene | Colour | Brightness | For |
|---|---|---|---|
| Wake | 2700 K | 27% | soft start, no glare at 06:30 |
| Focus | 5000 K | 82% | desk work |
| Midday | 5600 K | 92% | brightest, competes with daylight |
| Cook | 4200 K | 92% | kitchen is in this room — neutral general light |
| Afternoon | 4000 K | 75% | starting to warm |
| Evening | 2700 K | 47% | couch, relaxed |
| Movie | 2000 K | 14% | bias light for the TV; pegboard dimmed to 27% so the wall doesn't glare |
| Wind down | 2200 K | 27% | pre-sleep, low melatonin suppression |
| Night | 1800 K | 7% | amber only, preserves night vision |
| Party | rainbow | 67% | — |
| Off | — | 0 | — |

### The schedule

Eight slots, all editable. The board syncs its clock over NTP and handles
daylight saving from the POSIX `TZ_POSIX` string in `secrets.h`.

| Time | Scene |
|---|---|
| 06:30 | Wake |
| 08:00 | Focus |
| 12:00 | Midday |
| 17:00 | Afternoon |
| 19:00 | Evening |
| 21:30 | Wind down |
| 23:00 | Night |
| 01:00 | Off |

Scheduled changes crossfade over 20 minutes by default (tunable 0–60), which is
the range the circadian-lighting literature recommends — long enough that you
never catch the switch. Tapping a scene by hand is immediate. Manual changes
don't disarm the schedule; the next slot simply takes over.

## 4. The API (for scripting)

```bash
curl http://192.168.1.50/api/state
curl -X POST "http://192.168.1.50/api/scene?i=7"                 # Movie
curl -X POST "http://192.168.1.50/api/auto?on=0"                 # hold current look
curl -X POST "http://192.168.1.50/api/set?target=sec:Desk&color=ff2200"
curl -X POST "http://192.168.1.50/api/schedule?i=4&h=18&m=30"    # Evening at 18:30
```

`target` is `all`, `sec:<Pegboard|Panel|Desk>` or `seg:<0-6>`.

## 5. Updating the firmware

The board flashes over WiFi — no USB needed:

```bash
pio run -e strip-ota -t upload
```

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Nothing lights | no common ground; wrong end of strip; wrong GPIO |
| First LED lit, rest dark | broken strip segment, or data wire too long |
| Colors wrong (red ↔ green) | change `COLOR_ORDER` to `RGB` |
| Random flicker, or wrong colours in chunks | 3.3 V data marginal against a 5 V strip (threshold is 0.7xVcc = 3.5 V). Drop the supply to ~4.6 V to confirm; fix properly with a 74AHCT125 buffer |
| Strip ignores everything, onboard LED is fine | same 3.3 V logic-level problem — the onboard LED runs at 3.3 V so its threshold is only 2.3 V |
| Colours garbage after a rebuild | check the boot log for FastLED's `GENERIC fallback` warning; the library version must be pinned (see platformio.ini) |
| Board reboots when strip goes white | supply can't deliver; lower `MAX_MILLIAMPS` |
| Upload fails / port not found | use the `USB` port directly on the Mac (not through a hub); hold BOOT while plugging in if still stuck |
| `WiFi.status() = 1` in the log | SSID typo — compare against the scan list the board prints at boot |
