# Build log — what went wrong and why

Kept separate from the README (which is how to *use* the thing) because every
entry here cost real bench time and is worth re-reading before the next build.

## Status

Working. ESP32-S3-N16R8 + 150 WS2812B LEDs on proto board, local web app with
scenes and a daily schedule at **http://192.168.1.50**, updated over the air.

Open item: the strip runs at ~4.6 V as a workaround for the logic-level problem
below. The proper fix — a 74AHCT125 buffer on the data line — is not installed
yet. Do that before the enclosure build and the supply can go back to 5.0 V.

## 1. Breadboard brownout (real, cost ~an evening)

Powering the ESP32 from the bench supply's 5 V rail through breadboard rails and
dupont jumpers dropped 5.0 V → **4.1 V at the ESP32's pins** under ~1 A of strip
current. The AMS1117 regulator needs ≥ 4.4 V in, so its 3.3 V output sagged and
the chip sat in brownout reset — strip frozen on its last frame, board off WiFi.

- Breadboard rails are spring contacts good for ~1 A, with tenths of an ohm each.
  Anything pulling amps does not belong on a breadboard.
- Star-wire from the supply's terminals: one pair of wires per load, never
  daisy-chained, never sharing a rail.
- Dupont jumpers (~28 AWG) got hot at 1.8 A. The strip's power pair needs
  18–20 AWG.
- Diagnosis that worked: measure V between the ESP32's own 5V and GND pins while
  the strip is running. The supply's front-panel meter cannot see wire drop.

## 2. FastLED version (the big one — cost about a week)

Symptom: the strip froze or showed garbage, while the board itself was provably
healthy (API responding, frame counter climbing, uptime fine). I chased power,
grounds, and logic levels for days.

Cause: `lib_deps = fastled/FastLED@^3.9.13` resolved to **3.10.5** in the
`strip-ota` and `blink` envs (created later) but **3.10.3** in the original
`strip` env. 3.10.5 falls back to a generic bit-bang driver on this ESP32-S3 +
Arduino core combination, which cannot meet WS2812B timing → garbage output.
Every OTA upload shipped the broken build; every USB upload shipped a good one,
which is exactly the pattern that made it look like a power problem.

- **Each PlatformIO env resolves `lib_deps` independently** into
  `.pio/libdeps/<env>/`. Adding an env months later can silently pick up a newer
  version. Pin exact versions: `fastled/FastLED@3.10.3`, no caret.
- FastLED printed `WARN: Using GENERIC fallback clockless controller` on every
  boot the entire time. **Read the boot log before touching hardware.**
- To compare: `grep -m1 '"version"' .pio/libdeps/<env>/FastLED/library.json`

## 3. 3.3 V data into a 5 V strip

With FastLED fixed, the onboard LED (GPIO 48) tracked every command perfectly
while the strip still showed wrong colours. The onboard LED runs at 3.3 V so its
logic threshold is 2.3 V; the strip at 5 V wants 0.7 × 5 = **3.5 V**, which a
3.3 V GPIO cannot guarantee.

- Confirmed by dropping the supply to 4.6 V (threshold → 3.22 V): correct
  instantly.
- Running at 4.6 V works but has no margin and costs brightness.
- Proper fix: **74AHCT125** (or 74HCT245) buffer powered from 5 V on the data
  line. HCT inputs are 3.3 V-compatible, outputs swing to 5 V.
- Never raise the supply above 5.0 V to compensate for wire drop — it raises the
  strip's threshold too and makes this worse. (5.3 V broke it outright.)

## 4. mDNS on macOS/iOS

`ledstrip.local` resolved fine but every HTTP request stalled **5 seconds** in
name lookup (dual-stack lookup waiting on an AAAA record the ESP32 never
answers). It made the web UI feel completely broken while the board was healthy.

Fixed with a static IP (`STATIC_IP` in `secrets.h`). Use the IP, not the name.

## 5. Flashing this board on this Mac

- Use the port labelled **USB** (native USB, "USB JTAG/serial debug unit"), not
  `UART` — the CH343 UART bridge does not enumerate on this Mac.
- Plug directly into the iMac. Through the VIA Labs hub it does not enumerate.
- A background serial logger holding `/dev/cu.usbmodem*` makes uploads fail with
  "device reports readiness to read but returned no data" — stop it first.
- `ArduinoOTA.onStart` must call `esp_task_wdt_delete(NULL)`; the upload blocks
  `loop()` far longer than the 8 s task watchdog allows, so OTA rebooted mid-flash
  until that was added.

## 6. Persistence gotcha

Settings live in one `Persisted` struct written to NVS with `putBytes`. Adding a
field changes `sizeof`, so the old blob is rejected on boot and everything
reverts to defaults. Expect to lose saved settings whenever that struct changes.
