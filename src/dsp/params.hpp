// Hearth Channel — params.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace hearth {

// Control values in their natural DSP units.  The LV2 wrapper converts from
// the port units (percent, etc.) before filling this in.
struct Params {
  float inputGainDb = 0.0f;
  float driveDb = 0.0f;
  float character = 0.25f; // 0..1

  float hpfFreq = 0.0f; // 0 = off

  float lowFreq = 110.0f;
  float lowGainDb = 0.0f;
  float midFreq = 1600.0f;
  float midGainDb = 0.0f;
  float midQ = 0.4f;
  float highFreq = 12000.0f;
  float highGainDb = 0.0f;

  bool compOn = true;
  float compThresholdDb = -18.0f;
  float compRatio = 4.0f;
  float compAttackMs = 10.0f;
  float compReleaseMs = 250.0f;
  float compKneeDb = 6.0f;
  float compMakeupDb = 0.0f;
  float compMix = 1.0f; // 0..1
  bool compFeedback = false;

  float outputDb = 0.0f;
  bool enabled = true;
};

} // namespace hearth
