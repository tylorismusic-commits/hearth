// Hearth Channel — channel_strip.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

#include "compressor.hpp"
#include "eq.hpp"
#include "filters.hpp"
#include "params.hpp"
#include "preamp.hpp"
#include "smoother.hpp"
#include "util.hpp"

namespace hearth {

class ChannelStrip {
public:
  void prepare(float sampleRate) noexcept {
    sampleRate_ = sampleRate;

    for (auto& p : preamp_) {
      p.prepare(sampleRate);
    }
    hpf_.prepare(sampleRate);
    eq_.prepare(sampleRate);
    compressor_.prepare(sampleRate);

    inputGain_.setTime(20.0f, sampleRate);
    outputGain_.setTime(20.0f, sampleRate);
    enableAmt_.setTime(10.0f, sampleRate);
    inputGain_.reset(1.0f);
    outputGain_.reset(1.0f);
    enableAmt_.reset(1.0f);

    for (auto& clip : outputClip_) {
      clip.reset();
    }

    counter_ = 0;
  }

  void setTargets(const Params& p) noexcept {
    inputGain_.setTarget(dbToGain(p.inputGainDb));
    outputGain_.setTarget(dbToGain(p.outputDb));
    enableAmt_.setTarget(p.enabled ? 1.0f : 0.0f);

    for (auto& pre : preamp_) {
      pre.setTargets(p.driveDb, p.character);
    }

    hpf_.setTargets(p.hpfFreq);
    eq_.setTargets(p.lowFreq, p.lowGainDb, p.midFreq, p.midGainDb, p.midQ,
                   p.highFreq, p.highGainDb);
    compressor_.setTargets(p.compThresholdDb, p.compRatio, p.compAttackMs,
                           p.compReleaseMs, p.compKneeDb, p.compMakeupDb,
                           p.compMix, p.compOn, p.compFeedback);
  }

  void reset() noexcept {
    for (auto& p : preamp_) {
      p.reset();
    }
    hpf_.reset();
    eq_.reset();
    compressor_.reset();
    inputGain_.snap();
    outputGain_.snap();
    enableAmt_.snap();
    for (auto& clip : outputClip_) {
      clip.reset();
    }
    counter_ = 0;
  }

  // Processes `frames` samples from the input buffers into the output buffers
  // (which may alias the inputs).  Returns the gain reduction (<= 0 dB) and the
  // peak output level in dBFS (clamped for display).
  void process(const float* inLeft, const float* inRight, float* outLeft,
               float* outRight, uint32_t frames, float& grDb,
               float& outDb) noexcept {
    float lastGr = 0.0f;
    float peak = 0.0f;

    for (uint32_t i = 0; i < frames; ++i) {
      const float inGain = inputGain_.next();
      const float outGain = outputGain_.next();
      const float enable = enableAmt_.next();

      hpf_.tick();
      eq_.tick();
      compressor_.tick();

      if ((counter_ & 15u) == 0u) {
        hpf_.update();
        eq_.update();
        compressor_.updateCoeffs();
      }

      const float dryL = inLeft[i];
      const float dryR = inRight[i];

      float wetL = preamp_[0].process(dryL * inGain);
      float wetR = preamp_[1].process(dryR * inGain);

      wetL = hpf_.process(0, wetL);
      wetR = hpf_.process(1, wetR);

      wetL = eq_.process(0, wetL);
      wetR = eq_.process(1, wetR);

      // The compressor applies gain, makeup, and its parallel mix internally.
      lastGr = compressor_.process(wetL, wetR);

      const float faderL = wetL * outGain;
      const float faderR = wetR * outGain;

      // Hidden output protection: a soft clipper whose ceiling is +0.2 dBFS.
      // The ADAA tanh keeps the odd-order console saturation from aliasing.
      const float clippedL =
          kOutputClipLevel * outputClip_[0].process(faderL / kOutputClipLevel, 0.0f);
      const float clippedR =
          kOutputClipLevel * outputClip_[1].process(faderR / kOutputClipLevel, 0.0f);

      const float finalL = dryL + enable * (clippedL - dryL);
      const float finalR = dryR + enable * (clippedR - dryR);

      outLeft[i] = finalL;
      outRight[i] = finalR;

      peak = std::max(peak, std::max(std::fabs(finalL), std::fabs(finalR)));
      ++counter_;
    }

    grDb = lastGr;
    outDb = clamp(gainToDb(peak), -60.0f, 6.0f);
  }

private:
  float sampleRate_ = 48000.0f;
  Preamp preamp_[2];
  Hpf18 hpf_;
  EqSection eq_;
  Compressor compressor_;
  Smoothed inputGain_;
  Smoothed outputGain_;
  Smoothed enableAmt_;
  AdaaTanh outputClip_[2];
  uint32_t counter_ = 0;

  // +0.2 dBFS: 10^(0.2/20)
  static constexpr float kOutputClipLevel = 1.02329299f;
};

} // namespace hearth
