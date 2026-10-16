#pragma once
// Per-clip processing: 10-band graphic EQ -> saturator -> LA-2A-style compressor -> makeup.
// Plain C++ so the same code runs on the ESP32-C5 and in host tests.
//
// The C5 has no FPU (rv32imac), so per-sample work is integer: samples are
// int16 << 8 (full scale 2^23), biquad coefficients are Q28, products are
// 64-bit. Floating point runs only once per 32-frame block (compressor gain)
// or when settings change (coefficients).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

constexpr int EQ_BANDS = 10, EQ_MAX_DB = 12, COMP_MAX_DB = 60;
constexpr int SAT_MAX_DRIVE_DB = 48, SAT_SHAPE_MAX = 100, SAT_TOP_HARMONIC = 9; // shape sliders are 0..100; harmonics 2..9
constexpr double EQ_HZ[EQ_BANDS] = {31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
constexpr double FULL_SCALE = 8388608.0; // int16 << 8
constexpr int32_t SAMPLE_MAX = 8388607;
constexpr double DSP_PI = 3.14159265358979323846;

struct ClipSettings {
  bool loop = false;
  int8_t eq[EQ_BANDS] = {}; // dB, -12..12
  uint8_t comp = 0;         // "Compression" = input gain into the compressor, dB
  uint8_t drive = 0;        // saturator gain ahead of the compressor, dB (0 = saturator off)
  uint8_t oversample = 0;   // saturator oversampling: 0 = off, 1 = 2x, 2 = 4x
  uint8_t tilt = 25;        // harmonic order: 0 = low orders, falling steeply; 50 = even spread; 100 = high orders dominate
  uint8_t balance = 50;     // 0 = odd harmonics only, 50 = both, 100 = even harmonics only
  uint8_t hardness = 50;    // 0 = rounded knee past full scale, 100 = nearly a hard clip
  bool compPre = false;     // compressor before the EQ (default: after)
  bool compFast = false;    // ~1 ms attack instead of the LA-2A's ~10 ms
  float startMs = 0, endMs = 0; // trimmed from each end; fractional so trims can sit on exact frames
  float makeupDb = 0;       // set by loudness matching
  float inLufs = NAN, outLufs = NAN;
  bool flat() const {
    for (int8_t g : eq) if (g) return false;
    return drive == 0 && comp == 0;
  }
};

// API does not publish the 560's Q values, only that bandwidth is wide at low
// boost/cut and narrows to roughly a 12 dB/octave skirt at +/-12 dB. This
// linear fit is our approximation of that behaviour, not measured data.
inline double proportionalQ(int gainDb) { return 0.6 + 0.9 * std::abs(gainDb) / EQ_MAX_DB; }

struct Biquad {
  int32_t b0 = 1 << 28, b1 = 0, b2 = 0, a1 = 0, a2 = 0; // Q28, a0 normalized
  int32_t x1[2] = {}, x2[2] = {}, y1[2] = {}, y2[2] = {};
  void set(double nb0, double nb1, double nb2, double na0, double na1, double na2) {
    auto q = [&](double v) { return int32_t(std::lround(v / na0 * (1 << 28))); };
    b0 = q(nb0); b1 = q(nb1); b2 = q(nb2); a1 = q(na1); a2 = q(na2);
  }
  void reset() { *this = Biquad{b0, b1, b2, a1, a2}; }
  void peaking(double hz, double fs, int gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0), w = 2 * DSP_PI * hz / fs;
    const double alpha = std::sin(w) / (2 * proportionalQ(gainDb)), c = std::cos(w);
    set(1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A);
  }
  // Direct form I with a 64-bit accumulator; rounding avoids a DC offset.
  inline int32_t run(int ch, int32_t x) {
    const int64_t acc = int64_t(b0) * x + int64_t(b1) * x1[ch] + int64_t(b2) * x2[ch] -
                        int64_t(a1) * y1[ch] - int64_t(a2) * y2[ch];
    // Saturate: EQ boosts after heavy compression could exceed int32.
    const int64_t v = (acc + (1 << 27)) >> 28, lim = 0x7fffffffLL;
    const int32_t y = int32_t(v > lim ? lim : v < -lim ? -lim : v);
    x2[ch] = x1[ch]; x1[ch] = x; y2[ch] = y1[ch]; y1[ch] = y;
    return y;
  }
};

// The saturator's transfer curve: a Chebyshev polynomial up to full scale and a smooth
// continuation past it. For a sine whose peak just reaches full scale, T_n(cos t) =
// cos(nt), so the polynomial's coefficients are exactly the harmonic levels. Three
// sliders shape those levels: tilt (loudest order), balance (odd vs even) and hardness
// (the knee past full scale). Quieter signals give fewer, higher harmonics (harmonic n
// falls roughly as level^n); clipped ones add the knee's distortion on top. Building it
// takes about 14 ms on the C5 (no FPU), several times that while audio is playing, so the web handler builds a spare copy
// before taking the audio lock and the chain just adopts it.
// Even-harmonic share of the Odd-Even slider (0..100). Positions 45-55 are an exact 50/50
// "both"; outside that the share jumps to 47/53 (a 1 dB gap) and then widens to the ends.
inline double evenShare(int balance) {
  const int d = std::abs(balance - 50);
  if (d <= 5) return 0.5;
  const double eff = 3 + (d - 5) * 47.0 / 45;
  return (50 + (balance > 50 ? eff : -eff)) / SAT_SHAPE_MAX;
}
struct SatTable {
  static constexpr int BITS = 11, SIZE = (1 << BITS) + 2;
  static constexpr double SPAN = 2; // spans -SPAN..SPAN times full scale
  static constexpr double LOUDEST_DB = -10, TILT_DB_PER_ORDER = 12;
  int32_t data[SIZE] = {};
  uint8_t tilt = 0, balance = 0, hardness = 0;
  bool built = false;

  bool matches(const ClipSettings &s) const { return built && tilt == s.tilt && balance == s.balance && hardness == s.hardness; }
  void build(const ClipSettings &s) {
    tilt = s.tilt; balance = s.balance; hardness = s.hardness;
    // Levels in dB per harmonic: a straight slope across the orders (tilt),
    // plus a crossfade between the parities (balance: amplitude weights b and 1-b, fully off at the ends).
    // The loudest harmonic is pinned at LOUDEST_DB relative to the fundamental.
    constexpr int N = SAT_TOP_HARMONIC - 1;
    const double slope = (s.tilt - 50) / 50.0 * TILT_DB_PER_ORDER, evenWeight = evenShare(s.balance);
    double lvl[N], loudest = -1e9;
    float a[N];
    for (int k = 0; k < N; ++k) {
      const int n = k + 2;
      const bool even = n % 2 == 0;
      lvl[k] = (even ? s.balance <= 0 : s.balance >= 100) ? -1e9 : slope * (n - 5.5) + 20 * std::log10(even ? evenWeight : 1 - evenWeight);
      loudest = std::max(loudest, lvl[k]);
    }
    // Sign follows the square-wave series, so a harmonic flattens the peaks
    // like saturation does. Subtracting T_n(0) keeps silence at zero.
    for (int k = 0; k < N; ++k) {
      const int n = k + 2;
      a[k] = lvl[k] < -1e8 ? 0.0f : float(((n / 2) % 2 ? -1.0 : 1.0) * std::pow(10.0, (lvl[k] - loudest + LOUDEST_DB) / 20.0));
    }
    // Single precision: the C5 has no FPU and doubles cost several times as much.
    const int steps = 1 << BITS;
    // y(u) and y(-u) from one recurrence: T_n(-u) = (-1)^n T_n(u), so the even terms
    // are shared and the odd ones change sign.
    auto poly = [&](float u, float &yPos, float &yNeg) {
      float t0 = 1, t1 = u, even = 0, odd = 0;
      float z0 = 1, z1 = 0; // T_n(0)
      for (int n = 2; n <= SAT_TOP_HARMONIC; ++n) {
        const float tn = 2 * u * t1 - t0, zn = -z0, term = a[n - 2] * (tn - zn);
        (n % 2 ? odd : even) += term;
        t0 = t1; t1 = tn; z0 = z1; z1 = zn;
      }
      yPos = u + odd + even; yNeg = -u - odd + even;
    };
    // Slopes at +/-1 (T_n'(1) = n^2, T_n'(-1) = (-1)^(n+1) n^2) for the continuation.
    float slopeHi = 1, slopeLo = 1;
    for (int n = 2; n <= SAT_TOP_HARMONIC; ++n) { slopeHi += a[n - 2] * n * n; slopeLo += a[n - 2] * ((n % 2) ? 1 : -1) * n * n; }
    float topHi, topLo;
    poly(1, topHi, topLo);
    // Past +/-1 the curve keeps its value and slope, then levels off within
    // `reach` of the edge: wide = a rounded knee, narrow = close to a hard clip.
    const float reach = float(2.0 * std::pow(0.01, s.hardness / double(SAT_SHAPE_MAX)));
    // Pass 1 stores the curve in units of FULL_SCALE/16 (no room for a float copy) and tracks the
    // peak; pass 2 rescales so the peak is full scale (never boosting a curve that stays below it).
    constexpr float UNIT = float(FULL_SCALE / 16);
    float peak = 1;
    auto put = [&](int i, float v) { peak = std::max(peak, std::abs(v)); data[i] = int32_t(std::lround(v * UNIT)); };
    for (int i = steps / 2; i <= steps; ++i) { // t >= 0; each pass also fills the mirror entry at -t
      const float t = float(-SPAN + 2.0 * SPAN * i / steps);
      const int j = steps - i;
      if (t > 1) {
        const float zh = slopeHi * (t - 1) / reach, zl = slopeLo * (1 - t) / reach;
        put(i, topHi + reach * zh / (1 + std::abs(zh)));
        put(j, topLo + reach * zl / (1 + std::abs(zl)));
      } else {
        float yp, yn;
        poly(t, yp, yn);
        put(i, yp); put(j, yn);
      }
    }
    const float scale = 16 / peak;
    for (int i = 0; i <= steps; ++i) data[i] = int32_t(std::lround(data[i] * scale));
    data[steps + 1] = data[steps]; // so interpolation at exactly +1 stays in range
    built = true;
  }
};

class Chain {
 public:
  static constexpr size_t BLOCK = 32;
  static constexpr float THRESHOLD_DB = -10, RATIO = 3, KNEE_DB = 10;
  float grDb = 0; // current gain reduction, for the meter

  void configure(const ClipSettings &s, int channels, double fs = 44100) {
    ch = channels; rate = fs;
    nActive = 0;
    for (int b = 0; b < EQ_BANDS; ++b) {
      if (!s.eq[b]) { gains[b] = 0; continue; }
      if (gains[b] != s.eq[b]) {
        const bool wasOff = !gains[b];
        eq[b].peaking(EQ_HZ[b], fs, s.eq[b]);
        if (wasOff) eq[b].reset(); // Keep state when retuning to avoid clicks.
        gains[b] = s.eq[b];
      }
      active[nActive++] = b;
    }
    inGainDb = s.comp;
    configureSaturator(s);
    configureOversampler(s.oversample);
    compPre = s.compPre;
    makeupDb = s.makeupDb;
    // LA-2A attack is ~10 ms; Fast Attack uses ~1 ms (about one 32-frame block).
    const double attackTau = s.compFast ? 0.001 : 0.010;
    attack = 1 - std::exp(-double(BLOCK) / fs / attackTau);
    // LA-2A: the first 50% of release takes about 60 ms. The fast half of the
    // reduction uses a 20 ms time constant, so it is ~95% gone by 60 ms.
    fastRelease = 1 - std::exp(-double(BLOCK) / fs / 0.020);
    detector = 1 - std::exp(-double(BLOCK) / fs / attackTau); // level must follow as fast
  }
  void setMakeup(float db) { makeupDb = db; }
  float makeup() const { return makeupDb; }
  void reset() {
    for (Biquad &b : eq) b.reset();
    for (int c = 0; c < 2; ++c) dcX[c] = dcY[c] = 0;
    for (int c = 0; c < 2; ++c) { for (int32_t &v : upHist[c]) v = 0; for (int32_t &v : downHist[c]) v = 0; }
    upPos = downPos = 0;
    satGainQ16 = -1;
    power = fast = slow = history = 0;
    grDb = 0;
    gainQ16 = -1;
  }
  // In place. Mono processes only L. Output may exceed full scale; the
  // caller saturates after volume.
  void process(int32_t *L, int32_t *R, size_t n) {
    for (size_t start = 0; start < n; start += BLOCK) {
      const size_t len = n - start < BLOCK ? n - start : BLOCK;
      int32_t *l = L + start, *r = R + start;
      // Pre: the compressor hears the raw clip. Post (default): it hears the EQ.
      if (compPre) { saturate(l, r, len); applyGain(l, r, len, blockGainDb(l, r, len)); }
      for (size_t i = 0; i < len; ++i)
        for (int k = 0; k < nActive; ++k) {
          Biquad &f = eq[active[k]];
          l[i] = f.run(0, l[i]);
          if (ch == 2) r[i] = f.run(1, r[i]);
        }
      if (!compPre) { saturate(l, r, len); applyGain(l, r, len, blockGainDb(l, r, len)); }
    }
  }

 private:
  Biquad eq[EQ_BANDS];
  int8_t gains[EQ_BANDS] = {};
  int active[EQ_BANDS] = {}, nActive = 0, ch = 2;
  double rate = 44100;
  float inGainDb = 0, makeupDb = 0;
  bool compPre = false;
  double attack = 0, fastRelease = 0, detector = 0;
  double power = 0, fast = 0, slow = 0, history = 0;
  int32_t gainQ16 = -1; // previous block's gain; -1 = none yet

  // ---- Saturator. Drive gain, then the lookup table (SatTable above), then a 10 Hz DC blocker.
  SatTable table;
  bool satOn = false;
  int32_t satTargetQ16 = 65536, satGainQ16 = -1;
  int32_t dcX[2] = {}, dcY[2] = {};

  void configureSaturator(const ClipSettings &s) {
    satOn = s.drive != 0;
    satTargetQ16 = int32_t(std::lround(std::pow(10.0, s.drive / 20.0) * 65536));
    if (!table.matches(s)) table.build(s);
  }
 public:
  void adoptTable(const SatTable &spare) { table = spare; }
  bool hasTable(const ClipSettings &s) const { return table.matches(s); }
 private:
  inline int32_t shape(int64_t gain, int32_t x) const {
    int64_t v = (int64_t(x) * gain) >> 16;
    constexpr int64_t lim = int64_t(1) << 24; // +/-2 x full scale: the table's domain
    v = v > lim ? lim : v < -lim ? -lim : v;
    const uint32_t p = uint32_t(v + lim); // 0 .. 2^25
    const uint32_t idx = p >> 14, frac = p & 0x3fff;
    const int32_t a = table.data[idx], b = table.data[idx + 1];
    return a + int32_t((int64_t(b - a) * frac) >> 14);
  }
  // DC blocker, ~10 Hz: asymmetric (even-harmonic) shaping adds an offset.
  inline int32_t blockDc(int ch, int32_t x) {
    constexpr int64_t R = 65444; // 1 - 2 pi 10 / 44100, Q16
    const int32_t y = int32_t(int64_t(x) - dcX[ch] + ((R * dcY[ch]) >> 16));
    dcX[ch] = x; dcY[ch] = y;
    return y;
  }
  // ---- Oversampling. Hard clipping and the polynomials make harmonics far past
  // 22 kHz that fold back into the audio band. Running the shaper at 2x or 4x
  // moves the fold point up, and a low-pass before decimating removes what is
  // left above 22 kHz. One Kaiser-windowed-sinc prototype (cut-off at the
  // original Nyquist, 16 taps per phase) serves both directions.
  static constexpr int OS_TAPS_PER_PHASE = 16, OS_HIST = 64;
  int osL = 1;
  int32_t osTaps[OS_TAPS_PER_PHASE * 4] = {}; // Q30, sums to 1
  // History is written twice (at i and i + size) so the filters read it contiguously, with no wrap-around masking.
  int32_t upHist[2][2 * OS_TAPS_PER_PHASE] = {}, downHist[2][2 * OS_HIST] = {};
  int upPos = 0, downPos = 0;

  void configureOversampler(uint8_t mode) {
    const int L = mode == 0 ? 1 : mode == 1 ? 2 : 4;
    if (L == osL) return;
    osL = L;
    if (L == 1) return;
    const int N = OS_TAPS_PER_PHASE * L;
    const double beta = 6.5, centre = (N - 1) / 2.0;
    auto bessel0 = [](double x) { double sum = 1, term = 1; for (int k = 1; k < 30; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; } return sum; };
    double h[OS_TAPS_PER_PHASE * 4], total = 0;
    for (int j = 0; j < N; ++j) {
      const double x = j - centre, r = x / centre;
      const double sinc = x == 0 ? 1.0 / L : std::sin(DSP_PI * x / L) / (DSP_PI * x);
      h[j] = sinc * bessel0(beta * std::sqrt(1 - r * r)) / bessel0(beta);
      total += h[j];
    }
    for (int j = 0; j < N; ++j) osTaps[j] = int32_t(std::lround(h[j] / total * (1 << 30)));
    upPos = downPos = 0;
    for (int c = 0; c < 2; ++c) { for (int32_t &v : upHist[c]) v = 0; for (int32_t &v : downHist[c]) v = 0; }
  }
  // One channel, one input sample: L interpolated samples through the shaper, then back down.
  inline int32_t shapeOversampled(int ch, int64_t gain, int32_t x) {
    int32_t *up = upHist[ch], *down = downHist[ch];
    up[upPos] = up[upPos + OS_TAPS_PER_PHASE] = x;
    const int32_t *newestUp = up + upPos + OS_TAPS_PER_PHASE; // x[n - k] = newestUp[-k]
    for (int p = 0; p < osL; ++p) {
      int64_t acc = 0;
      const int32_t *tap = osTaps + p;
      for (int k = 0; k < OS_TAPS_PER_PHASE; ++k, tap += osL) acc += int64_t(*tap) * newestUp[-k];
      const int64_t v = (acc * osL) >> 30;
      const int w = (downPos + p + 1) & (OS_HIST - 1);
      down[w] = down[w + OS_HIST] = shape(gain, int32_t(v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v));
    }
    // The prototype is symmetric, so pair the samples that share a tap.
    const int N = OS_TAPS_PER_PHASE * osL;
    const int32_t *newest = down + ((downPos + osL) & (OS_HIST - 1)) + OS_HIST; // z[newest - j] = newest[-j]
    int64_t acc = 0;
    for (int j = 0; j < N / 2; ++j) acc += int64_t(osTaps[j]) * (int64_t(newest[-j]) + newest[-(N - 1 - j)]);
    return int32_t(acc >> 30);
  }
  void saturate(int32_t *l, int32_t *r, size_t len) {
    if (!satOn) { satGainQ16 = satTargetQ16; return; }
    const int32_t from = satGainQ16 < 0 ? satTargetQ16 : satGainQ16;
    satGainQ16 = satTargetQ16;
    for (size_t i = 0; i < len; ++i) {
      const int64_t g = from + (int64_t(satTargetQ16) - from) * int64_t(i + 1) / int64_t(len);
      if (osL == 1) {
        l[i] = blockDc(0, shape(g, l[i]));
        if (ch == 2) r[i] = blockDc(1, shape(g, r[i]));
      } else {
        // Both channels share the history positions; advance them once per frame.
        l[i] = blockDc(0, shapeOversampled(0, g, l[i]));
        if (ch == 2) r[i] = blockDc(1, shapeOversampled(1, g, r[i]));
        upPos = (upPos + 1) & (OS_TAPS_PER_PHASE - 1);
        downPos = (downPos + osL) & (OS_HIST - 1);
      }
    }
  }

  // Soft-knee 3:1 curve above a fixed threshold. The input gain ("Compression")
  // pushes the signal further over it, as driving an LA-2A harder does.
  static double staticGr(double levelDb) {
    const double over = levelDb - THRESHOLD_DB, slope = 1 - 1 / RATIO;
    if (over <= -KNEE_DB / 2) return 0;
    if (over >= KNEE_DB / 2) return over * slope;
    const double x = over + KNEE_DB / 2;
    return x * x / (2 * KNEE_DB) * slope;
  }
  float blockGainDb(const int32_t *l, const int32_t *r, size_t len) {
    if (!inGainDb) { grDb = 0; return makeupDb; }
    uint64_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
      sum += uint64_t(int64_t(l[i]) * l[i]);
      if (ch == 2) sum += uint64_t(int64_t(r[i]) * r[i]);
    }
    const double ms = double(sum) / (double(len) * ch) / (FULL_SCALE * FULL_SCALE);
    const double scale = double(len) / BLOCK;
    power += (ms - power) * detector * scale;
    const double target = staticGr(10 * std::log10(power + 1e-12) + inGainDb);
    // Two-stage, program-dependent release: half the reduction recovers in
    // about 60 ms; the other half takes 0.5-15 s, slower after long or
    // heavy compression (the T4 cell's memory), modelled by `history`.
    const double dt = double(len) / rate;
    const double goal = target / 10 > 1 ? 1 : target / 10;
    history += (goal - history) * dt / (goal > history ? 5.0 : 20.0);
    const double slowTau = (0.5 + 14.5 * history) / 3; // ~3 tau = full release
    const double slowRelease = 1 - std::exp(-dt / slowTau);
    fast += (target - fast) * (target > fast ? attack : fastRelease) * scale;
    slow += (target - slow) * (target > slow ? attack * scale : slowRelease);
    grDb = float(0.5 * (fast + slow));
    return inGainDb - grDb + makeupDb;
  }
  void applyGain(int32_t *l, int32_t *r, size_t len, float db) {
    double lin = std::pow(10.0, db / 20.0);
    if (lin > 30000) lin = 30000;
    const int32_t target = int32_t(std::lround(lin * 65536));
    const int32_t from = gainQ16 < 0 ? target : gainQ16;
    gainQ16 = target;
    if (from == 65536 && target == 65536) return; // Unity: bit-exact bypass.
    // Ramp across the block so gain changes do not "zipper".
    for (size_t i = 0; i < len; ++i) {
      const int64_t g = from + (int64_t(target) - from) * int64_t(i + 1) / int64_t(len);
      l[i] = clamp((int64_t(l[i]) * g) >> 16);
      if (ch == 2) r[i] = clamp((int64_t(r[i]) * g) >> 16);
    }
  }
  static int32_t clamp(int64_t v) {
    constexpr int64_t lim = int64_t(1) << 28; // Headroom for later volume scaling.
    return int32_t(v > lim ? lim : v < -lim ? -lim : v);
  }
};

// K-weighting stage 1 (head shelf) and stage 2 (RLB high-pass) from BS.1770,
// using libebur128's parameters and formulas so any sample rate works.
inline void kWeighting(Biquad &shelf, Biquad &highpass, double fs) {
  {
    const double hz = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    const double K = std::tan(DSP_PI * hz / fs), Vh = std::pow(10.0, G / 20);
    const double Vb = std::pow(Vh, 0.4996667741545416);
    shelf.set(Vh + Vb * K / Q + K * K, 2 * (K * K - Vh), Vh - Vb * K / Q + K * K,
              1 + K / Q + K * K, 2 * (K * K - 1), 1 - K / Q + K * K);
  }
  {
    const double hz = 38.13547087602444, Q = 0.5003270373238773, K = std::tan(DSP_PI * hz / fs);
    const double a0 = 1 + K / Q + K * K;
    highpass.set(a0, -2 * a0, a0, a0, 2 * (K * K - 1), 1 - K / Q + K * K);
  }
  shelf.reset(); highpass.reset();
}

// Integrated loudness per ITU-R BS.1770-4: K-weighting, 400 ms blocks with
// 75% overlap, -70 LUFS absolute gate, -10 LU relative gate.
class LoudnessMeter {
 public:
  void begin(int channels, double fs = 44100) {
    ch = channels;
    subLen = uint32_t(fs / 10); // 100 ms
    kWeighting(shelf, highpass, fs);
    shelf.reset(); highpass.reset();
    subs.clear(); acc = 0; count = 0; total = 0; totalCount = 0;
  }
  void add(int32_t l, int32_t r) {
    const int64_t a = highpass.run(0, shelf.run(0, l));
    uint64_t e = uint64_t(a * a);
    if (ch == 2) { const int64_t b = highpass.run(1, shelf.run(1, r)); e += uint64_t(b * b); }
    acc += e;
    if (++count == subLen) {
      subs.push_back(double(acc));
      total += double(acc); totalCount += count;
      acc = 0; count = 0;
    }
  }
  // Returns NAN for silence (nothing passes the absolute gate).
  double lufs() const {
    const double norm = FULL_SCALE * FULL_SCALE;
    std::vector<double> blocks;
    for (size_t i = 0; i + 4 <= subs.size(); ++i)
      blocks.push_back((subs[i] + subs[i+1] + subs[i+2] + subs[i+3]) / (4.0 * subLen) / norm);
    if (blocks.empty()) { // Shorter than one 400 ms block: use everything.
      const double n = double(totalCount + count);
      if (!n) return NAN;
      blocks.push_back((total + double(acc)) / n / norm);
    }
    auto loud = [](double e) { return -0.691 + 10 * std::log10(e); };
    double sum = 0; size_t n = 0;
    for (double e : blocks) if (e > 0 && loud(e) > -70) { sum += e; ++n; }
    if (!n) return NAN;
    const double gate = loud(sum / n) - 10;
    sum = 0; n = 0;
    for (double e : blocks) if (e > 0 && loud(e) > -70 && loud(e) > gate) { sum += e; ++n; }
    return n ? loud(sum / n) : NAN;
  }
  Biquad shelf, highpass; // public for coefficient tests
 private:
  int ch = 2;
  uint32_t subLen = 4410, count = 0, totalCount = 0;
  uint64_t acc = 0;
  double total = 0;
  std::vector<double> subs;
};

// Live makeup estimate while a clip plays: compares K-weighted power going
// into the chain with what comes out (makeup excluded), smoothed over ~150 ms.
// Used after a settings change until the full BS.1770 measurement finishes,
// so turning something up does not play louder in the meantime.
class LoudnessFollower {
 public:
  void begin(double fs, double tauSeconds = 0.15) {
    rate = fs; tau = tauSeconds;
    kWeighting(shelfIn, hpIn, fs); kWeighting(shelfOut, hpOut, fs);
    powerIn = powerOut = 0; sumIn = sumOut = 0; count = 0;
  }
  // Mono samples in the DSP scale: before the chain, and after it.
  void add(int32_t in, int32_t out) {
    // Clamp so 256 squared samples cannot overflow 64 bits.
    auto limit = [](int64_t v) { constexpr int64_t m = int64_t(1) << 27; return v > m ? m : v < -m ? -m : v; };
    const int64_t a = limit(hpIn.run(0, shelfIn.run(0, in))), b = limit(hpOut.run(0, shelfOut.run(0, out)));
    sumIn += uint64_t(a * a); sumOut += uint64_t(b * b); ++count;
  }
  // Once per processed chunk; makeupDb is the makeup the chain applied to it.
  void update(float makeupDb) {
    if (!count) return;
    const double k = 1 - std::exp(-double(count) / rate / tau);
    powerIn += (double(sumIn) / count - powerIn) * k;
    powerOut += (double(sumOut) / count / std::pow(10.0, makeupDb / 10.0) - powerOut) * k;
    sumIn = sumOut = 0; count = 0;
  }
  // Ignore near-silence, where the ratio means nothing.
  bool ready() const { return powerIn > FULL_SCALE * FULL_SCALE * 1e-6 && powerOut > 0; }
  float makeupDb() const { return float(10 * std::log10(powerIn / powerOut)); }
 private:
  Biquad shelfIn, hpIn, shelfOut, hpOut;
  double rate = 44100, tau = 0.15, powerIn = 0, powerOut = 0;
  uint64_t sumIn = 0, sumOut = 0;
  uint32_t count = 0;
};
