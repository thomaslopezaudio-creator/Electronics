#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <driver/i2s_std.h>
#include <esp_timer.h>
#include <vector>
#include "wav.h"
#include "dsp.h"
#include "web_page.h"
#include "rnnoise_wasm.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif
#ifndef SOUNDBOARD_AP_PASS
#define SOUNDBOARD_AP_PASS "soundboard"
#endif

constexpr uint32_t SAMPLE_RATE = 44100;
constexpr size_t MAX_CLIP = 1024 * 1024;
constexpr size_t FRAMES = 256;
constexpr float MAKEUP_MAX_DB = 60; // room to undo the full input gain
constexpr uint32_t FADE_FRAMES = 220; // 5 ms ramps at trim points and loop joins
constexpr gpio_num_t BCK = GPIO_NUM_5, LRCK = GPIO_NUM_6, DATA = GPIO_NUM_10;
WebServer server(80);
i2s_chan_handle_t tx;
SemaphoreHandle_t audioMutex;
File uploadFile;
bool uploading = false, storageOK = false, audioOK = false, analyzing = false;
uint32_t restartAt = 0; // millis() deadline; 0 = no restart requested
// Master trim in dB: 0 at the top of the fader, VOLUME_OFF_DB at the bottom = -inf (silence).
// The fader's 0 is VOLUME_BOOST_DB above unity: matched clips sit at their input level,
// which is often quiet. Hot clips can clip at the output near the top; back the fader off.
constexpr int VOLUME_OFF_DB = -70, VOLUME_BOOST_DB = 3;
constexpr int32_t volumeGainQ16(int db) { return db <= VOLUME_OFF_DB ? 0 : int32_t(65536 * __builtin_pow(10.0, (db + VOLUME_BOOST_DB) / 20.0) + 0.5); }
int volumeDb = -20 - VOLUME_BOOST_DB; // Same quiet level as before the boost, on every boot.
int32_t volumeQ16 = volumeGainQ16(-20 - VOLUME_BOOST_DB);
String audioError, uploadError, uploadTarget;
size_t uploaded = 0;
uint32_t lastUpload = 0;
constexpr const char *TEMP_PATH = "/incoming.tmp";
constexpr const char *SETTINGS_PATH = "/settings.txt", *SETTINGS_TEMP = "/settings.tmp";

// Playback state, shared with the audio task; guarded by audioMutex.
struct Playback {
  File file;
  WavInfo info;
  String name;
  uint32_t start = 0, end = 0, pos = 0; // byte offsets within the data chunk
  bool loop = false;
  Chain chain;
  LoudnessFollower follower;
  bool follow = false; // use the live makeup estimate until matching finishes
} play;
float dspLoad = 0; // fraction of real time spent processing audio
volatile uint32_t underruns = 0; // DMA ran out of fresh audio (heard as a dropout)
bool IRAM_ATTR onUnderrun(i2s_chan_handle_t, i2s_event_data_t *, void *) { ++underruns; return false; }

// Per-clip settings, used only from the loop task (HTTP handlers, analysis).
struct Entry { String name; ClipSettings s; };
std::vector<Entry> settingsList;
std::vector<String> pendingAnalysis;
uint32_t lastSettingsChange = 0;
bool settingsDirty = false; // saved once edits pause; flash writes stall audio
volatile bool playing = false;

String jsonString(const String &s) {
  String out = "\"";
  for (size_t i = 0; i < s.length(); ++i) {
    unsigned char c = s[i];
    if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
    else if (c >= 32) out += char(c);
  }
  return out + '"';
}
String jsonNumber(float v, int digits = 1) { return isnan(v) ? String("null") : String(v, digits); }
void errorResponse(int code, const String &message) {
  server.send(code, "application/json", "{\"error\":" + jsonString(message) + "}");
}
bool validName(const String &name) {
  if (!name.endsWith(".wav") || name.length() < 5 || name.length() > 48) return false;
  for (size_t i = 0; i < name.length(); ++i) {
    const char c = name[i];
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) return false;
  }
  return name.indexOf("..") < 0;
}
bool inspect(File &f, WavInfo &info) {
  return parseWav(f.size(), [&](uint32_t offset, uint8_t *dest, size_t n) {
    return f.seek(offset) && f.read(dest, n) == n;
  }, info);
}

// ---- Settings storage: one line per clip in a single file (a LittleFS file
// occupies at least one 4 KB block, so per-clip files would waste space).
Entry *findEntry(const String &name) {
  for (Entry &e : settingsList) if (e.name == name) return &e;
  return nullptr;
}
ClipSettings settingsFor(const String &name) {
  Entry *e = findEntry(name);
  return e ? e->s : ClipSettings{};
}
void saveSettings() {
  File f = LittleFS.open(SETTINGS_TEMP, "w");
  if (!f) return;
  for (const Entry &e : settingsList) {
    const ClipSettings &s = e.s;
    f.printf("%s|%d|", e.name.c_str(), s.loop ? 1 : 0);
    for (int b = 0; b < EQ_BANDS; ++b) f.printf(b ? ",%d" : "%d", s.eq[b]);
    f.printf("|%u|%.4f|%.4f|%.2f|%.2f|%.2f|%d|%d|%u|%u|%u|%u|%u|S\n", s.comp, s.startMs, s.endMs,
             s.makeupDb, s.inLufs, s.outLufs, s.compPre ? 1 : 0, s.compFast ? 1 : 0,
             s.drive, s.tilt, s.balance, s.hardness, s.oversample);
  }
  f.close();
  LittleFS.rename(SETTINGS_TEMP, SETTINGS_PATH);
}
void loadSettings() {
  File f = LittleFS.open(SETTINGS_PATH, "r");
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    const int bar = line.indexOf('|');
    if (bar < 0) continue;
    Entry e; e.name = line.substring(0, bar);
    int loop, eq[EQ_BANDS], comp, pre = 0, fast = 0, sat[6] = {}; float start, end, makeup, in, out;
    const int got = sscanf(line.c_str() + bar + 1, "%d|%d,%d,%d,%d,%d,%d,%d,%d,%d,%d|%d|%f|%f|%f|%f|%f|%d|%d|%d|%d|%d|%d|%d|%d",
      &loop, &eq[0], &eq[1], &eq[2], &eq[3], &eq[4], &eq[5], &eq[6], &eq[7], &eq[8], &eq[9],
      &comp, &start, &end, &makeup, &in, &out, &pre, &fast, &sat[0], &sat[1], &sat[2], &sat[3], &sat[4], &sat[5]);
    if (got < 15 || !validName(e.name) || !LittleFS.exists("/" + e.name)) continue;
    e.s.loop = loop;
    for (int b = 0; b < EQ_BANDS; ++b) e.s.eq[b] = int8_t(constrain(eq[b], -EQ_MAX_DB, EQ_MAX_DB));
    e.s.comp = uint8_t(constrain(comp, 0, COMP_MAX_DB));
    e.s.startMs = constrain(start, 0.0f, 60000.0f); e.s.endMs = constrain(end, 0.0f, 60000.0f); e.s.makeupDb = makeup;
    if (got >= 17) { e.s.inLufs = in; e.s.outLufs = out; }
    e.s.compPre = got >= 18 && pre; // older files lack these fields
    e.s.compFast = got >= 19 && fast;
    // Saturator fields: drive|tilt|balance|hardness|os then "S", or the older drive|h2|h3|h4|h5|os (harmonics dropped).
    const bool shaped = line.endsWith("|S");
    if (got >= 24) {
      e.s.drive = uint8_t(constrain(sat[0], 0, SAT_MAX_DRIVE_DB));
      if (shaped) {
        e.s.tilt = uint8_t(constrain(sat[1], 0, SAT_SHAPE_MAX));
        e.s.balance = uint8_t(constrain(sat[2], 0, SAT_SHAPE_MAX));
        e.s.hardness = uint8_t(constrain(sat[3], 0, SAT_SHAPE_MAX));
        e.s.oversample = uint8_t(constrain(sat[4], 0, 2));
      } else if (got >= 25) {
        e.s.oversample = uint8_t(constrain(sat[5], 0, 2));
      }
    }
    settingsList.push_back(e);
  }
}
void putSettings(const String &name, const ClipSettings &s) {
  Entry *e = findEntry(name);
  if (e) e->s = s; else settingsList.push_back({name, s});
  settingsDirty = true;
  lastSettingsChange = millis();
}
void forgetSettings(const String &name) {
  for (size_t i = 0; i < settingsList.size(); ++i)
    if (settingsList[i].name == name) { settingsList.erase(settingsList.begin() + i); break; }
  for (size_t i = 0; i < pendingAnalysis.size(); ++i)
    if (pendingAnalysis[i] == name) { pendingAnalysis.erase(pendingAnalysis.begin() + i); break; }
  saveSettings();
}

// Byte range to play after trimming, to the nearest frame; falls back to the
// whole clip if the trims would leave less than 50 ms.
void trimBounds(const WavInfo &info, const ClipSettings &s, uint32_t &start, uint32_t &end) {
  const uint32_t frameBytes = info.channels * 2, frames = info.bytes / frameBytes;
  const uint32_t a = lround(double(s.startMs) * SAMPLE_RATE / 1000), b = lround(double(s.endMs) * SAMPLE_RATE / 1000);
  if (uint64_t(a) + b + SAMPLE_RATE / 20 > frames) { start = 0; end = frames * frameBytes; return; }
  start = a * frameBytes;
  end = (frames - b) * frameBytes;
}
uint32_t durationMs(const WavInfo &info) {
  return uint64_t(info.bytes / (info.channels * 2)) * 1000 / SAMPLE_RATE;
}

// Caller holds audioMutex.
void stopLocked() {
  playing = false;
  if (play.file) play.file.close();
  play.name = "";
  play.chain.grDb = 0;
}
// Reads frames into L/R in the DSP scale (int16 << 8). Returns frames read.
size_t readFrames(File &f, const WavInfo &info, size_t frames, int32_t *L, int32_t *R) {
  uint8_t pcm[FRAMES * 4];
  const size_t frameBytes = info.channels * 2, want = frames * frameBytes;
  if (f.read(pcm, want) != want) return 0;
  for (size_t i = 0; i < frames; ++i) {
    // Multiplication avoids undefined left-shifting of negative integers.
    L[i] = int32_t(int16_t(le16(pcm + i * frameBytes))) * 256;
    R[i] = info.channels == 2 ? int32_t(int16_t(le16(pcm + i * frameBytes + 2))) * 256 : L[i];
  }
  return frames;
}
void audioTask(void *) {
  int32_t L[FRAMES], R[FRAMES], dry[FRAMES], output[FRAMES * 2];
  for (;;) {
    memset(output, 0, sizeof(output));
    xSemaphoreTake(audioMutex, portMAX_DELAY);
    if (play.file) {
      const int64_t began = esp_timer_get_time();
      const size_t frameBytes = play.info.channels * 2;
      const size_t n = min(size_t(FRAMES), size_t((play.end - play.pos) / frameBytes));
      if (!n || readFrames(play.file, play.info, n, L, R) != n) {
        audioError = n ? "Clip read failed." : "";
        stopLocked();
      } else {
        // Short fades wherever playback starts or stops mid-waveform.
        const uint32_t first = play.pos / frameBytes, startF = play.start / frameBytes, endF = play.end / frameBytes;
        for (size_t i = 0; i < n; ++i) {
          const uint32_t f = first + i, d = min(f - startF, endF - 1 - f);
          if (d < FADE_FRAMES) {
            L[i] = int32_t(int64_t(L[i]) * d / FADE_FRAMES);
            R[i] = int32_t(int64_t(R[i]) * d / FADE_FRAMES);
          }
        }
        const bool stereo = play.info.channels == 2;
        for (size_t i = 0; i < n; ++i) dry[i] = stereo ? (L[i] >> 1) + (R[i] >> 1) : L[i];
        play.chain.process(L, stereo ? R : L, n);
        for (size_t i = 0; i < n; ++i) play.follower.add(dry[i], stereo ? (L[i] >> 1) + (R[i] >> 1) : L[i]);
        play.follower.update(play.chain.makeup());
        if (play.follow && play.follower.ready())
          play.chain.setMakeup(constrain(play.follower.makeupDb(), -MAKEUP_MAX_DB, MAKEUP_MAX_DB));
        for (size_t i = 0; i < n; ++i) {
          const int32_t right = play.info.channels == 2 ? R[i] : L[i];
          const int32_t l = constrain(int32_t(int64_t(L[i]) * volumeQ16 >> 16), -SAMPLE_MAX, SAMPLE_MAX);
          const int32_t r = constrain(int32_t(int64_t(right) * volumeQ16 >> 16), -SAMPLE_MAX, SAMPLE_MAX);
          output[i * 2] = l * 256;     // 24-bit result in the high bits of a 32-bit slot
          output[i * 2 + 1] = r * 256;
        }
        play.pos += n * frameBytes;
        if (play.pos >= play.end) {
          if (play.loop && play.file.seek(play.info.offset + play.start)) play.pos = play.start;
          else stopLocked();
        }
      }
      const float busy = float(esp_timer_get_time() - began) / (FRAMES * 1e6f / SAMPLE_RATE);
      dspLoad += (busy - dspLoad) * 0.05f;
    } else dspLoad *= 0.95f;
    xSemaphoreGive(audioMutex);
    size_t written = 0;
    esp_err_t result = i2s_channel_write(tx, output, sizeof(output), &written, 1000);
    if (result != ESP_OK || written != sizeof(output)) {
      xSemaphoreTake(audioMutex, portMAX_DELAY);
      audioError = "I2S output failed; restart the board and check the serial log.";
      audioOK = false;
      stopLocked();
      xSemaphoreGive(audioMutex);
      Serial.printf("I2S write failed: %s (%u bytes)\n", esp_err_to_name(result), unsigned(written));
      vTaskDelete(nullptr);
    }
  }
}
bool initAudio() {
  i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channel.dma_desc_num = 12; // ~70 ms of slack: rides through a flash write
  channel.dma_frame_num = 256;
  channel.auto_clear = true;
  esp_err_t result = i2s_new_channel(&channel, &tx, nullptr);
  if (result != ESP_OK) { Serial.printf("I2S allocate: %s\n", esp_err_to_name(result)); return false; }
  i2s_event_callbacks_t callbacks = {};
  callbacks.on_send_q_ovf = onUnderrun;
  i2s_channel_register_event_callback(tx, &callbacks, nullptr);
  i2s_std_config_t config = {};
  config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
  config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  config.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  config.gpio_cfg.bclk = BCK;
  config.gpio_cfg.ws = LRCK;
  config.gpio_cfg.dout = DATA;
  config.gpio_cfg.din = I2S_GPIO_UNUSED;
  result = i2s_channel_init_std_mode(tx, &config);
  if (result == ESP_OK) result = i2s_channel_enable(tx);
  if (result != ESP_OK) { Serial.printf("I2S setup: %s\n", esp_err_to_name(result)); return false; }
  return true;
}

// ---- Loudness matching. Runs the same Chain over the trimmed clip and
// measures BS.1770 loudness before and after; makeup is the difference.
// Returns false if a settings change interrupted it; the clip then stays pending.
bool analyze(const String &name) {
  Entry *e = findEntry(name);
  if (!e) return true;
  ClipSettings s = e->s;
  if (s.flat()) { s.makeupDb = 0; s.inLufs = s.outLufs = NAN; }
  else {
    File f = LittleFS.open("/" + name, "r");
    WavInfo info;
    if (!f || !inspect(f, info)) { if (f) f.close(); return true; }
    uint32_t start, end;
    trimBounds(info, s, start, end);
    const int64_t began = esp_timer_get_time();
    // Measure with the input gain cancelled (makeup = -comp), so the signal stays near
    // its original scale. Playback applies input gain, reduction and makeup as one gain
    // with no clipping in between; clipping here under-read heavy settings by up to 10 dB.
    ClipSettings dry = s; dry.makeupDb = -float(s.comp);
    static Chain chain; // static: keeps ~1 KB off the loop task's stack
    chain.configure(dry, info.channels, SAMPLE_RATE); chain.reset();
    LoudnessMeter before, after;
    before.begin(info.channels, SAMPLE_RATE); after.begin(info.channels, SAMPLE_RATE);
    static int32_t L[FRAMES], R[FRAMES]; // static: the web server runs on this stack between chunks
    const uint32_t frameBytes = info.channels * 2;
    f.seek(info.offset + start);
    const uint32_t stamp = lastSettingsChange;
    analyzing = true;
    for (uint32_t pos = start, chunk = 0; pos < end; ++chunk) {
      const size_t n = min(size_t(FRAMES), size_t((end - pos) / frameBytes));
      if (!n || readFrames(f, info, n, L, R) != n) break;
      for (size_t i = 0; i < n; ++i) before.add(L[i], R[i]);
      chain.process(L, info.channels == 2 ? R : L, n);
      for (size_t i = 0; i < n; ++i) {
        // Wide enough that EQ boost is not clipped, small enough that squares cannot overflow.
        constexpr int32_t WIDE = 1 << 25;
        const int32_t l = constrain(L[i], -WIDE, WIDE);
        after.add(l, info.channels == 2 ? constrain(R[i], -WIDE, WIDE) : l);
      }
      pos += n * frameBytes;
      // Flash reads stall the audio task, so give it room after every chunk
      // while something plays (measured: 15 dropouts per match otherwise).
      if (playing || chunk % 16 == 15) vTaskDelay(1);
      // Keep the sliders responsive: serve requests between chunks, and give up if one
      // changed the settings (the clip is still pending, so it is measured again).
      if (chunk % 4 == 3) {
        server.handleClient();
        if (lastSettingsChange != stamp) { analyzing = false; f.close(); return false; }
      }
    }
    analyzing = false;
    f.close();
    s.inLufs = before.lufs(); s.outLufs = after.lufs();
    s.makeupDb = isnan(s.inLufs) || isnan(s.outLufs) ? 0 : constrain(s.inLufs - s.outLufs - s.comp, -MAKEUP_MAX_DB, MAKEUP_MAX_DB);
    Serial.printf("Matched %s: in %.1f LUFS, out %.1f LUFS, makeup %.1f dB (%u ms)\n", name.c_str(),
                  s.inLufs, s.outLufs, s.makeupDb, unsigned((esp_timer_get_time() - began) / 1000));
  }
  e = findEntry(name); // the list may have grown while requests were served
  if (!e) return true;
  e->s.makeupDb = s.makeupDb; e->s.inLufs = s.inLufs; e->s.outLufs = s.outLufs;
  settingsDirty = true;
  xSemaphoreTake(audioMutex, portMAX_DELAY);
  if (play.name == name) { play.chain.setMakeup(s.makeupDb); play.follow = false; }
  xSemaphoreGive(audioMutex);
  return true;
}
bool isPending(const String &name) {
  for (const String &p : pendingAnalysis) if (p == name) return true;
  return false;
}

// ---- HTTP handlers
void sendStatus() {
  xSemaphoreTake(audioMutex, portMAX_DELAY);
  String body = "{\"playing\":" + jsonString(play.name) +
    ",\"volume\":" + String(volumeDb) + ",\"audioReady\":" + (audioOK ? "true" : "false") +
    ",\"gr\":" + String(play.chain.grDb, 1) + ",\"makeup\":" + String(play.chain.makeup(), 1) + ",\"load\":" + String(int(dspLoad * 100 + 0.5f)) + ",\"underruns\":" + String(underruns) +
    ",\"matching\":" + (pendingAnalysis.empty() ? "false" : "true") +
    ",\"error\":" + jsonString(audioError) + "}";
  xSemaphoreGive(audioMutex);
  server.send(200, "application/json", body);
}
void listClips() {
  if (!storageOK) { errorResponse(503, "Clip storage could not be mounted."); return; }
  String body = "{\"clips\":[";
  File root = LittleFS.open("/");
  bool first = true;
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String name = f.name();
    if (name.startsWith("/")) name.remove(0, 1);
    if (!f.isDirectory() && validName(name)) {
      if (!first) body += ',';
      first = false;
      const ClipSettings s = settingsFor(name);
      body += "{\"name\":" + jsonString(name) + ",\"bytes\":" + String(f.size()) +
              ",\"loop\":" + (s.loop ? "true" : "false") + ",\"edited\":" +
              (s.flat() && !s.startMs && !s.endMs ? "false" : "true") + "}";
    }
    f.close();
  }
  body += "],\"free\":" + String(LittleFS.totalBytes() - LittleFS.usedBytes()) + "}";
  server.send(200, "application/json", body);
}
// Peak level (0-255) of the untrimmed clip in n equal slices, for drawing a waveform.
void sendPeaks() {
  const String name = server.arg("name");
  const int n = constrain(server.arg("n").toInt(), 20, 400);
  if (!storageOK || !validName(name)) { errorResponse(400, "Invalid clip."); return; }
  File f = LittleFS.open("/" + name, "r");
  WavInfo info;
  if (!f || !inspect(f, info)) { if (f) f.close(); errorResponse(404, "Clip not found."); return; }
  const uint32_t frameBytes = info.channels * 2, frames = info.bytes / frameBytes;
  std::vector<uint8_t> peaks(n, 0);
  int32_t L[FRAMES], R[FRAMES];
  f.seek(info.offset);
  uint32_t done = 0;
  for (uint32_t chunk = 0; done < frames; ++chunk) {
    const size_t want = min(size_t(FRAMES), size_t(frames - done));
    if (readFrames(f, info, want, L, R) != want) break;
    for (size_t i = 0; i < want; ++i) {
      const int bin = int(uint64_t(done + i) * n / frames);
      const int32_t m = max(abs(L[i]), abs(R[i])) / 256 / 128; // int16 -> 0..256
      if (m > peaks[bin]) peaks[bin] = uint8_t(min<int32_t>(m, 255));
    }
    done += want;
    if (playing || chunk % 16 == 15) vTaskDelay(1); // flash reads stall the audio task
  }
  f.close();
  String out = "{\"peaks\":[";
  for (int i = 0; i < n; ++i) { if (i) out += ','; out += peaks[i]; }
  server.send(200, "application/json", out + "]}");
}
void sendClip(const String &name) {
  File f = LittleFS.open("/" + name, "r");
  WavInfo info;
  if (!f || !inspect(f, info)) { if (f) f.close(); errorResponse(404, "Clip not found."); return; }
  f.close();
  const ClipSettings s = settingsFor(name);
  String eq;
  for (int b = 0; b < EQ_BANDS; ++b) { if (b) eq += ','; eq += String(s.eq[b]); }
  server.send(200, "application/json", "{\"name\":" + jsonString(name) +
    ",\"duration\":" + String(durationMs(info)) + ",\"frames\":" + String(info.bytes / (info.channels * 2)) + ",\"rate\":" + String(SAMPLE_RATE) +
    ",\"channels\":" + String(info.channels) +
    ",\"loop\":" + (s.loop ? "true" : "false") + ",\"eq\":[" + eq + "],\"comp\":" + String(s.comp) + ",\"drive\":" + String(s.drive) +
    ",\"tilt\":" + String(s.tilt) + ",\"balance\":" + String(s.balance) + ",\"hardness\":" + String(s.hardness) + ",\"os\":" + String(s.oversample) +
    ",\"pre\":" + (s.compPre ? "true" : "false") + ",\"fast\":" + (s.compFast ? "true" : "false") +
    ",\"start\":" + String(s.startMs, 4) + ",\"end\":" + String(s.endMs, 4) +
    ",\"makeup\":" + String(s.makeupDb, 1) + ",\"in\":" + jsonNumber(s.inLufs) +
    ",\"out\":" + jsonNumber(s.outLufs) + ",\"matching\":" + (isPending(name) ? "true" : "false") + "}");
}
bool parseUInt(const String &text, long maxValue, long &out) {
  if (text.isEmpty() || text.length() > 7) return false;
  for (size_t i = 0; i < text.length(); ++i) if (!isDigit(text[i])) return false;
  out = text.toInt();
  return out <= maxValue;
}
// Milliseconds, fractional allowed so a trim can land on an exact frame.
bool parseMs(const String &text, float &out) {
  if (text.isEmpty() || text.length() > 12) return false;
  int dots = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    if (text[i] == '.') ++dots;
    else if (!isDigit(text[i])) return false;
  }
  out = text.toFloat();
  return dots <= 1 && out <= 60000;
}
// POST: name plus any of loop, eq (10 comma-separated dB), comp, drive, tilt, balance, hardness (0-100), os (0-2), pre, fast, start, end.
void updateClip() {
  const String name = server.arg("name");
  if (!storageOK || !validName(name) || !LittleFS.exists("/" + name)) { errorResponse(404, "Clip not found."); return; }
  ClipSettings s = settingsFor(name);
  const int oldComp = s.comp;
  bool audible = false; long v; float ms;
  if (server.hasArg("loop")) s.loop = server.arg("loop") == "1";
  if (server.hasArg("eq")) {
    String list = server.arg("eq") + ",";
    for (int b = 0; b < EQ_BANDS; ++b) {
      const int comma = list.indexOf(',');
      String item = comma < 0 ? "" : list.substring(0, comma);
      list.remove(0, comma + 1);
      const bool negative = item.startsWith("-");
      if (negative) item.remove(0, 1);
      if (!parseUInt(item, EQ_MAX_DB, v)) { errorResponse(400, "EQ needs 10 values from -12 to 12 dB."); return; }
      s.eq[b] = int8_t(negative ? -v : v);
    }
    if (!list.isEmpty()) { errorResponse(400, "EQ needs 10 values from -12 to 12 dB."); return; }
    audible = true;
  }
  if (server.hasArg("pre")) {
    const bool pre = server.arg("pre") == "1";
    if (pre != s.compPre) { s.compPre = pre; audible = true; }
  }
  if (server.hasArg("fast")) {
    const bool fast = server.arg("fast") == "1";
    if (fast != s.compFast) { s.compFast = fast; audible = true; }
  }
  if (server.hasArg("comp")) {
    if (!parseUInt(server.arg("comp"), COMP_MAX_DB, v)) { errorResponse(400, "Compression must be 0-60 dB."); return; }
    s.comp = uint8_t(v); audible = true;
  }
  if (server.hasArg("drive")) {
    if (!parseUInt(server.arg("drive"), SAT_MAX_DRIVE_DB, v)) { errorResponse(400, "Drive must be 0-48 dB."); return; }
    s.drive = uint8_t(v); audible = true;
  }
  if (server.hasArg("os")) {
    if (!parseUInt(server.arg("os"), 2, v)) { errorResponse(400, "Oversampling must be 0, 1 or 2."); return; }
    s.oversample = uint8_t(v); audible = true;
  }
  struct { const char *arg; uint8_t ClipSettings::*field; } const shapes[] = {
    {"tilt", &ClipSettings::tilt}, {"balance", &ClipSettings::balance}, {"hardness", &ClipSettings::hardness}};
  for (const auto &sh : shapes) {
    if (!server.hasArg(sh.arg)) continue;
    if (!parseUInt(server.arg(sh.arg), SAT_SHAPE_MAX, v)) { errorResponse(400, String(sh.arg) + " must be 0-100."); return; }
    s.*(sh.field) = uint8_t(v); audible = true;
  }
  if (server.hasArg("start")) {
    if (!parseMs(server.arg("start"), ms)) { errorResponse(400, "Invalid start trim."); return; }
    s.startMs = ms; audible = true;
  }
  if (server.hasArg("end")) {
    if (!parseMs(server.arg("end"), ms)) { errorResponse(400, "Invalid end trim."); return; }
    s.endMs = ms; audible = true;
  }
  putSettings(name, s);
  if (audible) {
    if (!isPending(name)) pendingAnalysis.push_back(name);
    lastSettingsChange = millis();
  }
  // Apply live if this clip is playing. The saturator table takes tens of milliseconds to
  // build, so build it first, outside the audio lock, and let the chain adopt it.
  static SatTable spare;
  xSemaphoreTake(audioMutex, portMAX_DELAY);
  const bool live = play.name == name && !play.chain.hasTable(s);
  xSemaphoreGive(audioMutex);
  if (live && !spare.matches(s)) spare.build(s);
  xSemaphoreTake(audioMutex, portMAX_DELAY);
  if (play.name == name) {
    if (spare.matches(s)) play.chain.adoptTable(spare);
    play.loop = s.loop;
    const float makeup = play.chain.makeup();
    play.chain.configure(s, play.info.channels, SAMPLE_RATE);
    if (audible) {
      // Until the measurement returns: compression is input gain, so offset
      // it at once; the live follower then tracks the rest (EQ, gain reduction).
      play.chain.setMakeup(constrain(makeup - (s.comp - oldComp), -MAKEUP_MAX_DB, MAKEUP_MAX_DB));
      play.follow = true;
    }
    trimBounds(play.info, s, play.start, play.end);
    if (play.pos < play.start || play.pos >= play.end) {
      if (play.file.seek(play.info.offset + play.start)) play.pos = play.start;
      else stopLocked();
    }
  }
  xSemaphoreGive(audioMutex);
  sendClip(name);
}
void playClip() {
  if (!audioOK) { errorResponse(503, "Audio output is unavailable; check the serial log."); return; }
  if (uploading) { errorResponse(409, "Wait for the upload to finish."); return; }
  String name = server.arg("name");
  if (!storageOK || !validName(name)) { errorResponse(400, "Invalid clip."); return; }
  File next = LittleFS.open("/" + name, "r");
  if (!next) { errorResponse(404, "Clip not found."); return; }
  WavInfo info;
  const ClipSettings s = settingsFor(name);
  uint32_t start, end;
  if (!inspect(next, info) || (trimBounds(info, s, start, end), !next.seek(info.offset + start))) {
    next.close(); errorResponse(400, "Clip must be 44.1 kHz, 16-bit PCM WAV (mono or stereo)."); return;
  }
  xSemaphoreTake(audioMutex, portMAX_DELAY);
  stopLocked();
  audioError = "";
  play.name = name; play.file = next; play.info = info;
  play.start = start; play.end = end; play.pos = start; play.loop = s.loop;
  play.chain.configure(s, info.channels, SAMPLE_RATE);
  play.chain.reset();
  play.follower.begin(SAMPLE_RATE);
  play.follow = isPending(name);
  playing = true;
  xSemaphoreGive(audioMutex);
  server.send(200, "application/json", "{\"ok\":true}");
}
void renameClip() {
  const String name = server.arg("name"), to = server.arg("to");
  if (uploading) { errorResponse(409, "Wait for the upload to finish."); return; }
  if (analyzing) { errorResponse(409, "Measuring loudness; try again in a moment."); return; }
  if (!storageOK || !validName(name) || !LittleFS.exists("/" + name)) { errorResponse(404, "Clip not found."); return; }
  if (!validName(to)) { errorResponse(400, "Use letters, numbers, dashes or underscores."); return; }
  if (LittleFS.exists("/" + to)) { errorResponse(409, "That name is taken."); return; }
  xSemaphoreTake(audioMutex, portMAX_DELAY);
  if (play.name == name) stopLocked();
  const bool renamed = LittleFS.rename("/" + name, "/" + to);
  xSemaphoreGive(audioMutex);
  if (!renamed) { errorResponse(500, "Could not rename the clip."); return; }
  if (Entry *e = findEntry(name)) { e->name = to; saveSettings(); }
  for (String &p : pendingAnalysis) if (p == name) p = to;
  server.send(200, "application/json", "{\"ok\":true}");
}
void uploadChunk() {
  HTTPUpload &u = server.upload();
  lastUpload = millis();
  if (u.status == UPLOAD_FILE_START) {
    uploadError = "";
    uploaded = 0;
    uploading = true;
    xSemaphoreTake(audioMutex, portMAX_DELAY);
    stopLocked();
    xSemaphoreGive(audioMutex);
    uploadTarget = u.filename;
    if (!storageOK) uploadError = "Storage unavailable.";
    else if (!validName(uploadTarget)) uploadError = "Use a short WAV filename with letters, numbers, dashes or underscores.";
    else if (LittleFS.exists("/" + uploadTarget)) uploadError = "That filename already exists. Rename the clip first.";
    else {
      LittleFS.remove(TEMP_PATH);
      uploadFile = LittleFS.open(TEMP_PATH, "w");
      if (!uploadFile) uploadError = "Cannot create the clip file.";
    }
  } else if (u.status == UPLOAD_FILE_WRITE && uploadError.isEmpty()) {
    uploaded += u.currentSize;
    if (uploaded > MAX_CLIP) uploadError = "Clip is too long (maximum 1 MiB after conversion).";
    else if (uploadFile.write(u.buf, u.currentSize) != u.currentSize) uploadError = "Storage is full or the write failed.";
  } else if (u.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    if (uploadError.isEmpty()) {
      File f = LittleFS.open(TEMP_PATH, "r");
      WavInfo info;
      bool valid = f && inspect(f, info);
      f.close();
      if (!valid) uploadError = "Expected a 44.1 kHz, 16-bit PCM WAV file.";
      else if (!LittleFS.rename(TEMP_PATH, "/" + uploadTarget)) uploadError = "Could not save the clip.";
      else if (findEntry(uploadTarget)) forgetSettings(uploadTarget); // Stale settings from an old clip.
    }
    if (!uploadError.isEmpty() && storageOK) LittleFS.remove(TEMP_PATH);
    uploading = false;
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    if (storageOK) LittleFS.remove(TEMP_PATH);
    uploading = false;
    uploadError = "Upload interrupted.";
  }
}
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("\nSoundboard / %s / flash %u bytes\n", ESP.getChipModel(), ESP.getFlashChipSize());
  audioMutex = xSemaphoreCreateMutex();
  if (!audioMutex) { Serial.println("Cannot allocate audio mutex"); return; }
  // Dedicated prototype partition: formats only if it has no valid LittleFS.
  storageOK = LittleFS.begin(true);
  if (storageOK) { LittleFS.remove(TEMP_PATH); LittleFS.remove(SETTINGS_TEMP); loadSettings(); for (const Entry &e : settingsList) if (!e.s.flat()) pendingAnalysis.push_back(e.name); }
  Serial.printf("Storage: %s; %u clip settings\n", storageOK ? "ready" : "FAILED", unsigned(settingsList.size()));
  audioOK = initAudio();
  if (audioOK && xTaskCreate(audioTask, "audio", 8192, nullptr, 3, nullptr) != pdPASS) {
    audioOK = false;
    i2s_channel_disable(tx);
  }
  if (!audioOK) audioError = "I2S initialization failed. Check the serial log.";
  Serial.printf("Audio: %s; BCK=5 LRCK=6 DIN=10; 44100 Hz, stereo, 32-bit slots\n", audioOK ? "ready" : "FAILED");
  WiFi.setHostname("soundboard");
  if (strlen(WIFI_SSID)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Soundboard", SOUNDBOARD_AP_PASS);
    Serial.printf("Join Wi-Fi Soundboard; open http://%s\n", WiFi.softAPIP().toString().c_str());
  } else {
    WiFi.setSleep(false);
    Serial.printf("Open http://%s\n", WiFi.localIP().toString().c_str());
  }
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", WEB_PAGE); });
  server.on("/rnnoise.wasm", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "max-age=604800");
    server.send_P(200, "application/wasm", reinterpret_cast<const char *>(RNNOISE_WASM), sizeof(RNNOISE_WASM));
  });
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/clips", HTTP_GET, listClips);
  server.on("/api/clip", HTTP_GET, [] {
    const String name = server.arg("name");
    if (!storageOK || !validName(name)) { errorResponse(400, "Invalid clip."); return; }
    sendClip(name);
  });
  server.on("/api/peaks", HTTP_GET, sendPeaks);
  server.on("/api/clip", HTTP_POST, updateClip);
  server.on("/api/play", HTTP_POST, playClip);
  server.on("/api/rename", HTTP_POST, renameClip);
  server.on("/api/stop", HTTP_POST, [] {
    xSemaphoreTake(audioMutex, portMAX_DELAY); stopLocked(); xSemaphoreGive(audioMutex);
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/restart", HTTP_POST, [] {
    if (uploading) { errorResponse(409, "Wait for the upload to finish."); return; }
    xSemaphoreTake(audioMutex, portMAX_DELAY); stopLocked(); xSemaphoreGive(audioMutex);
    server.send(200, "application/json", "{\"ok\":true}");
    restartAt = millis() + 300; // let the reply reach the browser first
  });
  server.on("/api/volume", HTTP_POST, [] {
    // value: dB from 0 down to VOLUME_OFF_DB (-inf, silence).
    String s = server.arg("value");
    const bool negative = s.startsWith("-");
    if (negative) s.remove(0, 1);
    long v;
    if (!parseUInt(s, -VOLUME_OFF_DB, v) || (v && !negative)) { errorResponse(400, "Volume must be 0 to -70 dB."); return; }
    const int db = -int(v);
    xSemaphoreTake(audioMutex, portMAX_DELAY); volumeDb = db; volumeQ16 = volumeGainQ16(db); xSemaphoreGive(audioMutex);
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/upload", HTTP_POST, [] {
    if (!uploadError.isEmpty()) errorResponse(400, uploadError);
    else if (uploading || !uploaded) errorResponse(400, "No complete audio file received.");
    else server.send(201, "application/json", "{\"ok\":true}");
    uploaded = 0;
  }, uploadChunk);
  server.on("/api/delete", HTTP_POST, [] {
    String name = server.arg("name");
    if (uploading) { errorResponse(409, "Wait for the upload to finish."); return; }
    if (analyzing) { errorResponse(409, "Measuring loudness; try again in a moment."); return; }
    if (!storageOK || !validName(name)) { errorResponse(400, "Invalid clip."); return; }
    xSemaphoreTake(audioMutex, portMAX_DELAY);
    if (play.name == name) stopLocked();
    const bool removed = LittleFS.remove("/" + name);
    xSemaphoreGive(audioMutex);
    if (!removed) errorResponse(404, "Clip could not be removed.");
    else { forgetSettings(name); server.send(200, "application/json", "{\"ok\":true}"); }
  });
  server.onNotFound([] { errorResponse(404, "Not found."); });
  server.begin();
  Serial.println("Web soundboard ready; output is silent until Play.");
}
void loop() {
  if (!audioMutex) { delay(1000); return; }
  server.handleClient();
  if (restartAt && int32_t(millis() - restartAt) >= 0) {
    if (settingsDirty) saveSettings();
    Serial.println("Restart requested from the web page.");
    delay(50);
    ESP.restart();
  }
  if (uploading && millis() - lastUpload > 30000) {
    if (uploadFile) uploadFile.close();
    if (storageOK) LittleFS.remove(TEMP_PATH);
    uploading = false;
    uploadError = "Upload timed out.";
  }
  if (settingsDirty && millis() - lastSettingsChange > 1000) { settingsDirty = false; saveSettings(); }
  // Match loudness once the sliders have been still for a moment.
  if (!pendingAnalysis.empty() && !uploading && millis() - lastSettingsChange > 700) {
    const String name = pendingAnalysis.front();
    if (analyze(name)) {
      // Remove after analysing so the page sees "matching" until the result is saved.
      for (size_t i = 0; i < pendingAnalysis.size(); ++i)
        if (pendingAnalysis[i] == name) { pendingAnalysis.erase(pendingAnalysis.begin() + i); break; }
    }
  }
  delay(1);
}
