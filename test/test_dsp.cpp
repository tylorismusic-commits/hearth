// Hearth Channel — DSP tests
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <vector>

#include "channel_strip.hpp"
#include "eq.hpp"
#include "filters.hpp"
#include "params.hpp"

using namespace hearth;

static int g_failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    printf("FAIL: %s\n", what);
  }
}

static float sinePeak(const float* buf, uint32_t frames, float freq, float sr) {
  float peak = 0.0f;
  float phase = 0.0f;
  for (uint32_t i = 0; i < frames; ++i) {
    const float x = 1.0f * std::sin(phase);
    if (std::fabs(x) > peak) peak = std::fabs(x);
    phase += 2.0f * 3.14159265358979f * freq / sr;
    if (phase > 6.28318530717959f) phase -= 6.28318530717959f;
  }
  return peak;
}

int main() {
  printf("Hearth DSP tests\n");

  check(std::fabs(dbToGain(0.0f) - 1.0f) < 1.0e-6f, "dbToGain(0) == 1");
  check(std::fabs(gainToDb(1.0f)) < 1.0e-3f, "gainToDb(1) == 0");
  check(std::fabs(gainToDb(0.5f) + 6.0206f) < 0.05f, "gainToDb(0.5) == -6.02");

  // --- low shelf: +12 dB should give a DC gain of 10^0.6 ---
  {
    EqBand band;
    band.prepare(48000.0f, EqShape::LowShelf, 100.0f, 12.0f, 0.70710678f);
    band.setTargets(100.0f, 12.0f, 0.70710678f);
    float y = 0.0f;
    for (int i = 0; i < 48000; ++i) {
      band.tick();
      band.update();
      y = band.process(0, 1.0f);
    }
    check(std::fabs(y - 3.9811f) < 0.05f, "low shelf +12 dB at DC");
  }

  // --- mid bell: +12 dB should give a peak gain of 10^0.6 ---
  {
    EqBand band;
    band.prepare(48000.0f, EqShape::Bell, 1000.0f, 12.0f, 0.7f);
    band.setTargets(1000.0f, 12.0f, 0.7f);
    const float freq = 1000.0f;
    const float sr = 48000.0f;
    float peak = 0.0f;
    for (int i = 0; i < 48000; ++i) {
      band.tick();
      band.update();
      float x = 0.5f * std::sin(2.0f * 3.14159265358979f * freq * (i / sr));
      const float y = band.process(0, x);
      peak = std::max(peak, std::fabs(y));
    }
    check(std::fabs(peak - 1.99f) < 0.05f, "mid bell +12 dB at 1k");
  }

  // --- high shelf: +6 dB above the shelf corner should give a 2x gain ---
  {
    EqBand band;
    band.prepare(48000.0f, EqShape::HighShelf, 6000.0f, 6.0f, 0.70710678f);
    band.setTargets(6000.0f, 6.0f, 0.70710678f);
    const float freq = 12000.0f;
    const float sr = 48000.0f;
    float peak = 0.0f;
    for (int i = 0; i < 48000; ++i) {
      band.tick();
      band.update();
      float x = 0.5f * std::sin(2.0f * 3.14159265358979f * freq * (i / sr));
      const float y = band.process(0, x);
      peak = std::max(peak, std::fabs(y));
    }
    check(peak > 0.75f, "high shelf +6 dB above corner");  }

  // --- 18 dB/oct high-pass ---
  {
    Hpf18 hpf;
    hpf.prepare(48000.0f);
    hpf.setTargets(200.0f);
    const float sr = 48000.0f;
    float loudPeak = 0.0f;
    float quietPeak = 0.0f;
    for (uint32_t i = 0; i < 48000; ++i) {
      hpf.tick();
      hpf.update();
      const float f = (i < 24000) ? 50.0f : 2000.0f;
      const float x = std::sin(2.0f * 3.14159265358979f * f * (static_cast<float>(i) / sr));
      const float y = hpf.process(0, x);
      if (i >= 24000) {
        loudPeak = std::max(loudPeak, std::fabs(y));
      } else {
        quietPeak = std::max(quietPeak, std::fabs(y));
      }
    }
    check(loudPeak > 0.9f, "HPF passes 2kHz");
    check(quietPeak < 0.95f, "HPF attenuates 50Hz");  }

  // --- compressor: sine at 0.5 amplitude, threshold -20, ratio 4 ---
  {
    ChannelStrip strip;
    strip.prepare(48000.0f);
    Params p;
    p.enabled = true;
    p.inputGainDb = 0.0f;
    p.driveDb = 0.0f;
    p.character = 0.0f;
    p.hpfFreq = 0.0f;
    p.lowFreq = 110.0f;
    p.lowGainDb = 0.0f;
    p.midFreq = 1600.0f;
    p.midGainDb = 0.0f;
    p.midQ = 0.7f;
    p.highFreq = 12000.0f;
    p.highGainDb = 0.0f;
    p.compOn = true;
    p.compFeedback = false;
    p.compThresholdDb = -20.0f;
    p.compRatio = 4.0f;
    p.compAttackMs = 10.0f;
    p.compReleaseMs = 250.0f;
    p.compKneeDb = 6.0f;
    p.compMakeupDb = 0.0f;
    p.compMix = 1.0f;
    p.outputDb = 0.0f;
    strip.setTargets(p);
    strip.reset();

    float* l = new float[48000];
    float* r = new float[48000];
    for (int i = 0; i < 48000; ++i) {
      const float x = 0.5f * std::sin(2.0f * 3.14159265358979f * 1000.0f * i / 48000.0f);
      l[i] = x;
      r[i] = x;
    }
    float gr = 0.0f;
    float out = -60.0f;
    strip.process(l, r, l, r, 48000, gr, out);
    check(gr > -14.0f && gr < -8.0f, "compressor reports ~ -11 dB reduction");
    check(out < -3.0f, "compressor output reduced");
    delete[] l;
    delete[] r;
  }

  // --- bypass passes the signal through ---
  {
    ChannelStrip strip;
    strip.prepare(48000.0f);
    Params p;
    p.enabled = false;
    p.inputGainDb = 0.0f;
    p.driveDb = 0.0f;
    p.character = 0.0f;
    p.hpfFreq = 0.0f;
    p.lowFreq = 110.0f;
    p.lowGainDb = 0.0f;
    p.midFreq = 1600.0f;
    p.midGainDb = 0.0f;
    p.midQ = 0.7f;
    p.highFreq = 12000.0f;
    p.highGainDb = 0.0f;
    p.compOn = true;
    p.compThresholdDb = -20.0f;
    p.compRatio = 4.0f;
    p.compAttackMs = 10.0f;
    p.compReleaseMs = 250.0f;
    p.compKneeDb = 6.0f;
    p.compMakeupDb = 0.0f;
    p.compMix = 1.0f;
    p.outputDb = 0.0f;
    strip.setTargets(p);
    strip.reset();

    float* l = new float[48000];
    float* r = new float[48000];
    for (int i = 0; i < 48000; ++i) {
      const float x = std::sin(2.0f * 3.14159265358979f * 1000.0f * i / 48000.0f) * 0.5f;
      l[i] = x;
      r[i] = x;
    }
    float gr = 0.0f;
    float out = -60.0f;
    strip.process(l, r, l, r, 48000, gr, out);
    check(std::fabs(l[47999]) - 0.5f < 0.01f, "bypass passes signal");
    delete[] l;
    delete[] r;
  }

  // --- hidden output clipper at +0.2 dBFS ---
  {
    ChannelStrip strip;
    strip.prepare(48000.0f);
    Params p;
    p.enabled = true;
    p.compOn = false;
    p.inputGainDb = 0.0f;
    p.driveDb = 0.0f;
    p.character = 0.0f;
    p.hpfFreq = 0.0f;
    p.lowGainDb = 0.0f;
    p.midGainDb = 0.0f;
    p.highGainDb = 0.0f;
    p.outputDb = 12.0f; // deliberately hot, should be caught by the clipper
    strip.setTargets(p);
    strip.reset();

    const uint32_t n = 4800;
    std::vector<float> l(n), r(n);
    for (uint32_t i = 0; i < n; ++i) {
      const float x = 0.5f * std::sin(2.0f * 3.14159265358979f * 1000.0f * i / 48000.0f);
      l[i] = x;
      r[i] = x;
    }
    float gr = 0.0f;
    float out = -60.0f;
    strip.process(l.data(), r.data(), l.data(), r.data(), n, gr, out);
    float peak = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
      peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
    }
    check(peak <= 1.03f, "hidden output clipper ceiling");
    check(peak > 0.80f, "hidden output clipper does not mute");
    check(out <= 0.3f, "output meter respects clipper ceiling");

    // Bypass must stay clean.
    p.enabled = false;
    strip.setTargets(p);
    strip.reset();
    for (uint32_t i = 0; i < n; ++i) {
      const float x = 0.5f * std::sin(2.0f * 3.14159265358979f * 1000.0f * i / 48000.0f);
      l[i] = x;
      r[i] = x;
    }
    strip.process(l.data(), r.data(), l.data(), r.data(), n, gr, out);
    peak = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
      peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
    }
    check(peak > 0.48f && peak < 0.52f, "bypass bypasses the output clipper");
  }

  // --- full chain with extreme settings must stay finite ---
  {
    ChannelStrip strip;
    strip.prepare(48000.0f);
    Params p;
    p.enabled = true;
    p.inputGainDb = 12.0f;
    p.driveDb = 20.0f;
    p.character = 0.9f;
    p.hpfFreq = 80.0f;
    p.lowFreq = 110.0f;
    p.lowGainDb = 16.0f;
    p.midFreq = 1600.0f;
    p.midGainDb = 18.0f;
    p.midQ = 2.0f;
    p.highFreq = 12000.0f;
    p.highGainDb = 16.0f;
    p.compOn = true;
    p.compFeedback = true;
    p.compThresholdDb = -60.0f;
    p.compRatio = 20.0f;
    p.compAttackMs = 0.1f;
    p.compReleaseMs = 20.0f;
    p.compKneeDb = 24.0f;
    p.compMakeupDb = 30.0f;
    p.compMix = 1.0f;
    p.outputDb = 12.0f;
    strip.setTargets(p);
    strip.reset();

    const uint32_t frames = 4800;
    std::vector<float> l(frames);
    std::vector<float> r(frames);
    uint32_t seed = 12345;
    for (uint32_t i = 0; i < frames; ++i) {
      seed = seed * 1103515245u + 12345u;
      const float noise = static_cast<float>(static_cast<int>((seed >> 16) & 0x7fffu) - 4096) / 4096.0f;
      l[i] = noise;
      r[i] = noise;
    }
    float gr = 0.0f;
    float out = -60.0f;
    strip.process(l.data(), r.data(), l.data(), r.data(), frames, gr, out);
    check(std::isfinite(gr) && std::isfinite(out), "extreme chain stays finite");
  }

  // --- several sample rates ---
  for (const float sr : {44100.0f, 48000.0f, 96000.0f, 192000.0f}) {
    ChannelStrip strip;
    strip.prepare(sr);
    Params p;
    p.enabled = true;
    p.compOn = false;
    p.outputDb = 0.0f;
    strip.setTargets(p);
    strip.reset();
    float* l = new float[1024];
    float* r = new float[1024];
    for (int i = 0; i < 1024; ++i) {
      l[i] = 0.25f;
      r[i] = 0.25f;
    }
    float gr = 0.0f;
    float out = -60.0f;
    strip.process(l, r, l, r, 1024, gr, out);
    check(std::isfinite(l[0]) && std::isfinite(out), "finite at sample rate");
    delete[] l;
    delete[] r;
  }

  if (g_failures == 0) {
    printf("ALL DSP TESTS PASSED\n");
  }
  return g_failures == 0 ? 0 : 1;
}
