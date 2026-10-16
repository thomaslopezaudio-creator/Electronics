# PCM5102 soundboard reference

## Prototype

ESP32-C5 web soundboard -> I2S -> DAC -> 3.5 mm AUX -> powered speaker.
Both header rows have been fitted. Firmware/playback are not yet tested.
The earlier ESP32-S3 assumption came from the separate LED project and was
incorrect. Thomas confirmed C5; esptool identified revision v1.0 on
`/dev/cu.usbserial-110`. Chip identification reset the device but did not flash
firmware. Do not reuse the LED project's S3 build configuration.

Photographed six-pin row: SCK, BCK, DIN, LRCK, GND, VIN.
Other row: LROUT, AGND, ROUT, AGND, A3V3, FMT, XSMT, DEMP, FLT.
These are PCB labels, not physical IC pin numbers.

## Datasheet findings

Sources: [PCM5102](datasheets/texas-instruments/pcm5102.pdf), printed pages
3, 4 and 12; [PCM5102A](datasheets/texas-instruments/pcm5102a.pdf), printed
pages 5, 6, 25 and 26.

| Signal | Function / intended setting |
| --- | --- |
| BCK | Bit clock from ESP32 |
| DIN | Audio data from ESP32 |
| LRCK | Left/right sample clock from ESP32 |
| SCK | Ground for internal PLL operation using BCK |
| FMT | Low selects I2S |
| XSMT | High enables sound; low mutes |
| DEMP | Low disables de-emphasis |
| FLT | Low selects normal-latency filter |

PLL startup requires 16 valid LRCK periods with SCK low. A proposed initial
firmware configuration is 44.1 kHz with two 32-bit slots: BCK = 64 x LRCK =
2.8224 MHz, supported by both datasheets' PLL tables.

PCM5102 uses nominal 3.3 V analog, charge-pump and digital supplies. PCM5102A
also supports a 1.8 V digital configuration; this project intends 3.3 V logic.
Its 3.3 V DVDD and CPVDD recommended range is 3.1-3.46 V. The chip supply pins
are not 5 V inputs.

Output is stereo line-level, nominally 2.1 V RMS at full scale. Feed the powered
speaker's AUX input and start with digital attenuation.

## HiLetgo board identification

Thomas identified the brand as HiLetgo. Research on 2026-10-10 found:

- [HiLetgo product listing, ASIN B07Q9K5MT8](https://www.amazon.com/HiLetgo-Lossless-Digital-Converter-Raspberry/dp/B07Q9K5MT8/)
  specifies 5 V supply and a PCM5102A chip. Thomas has supplied the brand,
  but has not confirmed this order ASIN.
- [Arduino Audio Tools author's HiLetgo test notes](https://github.com/pschatzmann/arduino-audio-tools/wiki/External-DAC#hiletgo-pcm5102-i2s-iis-lossless-digital-audio-dac-decoder)
  explicitly connect VIN to 5 V. This is firsthand testing, not a HiLetgo
  manufacturer schematic.

These sources support 5 V at VIN for the documented HiLetgo module. The
expected circuit regulates that input to the chip's 3.3 V supplies. The exact
regulators and their routing on Thomas's unit have not been identified.
No official HiLetgo board schematic was found in this search.

Identifying every regulator need not be a prerequisite to following documented
module wiring. It remains useful for learning how the board implements its
power supply or investigating a mismatch. Keep the board's VIN supply distinct
from its 3.3 V digital signal levels.

## Remaining bench checks

The photo shows H1L-H4L configuration pads. Their connections have not been
electrically verified. Check existing straps before wiring control pins;
do not connect a pin directly high if it is already strapped low.

Power is wired ESP32 5V -> DAC VIN and GND -> GND. Current audio wiring:
GPIO5 -> BCK, GPIO6 -> LRCK, GPIO7 -> DIN, SCK -> GND.
XSMT -> A3V3; FMT, DEMP, and FLT -> common ground.

C5 correction: GPIO7 is a strapping pin. The DAC input does not by itself
prove a boot problem, but use GPIO10 for DIN to avoid this boot-configuration
dependency. Moving GPIO7 -> GPIO10 has not yet been confirmed by Thomas.
GPIO5 and GPIO6 remain the planned BCK and LRCK outputs.
Source: [Espressif C5 GPIO reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c5/api-reference/peripherals/gpio.html).

## Bench observations

- Thomas measured 4.62 V between the ESP32 development board's 5V and G pins
  with USB connected. This establishes voltage at that header under the
  measurement conditions, not its available current or the DAC's supply voltage.
  The cause of the difference from nominal 5 V has not been established.
- With the DAC powered, Thomas measured A3V3 = 3.3 V, XSMT = 3.3 V,
  and FMT/DEMP/FLT = 0 V relative to ground. These confirm the static supply
  and control levels; clock signals and audio playback remain untested.
- esptool `flash-id` reported ESP32-C5HF4 revision v1.0, 4 MB embedded flash
  (manufacturer 0x46 XMC, device 0x4016), no embedded PSRAM, 48 MHz crystal.
  The soundboard partition table ends at exactly 0x400000, so it fits.
- After the first flash, boot log showed Storage ready, Audio ready (I2S on
  BCK=5, LRCK=6, DIN=10) and Wi-Fi joined. The monitor initially held the chip
  in reset via RTS/DTR; `monitor_rts = 0` / `monitor_dtr = 0` fixed it.
- Idle firmware (silent output), DC volts at the DAC header to GND:
  BCK = 1.65 V, LRCK = 1.65 V, DIN = 0 V. The clocks are switching at roughly
  50% duty and DIN carries zero-valued samples. Frequencies not measured.
- First Sound check (440 Hz, both slots identical in firmware, 10% volume):
  heard only in the right side and heavily distorted, with noticeable
  1-2 kHz energy. The same cable and speaker play a phone's stereo cleanly, so
  the fault is upstream of the cable: DAC jack, DAC output stage, or the
  I2S signals as the DAC receives them. Cause not yet established.
- After the cable was unplugged and re-plugged at the DAC, Sound check was
  centred (both channels) but still distorted. The one-sided result was most
  likely plug seating/contact at the DAC jack; the distortion is separate.
- Idle, cable unplugged: LROUT -> AGND = 0 V, ROUT -> AGND = 0 V (DC).
  Consistent with ground-centred outputs at rest. Does not test the negative
  half of the swing, which only matters when signal is present.
- Distortion character is identical at 10% and 40% digital volume (only the
  loudness changes). Each Sound check starts as a brief clean sine, then turns
  into a buzz. Hypothesis, not yet tested: the firmware's sine generator falls
  behind real time (C5 is rv32imac, no hardware FPU; `sinf` slows as its
  argument grows), so the I2S queue runs dry and auto_clear inserts silence.
- Stored clips, including test-sine-440 (the same tone precomputed on the Mac),
  play cleanly. Only the on-board generated Sound check buzzed, consistent with
  the generator-timing hypothesis; the DAC, wiring and I2S link are fine.
  Sound check was then removed from the firmware at Thomas's request.
