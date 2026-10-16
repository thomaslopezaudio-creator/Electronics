#include "dsp.h"
#include <cassert>
#include <cstdio>

constexpr double FS = 44100;
bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// Sine at `dbfs` peak, stereo or mono, `seconds` long, in the firmware's scale.
struct Signal {
  std::vector<int32_t> l, r;
  Signal(double hz, double dbfs, double seconds) {
    const double amp = std::pow(10.0, dbfs / 20) * 32767 * 256;
    for (size_t i = 0; i < size_t(seconds * FS); ++i) {
      l.push_back(int32_t(std::lround(amp * std::sin(2 * DSP_PI * hz * i / FS))));
      r.push_back(l.back());
    }
  }
};
double rmsDb(const std::vector<int32_t> &v, size_t from) {
  double s = 0;
  for (size_t i = from; i < v.size(); ++i) s += double(v[i]) * v[i];
  return 10 * std::log10(s / (v.size() - from));
}
void run(Chain &c, Signal &s) {
  for (size_t i = 0; i < s.l.size(); i += 256) {
    const size_t n = std::min<size_t>(256, s.l.size() - i);
    c.process(s.l.data() + i, s.r.data() + i, n);
  }
}
double measure(const Signal &s, int ch = 2) {
  LoudnessMeter m; m.begin(ch, FS);
  for (size_t i = 0; i < s.l.size(); ++i) m.add(s.l[i], s.r[i]);
  return m.lufs();
}
double eqGainAt(int band, int gain, double hz) {
  ClipSettings cs; cs.eq[band] = int8_t(gain);
  Chain c; c.configure(cs, 2, FS); c.reset();
  Signal in(hz, -12, 1.0), out = in;
  run(c, out);
  return rmsDb(out.l, 22050) - rmsDb(in.l, 22050);
}

int main() {
  // 1. Flat settings must be a bit-exact bypass.
  {
    ClipSettings cs; Chain c; c.configure(cs, 2, FS); c.reset();
    Signal in(440, -6, 0.5), out = in;
    run(c, out);
    assert(out.l == in.l && out.r == in.r);
  }
  // 2. EQ: centre gain matches the slider; Q widens at low gain.
  {
    const double g12 = eqGainAt(5, 12, 1000), gm6 = eqGainAt(5, -6, 1000), g31 = eqGainAt(0, 12, 31.5);
    std::printf("EQ centre gain: +12 -> %.2f dB, -6 -> %.2f dB, 31.5 Hz +12 -> %.2f dB\n", g12, gm6, g31);
    assert(near(g12, 12, 0.3) && near(gm6, -6, 0.3) && near(g31, 12, 0.5));
    // One octave away, a wide low-gain filter keeps more of its gain (in
    // proportion) than a narrow high-gain one: proportional Q.
    const double low = eqGainAt(5, 3, 2000) / 3, high = eqGainAt(5, 12, 2000) / 12;
    std::printf("Gain one octave up, as fraction of centre: +3 dB -> %.2f, +12 dB -> %.2f\n", low, high);
    assert(low > high);
  }
  // 3. K-weighting coefficients at 48 kHz match the BS.1770-4 table.
  {
    LoudnessMeter m; m.begin(2, 48000);
    const double q = 1 << 28;
    assert(near(m.shelf.b0 / q, 1.53512485958697, 1e-5) && near(m.shelf.b1 / q, -2.69169618940638, 1e-5) &&
           near(m.shelf.b2 / q, 1.19839281085285, 1e-5) && near(m.shelf.a1 / q, -1.69065929318241, 1e-5) &&
           near(m.shelf.a2 / q, 0.73248077421585, 1e-5));
    assert(near(m.highpass.a1 / q, -1.99004745483398, 1e-5) && near(m.highpass.a2 / q, 0.99007225036621, 1e-5));
  }
  // 4. A -20 dBFS 1 kHz sine in both channels reads -20 LUFS; in one channel, -23.01.
  {
    Signal s(1000, -20, 3);
    const double both = measure(s);
    for (auto &x : s.r) x = 0;
    const double left = measure(s);
    std::printf("LUFS of -20 dBFS 1 kHz sine: stereo %.2f, left only %.2f\n", both, left);
    assert(near(both, -20, 0.1) && near(left, -23.01, 0.1));
    Signal quiet(1000, -100, 1);
    assert(std::isnan(measure(quiet)));
  }
  // 5. Compressor: steady-state reduction follows the 3:1 curve, then releases in two stages.
  {
    ClipSettings cs; cs.comp = 20;
    Chain c; c.configure(cs, 2, FS); c.reset();
    // -20 dBFS peak sine = -23 dBFS RMS; +20 dB input gain -> 7 dB over -10 threshold.
    Signal loud(1000, -20, 3);
    run(c, loud);
    const double gr0 = c.grDb, expected = 7 * (1 - 1 / 3.0);
    std::printf("Steady gain reduction: %.2f dB (curve: %.2f)\n", gr0, expected);
    assert(near(gr0, expected, 0.15));
    Signal silence(1000, -200, 0.005);
    double t = 0, at60 = 0, at500 = 0;
    for (int i = 0; i < 200; ++i) { // 200 x 5 ms = 1 s of silence
      Signal chunk = silence; run(c, chunk); t += 5;
      if (t == 60) at60 = c.grDb;
      if (t == 500) at500 = c.grDb;
    }
    std::printf("Release: %.0f%% left at 60 ms, %.0f%% at 500 ms, %.0f%% at 1 s\n",
                100 * at60 / gr0, 100 * at500 / gr0, 100 * c.grDb / gr0);
    assert(at60 / gr0 < 0.65 && at60 / gr0 > 0.4); // ~half recovered by ~60 ms
    assert(at500 / gr0 > 0.1);                     // the rest is slow
  }
  // 6. Loudness match: makeup = in - out brings the processed clip back to the input LUFS.
  {
    ClipSettings cs; cs.comp = 24; cs.eq[1] = 6; cs.eq[7] = -4; cs.eq[9] = 9;
    Signal music(220, -8, 4);
    for (size_t i = 0; i < music.l.size(); ++i) // Add a second tone and an envelope.
      music.l[i] = music.r[i] = int32_t(music.l[i] * (0.4 + 0.6 * std::fabs(std::sin(i / FS * 3))) * 0.6 +
                                        0.3 * 8388607 * std::sin(2 * DSP_PI * 3500 * i / FS));
    const double in = measure(music);
    Chain c; c.configure(cs, 2, FS); c.reset();
    Signal out = music; run(c, out);
    cs.makeupDb = float(in - measure(out));
    Chain d; d.configure(cs, 2, FS); d.reset();
    Signal matched = music; run(d, matched);
    std::printf("Loudness match: in %.2f LUFS, makeup %.2f dB, out %.2f LUFS\n", in, cs.makeupDb, measure(matched));
    assert(near(measure(matched), in, 0.2));
  }
  // 7. Mono processes only the left buffer.
  {
    ClipSettings cs; cs.eq[3] = 6; cs.comp = 10;
    Chain c; c.configure(cs, 1, FS); c.reset();
    Signal s(250, -10, 0.2); const auto right = s.r;
    run(c, s);
    assert(s.r == right && s.l != right);
  }
  // 8. Turning settings up mid-playback: without help, loudness jumps until
  // the full measurement returns; the follower plus instant compression
  // compensation keep it near the matched level.
  {
    Signal music(220, -8, 6);
    for (size_t i = 0; i < music.l.size(); ++i)
      music.l[i] = music.r[i] = int32_t(music.l[i] * (0.4 + 0.6 * std::fabs(std::sin(i / FS * 3))) * 0.6 +
                                        0.3 * 8388607 * std::sin(2 * DSP_PI * 3500 * i / FS));
    const double target = measure(music); // matching aims to keep the input's loudness
    ClipSettings a; a.comp = 6;
    { Chain c; c.configure(a, 2, FS); c.reset(); Signal o = music; run(c, o); a.makeupDb = float(target - measure(o)); }
    ClipSettings b = a; b.comp = 30; b.eq[1] = 8; b.eq[6] = 6; // much harder, brighter
    auto worst = [&](bool follow) {
      Chain c; c.configure(a, 2, FS); c.reset();
      LoudnessFollower f; f.begin(FS);
      Signal o = music;
      const size_t change = size_t(FS * 2);
      bool following = false, switched = false;
      for (size_t i = 0; i < o.l.size(); i += 256) {
        if (!switched && i >= change) {
          switched = true;
          if (follow) { b.makeupDb = c.makeup() - (b.comp - a.comp); following = true; }
          else b.makeupDb = a.makeupDb; // the old behaviour keeps stale makeup
          c.configure(b, 2, FS);
        }
        const size_t n = std::min<size_t>(256, o.l.size() - i);
        std::vector<int32_t> in(o.l.begin() + i, o.l.begin() + i + n);
        c.process(o.l.data() + i, o.r.data() + i, n);
        for (size_t k = 0; k < n; ++k) f.add(in[k], o.l[i + k]);
        f.update(c.makeup());
        if (following && f.ready()) c.setMakeup(f.makeupDb());
      }
      // Worst 400 ms loudness error in the 1.5 s after the change.
      double worstDb = 0;
      for (size_t s0 = (size_t(FS * 2) / 256 + 1) * 256; s0 + size_t(FS * 0.4) <= size_t(FS * 3.5); s0 += size_t(FS * 0.05)) {
        LoudnessMeter m; m.begin(2, FS);
        for (size_t k = s0; k < s0 + size_t(FS * 0.4); ++k) m.add(o.l[k], o.r[k]);
        const double ref = [&] { LoudnessMeter r; r.begin(2, FS); for (size_t k = s0; k < s0 + size_t(FS * 0.4); ++k) r.add(music.l[k], music.r[k]); return r.lufs(); }();
        worstDb = std::max(worstDb, std::fabs(m.lufs() - ref));
      }
      return worstDb;
    };
    const double before = worst(false), after = worst(true);
    std::printf("Loudness jump after turning up (worst 400 ms, first 1.5 s): stale makeup %.1f dB, follower %.1f dB\n", before, after);
    assert(after < before / 3 && after < 2.0);
  }
  // 9. Pre/post EQ: order changes the result; matching still lands on the input loudness.
  {
    Signal music(110, -10, 4);
    for (size_t i = 0; i < music.l.size(); ++i)
      music.l[i] = music.r[i] = int32_t(music.l[i] + 0.25 * 8388607 * std::sin(2 * DSP_PI * 2500 * i / FS));
    const double in = measure(music);
    ClipSettings post; post.comp = 24; post.eq[2] = 12; post.eq[3] = 10; // big low-mid boost
    ClipSettings pre = post; pre.compPre = true;
    double grPre = 0, grPost = 0, matched[2];
    int idx = 0;
    for (ClipSettings *cs : {&post, &pre}) {
      Chain c; c.configure(*cs, 2, FS); c.reset(); Signal o = music; run(c, o);
      (cs->compPre ? grPre : grPost) = c.grDb;
      cs->makeupDb = float(in - measure(o));
      Chain d; d.configure(*cs, 2, FS); d.reset(); Signal m = music; run(d, m);
      matched[idx++] = measure(m);
    }
    std::printf("Pre/post EQ: gain reduction post %.1f dB, pre %.1f dB; matched %.2f / %.2f LUFS (input %.2f)\n",
                grPost, grPre, matched[0], matched[1], in);
    assert(grPost > grPre + 1); // the boosted lows push a post-EQ compressor harder
    assert(near(matched[0], in, 0.2) && near(matched[1], in, 0.2));
  }
  // 10. Fast Attack: time to reach 90% of the final gain reduction on a sudden loud tone.
  {
    auto attackMs = [&](bool fast) {
      ClipSettings cs; cs.comp = 20; cs.compFast = fast;
      Chain c; c.configure(cs, 2, FS); c.reset();
      Signal quiet(1000, -60, 0.5); run(c, quiet);
      Signal loud(1000, -20, 0.5), probe = loud;
      double target = 0; { Chain d; d.configure(cs, 2, FS); d.reset(); Signal t = loud; run(d, t); target = d.grDb; }
      for (size_t i = 0; i < probe.l.size(); i += 32) {
        c.process(probe.l.data() + i, probe.r.data() + i, 32);
        if (c.grDb >= 0.9 * target) return (i + 32) * 1000.0 / FS;
      }
      return 1e9;
    };
    const double slow = attackMs(false), fast = attackMs(true);
    std::printf("Attack to 90%% of reduction: normal %.1f ms, fast %.1f ms\n", slow, fast);
    assert(fast < slow / 4 && fast < 6);
  }
  // 11. Matching a hot, hard-driven clip. The firmware measures with the input gain cancelled
  // (makeup = -comp): playback applies input gain, reduction and makeup as one gain, so
  // clipping the processed signal at full scale first (the old measurement) under-reads it.
  {
    Signal music(220, -3, 4);
    for (size_t i = 0; i < music.l.size(); ++i)
      music.l[i] = music.r[i] = int32_t(music.l[i] * (0.15 + 0.85 * std::fabs(std::sin(i / FS * 3))));
    const double in = measure(music);
    ClipSettings cs; cs.comp = 40;
    auto processed = [&](float makeup) {
      cs.makeupDb = makeup; Chain c; c.configure(cs, 2, FS); c.reset();
      Signal o = music; run(c, o); return o;
    };
    Signal cancelled = processed(-float(cs.comp));
    const float good = float(in - measure(cancelled) - cs.comp);
    Signal live = processed(good);
    Signal clipped = processed(0);
    for (auto &x : clipped.l) x = std::max(-SAMPLE_MAX, std::min(SAMPLE_MAX, x));
    for (auto &x : clipped.r) x = std::max(-SAMPLE_MAX, std::min(SAMPLE_MAX, x));
    const float old = float(in - measure(clipped));
    std::printf("Hot clip at 40 dB: input %.2f LUFS, playback %.2f LUFS; old measure makeup %.1f vs %.1f dB\n",
                in, measure(live), old, good);
    assert(near(measure(live), in, 0.3));
    assert(old > good + 3); // the clipped measurement left heavy settings several dB too loud
  }
  // 12. Saturator: tilt and balance set the harmonic levels relative to the fundamental for a sine
  // that just reaches full scale; drive 0 is a bypass; silence stays silent; hardness shapes the knee.
  {
    // A 1002.27 Hz sine (44 samples per period, 200 periods): every harmonic lands on a DFT bin.
    const int period = 44, periods = 200, N = period * periods;
    auto spectrum = [&](const std::vector<int32_t> &v, int bin) {
      double re = 0, im = 0;
      for (int i = N / 2; i < N; ++i) { // second half: past the DC blocker's settling
        const double w = 2 * DSP_PI * bin * i / N;
        re += v[i] * std::cos(w); im += v[i] * std::sin(w);
      }
      return std::sqrt(re * re + im * im);
    };
    auto sine = [&](double dbfs) {
      Signal sg(44100.0 / period, dbfs, double(N) / FS + 0.01);
      sg.l.resize(N); sg.r.resize(N);
      return sg;
    };
    // The design in the header, restated: dB per harmonic = slope * (n - 5.5) +/- parity, loudest pinned at -10.
    auto expected = [&](int tilt, int balance, int n) {
      auto raw = [&](int m) {
        const bool even = m % 2 == 0;
        if (even ? balance <= 0 : balance >= 100) return -1e9;
        return (tilt - 50) / 50.0 * 12 * (m - 5.5) + 20 * std::log10(even ? evenShare(balance) : 1 - evenShare(balance));
      };
      double loudest = -1e9; for (int m = 2; m <= 9; ++m) loudest = std::max(loudest, raw(m));
      return raw(n) - loudest - 10;
    };
    // Drive 1 dB on a -1 dBFS sine puts the peak exactly at full scale, where T_n(cos t) = cos(nt).
    struct Case { int tilt, balance; } cases[] = {{25, 50}, {0, 50}, {100, 50}, {50, 0}, {50, 100}, {75, 30}, {50, 10}, {50, 90}, {50, 47}, {50, 56}};
    double f0 = 0;
    for (const Case &k : cases) {
      ClipSettings cs; cs.drive = 1; cs.tilt = uint8_t(k.tilt); cs.balance = uint8_t(k.balance);
      Chain c; c.configure(cs, 2, FS); c.reset();
      Signal o = sine(-1); run(c, o);
      const double fund = spectrum(o.l, periods);
      if (&k == &cases[0]) f0 = fund;
      std::printf("tilt %3d balance %3d:", k.tilt, k.balance);
      for (int n = 2; n <= 9; ++n) {
        const double want = expected(k.tilt, k.balance, n), got = 20 * std::log10(spectrum(o.l, n * periods) / fund);
        std::printf(" H%d %.1f", n, got);
        // Table interpolation and 24-bit rounding leave a floor near -70 dB, so only the strong harmonics are held to 0.3 dB.
        if (want > -50) assert(near(got, want, 0.3)); else assert(got < std::max(want, -70.0) + 3);
      }
      std::puts("");
    }
    ClipSettings cs; cs.drive = 1; // default tilt and balance
    // 12 dB quieter: the n-th harmonic should fall by roughly n * 12 dB relative to the fundamental.
    Chain q; q.configure(cs, 2, FS); q.reset();
    Signal quiet = sine(-13); run(q, quiet);
    const double qf = spectrum(quiet.l, periods);
    std::printf("At -12 dB below full scale: H2 %.1f, H3 %.1f dB\n", 20 * std::log10(spectrum(quiet.l, 2 * periods) / qf),
                20 * std::log10(spectrum(quiet.l, 3 * periods) / qf));
    // Bypass and silence.
    ClipSettings off; off.tilt = 90; off.balance = 10; Chain b; b.configure(off, 2, FS); b.reset();
    Signal bi = sine(-6), bo = bi; run(b, bo);
    assert(bo.l == bi.l);
    Chain z; z.configure(cs, 2, FS); z.reset();
    Signal zero = sine(-200); for (auto &x : zero.l) x = 0; zero.r = zero.l; run(z, zero);
    for (int32_t x : zero.l) assert(x == 0);
    // Hardness: a little past full scale, a harder knee puts more energy into the upper harmonics
    // (far past it, any curve acts like a hard clip and the setting stops mattering).
    double upper[2];
    for (int h = 0; h < 2; ++h) {
      ClipSettings hs; hs.drive = 6; hs.tilt = 0; hs.hardness = h ? 100 : 0;
      Chain hc; hc.configure(hs, 2, FS); hc.reset();
      Signal hso = sine(-1); run(hc, hso);
      const double fund = spectrum(hso.l, periods);
      double e = 0; for (int n = 5; n <= 19; ++n) { const double m = spectrum(hso.l, n * periods) / fund; e += m * m; }
      upper[h] = 10 * std::log10(e);
    }
    std::printf("Harmonics 5-19 with the peak 5 dB over full scale: soft %.1f dB, hard %.1f dB vs fundamental\n", upper[0], upper[1]);
    assert(upper[1] > upper[0] + 3);
    // However hard it is driven, the table keeps the output within full scale (the DC blocker's sag overshoots ~1.5%).
    ClipSettings dr; dr.drive = 24; Chain d; d.configure(dr, 2, FS); d.reset();
    Signal driven = sine(0); run(d, driven);
    int32_t top = 0; for (int32_t x : driven.l) top = std::max(top, x);
    std::printf("Drive +24 dB on a 0 dBFS sine: output peak %.3f of full scale\n", top / 8388608.0);
    assert(top > 8388608 * 0.3 && top < 8388608 * 1.03);
    // How much of an extreme-drive result is aliasing? 5 kHz is bin 500 of 4410; its harmonics fold
    // back (25 kHz -> 19.1 kHz), landing on bins that are not multiples of 500.
    double aliasDb[3];
    for (int os = 0; os < 3; ++os) {
      const int M = 4410; ClipSettings ex; ex.drive = 48; ex.oversample = uint8_t(os);
      ex.tilt = 100; ex.hardness = 100;
      Chain e; e.configure(ex, 2, FS); e.reset();
      Signal hot(5000, -6, double(M) * 3 / FS + 0.01); hot.l.resize(M * 3); hot.r.resize(M * 3); run(e, hot);
      double wanted = 0, alias = 0;
      for (int bin = 1; bin < M / 2; ++bin) {
        double re = 0, im = 0;
        for (int i = 2 * M; i < 3 * M; ++i) { const double w = 2 * DSP_PI * bin * (i - 2 * M) / M; re += hot.l[i] * std::cos(w); im += hot.l[i] * std::sin(w); }
        (bin % 500 == 0 ? wanted : alias) += re * re + im * im;
      }
      aliasDb[os] = 10 * std::log10(wanted / alias);
    }
    std::printf("Extreme drive (+48 dB, high orders, hard knee) on 5 kHz, aliased energy below the harmonics: 1x %.1f dB, 2x %.1f dB, 4x %.1f dB\n",
                aliasDb[0], aliasDb[1], aliasDb[2]);
    assert(aliasDb[1] > aliasDb[0] + 6 && aliasDb[2] > aliasDb[1] + 3);
    // The oversampler must not change the chosen ratios, and must pass the fundamental unchanged.
    for (int os = 1; os < 3; ++os) {
      ClipSettings o = cs; o.oversample = uint8_t(os);
      Chain oc; oc.configure(o, 2, FS); oc.reset();
      Signal so = sine(-1); run(oc, so);
      const double of0 = spectrum(so.l, periods);
      const double o2 = 20 * std::log10(spectrum(so.l, 2 * periods) / of0), o3 = 20 * std::log10(spectrum(so.l, 3 * periods) / of0);
      std::printf("%dx: fundamental %.2f dB vs 1x, H2 %.2f, H3 %.2f\n", 1 << os, 20 * std::log10(of0 / f0), o2, o3);
      assert(near(o2, expected(25, 50, 2), 0.3) && near(o3, expected(25, 50, 3), 0.3) && near(20 * std::log10(of0 / f0), 0, 0.2));
    }
  }
  std::puts("DSP: bypass, EQ, K-weighting, LUFS, compressor, loudness match, mono, live follower, pre/post EQ, fast attack, hot-clip matching and saturator passed.");
}
