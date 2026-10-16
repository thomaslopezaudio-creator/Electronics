# Web soundboard - ESP32-C5 + HiLetgo PCM5102

The ESP32 hosts a web page, stores short clips in flash, and plays one at a time
through the DAC into a powered speaker's AUX input. No SD card or PSRAM is
required. The browser converts supported audio files into 44.1 kHz, 16-bit PCM
WAV before upload. The ESP32 sends those samples in 32-bit stereo I2S slots.

## Wiring established at the bench

Disconnect USB before changing wiring.

| From | To |
| --- | --- |
| ESP32 5V | DAC VIN |
| ESP32 G | DAC GND |
| ESP32 GPIO5 | DAC BCK |
| ESP32 GPIO6 | DAC LRCK |
| ESP32 GPIO10 | DAC DIN |
| DAC SCK | GND |
| DAC XSMT | DAC A3V3 |
| DAC FMT, DEMP, FLT | Common ground |
| DAC 3.5 mm jack | Powered speaker AUX input |

GPIO10 replaces the initially suggested GPIO7, a C5 strapping pin.
Thomas confirmed the move. This prototype is a C5, not the S3 in `led-strip/`.
The board was identified through esptool as ESP32-C5 revision v1.0.

Measured: ESP32 5V header = 4.62 V; DAC A3V3 and XSMT = 3.3 V;
FMT, DEMP and FLT = 0 V. With the firmware idle, BCK and LRCK average 1.65 V
(switching) and DIN is 0 V (silence). Uploaded clips play cleanly through the
speaker. See [datasheet notes](../library/pcm5102-notes.md).

## Build and upload

Use VS Code + PlatformIO. The pinned pioarduino platform adds the C5 support
missing from this workspace's older Arduino platform. No external application
libraries are required. The target uses a 4 MB flash layout that ends exactly
at 0x400000. esptool identified the chip as ESP32-C5HF4 with 4 MB embedded
flash (XMC, device ID 0x4016), so the layout fits this board.

```sh
cd soundboard
pio run
pio run -t upload --upload-port /dev/cu.usbserial-110
pio device monitor --port /dev/cu.usbserial-110 --baud 115200
```

The port can change; use `pio device list`. This board uses a USB/UART bridge,
so the S3 native USB flags from the LED project must not be copied here.
The firmware replaces the current application and uses its own LittleFS
partition. First boot formats that partition if it is not already mountable.
There is no OTA slot in this initial layout. Flashing the application again
with this same layout normally preserves clips; do not use `uploadfs` to add
sounds, since that replaces the filesystem image.

## Connect and play

Copy `include/secrets.h.example` to `include/secrets.h` for home Wi-Fi; the latter
is gitignored. This workspace has a local copy of the existing Wi-Fi credentials
from the LED project. The board prints its numeric web address in the serial log.
After 15 seconds without a Wi-Fi connection it creates the `Soundboard` network,
password `soundboard`, with the page at `http://192.168.4.1`. Change the AP password
in the local secrets header if desired. Use this prototype on your local network;
it has no web authentication.

Nothing autoplays. Every boot starts at -23 dB on the volume trim (the quiet level the old 10% gave); set the speaker low before
the first clip. Add sounds with
the file picker; tap a clip to play or replace the current clip. Stop ends it.
Removing a clip asks for confirmation. Clip names are sanitized before upload,
and duplicate filenames are rejected rather than silently overwritten.

**Record** (for phones) opens the camera: film a short video and only its sound
is kept, as mono, trimmed to about 12 seconds. Phones block microphone access
for plain-HTTP pages such as this one (`getUserMedia` requires a secure context).
A camera capture is an ordinary file pick, so it is allowed. iOS Safari cannot
decode audio inside a `.MOV` directly, so the page copies the AAC frames out of
the QuickTime boxes and adds a 7-byte ADTS header to each one before decoding.
Tested on Thomas's iPhone: the capture is 48 kHz mono AAC.

Each converted clip can be at most 1 MiB: about 5.9 seconds stereo or 11.9
seconds mono. About 2.4 MB is allocated to clip storage, with some filesystem
overhead. The browser accepts only formats its audio decoder supports.
Uploads stop playback to avoid reading sound data while flash is being written.
Completed uploads survive restart; interrupted temporary uploads are discarded.

## Noise reduction

The **Noise reduction** toggle (on by default, remembered by the browser)
cleans steady background noise such as hiss, hum and fans from recordings and
added files before upload. It runs on the phone: the board serves
`/rnnoise.wasm`, a WebAssembly build of [RNNoise](https://github.com/xiph/rnnoise)
(see `THIRD_PARTY.md`), and the page runs it on the decoded audio at 48 kHz
before converting to 44.1 kHz. It changes the uploaded audio permanently, so
turn it off for music or sounds you want untouched.

RNNoise needs about half a second to learn the noise, so the page first runs
the opening second through it and discards the output, then processes the
clip. Its output lags by one 480-sample frame, which the page removes.
Measured on synthetic speech with white noise at -40 dBFS: noise fell to
-62 dBFS at the start (-41 without priming) and -69 dBFS later; speech level
was unchanged (-19.2 dBFS); 5 s of audio took 0.25 s in Chromium on the Mac.
Not yet measured on the iPhone.

## Clip tools and the edit page

Each clip has **Loop**, **Edit**, **Rename** and **Remove**. Settings are stored
per clip in `/settings.txt` on the board and applied during playback, so the
WAV itself is never changed. With Loop on, edits are heard within a fraction of
a second.

The edit page is laid out as a console channel strip (styled after a
well-used SSL 4000 G: dark strip, silkscreen labels, LED switches, coloured
knob caps, a masking-tape scribble strip). Top to bottom: name and transport,
trim, dynamics, EQ, loudness match. It has:

- **Graphic EQ**: 10 horizontal sliders on a black faceplate styled after an
  API 560 (from memory of its look, not a copy): a small actuator sliding inside a
  recessed slot, major marks only. Drag left/right anywhere in a band's row
  (relative, so a tap changes nothing); the faceplate claims horizontal swipes
  so the browser does not treat them as back navigation. Octave bands 31 Hz (top) to
  16 kHz, -12 dB left to +12 dB right in 1 dB steps.
  The Q widens at small boost/cut and narrows at large settings, like API's
  "Proportional Q". API publishes only that behaviour (and a ~12 dB/octave
  skirt at +/-12 dB), not values, so `proportionalQ()` in `include/dsp.h`
  (Q 0.6 + 0.9 x |gain|/12) is our approximation. The 560's stepped switch
  positions are not reproduced.
- **Leveling amplifier**: the **Input gain** fader (vertical, 0 dB at the
  bottom) is the input gain (0-60 dB) into a
  fixed -10 dBFS threshold, soft-knee 3:1. Like the LA-2A it has no attack or
  release controls: attack is ~10 ms; release is two-stage, about half in
  ~60 ms and the rest over 0.5-15 s, slower after long or heavy compression.
  0 dB bypasses it. **Pre EQ** puts the compressor before the EQ (default:
  after), so EQ boosts no longer push it harder. **Fast attack** shortens the
  attack from ~10 ms to ~1 ms (host test: 90% of the reduction in 4.4 ms vs
  36 ms); the release is unchanged. A vertical LED meter labelled **Reduction**, beside the fader, shows
  gain reduction: green 1-6 dB, yellow 8-12 dB, red 16-20 dB.
- **Drive** (vertical fader left of Input gain, 0-48 dB) and three **Drive shape**
  sliders: a saturator ahead of the compressor. Drive raises the level, then a lookup
  table shapes it: a Chebyshev polynomial up to full scale, and past full scale a smooth
  continuation (same value and slope, then levelling off) so there is no sharp corner.
  For a sine that peaks just at full scale each polynomial term is exactly one harmonic
  (2nd to 9th), so the sliders set harmonic levels against the fundamental. The loudest
  harmonic is pinned at -10 dB. **Low-High** (tilt) slopes the levels across the orders,
  -12 dB per order at 0 (low orders dominate, falling steeply), flat at 50, +12 dB per
  order at 100 (the 9th is loudest, falling toward the low orders). **Odd-Even** (balance)
  crossfades the parities by amplitude; positions 45-55 are an exact even split ("both"), then the gap starts at 1 dB and widens smoothly to the ends (about 10: 19 dB, 25: 9 dB); 0 is odd harmonics only, 100 even only.
  **Soft-Hard** sets how wide the knee past full scale is (rounded at 0, close to a hard
  clip at 100); it only matters when the peak is a little past full scale, because with
  a lot of drive any curve acts like a hard clip. Host test 12 checks the levels to
  0.3 dB at six slider settings. Quieter signals give fewer, higher harmonics (default
  shape, 12 dB below full scale: H2 -18.1, H3 -34.1 dB). Even terms use the square-wave
  sign so they flatten the peaks; a 10 Hz DC blocker removes the offset they create.
  Drive 0 is a bypass. The range 2nd-9th is a limit of this method (each order needs a
  polynomial term and more means more aliasing), not a claim about Neve consoles; the
  research found no measured 1073 harmonic spectrum. The table also scales the output to
  stay within full scale, so heavy drive lowers the level; loudness matching restores it.
  **Oversample** (per clip: off, 2x, 4x) runs the shaper at a higher rate to cut
  aliasing. On a 5 kHz sine, folded-back energy below the harmonics (host): drive +12 dB
  34 dB off, 55 dB at 2x; +24 dB 14 / 34 / 36 dB for off / 2x / 4x; +48 dB 10 / 18 / 24 dB
  (a high enough gain turns any curve into a near-hard clip, so heavy drive stays rough).
  Cost on the board: mono clip with 10 EQ bands, DSP load ~36% off, 58% at 2x, 79% at 4x.
  Stereo with a full EQ and compression at 4x underran (148 dropouts); stereo at 2x with
  looping made the web server unresponsive until reset. Use oversampling on mono clips.
- **Loudness match**: after changes settle (0.7 s), the board runs the clip
  through the same processing and measures BS.1770-4 integrated loudness
  (K-weighting, 400 ms gated blocks) before and after. Makeup gain = in - out,
  so the edited clip plays at the original's LUFS. The measurement cancels the
  input gain first (makeup = -input gain) and does not clip the result, because
  playback applies input gain, reduction and makeup as one gain. An earlier
  version clipped at full scale before measuring and so left hard-driven clips
  up to ~10 dB too loud (host test 11). Edited clips are re-matched at every boot. Measured on the board: 1.3 s
  for a 2.2 s stereo clip when idle, longer while playing (it pauses after
  every chunk so the audio task keeps up). Until it finishes, a live estimate
  holds the level: the Input gain change is offset at once (it is input
  gain), and a follower compares K-weighted loudness before and after the
  processing over ~150 ms. In the host test, turning settings up mid-playback
  jumped 10.8 dB with the old makeup and stayed within 0.4 dB with the follower.
- **Start / end buffer**: one slider with two thumbs, Start on the left and
  End on the right, trimming from each end (at least 50 ms remains), with
  5 ms fades so cuts do not click. The played length always equals a whole
  BPM at 2 beats, so the BPMs shown at /2, /4, /6 and /12 are all whole
  (BPM = 60 x beats / seconds). The BPM is rounded first and the End is
  placed from it; Start stays where it is dropped; trims are stored to the nearest frame (22.7 us). Long clips snap in
  coarse steps: 12 s is 10 BPM at /2, and the next step, 11, is 10.9 s.
  Behind the slider, the clip's waveform is drawn from `/api/peaks` (the board
  reads the WAV once and returns 160 peak levels); the part trimmed off is dimmed.

The C5 has no floating-point unit (`-march=rv32imac`), so per-sample processing
is integer: biquads use Q28 coefficients and 64-bit accumulators; floating
point runs only once per 32-frame block. Measured DSP load while playing a
stereo clip: ~34% with 4 bands + compressor, ~48% with all 10 bands.

## What the firmware does

- `src/main.cpp`: networking, HTTP endpoints, storage, and a separate audio task.
- `src/web_page.h`: the page and local audio conversion, embedded in firmware.
- `include/wav.h`: bounds-checked RIFF/WAV validation.
- `include/dsp.h`: EQ, compressor and loudness meter, shared by playback and tests.
- **Volume trim**: a vertical fader from 0 dB down to -60 dB in 1 dB steps,
  marked every 10 dB; the bottom stop is -inf (silence). The fader's 0 is
  3 dB above unity (`VOLUME_BOOST_DB`), because loudness matching returns clips
  to their input level, which is often quiet. Hot clips can clip at the output
  near the top; back the fader off. The board turns dB into a Q16 gain,
  65536 x 10^((dB+3)/20), once per change.
- The audio task reads 256 frames at a time, scales their amplitude for volume,
  and expands 16-bit samples into the high bits of 32-bit I2S slots. Mono is
  duplicated into both channels. Idle output is zero-valued samples.
- LRCK is nominally 44,100 Hz. Each stereo frame uses 64 BCK pulses, so BCK is
  nominally 2,822,400 Hz. The hardware peripheral generates these signals;
  software does not toggle individual clock pulses.
- Around 70 ms of audio may already be queued when Stop or another clip is
  selected. The buffer is that long so flash writes (which pause the CPU's
  code cache) do not cause dropouts; settings are saved once edits pause for
  1 s. `/api/status` reports `underruns`, the number of times the I2S DMA ran
  out of fresh audio. Measured while editing a looping clip: 15 dropouts per
  loudness match before these changes, 0 after. The prototype does not mix simultaneous clips.

## Validation

Host parser check (macOS):

```sh
clang++ -std=c++17 -Wall -Wextra -Werror -Iinclude tests/wav_test.cpp -o /tmp/soundboard-wav-test
/tmp/soundboard-wav-test
```

DSP check (EQ gain, K-weighting coefficients vs. the BS.1770 table, LUFS of
reference sines, compressor curve and release timing, loudness matching):

```sh
clang++ -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tests/dsp_test.cpp -o /tmp/soundboard-dsp-test
/tmp/soundboard-dsp-test
```

Parser checks cover mono/stereo, padded metadata, reordered chunks, truncated data,
wrong formats and oversized chunks. A successful firmware build does not
establish working I2S wiring or audible playback; those require bench checks.

Primary software references:
[pioarduino release](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.39),
[Espressif C5 I2S](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c5/api-reference/peripherals/i2s.html).
