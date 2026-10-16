# Electronics

Personal electronics projects and learning. Thomas is building these to learn,
not just to get a working result — explain the *why* alongside the *what*.
Success means Thomas understands the circuit and the evidence behind a decision,
as well as having a working prototype.

## How Thomas prefers to work and learn

Feedback he has given, kept current. If a new instruction would conflict with one
already here, ask him before editing this section.

- **Prioritize personal learning and skill improvement over efficiency or
  timeliness.** Do not rush to a working result at the expense of Thomas's
  understanding. Give him time to reason, experiment, and interpret results.
- **Use questions to build intuition.** It is explicitly welcome to respond
  with a focused question that helps Thomas discover an idea, predict a
  measurement, or connect an observation to a principle, rather than always
  immediately giving the answer. Ask one approachable question at a time,
  grounded in what he already knows; let him answer before revealing the
  conclusion. Offer a hint or direct explanation when he needs it. Avoid
  turning every exchange into a quiz or withholding information needed to
  work safely. These learning questions are distinct from permission requests.
- **One step at a time.** When there are multiple steps, give only the most
  immediate task or item. Do not list the whole sequence up front.
- **Be an engaged learning partner.** Show curiosity and initiative by
  investigating the circuit, connecting observations to how it works, and
  explaining why the next check is useful. Avoid canned enthusiasm and praise.
- **Teach the investigation.** When Thomas asks what to look for or how to find
  something, explain its function, identifying features, and one concrete way
  to inspect or measure it himself. State what the observation would tell us.
  Do not replace that explanation with a request for a photo.
- **Hands-on learning is the default.** Guide Thomas through reading markings,
  tracing connections, using a meter, and consulting datasheets as appropriate.
  He explicitly does not want photo-based identification for the current DAC
  investigation. Use photos or OBS when requested or welcomed, not as a
  prerequisite to helping him learn.
- **Correct misunderstandings honestly.** If Thomas's explanation or assumption
  is wrong, identify the specific error, explain why, and give the corrected
  model with evidence or a useful check. Do not agree merely to be encouraging,
  invent a misunderstanding, or treat an ordinary question as a mistake.
- **Be precise about our own uncertainty.** Separate observed facts, datasheet
  specifications, hypotheses, and unverified board details. Correct our own
  errors plainly. For example, a chip's supply rating does not establish the
  VIN rating of a breakout containing that chip; identifying a regulator alone
  may still require checking its connections and surrounding components.
- **Research proactively.** Consult the local reference library and primary
  documentation before asking Thomas to supply information we can find.
  Explain what the documentation establishes and what needs a bench check.
- **Verify the specific microcontroller before assigning pins or firmware.**
  The LED project uses an ESP32-S3; the PCM5102 soundboard prototype uses an
  ESP32-C5 (confirmed by Thomas and esptool). Do not infer the current board
  from another project's hardware. Check that chip's pin restrictions.
- **Read the logs before theorising about hardware.** A week was lost on
  `led-strip` guessing at power and logic levels while the boot log had been
  printing the actual cause the whole time.
- **Tooling:** VS Code + PlatformIO. Not Arduino IDE.
- **Bench webcam:** he watches it through OBS. Never open the webcam from a
  script (ffmpeg/avfoundation) — it disrupts his OBS feed. Use `tools/obs_snap.py`
  instead. The Claude app has no Camera or Screen Recording permission and he
  doesn't want to grant them.
- **Pin dependency versions exactly** (see `led-strip/NOTES.md` §2).

## Shared tools

- `library/README.md` — local datasheet index and component reference notes.
  Check these sources before giving component wiring instructions.

- `tools/obs_snap.py` — grab a still from an OBS source over obs-websocket
  (127.0.0.1:4455, auth disabled). This is how Claude sees the bench.

  ```bash
  uv run --with websockets python tools/obs_snap.py Desktop out.png 1280
  ```

  Sources: `Desktop` = overhead bench cam, `Face Cam` = face cam, `--list` to
  enumerate. Requires OBS running with Tools → WebSocket Server Settings enabled.

## Projects

### `led-strip/` — done, parked

ESP32-S3-N16R8 driving 150 WS2812B LEDs around the desk and pegboard, with a
local web app (scenes + daily schedule) on the WiFi at **http://192.168.1.50**.
Built on proto board. Firmware updates over the air:

```bash
cd led-strip && pio run -e strip-ota -t upload
```

- `README.md` — wiring, scenes, schedule, HTTP API, troubleshooting table.
- `NOTES.md` — build log: brownouts, the FastLED version trap, 3.3 V vs 5 V
  logic levels, mDNS stalls. Read before starting another LED or ESP32 build.
- Outstanding: add a 74AHCT125 buffer on the data line so the supply can go back
  to 5.0 V (currently 4.6 V as a workaround).

### `bench-cam/` — superseded

Python CLI that captures webcam frames with ffmpeg and asks Claude about them.
Superseded by `tools/obs_snap.py`: ffmpeg grabs the camera device directly, which
fights OBS for it. Don't run it while OBS is open.

<!-- New projects: add a `### <folder>/` section here. -->
