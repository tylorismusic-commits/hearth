// Hearth Channel — compressor.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "filters.hpp"
#include "smoother.hpp"
#include "util.hpp"

namespace hearth {

// Stereo-linked console compressor.  The detector is peak-based with a fixed
// 120 Hz side-chain high-pass (the transformer-coupled detector of the classic
// diode-bridge units), a soft knee, and a selectable feed-forward or feedback
// topology.  Gain reduction is shared by both channels so the stereo image
// stays put.

class Compressor {
public:
  void prepare(float sampleRate) noexcept {
    sampleRate_ = sampleRate;

    threshold_.setTime(20.0f, sampleRate);
    ratio_.setTime(20.0f, sampleRate);
    attackMs_.setTime(20.0f, sampleRate);
    releaseMs_.setTime(20.0f, sampleRate);
    knee_.setTime(20.0f, sampleRate);
    makeup_.setTime(20.0f, sampleRate);
    mix_.setTime(20.0f, sampleRate);
    active_.setTime(10.0f, sampleRate);

    threshold_.reset(-18.0f);
    ratio_.reset(4.0f);
    attackMs_.reset(10.0f);
    releaseMs_.reset(250.0f);
    knee_.reset(6.0f);
    makeup_.reset(1.0f);
    mix_.reset(1.0f);
    active_.reset(1.0f);

    for (auto& hp : sidechainHp_) {
      hp.set(120.0f, sampleRate);
      hp.reset();
    }

    env_ = 0.0f;
    gain_ = 1.0f;
    updateCoeffs();
  }

  void setTargets(float thresholdDb, float ratio, float attackMs,
                  float releaseMs, float kneeDb, float makeupDb, float mix01,
                  bool compOn, bool feedback) noexcept {
    threshold_.setTarget(thresholdDb);
    ratio_.setTarget(ratio);
    attackMs_.setTarget(attackMs);
    releaseMs_.setTarget(releaseMs);
    knee_.setTarget(kneeDb);
    makeup_.setTarget(dbToGain(makeupDb));
    mix_.setTarget(clamp(mix01, 0.0f, 1.0f));
    active_.setTarget(compOn ? 1.0f : 0.0f);
    feedback_ = feedback;
  }

  void tick() noexcept {
    threshold_.next();
    ratio_.next();
    attackMs_.next();
    releaseMs_.next();
    knee_.next();
    makeup_.next();
    mix_.next();
    active_.next();
  }

  void updateCoeffs() noexcept {
    const float attackSamples =
        std::max(0.02f, attackMs_.current() * 0.001f * sampleRate_);
    const float releaseSamples =
        std::max(0.1f, releaseMs_.current() * 0.001f * sampleRate_);
    attackCoeff_ = std::exp(-1.0f / attackSamples);
    releaseCoeff_ = std::exp(-1.0f / releaseSamples);
  }

  // Applies the (makeup-compensated) gain reduction and parallel mix, and
  // returns the final gain reduction in dB (<= 0).  Returns 0 when the
  // compressor is off.
  float process(float& left, float& right) noexcept {
    const float detLeft = sidechainHp_[0].hp(feedback_ ? left * gain_ : left);
    const float detRight = sidechainHp_[1].hp(feedback_ ? right * gain_ : right);
    const float detector = std::max(std::fabs(detLeft), std::fabs(detRight));

    if (detector > env_) {
      env_ = attackCoeff_ * env_ + (1.0f - attackCoeff_) * detector;
    } else {
      env_ = releaseCoeff_ * env_ + (1.0f - releaseCoeff_) * detector;
    }
    if (env_ < 1.0e-20f) {
      env_ = 0.0f;
    }

    const float levelDb = clamp(gainToDb(env_), -120.0f, 24.0f);
    const float threshold = threshold_.current();
    const float ratio = std::max(ratio_.current(), 1.0f);
    const float knee = std::max(knee_.current(), 0.0f);
    const float slope = 1.0f - 1.0f / ratio;

    const float over = levelDb - threshold;
    float reductionDb;
    if (over <= -0.5f * knee) {
      reductionDb = 0.0f;
    } else if (over >= 0.5f * knee) {
      reductionDb = slope * over;
    } else {
      const float t = over + 0.5f * knee;
      reductionDb = slope * t * t / (2.0f * knee + 1.0e-9f);
    }
    reductionDb = std::max(reductionDb, 0.0f);

    const float nextGain = dbToGain(-reductionDb);
    const float makeup = makeup_.current();
    const float mixAmt = mix_.current();
    const float active = active_.current();

    const float inLeft = left;
    const float inRight = right;
    const float outLeft = inLeft * nextGain * makeup;
    const float outRight = inRight * nextGain * makeup;

    // Parallel mix, then a smoothed bypass blend so toggling is click-free.
    left = inLeft + mixAmt * (outLeft - inLeft);
    right = inRight + mixAmt * (outRight - inRight);
    left = inLeft + active * (left - inLeft);
    right = inRight + active * (right - inRight);

    gain_ = nextGain;
    return active > 1.0e-4f ? gainToDb(nextGain) : 0.0f;
  }

  void reset() noexcept {
    for (auto& hp : sidechainHp_) {
      hp.reset();
    }
    env_ = 0.0f;
    gain_ = 1.0f;
    threshold_.snap();
    ratio_.snap();
    attackMs_.snap();
    releaseMs_.snap();
    knee_.snap();
    makeup_.snap();
    mix_.snap();
    active_.snap();
    updateCoeffs();
  }

private:
  float sampleRate_ = 48000.0f;
  Smoothed threshold_;
  Smoothed ratio_;
  Smoothed attackMs_;
  Smoothed releaseMs_;
  Smoothed knee_;
  Smoothed makeup_;
  Smoothed mix_;
  Smoothed active_;
  OnePole sidechainHp_[2];
  float attackCoeff_ = 0.0f;
  float releaseCoeff_ = 0.0f;
  float env_ = 0.0f;
  float gain_ = 1.0f;
  bool feedback_ = false;
};

} // namespace hearth
